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

	// Pause / resume only the SCAN side (advertising stays up). Phase 1 uses
	// these to release the radio for a CoreWLAN Wi-Fi scan on macOS, which
	// otherwise hits "Resource busy" while CBCentralManager owns the antenna.
	// Defaults are no-ops so non-BLE backends don't have to care.
	virtual void pauseScan()  {}
	virtual void resumeScan() {}

	// Pause / resume the ADVERTISE side. On Apple silicon BT and Wi-Fi share
	// one antenna; an active CBPeripheralManager advertise keeps CoreWLAN's
	// scanForNetworksWithName: stuck on "Resource busy" indefinitely. The Mac
	// client pauses advertising for the duration of the Wi-Fi join, then
	// resumes so the peer can rediscover it for the next session. Defaults
	// are no-ops so non-BLE backends don't have to care.
	virtual void pauseAdvertise()  {}
	virtual void resumeAdvertise() {}

	// Host-side connect invite (Phase 1 Windows). When the host's user taps a
	// peer to send to it, the host re-advertises a "connect requested" marker
	// so the client (Mac — the only side that can join the hotspot) initiates
	// the Wi-Fi join from its end. Set false once the peer is reachable.
	// Default no-op for non-BLE backends and for the client side.
	virtual void setConnectRequested(bool /*requested*/) {}
};

} // namespace BetterSend
