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
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>

#if defined(_WIN32)
// For winrt::init_apartment on the eager-host worker thread and for
// catching winrt::hresult_error (which does NOT derive from std::exception
// in the locally-installed Windows SDK).
#include <winrt/base.h>
#endif

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

// Pending outbound transfer — sender waits for the peer's Accept/Decline
// control reply before opening the actual File send.
struct PendingOutgoing {
	std::string filePath;
	std::string peerName;
};

// Pending inbound transfer — receiver has surfaced a request to the UI and
// waits for the user's accept/decline decision before doing anything.
struct PendingIncoming {
	std::string senderName;
	std::string senderIp;
	std::string filename;
	std::size_t size{};
};

// Callback signatures the Flutter side may register.
using TransferRequestCallback = void(*)(const char* transferId,
                                        const char* senderName,
                                        const char* filename,
                                        long long   sizeBytes);
using TransferDeclineCallback = void(*)(const char* transferId);

struct BetterSendContext {
	std::string                          localDeviceName;
	std::shared_ptr<TransferProtocol>    protocol;
	std::unique_ptr<TcpTransport>        transport;
	std::unique_ptr<IDiscovery>          discovery;
	std::unique_ptr<IPeerHandshake>      handshake;
	std::unique_ptr<IConnectionBroker>   broker;

	std::mutex                                          peersMu;
	std::unordered_map<std::string, PeerInfo>           peersByName;

	std::mutex                                          transfersMu;
	std::unordered_map<std::string, PendingOutgoing>    pendingOutgoing;
	std::unordered_map<std::string, PendingIncoming>    pendingIncoming;

	TransferRequestCallback                requestCb{nullptr};
	TransferDeclineCallback                declineCb{nullptr};

	// Discovery callback the Flutter UI registered. Kept on ctx (in addition
	// to the lambda capture inside startDiscovery) so the TCP Hello handler
	// can surface the Mac peer to the UI even when the Windows watcher hasn't
	// caught a Mac BLE advert directly. Phase 1 Mac→Windows visibility on
	// the shared Apple-silicon BT/Wi-Fi antenna is unreliable; Hello is the
	// authoritative liveness signal once the hotspot join completed.
	void (*onFoundCb)(const char* name, const char* ip, int port){nullptr};

	IConnectionBroker::Credentials  hostCreds;
	bool                            isHosting{false};
};

namespace {

constexpr const char* kControlPrefix = "BS\t";  // "BS" + TAB, distinct from Hello
constexpr std::size_t kControlPrefixLen = 3;

bool isHelloPayload(const std::string& body) {
	const std::string magic(kHelloMagic, kHelloMagic + std::strlen(kHelloMagic));
	return body.size() >= magic.size()
	    && std::memcmp(body.data(), magic.data(), magic.size()) == 0;
}

std::string helloBody(const std::string& deviceName) {
	const std::string magic(kHelloMagic, kHelloMagic + std::strlen(kHelloMagic));
	return magic + deviceName;
}

bool isControlPayload(const std::string& body) {
	return body.size() > kControlPrefixLen
	    && std::memcmp(body.data(), kControlPrefix, kControlPrefixLen) == 0;
}

std::string makeControlBody(const nlohmann::json& j) {
	return std::string(kControlPrefix) + j.dump();
}

char* heapCopy(const std::string& s) {
	char* out = new char[s.size() + 1];
	std::memcpy(out, s.c_str(), s.size() + 1);
	return out;
}

#if defined(_WIN32)
// Host-side bring-up (Windows in Phase 1). Idempotent — only runs once.
// Serialized: the eager bring-up worker spawned by bettersend_start_discovery
// and the onFound retry path can both reach this; without the mutex two
// threads can read ctx.isHosting == false and race the broker/handshake.
//
// Exception safety: the WinRT calls inside broker->startHost() (the three
// .get()'s on tethering async ops) and handshake->publishPayload() (Gatt
// CreateAsync/CreateCharacteristicAsync/StartAdvertising) can throw
// winrt::hresult_error, which does NOT derive from std::exception in the
// locally-installed Windows SDK. We catch hresult_error explicitly so the
// HRESULT is logged, std::exception for completeness, and (...) so nothing
// can escape across the extern "C" FFI boundary.
void ensureHostStarted(BetterSendContext& ctx, int port) {
	static std::mutex hostStartMu;
	std::lock_guard<std::mutex> lock(hostStartMu);
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
	} catch (const winrt::hresult_error& e) {
		BS_LOG_ERROR(kApiComponent,
			"Host bring-up failed (hresult {:#x}): {}",
			static_cast<uint32_t>(e.code().value),
			winrt::to_string(e.message()));
	} catch (const std::exception& e) {
		BS_LOG_ERROR(kApiComponent, "Host bring-up failed: {}", e.what());
	} catch (...) {
		BS_LOG_ERROR(kApiComponent, "Host bring-up failed: unknown exception");
	}
}
#endif

#if defined(__APPLE__)
// Client-side bring-up (Mac in Phase 1). One-shot per peer.
//
// Phase 1 — Path A (NEHotspotConfiguration auto-join):
//   After the BLE/GATT handshake delivers {ssid, psk, hostIp, port}, the
//   Mac immediately calls `broker->joinNetwork(...)` — which is now the
//   `MacWifiClientBroker` impl built on `NEHotspotConfiguration` (the
//   modern macOS Wi-Fi API). The OS handles scan + associate + DHCP
//   internally; the user does NOT need to touch the Wi-Fi menu. First
//   join per SSID may surface a one-shot system "Allow this app to join
//   <ssid>?" dialog; subsequent joins on the same SSID are silent.
//
//   Once the broker reports a successful join, a short retry loop
//   (`kHelloAfterJoinRetries × kHelloRetryMs`) waits for Windows's TCP
//   server to become reachable on the hotspot subnet — DHCP can lag the
//   join completion by a second or two — and fires the Hello so the
//   host learns the Mac's hotspot IP via `socket.remote_endpoint()`.
constexpr int kHelloAfterJoinRetries = 10;
constexpr int kHelloRetryMs          = 1000;

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

		// Release the handshake central — its job is done.
		ctx.handshake->stop();

		// Cache the host's hotspot-subnet address so the transfer code can
		// already address it; helloSent stays false until the first real
		// round-trip confirms Wi-Fi routing is up.
		{
			std::lock_guard lock(ctx.peersMu);
			auto& info  = ctx.peersByName[peer.name];
			info.peerId = peer.ip;
			info.ip     = hostIp;
			info.port   = hostPort;
		}

		if (!ctx.broker->joinNetwork(ssid, psk)) {
			BS_LOG_ERROR(kApiComponent,
				"joinNetwork('{}') failed; aborting handshake. Mac will "
				"retry on the next BLE sighting per kPeerRetrySec.", ssid);
			return;
		}

		// DHCP and the Windows TCP listener may take a moment to become
		// reachable after the join completes. Retry the Hello briefly so
		// the user-visible delay between joining and pair-ready stays tight.
		TextTransferable hello{helloBody(ctx.localDeviceName)};
		for (int attempt = 1; attempt <= kHelloAfterJoinRetries; ++attempt) {
			std::this_thread::sleep_for(std::chrono::milliseconds(kHelloRetryMs));
			if (ctx.transport->send(hostIp, hostPort, hello)) {
				{
					std::lock_guard lock(ctx.peersMu);
					ctx.peersByName[peer.name].helloSent = true;
				}
				BS_LOG_INFO(kApiComponent,
					"Sent Hello to host {}:{} (attempt {} after join)",
					hostIp, hostPort, attempt);
				return;
			}
		}
		BS_LOG_ERROR(kApiComponent,
			"Hello to '{}' failed after join; peer will retry on next BLE sighting",
			peer.name);
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
					bool firstSighting = false;
					{
						std::lock_guard lock(ctx->peersMu);
						auto& info = ctx->peersByName[t.senderName];
						firstSighting = info.ip.empty();
						info.ip   = t.senderIp;
						info.port = BetterSend::kDefaultPort;
					}
					// Surface the peer to the Flutter UI even if the watcher
					// never caught a BLE advert from this Mac — the Hello
					// proves the peer is alive and reachable on the hotspot
					// subnet, which is all the UI's nearby-devices card
					// needs. Heap-copy the strings because the FFI
					// NativeCallable.listener delivers asynchronously and
					// the Transfer struct destructs before Dart consumes
					// them. Dart's bridge frees them via bettersend_free_cstr.
					if (firstSighting && ctx->onFoundCb) {
						char* name = new char[t.senderName.size() + 1];
						std::memcpy(name, t.senderName.c_str(), t.senderName.size() + 1);
						char* ip   = new char[t.senderIp.size() + 1];
						std::memcpy(ip,   t.senderIp.c_str(),   t.senderIp.size() + 1);
						ctx->onFoundCb(name, ip, BetterSend::kDefaultPort);
						BS_LOG_INFO("API",
							"Surfaced '{}' to UI via Hello (no BLE advert seen)",
							t.senderName);
					}
					return;
				}

				// Control plane: request / accept / decline are carried as a
				// Clipboard payload prefixed with "BS\t" + JSON. Keeps the
				// wire format unchanged; only the API layer parses it.
				if (t.type == BetterSend::Transfer::Type::Clipboard &&
				    BetterSend::isControlPayload(t.data)) {
					try {
						const auto j = nlohmann::json::parse(
							t.data.substr(BetterSend::kControlPrefixLen));
						const std::string kind = j.value("kind", std::string{});
						const std::string id   = j.value("id",   std::string{});

						if (kind == "request") {
							const std::string filename = j.value("name", std::string{});
							const std::size_t sz       = j.value("size", std::size_t{0});
							{
								std::lock_guard lock(ctx->transfersMu);
								ctx->pendingIncoming[id] = BetterSend::PendingIncoming{
									t.senderName, t.senderIp, filename, sz};
							}
							BS_LOG_INFO("API",
								"Incoming request id={} from '{}' file='{}' size={}",
								id, t.senderName, filename, sz);
							if (ctx->requestCb) {
								ctx->requestCb(
									BetterSend::heapCopy(id),
									BetterSend::heapCopy(t.senderName),
									BetterSend::heapCopy(filename),
									static_cast<long long>(sz));
							}
							return;
						}

						if (kind == "accept") {
							std::string filePath;
							std::string peerName;
							{
								std::lock_guard lock(ctx->transfersMu);
								auto it = ctx->pendingOutgoing.find(id);
								if (it == ctx->pendingOutgoing.end()) {
									BS_LOG_WARN("API", "Accept for unknown id={}", id);
									return;
								}
								filePath = it->second.filePath;
								peerName = it->second.peerName;
								ctx->pendingOutgoing.erase(it);
							}
							std::string ip;
							int port2 = BetterSend::kDefaultPort;
							{
								std::lock_guard lock(ctx->peersMu);
								auto pit = ctx->peersByName.find(peerName);
								if (pit == ctx->peersByName.end() || pit->second.ip.empty()) {
									BS_LOG_ERROR("API",
										"Accept arrived but peer '{}' has no IP", peerName);
									return;
								}
								ip    = pit->second.ip;
								port2 = pit->second.port;
							}
							BS_LOG_INFO("API", "Accept id={} -> sending {} to {}:{}",
								id, filePath, ip, port2);
							BetterSend::FileTransferable item{filePath};
							ctx->transport->send(ip, port2, item);
							return;
						}

						if (kind == "decline") {
							{
								std::lock_guard lock(ctx->transfersMu);
								ctx->pendingOutgoing.erase(id);
							}
							BS_LOG_INFO("API", "Decline id={}", id);
							if (ctx->declineCb) {
								ctx->declineCb(BetterSend::heapCopy(id));
							}
							return;
						}

						BS_LOG_WARN("API", "Unknown control kind='{}'", kind);
					} catch (const std::exception& e) {
						BS_LOG_ERROR("API", "Bad control payload: {}", e.what());
					}
					return;
				}

				if (!onReceive) return;
				onReceive(
					t.type == BetterSend::Transfer::Type::File ? 0 : 1,
					BetterSend::heapCopy(t.senderName),
					BetterSend::heapCopy(t.name),
					BetterSend::heapCopy(t.data),
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
		// Stash on ctx so the TCP Hello handler can also surface peers to
		// the UI (Phase 1 fallback when the watcher misses the Mac advert).
		ctx->onFoundCb = onFound;
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

#if defined(_WIN32)
		// Eager host bring-up — breaks the chicken-and-egg deadlock between
		// the Mac's BleHandshake_Mac (which scans BLE filtered by the
		// BetterSend service UUID and only sees connectable peers) and the
		// Windows side's GattServiceProvider (which only starts broadcasting
		// the service UUID inside ensureHostStarted). Without this, the Mac
		// sees Windows on ManufacturerData, kicks clientHandshakeAndJoin,
		// and the handshake central times out for 20 s because no peripheral
		// is yet advertising the service UUID — leading to "Empty GATT
		// payload from <peer>" and no join. The onFound path below still
		// calls ensureHostStarted as a retry if this first attempt failed.
		//
		// Runs on a DETACHED thread, NOT on the FFI caller thread:
		// broker->startHost() blocks on three synchronous .get() calls
		// (StopTetheringAsync/ConfigureAccessPointAsync/StartTetheringAsync)
		// that routinely take 5–60 s; running them on the Dart isolate
		// would freeze the watcher startup and the whole UI. The detached
		// worker init's its own WinRT apartment because the brokers'
		// constructors only init the apartment for the thread they were
		// constructed on (the Dart isolate). ensureHostStarted is now
		// serialized with a static mutex so the eager call and the
		// onFound retry can't race.
		std::thread([ctx]() {
			try {
				winrt::init_apartment(winrt::apartment_type::multi_threaded);
			} catch (const winrt::hresult_error&) {
				// already initialized on this thread — fine
			}
			BS_LOG_INFO("API", "Eager host bring-up: start");
			BetterSend::ensureHostStarted(*ctx, BetterSend::kDefaultPort);
			BS_LOG_INFO("API", "Eager host bring-up: end");
		}).detach();
#endif
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_start_discovery failed: {}", e.what());
	}
}

void bettersend_free_cstr(const char* p) {
	delete[] const_cast<char*>(p);
}

// Send a Request control to peer. The actual file bytes are sent only after
// peer responds with Accept (handled in the startServer callback).
void bettersend_send_file(void* handle, const char* peerName,
                          const char* filePath, const char* transferId) {
	if (!handle || !peerName || !filePath || !transferId) return;
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

		std::error_code fec;
		const auto fsize = std::filesystem::file_size(filePath, fec);
		if (fec) {
			BS_LOG_ERROR("API", "send_file: stat failed for '{}': {}",
				filePath, fec.message());
			return;
		}
		const std::string filename =
			std::filesystem::path(filePath).filename().string();

		{
			std::lock_guard lock(ctx->transfersMu);
			ctx->pendingOutgoing[transferId] =
				BetterSend::PendingOutgoing{filePath, peerName};
		}

		nlohmann::json j;
		j["kind"] = "request";
		j["id"]   = transferId;
		j["size"] = fsize;
		j["name"] = filename;
		BetterSend::TextTransferable req{BetterSend::makeControlBody(j)};
		ctx->transport->send(ip, port, req);
		BS_LOG_INFO("API", "Request id={} '{}' ({} bytes) -> {} @ {}:{}",
			transferId, filename, fsize, peerName, ip, port);
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_send_file failed: {}", e.what());
	}
}

void bettersend_accept_transfer(void* handle, const char* transferId) {
	if (!handle || !transferId) return;
	try {
		auto* ctx = static_cast<BetterSend::BetterSendContext*>(handle);
		std::string senderIp;
		{
			std::lock_guard lock(ctx->transfersMu);
			auto it = ctx->pendingIncoming.find(transferId);
			if (it == ctx->pendingIncoming.end()) {
				BS_LOG_WARN("API", "accept_transfer: unknown id={}", transferId);
				return;
			}
			senderIp = it->second.senderIp;
		}
		nlohmann::json j;
		j["kind"] = "accept";
		j["id"]   = transferId;
		BetterSend::TextTransferable ack{BetterSend::makeControlBody(j)};
		ctx->transport->send(senderIp, BetterSend::kDefaultPort, ack);
		BS_LOG_INFO("API", "Accept id={} -> {}", transferId, senderIp);
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_accept_transfer failed: {}", e.what());
	}
}

void bettersend_decline_transfer(void* handle, const char* transferId) {
	if (!handle || !transferId) return;
	try {
		auto* ctx = static_cast<BetterSend::BetterSendContext*>(handle);
		std::string senderIp;
		{
			std::lock_guard lock(ctx->transfersMu);
			auto it = ctx->pendingIncoming.find(transferId);
			if (it == ctx->pendingIncoming.end()) {
				BS_LOG_WARN("API", "decline_transfer: unknown id={}", transferId);
				return;
			}
			senderIp = it->second.senderIp;
			ctx->pendingIncoming.erase(it);
		}
		nlohmann::json j;
		j["kind"] = "decline";
		j["id"]   = transferId;
		BetterSend::TextTransferable dec{BetterSend::makeControlBody(j)};
		ctx->transport->send(senderIp, BetterSend::kDefaultPort, dec);
		BS_LOG_INFO("API", "Decline id={} -> {}", transferId, senderIp);
	} catch (const std::exception& e) {
		BS_LOG_ERROR("API", "bettersend_decline_transfer failed: {}", e.what());
	}
}

void bettersend_set_request_callback(void* handle,
                                     BetterSend::TransferRequestCallback cb) {
	if (!handle) return;
	static_cast<BetterSend::BetterSendContext*>(handle)->requestCb = cb;
}

void bettersend_set_decline_callback(void* handle,
                                     BetterSend::TransferDeclineCallback cb) {
	if (!handle) return;
	static_cast<BetterSend::BetterSendContext*>(handle)->declineCb = cb;
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
