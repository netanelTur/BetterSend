#include "FileTransferable.h"
#include "IConnectionBroker.h"
#include "IPeerHandshake.h"
#include "MdnsDiscovery.h"
#include "TcpTransport.h"
#include "TextTransferable.h"
#include "TransferProtocol.h"
#include "Logger.h"
#include "Constants.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

// ── bettersend_api.cpp ───────────────────────────────────────────────────────
// C API surface exposed to Flutter via dart:ffi.
//
// Rules for this file:
//   - All functions are extern "C" — no name mangling
//   - No exceptions cross the boundary — wrap everything in try/catch
//   - No std::string in signatures — only const char* / int / long long
//   - Callbacks must be safe to call from a background thread
//
// Phase 1 flow wired here:
//   1. bettersend_create — instantiate Logger, Discovery, Handshake, Broker,
//      Protocol, Transport. Nothing starts yet.
//   2. bettersend_start — kicks off TCP server + BLE advertise + BLE
//      discovery. On each BLE peer event:
//        Windows host role:
//          - broker.startHost()      (Mobile Hotspot up via WinRT)
//          - handshake.publishPayload({ssid,psk,hostIp,port})
//        macOS client role:
//          - handshake.fetchPayload(peer)   (read JSON via GATT)
//          - broker.joinNetwork(ssid,psk)   (CoreWLAN associate)
//          - transport.send(hostIp, port, Hello)   (let Windows learn the
//            Mac's hotspot-subnet IP via socket.remote_endpoint())
//   3. Incoming Clipboard "HELLO\0<name>" payload is intercepted and used
//      to record the sender's IP, then suppressed (not surfaced to UI).
//   4. bettersend_send_file / bettersend_send_clipboard — resolve peer IP
//      from the in-memory peer registry (built up by steps 2 & 3) and
//      hand off to TcpTransport.send.

namespace BetterSend {

constexpr const char* kApiComponent = "API";
constexpr const char* kHelloMagic   = "BetterSendHello\0";
constexpr int         kHandshakeTimeoutSec = 20;

struct PeerInfo {
	std::string                           peerId;       // "ble:<platformId>" from BLE discovery
	std::string                           ip;           // hotspot-subnet IP, learned at runtime
	int                                   port{kDefaultPort};
	bool                                  helloSent{false};       // Mac: did we send our Hello?
	bool                                  attemptInFlight{false}; // Mac: handshake/join in progress
	std::chrono::steady_clock::time_point lastAttempt{};          // Mac: cooldown anchor
};

struct BetterSendContext {
	std::string                          localDeviceName;
	std::shared_ptr<TransferProtocol>    protocol;
	std::unique_ptr<TcpTransport>        transport;
	std::unique_ptr<IDiscovery>          discovery;
	std::unique_ptr<IPeerHandshake>      handshake;
	std::unique_ptr<IConnectionBroker>   broker;

	std::mutex                                    peersMu;
	std::unordered_map<std::string, PeerInfo>     peersByName;

	IConnectionBroker::Credentials  hostCreds;
	bool                            isHosting{false};
};

namespace {

bool isHelloPayload(const std::string& body) {
	const std::string magic(kHelloMagic, kHelloMagic + std::strlen(kHelloMagic));
	return body.size() >= magic.size()
	    && std::memcmp(body.data(), magic.data(), magic.size()) == 0;
}

std::string helloBody(const std::string& deviceName) {
	const std::string magic(kHelloMagic, kHelloMagic + std::strlen(kHelloMagic));
	return magic + deviceName;
}

#if defined(_WIN32)
// Host-side bring-up (Windows in Phase 1). Idempotent — only runs once.
void ensureHostStarted(BetterSendContext& ctx, int port) {
	if (ctx.isHosting) return;
	try {
		ctx.hostCreds = ctx.broker->startHost();
		nlohmann::json j;
		j["ssid"]   = ctx.hostCreds.ssid;
		j["psk"]    = ctx.hostCreds.psk;
		j["hostIp"] = ctx.hostCreds.hostIp;
		j["port"]   = port;
		ctx.handshake->publishPayload(j.dump());
		ctx.isHosting = true;
		BS_LOG_INFO(kApiComponent,
			"Host phase up: ssid='{}' hostIp={} port={}",
			ctx.hostCreds.ssid, ctx.hostCreds.hostIp, port);
	} catch (const std::exception& e) {
		BS_LOG_ERROR(kApiComponent, "Host bring-up failed: {}", e.what());
	}
}
#endif

#if defined(__APPLE__)
// Client-side bring-up (Mac in Phase 1). One-shot per peer.
void clientHandshakeAndJoin(BetterSendContext& ctx, const Device& peer) {
	try {
		BS_LOG_INFO(kApiComponent, "Client handshake with '{}'", peer.name);
		const std::string payload = ctx.handshake->fetchPayload(
			peer.ip, kHandshakeTimeoutSec);
		if (payload.empty()) {
			BS_LOG_ERROR(kApiComponent, "Empty GATT payload from '{}'", peer.name);
			return;
		}
		auto j = nlohmann::json::parse(payload);
		const std::string ssid   = j.value("ssid",   std::string{});
		const std::string psk    = j.value("psk",    std::string{});
		const std::string hostIp = j.value("hostIp", std::string{});
		const int         hostPort = j.value("port", kDefaultPort);

		if (ssid.empty() || hostIp.empty()) {
			BS_LOG_ERROR(kApiComponent, "Malformed GATT JSON: '{}'", payload);
			return;
		}

		if (!ctx.broker->joinNetwork(ssid, psk)) {
			BS_LOG_ERROR(kApiComponent, "joinNetwork('{}') failed", ssid);
			return;
		}

		{
			std::lock_guard lock(ctx.peersMu);
			auto& info  = ctx.peersByName[peer.name];
			info.peerId = peer.ip;
			info.ip     = hostIp;
			info.port   = hostPort;
		}

		// Give DHCP a moment, then ping Windows so its TCP server sees
		// our hotspot IP via socket.remote_endpoint().
		std::this_thread::sleep_for(std::chrono::milliseconds(1500));
		TextTransferable hello{helloBody(ctx.localDeviceName)};
		ctx.transport->send(hostIp, hostPort, hello);

		{
			std::lock_guard lock(ctx.peersMu);
			ctx.peersByName[peer.name].helloSent = true;
		}
		BS_LOG_INFO(kApiComponent, "Sent Hello to host {}:{}", hostIp, hostPort);
	} catch (const std::exception& e) {
		BS_LOG_ERROR(kApiComponent, "Client handshake failed: {}", e.what());
	}
}
#endif

} // namespace

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
		ctx->localDeviceName = deviceName ? deviceName : "BetterSend";
		ctx->protocol        = std::make_shared<BetterSend::TransferProtocol>();
		ctx->transport       = std::make_unique<BetterSend::TcpTransport>(
			ctx->protocol, ctx->localDeviceName);
		ctx->discovery       = BetterSend::makeDiscovery();
		ctx->handshake       = BetterSend::makePeerHandshake();
		ctx->broker          = BetterSend::makeConnectionBroker();
		BS_LOG_INFO("API", "Context created for '{}'", ctx->localDeviceName);
		return ctx;
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_create failed: {}", e.what());
		return nullptr;
	}
}

void bettersend_destroy(void* handle) {
	if (!handle) return;
	try {
		auto* ctx = static_cast<BetterSend::BetterSendContext*>(handle);
		if (ctx->discovery)  ctx->discovery->stop();
		if (ctx->handshake)  ctx->handshake->stop();
		if (ctx->broker)     ctx->broker->stop();
		if (ctx->transport)  ctx->transport->stop();
		delete ctx;
		BS_LOG_INFO("API", "Context destroyed");
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_destroy failed: {}", e.what());
	}
}

void bettersend_start_server(void* handle, int port,
                             TransferReceivedCallback onReceive) {
	if (!handle) return;
	try {
		auto* ctx = static_cast<BetterSend::BetterSendContext*>(handle);
		ctx->transport->startServer(port,
			[ctx, onReceive](BetterSend::Transfer t) {
				// Intercept the Mac-side Hello so it never reaches the UI;
				// use it to register the sender's hotspot IP for later sends.
				if (t.type == BetterSend::Transfer::Type::Clipboard &&
				    BetterSend::isHelloPayload(t.data)) {
					BS_LOG_INFO("API", "Hello from '{}' @ {}", t.senderName, t.senderIp);
					std::lock_guard lock(ctx->peersMu);
					auto& info = ctx->peersByName[t.senderName];
					info.ip   = t.senderIp;
					info.port = BetterSend::kDefaultPort;
					return;
				}

				if (!onReceive) return;
				char* senderName = new char[t.senderName.size() + 1];
				std::memcpy(senderName, t.senderName.c_str(), t.senderName.size() + 1);
				char* name = new char[t.name.size() + 1];
				std::memcpy(name, t.name.c_str(), t.name.size() + 1);
				char* data = new char[t.data.size() + 1];
				std::memcpy(data, t.data.c_str(), t.data.size() + 1);
				const int wireType = (t.type == BetterSend::Transfer::Type::File) ? 0 : 1;
				onReceive(wireType, senderName, name, data,
					static_cast<long long>(t.sizeBytes));
			});
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
		ctx->discovery->startDiscovery([ctx, onFound](BetterSend::Device d) {
			{
				std::lock_guard lock(ctx->peersMu);
				auto& info  = ctx->peersByName[d.name];
				info.peerId = d.ip;
				if (info.port == 0) info.port = d.port;
			}

			// Phase 1 tie-break — each platform takes its fixed role on
			// peer sight. broker / handshake throw if asked to play the
			// wrong role, so we dispatch by build-time platform tag.
#if defined(_WIN32)
			BetterSend::ensureHostStarted(*ctx, BetterSend::kDefaultPort);
#elif defined(__APPLE__)
			bool shouldSpawn = false;
			{
				std::lock_guard lock(ctx->peersMu);
				auto& info = ctx->peersByName[d.name];
				const auto now = std::chrono::steady_clock::now();
				const bool inCooldown =
					info.lastAttempt.time_since_epoch().count() != 0 &&
					now - info.lastAttempt < std::chrono::seconds(BetterSend::kPeerRetrySec);
				if (!info.helloSent && !info.attemptInFlight && !inCooldown) {
					info.attemptInFlight = true;
					info.lastAttempt     = now;
					shouldSpawn          = true;
				}
			}
			if (shouldSpawn) {
				BetterSend::Device snap = d;
				std::thread([ctx, snap]() mutable {
					BetterSend::clientHandshakeAndJoin(*ctx, snap);
					std::lock_guard lock(ctx->peersMu);
					ctx->peersByName[snap.name].attemptInFlight = false;
				}).detach();
			}
#endif

			if (onFound) {
				// Heap-copy strings because NativeCallable.listener delivers
				// the callback asynchronously and Device destructs first.
				char* name = new char[d.name.size() + 1];
				std::memcpy(name, d.name.c_str(), d.name.size() + 1);
				char* ip = new char[d.ip.size() + 1];
				std::memcpy(ip, d.ip.c_str(), d.ip.size() + 1);
				onFound(name, ip, d.port);
			}
		});
		BS_LOG_INFO("API", "Discovery started");
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_start_discovery failed: {}", e.what());
	}
}

void bettersend_free_cstr(const char* p) {
	delete[] const_cast<char*>(p);
}

void bettersend_send_file(void* handle, const char* peerName, const char* filePath) {
	if (!handle || !peerName || !filePath) return;
	try {
		auto* ctx = static_cast<BetterSend::BetterSendContext*>(handle);

		std::string ip;
		int         port = BetterSend::kDefaultPort;
		{
			std::lock_guard lock(ctx->peersMu);
			auto it = ctx->peersByName.find(peerName);
			if (it == ctx->peersByName.end() || it->second.ip.empty()) {
				BS_LOG_ERROR("API",
					"send_file: peer '{}' not paired yet (hotspot IP unknown)",
					peerName);
				return;
			}
			ip   = it->second.ip;
			port = it->second.port;
		}

		BetterSend::FileTransferable item{filePath};
		ctx->transport->send(ip, port, item);
		BS_LOG_INFO("API", "send_file '{}' -> {} @ {}:{}", filePath, peerName, ip, port);
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_send_file failed: {}", e.what());
	}
}

void bettersend_send_clipboard(void* handle, const char* peerName, const char* text) {
	if (!handle || !peerName || !text) return;
	try {
		auto* ctx = static_cast<BetterSend::BetterSendContext*>(handle);

		std::string ip;
		int         port = BetterSend::kDefaultPort;
		{
			std::lock_guard lock(ctx->peersMu);
			auto it = ctx->peersByName.find(peerName);
			if (it == ctx->peersByName.end() || it->second.ip.empty()) {
				BS_LOG_ERROR("API",
					"send_clipboard: peer '{}' not paired yet (hotspot IP unknown)",
					peerName);
				return;
			}
			ip   = it->second.ip;
			port = it->second.port;
		}

		BetterSend::TextTransferable item{text};
		ctx->transport->send(ip, port, item);
		BS_LOG_INFO("API", "send_clipboard {} bytes -> {} @ {}:{}",
			std::strlen(text), peerName, ip, port);
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_send_clipboard failed: {}", e.what());
	}
}

// ── Phase 3: Clipboard system bridge (stubs) ─────────────────────────────────

int bettersend_clipboard_read(void* handle, char* buf, int bufSize) {
	if (!handle || !buf || bufSize <= 0) return 0;
	return 0;
}

void bettersend_clipboard_write(void* handle, const char* text) {
	(void)handle; (void)text;
}

} // extern "C"
