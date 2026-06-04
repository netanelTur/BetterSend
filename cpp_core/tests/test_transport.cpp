#include <gtest/gtest.h>

#include "TcpTransport.h"
#include "TransferProtocol.h"
#include "TextTransferable.h"
#include "FileTransferable.h"
#include "Constants.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <vector>

// ── test_transport.cpp ────────────────────────────────────────────────────────
// localhost round-trip tests for TcpTransport. Each test uses a distinct
// port so failures don't bleed across cases.

using namespace BetterSend;
using namespace std::chrono_literals;

namespace {

std::shared_ptr<IProtocol> makeProto() {
	return std::make_shared<TransferProtocol>();
}

std::filesystem::path writeTempFile(const std::string& contents,
                                    const std::string& name = "test.bin") {
	const auto path = std::filesystem::temp_directory_path() / name;
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
	out.close();
	return path;
}

std::string readAll(const std::filesystem::path& path) {
	std::ifstream in(path, std::ios::binary);
	return std::string{std::istreambuf_iterator<char>(in),
	                   std::istreambuf_iterator<char>()};
}

} // namespace

TEST(TransportTest, SendAndReceiveClipboard_LocalhostRoundTrip) {
	TcpTransport transport(makeProto(), "TestDevice");

	std::promise<Transfer> promise;
	auto future = promise.get_future();

	constexpr int kPort = 19011;
	transport.startServer(kPort, [&promise](Transfer t) {
		promise.set_value(std::move(t));
	});
	std::this_thread::sleep_for(100ms);

	transport.send("127.0.0.1", kPort, TextTransferable{"hello world"});

	ASSERT_EQ(future.wait_for(3s), std::future_status::ready);
	const auto rec = future.get();
	EXPECT_EQ(rec.type,       Transfer::Type::Clipboard);
	EXPECT_EQ(rec.senderName, "TestDevice");
	EXPECT_EQ(rec.data,       "hello world");
	EXPECT_EQ(rec.sizeBytes,  11u);

	transport.stop();
}

TEST(TransportTest, SendAndReceiveFile_LocalhostRoundTrip) {
	const std::string body(8192, 'X');
	const auto src = writeTempFile(body, "bettersend_send.bin");

	TcpTransport transport(makeProto(), "TestDevice");

	std::promise<Transfer> promise;
	auto future = promise.get_future();

	constexpr int kPort = 19012;
	transport.startServer(kPort, [&promise](Transfer t) {
		promise.set_value(std::move(t));
	});
	std::this_thread::sleep_for(100ms);

	transport.send("127.0.0.1", kPort, FileTransferable{src});

	ASSERT_EQ(future.wait_for(5s), std::future_status::ready);
	const auto rec = future.get();
	EXPECT_EQ(rec.type,      Transfer::Type::File);
	EXPECT_EQ(rec.sizeBytes, body.size());

	ASSERT_FALSE(rec.data.empty()) << "Receiver should report a saved path";
	const auto got = readAll(rec.data);
	EXPECT_EQ(got.size(), body.size());
	EXPECT_EQ(got,        body);

	std::error_code ec;
	std::filesystem::remove(rec.data, ec);
	std::filesystem::remove(src,      ec);
	transport.stop();
}

TEST(TransportTest, MultipleSequentialTransfers_AllArrive) {
	TcpTransport transport(makeProto(), "TestDevice");

	std::mutex                                      mu;
	std::vector<Transfer>                           received;
	std::promise<void>                              allDone;
	auto                                            future = allDone.get_future();
	constexpr std::size_t                           kCount = 3;
	std::atomic<std::size_t>                        seen{0};

	constexpr int kPort = 19013;
	transport.startServer(kPort, [&](Transfer t) {
		{
			std::lock_guard lock(mu);
			received.push_back(std::move(t));
		}
		if (++seen == kCount) allDone.set_value();
	});
	std::this_thread::sleep_for(100ms);

	for (std::size_t i = 0; i < kCount; ++i) {
		transport.send("127.0.0.1", kPort,
			TextTransferable{"msg-" + std::to_string(i)});
	}

	ASSERT_EQ(future.wait_for(3s), std::future_status::ready);
	std::lock_guard lock(mu);
	ASSERT_EQ(received.size(), kCount);

	transport.stop();
}

TEST(TransportTest, StopServer_NoCrashAfterStop) {
	TcpTransport transport(makeProto(), "TestDevice");

	constexpr int kPort = 19014;
	std::atomic<bool> received{false};
	transport.startServer(kPort, [&received](Transfer) { received = true; });
	std::this_thread::sleep_for(100ms);

	transport.stop();

	// Sending to a stopped server should fail gracefully (no callback fire,
	// no crash). We don't assert on the connect error itself — Asio
	// behaviour for connection-refused varies by platform.
	transport.send("127.0.0.1", kPort, TextTransferable{"after-stop"});
	std::this_thread::sleep_for(200ms);
	EXPECT_FALSE(received.load());
}
