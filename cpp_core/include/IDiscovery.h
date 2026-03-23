#pragma once
#include <functional>
#include <string>
#include "Device.h"

namespace BetterSend {

// ── IDiscovery ────────────────────────────────────────────────────────────────
// Strategy interface for peer discovery on the local network.
//
// Responsibilities:
//   1. Advertise this device on the network (mDNS PTR/SRV/A records)
//   2. Discover other devices advertising the same service type
//
// Service type: "_bettersend._tcp.local."
//
// Implementations:
//   - MdnsDiscovery   (production — uses mdns.h)
//   - MockDiscovery   (tests — feeds synthetic Device events)
//
// Thread safety: callbacks may be invoked from a background thread.
// The caller must synchronize access to shared state inside the callback.

class IDiscovery {
public:
    virtual ~IDiscovery() = default;

    // Begin advertising this device so peers can discover it.
    // deviceName — display name sent in mDNS records
    // port       — TCP port ITransport::startServer is listening on
    virtual void startAdvertising(const std::string& deviceName, int port) = 0;

    // Begin scanning for peers.
    // onFound — called (from a background thread) each time a new peer appears
    virtual void startDiscovery(std::function<void(Device)> onFound) = 0;

    // Stop both advertising and discovery; blocks until background threads exit.
    virtual void stop() = 0;
};

} // namespace BetterSend
