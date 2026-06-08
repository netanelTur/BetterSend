#pragma once
#include "ITransport.h"
#include "IProtocol.h"

#include <atomic>
#include <memory>
#include <mutex>
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

	// Directory where incoming files are written. Empty (default) → a
	// "bettersend_incoming" folder under the OS temp dir. Thread-safe:
	// receive runs on background threads, the UI may set this concurrently.
	void setSaveDirectory(std::string dir);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;

	std::shared_ptr<IProtocol>      protocol_;
	std::string                     localDeviceName_;
	std::function<void(Transfer)>   onReceive_;
	std::thread                     ioThread_;
	std::atomic<bool>               running_{false};

	std::mutex                      saveDirMu_;
	std::string                     saveDir_;   // empty → OS temp fallback
};

} // namespace BetterSend
