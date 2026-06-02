#include "IDiscovery.h"
#include "MdnsDiscovery.h"
#include "Logger.h"
#include "Constants.h"
#include "mdns.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <functional>
#include <string>
#include <cstring>
#ifdef _WIN32
#	include <winsock2.h>
#	include <ws2tcpip.h>
#	include <iphlpapi.h>
#	include <vector>
#else
#	include <sys/select.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
#	include <ifaddrs.h>
#	include <net/if.h>
#endif

namespace BetterSend {

static constexpr std::string_view kComponent = "Discovery";
static constexpr size_t kBufSize = 2048;

// ── Helpers ───────────────────────────────────────────────────────────────────

#ifdef _WIN32
static std::string getLocalIPv4() {
	ULONG bufLen = 15000;
	std::vector<BYTE> buf(bufLen);
	auto* addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
	if (GetAdaptersAddresses(AF_INET,
		GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
		nullptr, addrs, &bufLen) != NO_ERROR) return {};
	for (auto* a = addrs; a; a = a->Next) {
		if (a->OperStatus != IfOperStatusUp) continue;
		if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
		for (auto* ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
			auto* sin = reinterpret_cast<sockaddr_in*>(ua->Address.lpSockaddr);
			if (sin->sin_family != AF_INET) continue;
			char ipbuf[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &sin->sin_addr, ipbuf, sizeof(ipbuf));
			return std::string(ipbuf);
		}
	}
	return {};
}
#else
static std::string getLocalIPv4() {
	struct ifaddrs* ifa = nullptr;
	if (getifaddrs(&ifa) != 0) return {};
	std::string result;
	for (auto* p = ifa; p; p = p->ifa_next) {
		if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
		if (p->ifa_flags & IFF_LOOPBACK) continue;
		char buf[INET_ADDRSTRLEN];
		auto* sin = reinterpret_cast<sockaddr_in*>(p->ifa_addr);
		inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
		result = buf;
		break;
	}
	freeifaddrs(ifa);
	return result;
}
#endif

// ── Advertising state + callback ──────────────────────────────────────────────

struct AdvertiseState {
	std::string   serviceType;
	std::string   instance;   // "DeviceName._bettersend._tcp.local."
	std::string   hostname;   // "DeviceName.local."
	mdns_record_t ptrRec;
	mdns_record_t srvRec;
	mdns_record_t aRec;
	mdns_record_t additional[2]; // {srvRec, aRec}
	alignas(4) char recvBuf[kBufSize]; // receive buffer — passed to socket_listen
	alignas(4) char sendBuf[kBufSize]; // send buffer — passed to query_answer_*
};

static int advertCb(int sock, const struct sockaddr* from, size_t addrlen,
                    mdns_entry_type_t entry, uint16_t qid, uint16_t rtype,
                    uint16_t rclass, uint32_t, const void* data, size_t size,
                    size_t name_offset, size_t, size_t, size_t, void* ud) {
	if (entry != MDNS_ENTRYTYPE_QUESTION) return 0;
	if (rtype != MDNS_RECORDTYPE_PTR && rtype != MDNS_RECORDTYPE_ANY) return 0;

	// Check that the question is for our service type
	char nbuf[256];
	size_t ofs = name_offset;
	mdns_string_t qname = mdns_string_extract(data, size, &ofs, nbuf, sizeof(nbuf));
	if (std::string_view(qname.str, qname.length).find("_bettersend._tcp") == std::string_view::npos)
		return 0;

	auto* st = static_cast<AdvertiseState*>(ud);
	bool unicast = (rclass & MDNS_UNICAST_RESPONSE) != 0;
	if (unicast) {
		mdns_query_answer_unicast(sock, from, addrlen,
			st->sendBuf, kBufSize, qid, (mdns_record_type_t)rtype,
			st->serviceType.c_str(), st->serviceType.size(),
			st->ptrRec, nullptr, 0, st->additional, 2);
	} else {
		mdns_query_answer_multicast(sock, st->sendBuf, kBufSize,
			st->ptrRec, nullptr, 0, st->additional, 2);
	}
	return 0;
}

// ── Discovery state + callback ────────────────────────────────────────────────

struct DiscoveryState {
	std::function<void(Device)>     onFound;
	std::unordered_set<std::string> seen;
	std::mutex                      mutex;
	char strbuf[256];
	char srvbuf[256];
};

static int discoveryCb(int, const struct sockaddr* from, size_t,
                       mdns_entry_type_t entry, uint16_t, uint16_t rtype,
                       uint16_t, uint32_t, const void* data, size_t size,
                       size_t name_offset, size_t, size_t record_offset,
                       size_t record_length, void* ud) {
	if (entry == MDNS_ENTRYTYPE_QUESTION) return 0;
	if (rtype != MDNS_RECORDTYPE_SRV) return 0;

	auto* st = static_cast<DiscoveryState*>(ud);

	// Instance name (e.g. "DeviceName._bettersend._tcp.local.")
	size_t ofs = name_offset;
	mdns_string_t instName = mdns_string_extract(data, size, &ofs, st->strbuf, sizeof(st->strbuf));
	std::string instanceName(instName.str, instName.length);

	// Port from SRV RDATA
	mdns_record_srv_t srv = mdns_record_parse_srv(data, size, record_offset, record_length,
	                                               st->srvbuf, sizeof(st->srvbuf));

	// IP from sender (no A record needed — sender address is authoritative)
	char ipbuf[INET_ADDRSTRLEN] = {};
	if (from->sa_family == AF_INET)
		inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(from)->sin_addr, ipbuf, sizeof(ipbuf));
	std::string ip = ipbuf;
	if (ip.empty()) return 0;

	// Extract device name: first label of instance name
	std::string devName = instanceName;
	auto dot = instanceName.find("._bettersend");
	if (dot != std::string::npos) devName = instanceName.substr(0, dot);

	std::lock_guard<std::mutex> lock(st->mutex);
	if (!st->seen.count(ip)) {
		st->seen.insert(ip);
		BS_LOG_INFO(kComponent, "Found: name='{}' ip={} port={}", devName, ip, srv.port);
		st->onFound(Device{devName, ip, static_cast<int>(srv.port)});
	}
	return 0;
}

// ── MdnsDiscovery ─────────────────────────────────────────────────────────────

class MdnsDiscovery : public IDiscovery {
public:
	~MdnsDiscovery() override { stop(); }

	void startAdvertising(const std::string& deviceName, int port) override {
		BS_LOG_INFO(kComponent, "Advertising: name='{}' port={}", deviceName, port);
		std::string localIP = getLocalIPv4();
		if (localIP.empty()) {
			BS_LOG_WARN(kComponent, "No local IPv4; advertising skipped");
			return;
		}

		// Stable strings — built before records reference them
		adState_.serviceType = kServiceType;
		adState_.instance    = deviceName + "." + kServiceType;
		adState_.hostname    = deviceName + ".local.";

		// PTR: _bettersend._tcp.local. → DeviceName._bettersend._tcp.local.
		adState_.ptrRec = {};
		adState_.ptrRec.name           = {adState_.serviceType.c_str(), adState_.serviceType.size()};
		adState_.ptrRec.type           = MDNS_RECORDTYPE_PTR;
		adState_.ptrRec.data.ptr.name  = {adState_.instance.c_str(), adState_.instance.size()};
		adState_.ptrRec.rclass         = MDNS_CLASS_IN;
		adState_.ptrRec.ttl            = kMdnsTtlSeconds;

		// SRV: DeviceName._bettersend._tcp.local. → hostname.local. :port
		adState_.srvRec = {};
		adState_.srvRec.name                = {adState_.instance.c_str(), adState_.instance.size()};
		adState_.srvRec.type                = MDNS_RECORDTYPE_SRV;
		adState_.srvRec.data.srv.priority   = 0;
		adState_.srvRec.data.srv.weight     = 0;
		adState_.srvRec.data.srv.port       = static_cast<uint16_t>(port);
		adState_.srvRec.data.srv.name       = {adState_.hostname.c_str(), adState_.hostname.size()};
		adState_.srvRec.rclass              = MDNS_CLASS_IN;
		adState_.srvRec.ttl                 = kMdnsTtlSeconds;

		// A: hostname.local. → IP
		adState_.aRec = {};
		adState_.aRec.name                       = {adState_.hostname.c_str(), adState_.hostname.size()};
		adState_.aRec.type                       = MDNS_RECORDTYPE_A;
		adState_.aRec.data.a.addr.sin_family     = AF_INET;
		adState_.aRec.data.a.addr.sin_port       = 0;
		inet_pton(AF_INET, localIP.c_str(), &adState_.aRec.data.a.addr.sin_addr);
		adState_.aRec.rclass                     = MDNS_CLASS_IN;
		adState_.aRec.ttl                        = kMdnsTtlSeconds;

		// Copy into additional array (pointers still point into adState_ strings)
		adState_.additional[0] = adState_.srvRec;
		adState_.additional[1] = adState_.aRec;

		struct sockaddr_in saddr{};
		saddr.sin_family      = AF_INET;
		saddr.sin_port        = htons(MDNS_PORT);
		saddr.sin_addr.s_addr = INADDR_ANY;
		advertSock_ = mdns_socket_open_ipv4(&saddr);
		if (advertSock_ < 0) {
			BS_LOG_ERROR(kComponent, "Cannot open advertising socket (port 5353 in use?)");
			return;
		}

		running_.store(true);
		advertThread_ = std::thread([this]() {
			// Unsolicited announcement on join
			mdns_announce_multicast(advertSock_, adState_.sendBuf, kBufSize,
				adState_.ptrRec, nullptr, 0, adState_.additional, 2);
			BS_LOG_DEBUG(kComponent, "Announced '{}'", adState_.instance);

			while (running_) {
				fd_set fds; FD_ZERO(&fds); FD_SET(advertSock_, &fds);
				struct timeval tv{0, 100000}; // 100ms poll
				if (select(advertSock_ + 1, &fds, nullptr, nullptr, &tv) > 0 && running_)
					mdns_socket_listen(advertSock_, adState_.recvBuf, kBufSize, advertCb, &adState_);
			}
			BS_LOG_DEBUG(kComponent, "Advertising thread exit");
		});
	}

	void startDiscovery(std::function<void(Device)> onFound) override {
		BS_LOG_INFO(kComponent, "Starting discovery for '{}'", kServiceType);
		discState_.onFound = std::move(onFound);

		discSock_ = mdns_socket_open_ipv4(nullptr); // ephemeral port
		if (discSock_ < 0) {
			BS_LOG_ERROR(kComponent, "Cannot open discovery socket");
			return;
		}

		running_.store(true);
		discThread_ = std::thread([this]() {
			alignas(4) char buf[kBufSize];

			int qid = mdns_query_send(discSock_, MDNS_RECORDTYPE_PTR,
				kServiceType, strlen(kServiceType), buf, kBufSize, 0);
			BS_LOG_DEBUG(kComponent, "PTR query sent qid={}", qid);

			while (running_) {
				fd_set fds; FD_ZERO(&fds); FD_SET(discSock_, &fds);
				struct timeval tv{0, 100000}; // 100ms poll
				if (select(discSock_ + 1, &fds, nullptr, nullptr, &tv) > 0 && running_)
					mdns_query_recv(discSock_, buf, kBufSize, discoveryCb, &discState_, qid);
			}
			BS_LOG_DEBUG(kComponent, "Discovery thread exit");
		});
	}

	void stop() override {
		if (!running_.exchange(false)) return;
		BS_LOG_INFO(kComponent, "Stopping");
		// Close sockets — unblocks any pending select/recv in threads
		if (advertSock_ >= 0) { mdns_socket_close(advertSock_); advertSock_ = -1; }
		if (discSock_   >= 0) { mdns_socket_close(discSock_);   discSock_   = -1; }
		if (advertThread_.joinable()) advertThread_.join();
		if (discThread_.joinable())   discThread_.join();
		BS_LOG_DEBUG(kComponent, "All threads stopped");
	}

private:
	std::atomic<bool> running_{false};
	int               advertSock_ = -1;
	int               discSock_   = -1;
	std::thread       advertThread_;
	std::thread       discThread_;
	AdvertiseState    adState_{};
	DiscoveryState    discState_{};
};

// ── Factory ───────────────────────────────────────────────────────────────────

std::unique_ptr<IDiscovery> makeDiscovery() {
	return std::make_unique<MdnsDiscovery>();
}

} // namespace BetterSend
