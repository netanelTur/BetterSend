#include "IDiscovery.h"
#include "ITransport.h"
#include "IClipboard.h"
#include "FileTransferable.h"
#include "TextTransferable.h"
#include "Logger.h"
#include "Constants.h"
#include <memory>
#include <cstring>

// ── bettersend_api.cpp ────────────────────────────────────────────────────────
// C API surface exposed to Flutter via dart:ffi.
//
// Rules for this file:
//   - All functions are extern "C" — no name mangling
//   - No exceptions cross the boundary — wrap everything in try/catch
//   - No std::string in signatures — only const char* / int / long long
//   - Callbacks must be safe to call from a background thread
//
// Typical Flutter usage sequence:
//   handle = bettersend_create("iPhone-Yoni")
//   bettersend_start_server(handle, port, onReceiveCallback)
//   bettersend_start_advertising(handle, port)
//   bettersend_start_discovery(handle, onDeviceFoundCallback)
//   ...
//   bettersend_send_file(handle, "192.168.1.5", 9000, "/path/photo.jpg")
//   bettersend_destroy(handle)

namespace BetterSend {

// ── Internal context ──────────────────────────────────────────────────────────
// Heap-allocated; passed back to Flutter as an opaque void*.
// Design pattern: Facade — single entry point over Discovery + Transport + Clipboard.

struct BetterSendContext {
	std::string                 localDeviceName;
	std::unique_ptr<IDiscovery> discovery;
	std::unique_ptr<ITransport> transport;
	std::unique_ptr<IClipboard> clipboard; // Phase 2

	// TODO: construct with concrete implementations:
	//   discovery = std::make_unique<MdnsDiscovery>();
	//   transport = std::make_unique<TcpTransport>(
	//                   std::make_shared<TransferProtocol>(), localDeviceName);
};

} // namespace BetterSend

// ── Callback typedefs ─────────────────────────────────────────────────────────

// Called when a new peer is found via mDNS.
// name/ip are valid only for the duration of the callback — Flutter must copy them.
using DeviceFoundCallback = void(*)(const char* name, const char* ip, int port);

// Called when an incoming transfer completes.
// type: 0=file, 1=clipboard
// data: file path (for File) or text content (for Clipboard)
using TransferReceivedCallback = void(*)(int type, const char* senderName,
                                         const char* name, const char* data,
                                         long long sizeBytes);

// ─────────────────────────────────────────────────────────────────────────────

extern "C" {

// Create a context for a device with the given name.
// Returns an opaque handle; pass it to all other functions.
// Returns nullptr on failure (logged).
void* bettersend_create(const char* deviceName) {
	try {
		auto* ctx = new BetterSend::BetterSendContext{};
		ctx->localDeviceName = deviceName;
		// TODO: ctx->discovery = std::make_unique<BetterSend::MdnsDiscovery>();
		// TODO: ctx->transport = std::make_unique<BetterSend::TcpTransport>(...);
		BS_LOG_INFO("API", "Context created for device '{}'", deviceName);
		return ctx;
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_create failed: {}", e.what());
		return nullptr;
	}
}

// Destroy the context and release all resources.
void bettersend_destroy(void* handle) {
	if (!handle) return;
	try {
		delete static_cast<BetterSend::BetterSendContext*>(handle);
		BS_LOG_INFO("API", "Context destroyed");
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_destroy failed: {}", e.what());
	}
}

// Round-trip echo — proves the Flutter ↔ C++ FFI pipeline is wired up.
// Returns a pointer into a thread-local buffer; valid until next call on this thread.
const char* bettersend_echo(const char* text) {
	static thread_local std::string buf;
	buf = "Hello from C++: ";
	buf += (text ? text : "");
	BS_LOG_DEBUG("API", "echo: '{}'", buf);
	return buf.c_str();
}

// Start the TCP server on the given port.
void bettersend_start_server(void* handle, int port,
                             TransferReceivedCallback onReceive) {
	if (!handle) return;
	try {
		// TODO:
		// auto* ctx = static_cast<BetterSend::BetterSendContext*>(handle);
		// ctx->transport->startServer(port, [onReceive](BetterSend::Transfer t) {
		//     int type = (t.type == BetterSend::Transfer::Type::File) ? 0 : 1;
		//     onReceive(type, t.senderName.c_str(), t.name.c_str(),
		//               t.data.c_str(), static_cast<long long>(t.sizeBytes));
		// });
		BS_LOG_INFO("API", "Server starting on port {}", port);
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_start_server failed: {}", e.what());
	}
}

// Advertise this device on the local network.
void bettersend_start_advertising(void* handle, int port) {
	if (!handle) return;
	try {
		// TODO:
		// auto* ctx = static_cast<BetterSend::BetterSendContext*>(handle);
		// ctx->discovery->startAdvertising(ctx->localDeviceName, port);
		BS_LOG_INFO("API", "Advertising started on port {}", port);
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_start_advertising failed: {}", e.what());
	}
}

// Begin peer discovery; onFound is called for each new device found.
void bettersend_start_discovery(void* handle, DeviceFoundCallback onFound) {
	if (!handle) return;
	try {
		// TODO:
		// auto* ctx = static_cast<BetterSend::BetterSendContext*>(handle);
		// ctx->discovery->startDiscovery([onFound](BetterSend::Device d) {
		//     onFound(d.name.c_str(), d.ip.c_str(), d.port);
		// });
		BS_LOG_INFO("API", "Discovery started");
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_start_discovery failed: {}", e.what());
	}
}

// Send a file to a remote peer.
void bettersend_send_file(void* handle, const char* ip, int port,
                          const char* filePath) {
	if (!handle) return;
	try {
		// TODO:
		// auto* ctx = static_cast<BetterSend::BetterSendContext*>(handle);
		// ctx->transport->send(ip, port, BetterSend::FileTransferable{filePath});
		BS_LOG_INFO("API", "sendFile: {} → {}:{}", filePath, ip, port);
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_send_file failed: {}", e.what());
	}
}

// Send clipboard text to a remote peer.
void bettersend_send_clipboard(void* handle, const char* ip, int port,
                               const char* text) {
	if (!handle) return;
	try {
		// TODO:
		// auto* ctx = static_cast<BetterSend::BetterSendContext*>(handle);
		// ctx->transport->send(ip, port, BetterSend::TextTransferable{text});
		BS_LOG_INFO("API", "sendClipboard: {} bytes → {}:{}", std::strlen(text), ip, port);
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_send_clipboard failed: {}", e.what());
	}
}

// ── Phase 2: Clipboard ────────────────────────────────────────────────────────

// Read local clipboard text into buf (null-terminated).
// Returns number of bytes written (0 = empty or error).
int bettersend_clipboard_read(void* handle, char* buf, int bufSize) {
	if (!handle || !buf || bufSize <= 0) return 0;
	try {
		// TODO:
		// auto* ctx = static_cast<BetterSend::BetterSendContext*>(handle);
		// std::string text = ctx->clipboard->read();
		// int n = std::min(static_cast<int>(text.size()), bufSize - 1);
		// std::memcpy(buf, text.data(), n);
		// buf[n] = '\0';
		// return n;
		return 0;
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_clipboard_read failed: {}", e.what());
		return 0;
	}
}

// Write text to the local clipboard.
void bettersend_clipboard_write(void* handle, const char* text) {
	if (!handle || !text) return;
	try {
		// TODO:
		// auto* ctx = static_cast<BetterSend::BetterSendContext*>(handle);
		// ctx->clipboard->write(text);
		BS_LOG_INFO("API", "clipboard_write: {} bytes", std::strlen(text));
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_clipboard_write failed: {}", e.what());
	}
}

} // extern "C"
