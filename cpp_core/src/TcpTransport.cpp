#include "ITransport.h"
#include "IProtocol.h"
#include "Logger.h"
#include "Constants.h"
#include <memory>
#include <thread>

// ── TcpTransport ──────────────────────────────────────────────────────────────
// Concrete ITransport using TCP sockets via Asio (standalone, no Boost).
//
// Asio docs: https://think-async.com/Asio/asio-1.28.0/doc/
//
// How to implement startServer:
//   1. Create asio::io_context + asio::ip::tcp::acceptor on the given port
//   2. Launch async accept loop:
//        acceptor_.async_accept([](socket) {
//            readHeader(socket) → readPayload → call onReceive
//            then loop back to async_accept
//        })
//   3. Run io_context on a background thread
//
// How to implement send:
//   1. Encode item.makeHeader(localDeviceName_) with protocol_->encodeHeader
//   2. TCP connect → write encoded header → write item.payload() in kReceiveBufferSize chunks
//
// How to implement stop:
//   io_.stop() then join the io thread

namespace BetterSend {

static constexpr std::string_view kComponent = "Transport";

class TcpTransport : public ITransport {
public:
	explicit TcpTransport(std::shared_ptr<IProtocol> protocol,
	                      std::string localDeviceName)
		: protocol_{std::move(protocol)}
		, localDeviceName_{std::move(localDeviceName)}
	{}

	~TcpTransport() override { stop(); }

	void startServer(int port, std::function<void(Transfer)> onReceive) override {
		BS_LOG_INFO(kComponent, "Starting TCP server on port {}", port);
		// TODO:
		//   acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(
		//       io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port));
		//   doAccept(onReceive);
		//   ioThread_ = std::thread([this]{ io_.run(); });
	}

	void send(const std::string& ip, int port,
	          const ITransferable& item) override {
		BS_LOG_INFO(kComponent, "Sending to {}:{}", ip, port);
		// TODO:
		//   auto hdr     = item.makeHeader(localDeviceName_);
		//   auto encoded = protocol_->encodeHeader(hdr);
		//   auto bytes   = item.payload();
		//   // TCP connect → write encoded → write bytes
	}

	void stop() override {
		BS_LOG_INFO(kComponent, "Stopping transport");
		// TODO: io_.stop()
		if (ioThread_.joinable()) ioThread_.join();
		BS_LOG_DEBUG(kComponent, "Transport stopped");
	}

private:
	std::shared_ptr<IProtocol> protocol_;
	std::string                localDeviceName_;
	std::thread                ioThread_;

	// TODO: add these Asio members:
	//   asio::io_context                           io_;
	//   std::unique_ptr<asio::ip::tcp::acceptor>   acceptor_;
};

} // namespace BetterSend
