#pragma once
#include <memory>
#include <string>

namespace BetterSend {

// ── IConnectionBroker ─────────────────────────────────────────────────────────
// Strategy interface for bringing up the actual IP-level network the
// transport runs on, after BLE discovery has identified a peer.
//
// Two roles — `startHost()` and `joinNetwork()`. Each platform implements
// the role it can play in Phase 1:
//
//   Windows  → WindowsHotspotBroker   (host)
//                Brings up Mobile Hotspot via WinRT
//                NetworkOperatorTetheringManager, returns the SSID/PSK
//                that the client side will join via BLE GATT.
//   macOS    → MacWifiClientBroker    (client)
//                Joins an existing SSID via CoreWLAN
//                `CWInterface associateToNetwork:password:error:`.
//
// Phase 1 tie-break: when one Windows + one macOS peer meet, Windows is
// the host (macOS programmatic hotspot is restricted; the Mac is always
// the client). Later phases will extend each impl to support both roles
// or introduce platform-specific siblings (AndroidHotspotBroker,
// iOSWifiClientBroker, MacHotspotBroker, …).
//
// Strategy implementations may throw std::runtime_error for the role they
// don't support so misuse from the higher layer fails loudly instead of
// silently doing nothing.

class IConnectionBroker {
public:
	virtual ~IConnectionBroker() = default;

	struct Credentials {
		std::string ssid;
		std::string psk;
		std::string hostIp;   // IP the client should connect to after joining
	};

	// Host role — bring up the network. Returns credentials the peer must
	// use to join. Throws on failure or if this broker can't be a host.
	[[nodiscard]] virtual Credentials startHost() = 0;

	// Client role — join an existing network. Blocks until associated or
	// timeout. Returns true on success.
	// Throws if this broker can't be a client.
	virtual bool joinNetwork(const std::string& ssid, const std::string& psk) = 0;

	// Tear down: stops the hotspot (host) or leaves the joined network
	// (client). Safe to call multiple times.
	virtual void stop() = 0;
};

// Factory — picks the right concrete broker for the host platform.
// Defined in WindowsHotspotBroker.cpp (Windows) and MacWifiClientBroker.mm
// (macOS). One per platform, just like makeDiscovery().
std::unique_ptr<IConnectionBroker> makeConnectionBroker();

} // namespace BetterSend
