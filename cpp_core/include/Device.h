#pragma once
#include <string>
#include <compare>
#include <tuple>

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

	// Set when this peer's advert carries the "connect requested" marker —
	// the host (Windows) is asking us (Mac) to join its hotspot because its
	// user tapped us. Not part of identity; excluded from comparison.
	bool        connectRequested{false};

	// Equality + ordering by name/IP:port (useful for deduplication in sets).
	// connectRequested is a transient signal, not identity, so it's excluded.
	auto operator<=>(const Device& o) const {
		return std::tie(name, ip, port) <=> std::tie(o.name, o.ip, o.port);
	}
	bool operator==(const Device& o) const {
		return name == o.name && ip == o.ip && port == o.port;
	}
};

} // namespace BetterSend
