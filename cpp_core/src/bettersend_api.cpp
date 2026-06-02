#include "ITransport.h"
#include "IClipboard.h"
#include "MdnsDiscovery.h"
#include "Logger.h"
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

namespace BetterSend {

struct BetterSendContext {
	std::string                 localDeviceName;
	std::unique_ptr<IDiscovery> discovery;
	std::unique_ptr<ITransport> transport;
	std::unique_ptr<IClipboard> clipboard; // Phase 2
};

} // namespace BetterSend

// ── Callback typedefs ─────────────────────────────────────────────────────────

using DeviceFoundCallback = void(*)(const char* name, const char* ip, int port);

using TransferReceivedCallback = void(*)(int type, const char* senderName,
                                         const char* name, const char* data,
                                         long long sizeBytes);

// ─────────────────────────────────────────────────────────────────────────────

extern "C" {

void* bettersend_create(const char* deviceName) {
	try {
		auto* ctx = new BetterSend::BetterSendContext{};
		ctx->localDeviceName = deviceName;
		ctx->discovery       = BetterSend::makeDiscovery();
		BS_LOG_INFO("API", "Context created for '{}'", deviceName);
		return ctx;
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_create failed: {}", e.what());
		return nullptr;
	}
}

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

void bettersend_start_server(void* handle, int port,
                             TransferReceivedCallback /*onReceive*/) {
	if (!handle) return;
	try {
		// TODO:
		// auto* ctx = static_cast<BetterSend::BetterSendContext*>(handle);
		// ctx->transport->startServer(port, [onReceive](...) { onReceive(...); });
		BS_LOG_INFO("API", "Server starting on port {}", port);
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_start_server failed: {}", e.what());
	}
}

void bettersend_start_advertising(void* handle, int port) {
	if (!handle) return;
	try {
		auto* ctx = static_cast<BetterSend::BetterSendContext*>(handle);
		ctx->discovery->startAdvertising(ctx->localDeviceName, port);
		BS_LOG_INFO("API", "Advertising started on port {}", port);
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_start_advertising failed: {}", e.what());
	}
}

void bettersend_start_discovery(void* handle, DeviceFoundCallback onFound) {
	if (!handle) return;
	try {
		auto* ctx = static_cast<BetterSend::BetterSendContext*>(handle);
		ctx->discovery->startDiscovery([onFound](BetterSend::Device d) {
			// Dart-side NativeCallable.listener is async — d destructs before Dart reads.
			// Heap-allocate copies; Dart owns them and calls bettersend_free_cstr.
			char* name = new char[d.name.size() + 1];
			std::memcpy(name, d.name.c_str(), d.name.size() + 1);
			char* ip = new char[d.ip.size() + 1];
			std::memcpy(ip, d.ip.c_str(), d.ip.size() + 1);
			onFound(name, ip, d.port);
		});
		BS_LOG_INFO("API", "Discovery started");
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_start_discovery failed: {}", e.what());
	}
}

void bettersend_free_cstr(const char* p) {
	delete[] const_cast<char*>(p);
}

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

int bettersend_clipboard_read(void* handle, char* buf, int bufSize) {
	if (!handle || !buf || bufSize <= 0) return 0;
	try {
		// TODO: ctx->clipboard->read()
		return 0;
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_clipboard_read failed: {}", e.what());
		return 0;
	}
}

void bettersend_clipboard_write(void* handle, const char* text) {
	if (!handle || !text) return;
	try {
		// TODO: ctx->clipboard->write(text)
		BS_LOG_INFO("API", "clipboard_write: {} bytes", std::strlen(text));
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_clipboard_write failed: {}", e.what());
	}
}

} // extern "C"
