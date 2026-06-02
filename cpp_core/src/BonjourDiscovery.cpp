#include "IDiscovery.h"
#include "MdnsDiscovery.h"
#include "Logger.h"
#include <dns_sd.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <functional>
#include <string>
#include <sys/select.h>
#include <arpa/inet.h>

// ── BonjourDiscovery ──────────────────────────────────────────────────────────
// Uses Apple's dns_sd.h (Bonjour) API — goes through mDNSResponder.
// Required on macOS/iOS because the sandbox blocks raw UDP port 5353.
// All sub-operations share one DNSServiceRef connection → one fd → one select loop.
//
// kDNSServiceFlagsIncludeAWDL: advertise/browse over AWDL (peer-to-peer WiFi).
// kDNSServiceFlagsIncludeP2P:  advertise/browse over P2P interfaces.
// These flags enable AirDrop-style discovery without a shared WiFi network.

namespace BetterSend {

static constexpr std::string_view kComponent = "Discovery";
static constexpr const char* kRegType = "_bettersend._tcp";

// ── Shared state ──────────────────────────────────────────────────────────────

struct DiscoveryState {
	std::function<void(Device)>     onFound;
	std::unordered_set<std::string> seen;
	std::mutex                      mutex;
	DNSServiceRef                   mainRef{nullptr};
};

// ── Callback chain: browse → resolve → addrInfo → Device ─────────────────────

// Context passed from resolve through addrInfo
struct AddrCtx {
	DiscoveryState* state;
	std::string     serviceName; // device display name (from browse)
	int             port;        // from resolve
};

// Context passed from browse to resolve
struct ResolveCtx {
	DiscoveryState* state;
	std::string     serviceName;
};

static void DNSSD_API addrInfoCb(DNSServiceRef sdRef, DNSServiceFlags flags,
    uint32_t, DNSServiceErrorType err, const char*,
    const struct sockaddr* addr, uint32_t, void* ud) {

	auto* ctx = static_cast<AddrCtx*>(ud);

	if (err == kDNSServiceErr_NoError && (flags & kDNSServiceFlagsAdd)
            && addr && addr->sa_family == AF_INET) {
		char ipbuf[INET_ADDRSTRLEN];
		inet_ntop(AF_INET,
			&reinterpret_cast<const sockaddr_in*>(addr)->sin_addr,
			ipbuf, sizeof(ipbuf));
		std::string ip = ipbuf;

		std::lock_guard<std::mutex> lock(ctx->state->mutex);
		if (!ctx->state->seen.count(ip)) {
			ctx->state->seen.insert(ip);
			BS_LOG_INFO(kComponent, "Found: name='{}' ip={} port={}",
				ctx->serviceName, ip, ctx->port);
			ctx->state->onFound(Device{ctx->serviceName, ip, ctx->port});
		}
	}

	// Deallocate sub-ref and context once we have the result (or on error/timeout)
	if (!(flags & kDNSServiceFlagsMoreComing)) {
		DNSServiceRefDeallocate(sdRef);
		delete ctx;
	}
}

static void DNSSD_API resolveCb(DNSServiceRef sdRef, DNSServiceFlags,
    uint32_t ifIdx, DNSServiceErrorType err, const char*,
    const char* hosttarget, uint16_t port, uint16_t, const unsigned char*, void* ud) {

	auto* rc = static_cast<ResolveCtx*>(ud);
	DNSServiceRefDeallocate(sdRef);

	if (err != kDNSServiceErr_NoError) {
		BS_LOG_WARN(kComponent, "DNSServiceResolve error: {}", err);
		delete rc;
		return;
	}

	auto* ac = new AddrCtx{rc->state, rc->serviceName, ntohs(port)};
	delete rc;

	DNSServiceRef addrRef = ac->state->mainRef;
	DNSServiceErrorType e = DNSServiceGetAddrInfo(&addrRef,
		kDNSServiceFlagsShareConnection | kDNSServiceFlagsTimeout,
		ifIdx, kDNSServiceProtocol_IPv4, hosttarget, addrInfoCb, ac);

	if (e != kDNSServiceErr_NoError) {
		BS_LOG_WARN(kComponent, "DNSServiceGetAddrInfo error: {}", e);
		delete ac;
	}
}

static void DNSSD_API browseCb(DNSServiceRef, DNSServiceFlags flags,
    uint32_t ifIdx, DNSServiceErrorType err, const char* serviceName,
    const char* regtype, const char* domain, void* ud) {

	if (err != kDNSServiceErr_NoError) return;
	if (!(flags & kDNSServiceFlagsAdd)) return; // ignore "remove" events

	auto* st = static_cast<DiscoveryState*>(ud);
	auto* rc = new ResolveCtx{st, serviceName};

	DNSServiceRef resolveRef = st->mainRef;
	DNSServiceErrorType e = DNSServiceResolve(&resolveRef,
		kDNSServiceFlagsShareConnection, ifIdx,
		serviceName, regtype, domain, resolveCb, rc);

	if (e != kDNSServiceErr_NoError) {
		BS_LOG_WARN(kComponent, "DNSServiceResolve start error: {}", e);
		delete rc;
	}
}

// ── BonjourDiscovery class ────────────────────────────────────────────────────

class BonjourDiscovery : public IDiscovery {
public:
	~BonjourDiscovery() override { stop(); }

	void startAdvertising(const std::string& deviceName, int port) override {
		BS_LOG_INFO(kComponent, "Advertising '{}' on port {}", deviceName, port);
		ensureMainRef();

		static constexpr DNSServiceFlags kP2PFlags =
			kDNSServiceFlagsShareConnection |
			kDNSServiceFlagsIncludeAWDL |  // peer-to-peer WiFi (no shared router)
			kDNSServiceFlagsIncludeP2P;    // other P2P interfaces

		regRef_ = mainRef_;
		DNSServiceErrorType err = DNSServiceRegister(&regRef_,
			kP2PFlags, 0,
			deviceName.c_str(), kRegType,
			nullptr, nullptr,
			htons(static_cast<uint16_t>(port)),
			0, nullptr, nullptr, nullptr);

		if (err != kDNSServiceErr_NoError) {
			BS_LOG_ERROR(kComponent, "DNSServiceRegister failed: {}", err);
			return;
		}
		ensureEventLoop();
	}

	void startDiscovery(std::function<void(Device)> onFound) override {
		BS_LOG_INFO(kComponent, "Starting Bonjour browse for {}", kRegType);
		discState_.onFound = std::move(onFound);
		ensureMainRef();
		discState_.mainRef = mainRef_;

		browseRef_ = mainRef_;
		DNSServiceErrorType err = DNSServiceBrowse(&browseRef_,
			kDNSServiceFlagsShareConnection |
			kDNSServiceFlagsIncludeAWDL |
			kDNSServiceFlagsIncludeP2P,
			0, kRegType, nullptr, browseCb, &discState_);

		if (err != kDNSServiceErr_NoError) {
			BS_LOG_ERROR(kComponent, "DNSServiceBrowse failed: {}", err);
			return;
		}
		ensureEventLoop();
	}

	void stop() override {
		if (!running_.exchange(false)) return;
		BS_LOG_INFO(kComponent, "Stopping Bonjour");
		if (mainRef_) {
			DNSServiceRefDeallocate(mainRef_);
			mainRef_    = nullptr;
			regRef_     = nullptr;
			browseRef_  = nullptr;
		}
		if (eventThread_.joinable()) eventThread_.join();
	}

private:
	void ensureMainRef() {
		if (!mainRef_)
			DNSServiceCreateConnection(&mainRef_);
	}

	void ensureEventLoop() {
		if (running_.exchange(true)) return; // already running
		eventThread_ = std::thread([this]() {
			BS_LOG_DEBUG(kComponent, "Bonjour event loop started");
			while (running_ && mainRef_) {
				int fd = DNSServiceRefSockFD(mainRef_);
				if (fd < 0) break;
				fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
				struct timeval tv{0, 100000};
				if (select(fd + 1, &fds, nullptr, nullptr, &tv) > 0
						&& running_ && mainRef_) {
					DNSServiceProcessResult(mainRef_);
				}
			}
			BS_LOG_DEBUG(kComponent, "Bonjour event loop exit");
		});
	}

	std::atomic<bool> running_{false};
	std::thread       eventThread_;
	DNSServiceRef     mainRef_{nullptr};
	DNSServiceRef     regRef_{nullptr};
	DNSServiceRef     browseRef_{nullptr};
	DiscoveryState    discState_{};
};

// ── Factory ───────────────────────────────────────────────────────────────────

std::unique_ptr<IDiscovery> makeDiscovery() {
	return std::make_unique<BonjourDiscovery>();
}

} // namespace BetterSend
