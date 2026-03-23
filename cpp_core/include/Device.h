#pragma once
#include <string>
#include <compare>

namespace BetterSend {

// ── Device ────────────────────────────────────────────────────────────────────
// Value type representing a peer device discovered on the local network.
// Passed via the IDiscovery::startDiscovery callback and through the C API to Flutter.
//
// Design note: kept as a simple aggregate (no invariants to enforce).
// If device types (iOS/Android/macOS/Windows) are added later, introduce
// a DeviceType enum and a factory — don't add fields ad-hoc.

struct Device {
    std::string name;  // Human-readable device name advertised via mDNS
    std::string ip;    // IPv4 address
    int         port;  // TCP port the device is listening on

    // Equality + ordering by IP:port (useful for deduplication in sets)
    auto operator<=>(const Device&) const = default;
};

} // namespace BetterSend
