#pragma once
#include <memory>
#include <string>

namespace BetterSend {

// ── IPeerHandshake ────────────────────────────────────────────────────────────
// Strategy interface for the post-discovery / pre-transport credential
// exchange. Once BLE discovery has identified a peer, the host side
// brings up its hotspot via IConnectionBroker::startHost() and publishes
// the resulting {ssid, psk, hostIp, port} JSON via this handshake. The
// client side reads it back from that same peer over BLE GATT, and then
// calls IConnectionBroker::joinNetwork on its side.
//
// Phase 1 tie-break (mirrors IConnectionBroker):
//   Windows  → BleHandshake_Windows  (host role only)
//                Exposes a GATT service+characteristic via
//                Windows.Devices.Bluetooth.GenericAttributeProfile.
//   macOS    → BleHandshake_Mac      (client role only)
//                Uses CoreBluetooth to connect/discover/read the
//                characteristic from the peer.
//
// Phase 1 wire format inside the characteristic: the same UTF-8 JSON as
// the broker credentials, so future phases can swap the transport (e.g.
// classic Bluetooth, NFC) without touching the higher layers.

class IPeerHandshake {
public:
	virtual ~IPeerHandshake() = default;

	// Host role — publish a payload that connecting peers can read.
	// Subsequent calls overwrite the previous payload (idempotent).
	// Throws if this impl can't be a host.
	virtual void publishPayload(const std::string& payload) = 0;

	// Client role — read the payload from `peerId` over GATT.
	// `peerId` is the value carried in BleDiscovery's Device::ip after the
	// "ble:" prefix. Format is platform-specific (Mac UUID string,
	// Windows BD address). Blocks up to `timeoutSeconds`; returns empty
	// string on failure.
	// Throws if this impl can't be a client.
	[[nodiscard]] virtual std::string fetchPayload(const std::string& peerId,
	                                               int timeoutSeconds) = 0;

	// Tear down: stops the GATT server (host) or any in-flight client
	// connection. Safe to call multiple times.
	virtual void stop() = 0;
};

// Factory — picks the right concrete handshake for the host platform.
// Defined in BleHandshake_Windows.cpp (Windows) and BleHandshake_Mac.mm
// (macOS), exactly like makeDiscovery() / makeConnectionBroker().
std::unique_ptr<IPeerHandshake> makePeerHandshake();

} // namespace BetterSend
