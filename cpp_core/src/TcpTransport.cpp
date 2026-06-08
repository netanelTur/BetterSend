#include "TcpTransport.h"
#include "Logger.h"
#include "Constants.h"

#include <asio.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

// ── TcpTransport ──────────────────────────────────────────────────────────────
// Async TCP server + sync send client.
//
//   startServer(port, onReceive)
//     acceptor accepts on background io thread; each accepted socket spawns
//     a worker thread that reads:
//       [4B BE length N] [N bytes JSON header] [header.size bytes payload]
//     Header passes through IProtocol::decodeHeader. Payload is streamed:
//       Clipboard → assembled in memory, returned in Transfer.data.
//       File      → written to a unique path under temp_directory_path(),
//                   that path is returned in Transfer.data.
//
//   send(ip, port, ITransferable&)
//     Blocking connect + write of [encoded header][payload bytes].
//     Payload is fetched once via ITransferable::payload() — fine for
//     Phase 1 (10 MB photo). True streaming send is a Phase 4 follow-up.
//
//   stop()
//     Stops accept loop, cancels acceptor, drains io thread.

namespace BetterSend {

namespace {

constexpr std::string_view kComponent = "Transport";

uint32_t readBigEndianU32(const uint8_t* in) {
	return (static_cast<uint32_t>(in[0]) << 24)
	     | (static_cast<uint32_t>(in[1]) << 16)
	     | (static_cast<uint32_t>(in[2]) <<  8)
	     |  static_cast<uint32_t>(in[3]);
}

std::filesystem::path makeIncomingFilePath(const std::string& filename,
                                           const std::string& saveDir) {
	const auto base = saveDir.empty()
		? std::filesystem::temp_directory_path() / "bettersend_incoming"
		: std::filesystem::path(saveDir);
	std::error_code ec;
	std::filesystem::create_directories(base, ec);
	const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	std::string safeName = filename.empty() ? std::string("file.bin") : filename;
	return base / (std::to_string(stamp) + "_" + safeName);
}

} // namespace

// ── Impl ──────────────────────────────────────────────────────────────────────
// Holds the Asio members; kept behind a forward declaration so users of
// TcpTransport.h don't have to include <asio.hpp>.

struct TcpTransport::Impl {
	asio::io_context                                                       io;
	std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> work;
	std::unique_ptr<asio::ip::tcp::acceptor>                                acceptor;
};

TcpTransport::TcpTransport(std::shared_ptr<IProtocol> protocol,
                           std::string localDeviceName)
	: impl_{std::make_unique<Impl>()}
	, protocol_{std::move(protocol)}
	, localDeviceName_{std::move(localDeviceName)}
{}

TcpTransport::~TcpTransport() { stop(); }

void TcpTransport::setSaveDirectory(std::string dir) {
	std::lock_guard<std::mutex> lk(saveDirMu_);
	saveDir_ = std::move(dir);
	BS_LOG_INFO(kComponent, "Incoming-file save directory set to '{}'",
		saveDir_.empty() ? "<temp>" : saveDir_);
}

void TcpTransport::startServer(int port, std::function<void(Transfer)> onReceive) {
	if (running_.exchange(true)) {
		BS_LOG_WARN(kComponent, "startServer called while already running");
		return;
	}
	onReceive_ = std::move(onReceive);

	BS_LOG_INFO(kComponent, "Starting TCP server on port {}", port);

	try {
		impl_->work = std::make_unique<
			asio::executor_work_guard<asio::io_context::executor_type>>(
				impl_->io.get_executor());

		impl_->acceptor = std::make_unique<asio::ip::tcp::acceptor>(
			impl_->io,
			asio::ip::tcp::endpoint(asio::ip::tcp::v4(), static_cast<uint16_t>(port)));
		impl_->acceptor->set_option(asio::socket_base::reuse_address(true));
	} catch (const std::exception& e) {
		BS_LOG_ERROR(kComponent, "Acceptor setup failed: {}", e.what());
		running_.store(false);
		impl_->work.reset();
		impl_->acceptor.reset();
		return;
	}

	auto doAccept = std::make_shared<std::function<void()>>();
	*doAccept = [this, doAccept]() {
		impl_->acceptor->async_accept(
			[this, doAccept](std::error_code ec, asio::ip::tcp::socket sock) {
				if (ec) {
					if (running_.load()) {
						BS_LOG_WARN(kComponent, "async_accept error: {}", ec.message());
					}
					return;
				}

				std::thread([this, sock = std::move(sock)]() mutable {
					try {
						// Snapshot the peer IP up front — Windows knows the Mac
						// peer's hotspot-subnet address only after the first
						// inbound packet arrives, so we surface it on every
						// transfer (including the Mac's auto-Hello).
						std::string remoteIp;
						{
							std::error_code rec;
							auto ep = sock.remote_endpoint(rec);
							if (!rec) remoteIp = ep.address().to_string();
						}

						// 1) 4-byte BE length prefix
						uint8_t lenBuf[kHeaderLengthBytes];
						asio::read(sock, asio::buffer(lenBuf, kHeaderLengthBytes));
						const uint32_t jsonLen = readBigEndianU32(lenBuf);
						if (jsonLen == 0 || jsonLen > kMaxHeaderSizeBytes) {
							BS_LOG_ERROR(kComponent, "Bad header length {}", jsonLen);
							return;
						}

						// 2) Header bytes — prefix + JSON, passed whole to decodeHeader
						std::vector<uint8_t> headerBuf(kHeaderLengthBytes + jsonLen);
						std::memcpy(headerBuf.data(), lenBuf, kHeaderLengthBytes);
						asio::read(sock, asio::buffer(headerBuf.data() + kHeaderLengthBytes, jsonLen));
						const MessageHeader hdr = protocol_->decodeHeader(headerBuf);

						BS_LOG_INFO(kComponent, "Incoming {} '{}' {} bytes from '{}' @ {}",
							hdr.type == MessageHeader::Type::File ? "file" : "clipboard",
							hdr.name, hdr.size, hdr.senderName, remoteIp);

						Transfer transfer{};
						transfer.senderName = hdr.senderName;
						transfer.senderIp   = remoteIp;
						transfer.name       = hdr.name;
						transfer.sizeBytes  = hdr.size;
						transfer.type       = (hdr.type == MessageHeader::Type::File)
							? Transfer::Type::File
							: Transfer::Type::Clipboard;

						// 3) Payload
						if (transfer.type == Transfer::Type::File) {
							std::string saveDir;
							{
								std::lock_guard<std::mutex> lk(saveDirMu_);
								saveDir = saveDir_;
							}
							const auto path = makeIncomingFilePath(hdr.name, saveDir);
							std::ofstream out(path, std::ios::binary | std::ios::trunc);
							if (!out) {
								BS_LOG_ERROR(kComponent, "Cannot open incoming file {}",
									path.string());
								return;
							}
							std::vector<char> chunk(kReceiveBufferSize);
							std::size_t left = hdr.size;
							while (left > 0) {
								const std::size_t want =
									std::min<std::size_t>(left, chunk.size());
								asio::read(sock, asio::buffer(chunk.data(), want));
								out.write(chunk.data(), static_cast<std::streamsize>(want));
								left -= want;
							}
							out.close();
							transfer.data = path.string();
						} else {
							std::string body;
							body.resize(hdr.size);
							if (hdr.size > 0) {
								asio::read(sock, asio::buffer(body.data(), hdr.size));
							}
							transfer.data = std::move(body);
						}

						if (onReceive_) onReceive_(std::move(transfer));
					} catch (const std::exception& e) {
						BS_LOG_ERROR(kComponent, "Session error: {}", e.what());
					}
				}).detach();

				if (running_.load()) (*doAccept)();
			});
	};
	(*doAccept)();

	ioThread_ = std::thread([this] {
		try {
			impl_->io.run();
		} catch (const std::exception& e) {
			BS_LOG_ERROR(kComponent, "io.run() crashed: {}", e.what());
		}
	});
}

bool TcpTransport::send(const std::string& ip, int port, const ITransferable& item) {
	BS_LOG_INFO(kComponent, "Sending to {}:{}", ip, port);
	try {
		const auto headerStruct = item.makeHeader(localDeviceName_);
		const auto encoded      = protocol_->encodeHeader(headerStruct);
		const auto payload      = item.payload();

		asio::io_context io;
		asio::ip::tcp::socket sock(io);

		std::error_code ec;
		auto addr = asio::ip::make_address(ip, ec);
		if (ec) {
			BS_LOG_ERROR(kComponent, "Bad IP '{}': {}", ip, ec.message());
			return false;
		}
		sock.connect(asio::ip::tcp::endpoint(addr, static_cast<uint16_t>(port)));

		asio::write(sock, asio::buffer(encoded.data(), encoded.size()));
		if (!payload.empty()) {
			asio::write(sock, asio::buffer(payload.data(), payload.size()));
		}
		sock.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
		sock.close();

		BS_LOG_INFO(kComponent, "Sent {} bytes header + {} bytes payload",
			encoded.size(), payload.size());
		return true;
	} catch (const std::exception& e) {
		BS_LOG_ERROR(kComponent, "send failed: {}", e.what());
		return false;
	}
}

void TcpTransport::stop() {
	if (!running_.exchange(false)) {
		if (ioThread_.joinable()) ioThread_.join();
		return;
	}
	BS_LOG_INFO(kComponent, "Stopping transport");
	try {
		if (impl_->acceptor) {
			std::error_code ec;
			impl_->acceptor->close(ec);
		}
		if (impl_->work) impl_->work.reset();
		impl_->io.stop();
	} catch (const std::exception& e) {
		BS_LOG_ERROR(kComponent, "stop cleanup error: {}", e.what());
	}
	if (ioThread_.joinable()) ioThread_.join();
	impl_->acceptor.reset();
	impl_->io.restart();
	BS_LOG_DEBUG(kComponent, "Transport stopped");
}

} // namespace BetterSend
