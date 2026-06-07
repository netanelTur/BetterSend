#pragma once
#include "ITransport.h"
#include "IProtocol.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

// Forward declarations so this header doesn't drag <asio.hpp> into every
// translation unit that includes it. The acceptor and io_context are held
// behind a unique_ptr to a forward-declared impl struct.
namespace BetterSend {

class TcpTransport : public ITransport {
public:
	TcpTransport(std::shared_ptr<IProtocol> protocol,
	             std::string localDeviceName);
	~TcpTransport() override;

	void startServer(int port, std::function<void(Transfer)> onReceive) override;
	bool send(const std::string& ip, int port, const ITransferable& item) override;
	void stop() override;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;

	std::shared_ptr<IProtocol>      protocol_;
	std::string                     localDeviceName_;
	std::function<void(Transfer)>   onReceive_;
	std::thread                     ioThread_;
	std::atomic<bool>               running_{false};
};

} // namespace BetterSend
