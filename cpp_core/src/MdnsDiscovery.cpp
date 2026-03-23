#include "IDiscovery.h"
#include "Logger.h"
#include "Constants.h"
#include <thread>
#include <atomic>

// ── MdnsDiscovery ─────────────────────────────────────────────────────────────
// Concrete IDiscovery using Multicast DNS (RFC 6762 / Bonjour-compatible).
//
// Dependency: mdns.h — single-header, public domain
//   Repo   : https://github.com/mjansson/mdns
//   Install: download mdns.h → place in cpp_core/include/mdns.h
//
// How to implement startAdvertising:
//   1. Open a UDP socket on kMdnsPort (5353)
//   2. Join multicast group kMdnsMulticastAddr
//   3. Respond to PTR/SRV/A queries for kServiceType in a loop thread
//   4. Announce on join (mdns_announce_multicast)
//
// How to implement startDiscovery:
//   1. Send mDNS PTR query for kServiceType
//   2. Read replies in a loop; each SRV+A pair → Device → call onFound
//   3. Deduplicate by IP before calling onFound
//
// How to implement stop:
//   1. Set running_ = false to exit both loops
//   2. Close sockets to unblock recv calls
//   3. Join threads

namespace BetterSend {

static constexpr std::string_view kComponent = "Discovery";

class MdnsDiscovery : public IDiscovery {
public:
    MdnsDiscovery() = default;
    ~MdnsDiscovery() override { stop(); }

    void startAdvertising(const std::string& deviceName, int port) override {
        BS_LOG_INFO(kComponent, "Starting advertising: name={} port={}", deviceName, port);
        running_ = true;
        advertiseThread_ = std::thread([this, deviceName, port]() {
            // TODO: open UDP socket, join multicast, respond to queries
            // Use: mdns_socket_open_ipv4() / mdns_announce_multicast()
            BS_LOG_DEBUG(kComponent, "Advertising thread started");
            while (running_) {
                // TODO: mdns query/response loop
            }
            BS_LOG_DEBUG(kComponent, "Advertising thread exiting");
        });
    }

    void startDiscovery(std::function<void(Device)> onFound) override {
        BS_LOG_INFO(kComponent, "Starting discovery for {}", kServiceType);
        discoveryThread_ = std::thread([this, onFound = std::move(onFound)]() {
            // TODO: mdns_query_send() then loop on mdns_query_recv()
            // For each SRV record: build Device{name, ip, port} and call onFound
            BS_LOG_DEBUG(kComponent, "Discovery thread started");
            while (running_) {
                // TODO: poll / select on socket, parse SRV+A records
            }
            BS_LOG_DEBUG(kComponent, "Discovery thread exiting");
        });
    }

    void stop() override {
        if (!running_.exchange(false)) return; // already stopped
        BS_LOG_INFO(kComponent, "Stopping discovery and advertising");
        // TODO: close sockets here to unblock recv() in threads
        if (advertiseThread_.joinable())  advertiseThread_.join();
        if (discoveryThread_.joinable()) discoveryThread_.join();
        BS_LOG_DEBUG(kComponent, "All threads stopped");
    }

private:
    std::atomic<bool> running_{false};
    std::thread       advertiseThread_;
    std::thread       discoveryThread_;
    // TODO: add socket handles (int or platform handle type) as members
};

} // namespace BetterSend
