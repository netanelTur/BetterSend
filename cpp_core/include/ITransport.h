#pragma once
#include <functional>
#include <string>
#include <cstdint>

namespace BetterSend {

// ── Transfer ──────────────────────────────────────────────────────────────────
// Represents a completed incoming transfer.
// Passed to the onReceive callback registered in ITransport::startServer.

struct Transfer {
    enum class Type : uint8_t { File, Clipboard };

    Type        type;
    std::string senderName;   // Device name from the protocol header
    std::string name;         // Filename (empty for Clipboard transfers)
    std::size_t sizeBytes{};  // Payload size in bytes
    std::string data;         // For Clipboard: the text. For File: path to saved file.
};

// ── ITransport ────────────────────────────────────────────────────────────────
// Strategy interface for peer-to-peer data transfer over TCP.
//
// Responsibilities:
//   1. Run a TCP server that accepts incoming transfers
//   2. Send files and clipboard text to a remote peer
//
// Implementations:
//   - TcpTransport   (production — uses Asio standalone)
//   - MockTransport  (tests — in-memory loopback)
//
// Thread safety: startServer runs an Asio io_context on a background thread.
// The onReceive callback is invoked from that background thread.

class ITransport {
public:
    virtual ~ITransport() = default;

    // Start the TCP server on the given port.
    // onReceive — called (from background thread) when a full transfer arrives
    virtual void startServer(int port, std::function<void(Transfer)> onReceive) = 0;

    // Send a file located at filePath to ip:port.
    virtual void sendFile(const std::string& ip, int port,
                          const std::string& filePath) = 0;

    // Send clipboard text to ip:port.
    virtual void sendClipboard(const std::string& ip, int port,
                               const std::string& text) = 0;

    // Stop the server and drain in-flight sends; blocks until clean shutdown.
    virtual void stop() = 0;
};

} // namespace BetterSend
