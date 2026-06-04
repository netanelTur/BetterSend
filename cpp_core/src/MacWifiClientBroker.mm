// ── MacWifiClientBroker.mm ───────────────────────────────────────────────────
// macOS implementation of BetterSend::IConnectionBroker — client role only.
// Built only when the host platform is Apple-desktop (see CMakeLists.txt
// guard).
//
// Joins an existing Wi-Fi SSID (the one the Windows peer brought up via
// WindowsHotspotBroker) via CoreWLAN
// `CWInterface associateToNetwork:password:error:`.
//
// Notes for Phase 1:
//   - macOS does NOT expose a programmatic API to start a Wi-Fi hotspot.
//     Apple gates that behind the System Settings UI. That's why the tie-
//     break in CLAUDE.md / plan.md is "Windows always hosts when present".
//     The host role here throws on call — misuse from the higher layer
//     fails loudly instead of silently doing nothing.
//   - `associateToNetwork:` may show a one-shot system prompt the first
//     time the app asks to join an unknown SSID. Subsequent joins are
//     silent.
//   - Wi-Fi must be powered on. If `[CWInterface power]` is NO we flip
//     it on before scanning so the join can succeed on a cold start.

#import <Foundation/Foundation.h>
#import <CoreWLAN/CoreWLAN.h>

#include "IConnectionBroker.h"
#include "Logger.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

namespace BetterSend {

namespace {

constexpr const char* kComponent = "WifiClient";

NSString* toNs(const std::string& s) {
	return [NSString stringWithUTF8String:s.c_str()];
}

CWNetwork* findNetwork(CWInterface* iface, NSString* ssid) {
	// scanForNetworksWithName: returns matches in one pass; if the radio
	// missed the beacon — or BT contention left the Wi-Fi scanner
	// "Resource busy" — we back off and try again. The Windows hotspot
	// often takes a few seconds to become visible after StartTetheringAsync
	// returns, so a longer window helps cold starts.
	constexpr int kAttempts        = 6;
	constexpr int kBackoffMs       = 1500;
	constexpr int kBusyBackoffMs   = 2500;
	for (int attempt = 0; attempt < kAttempts; ++attempt) {
		NSError* scanErr = nil;
		NSSet<CWNetwork*>* found = [iface scanForNetworksWithName:ssid error:&scanErr];
		if (scanErr) {
			const char* desc = scanErr.localizedDescription.UTF8String;
			BS_LOG_WARN(kComponent, "scan attempt {} failed: {}", attempt, desc);
			const bool resourceBusy =
				desc && std::string(desc).find("Resource busy") != std::string::npos;
			std::this_thread::sleep_for(std::chrono::milliseconds(
				resourceBusy ? kBusyBackoffMs : kBackoffMs));
			continue;
		}
		for (CWNetwork* net in found) {
			if ([net.ssid isEqualToString:ssid]) return net;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(kBackoffMs));
	}
	return nil;
}

} // namespace

class MacWifiClientBroker : public IConnectionBroker {
public:
	~MacWifiClientBroker() override { stop(); }

	[[nodiscard]] Credentials startHost() override {
		throw std::runtime_error(
			"MacWifiClientBroker: host role not supported on macOS. "
			"Programmatic Wi-Fi hotspot is not exposed by Apple. "
			"In Phase 1, Windows always hosts.");
	}

	bool joinNetwork(const std::string& ssid, const std::string& psk) override {
		@autoreleasepool {
			CWInterface* iface = [[CWWiFiClient sharedWiFiClient] interface];
			if (!iface) {
				BS_LOG_ERROR(kComponent, "No CWInterface available");
				return false;
			}
			if (!iface.powerOn) {
				NSError* powerErr = nil;
				[iface setPower:YES error:&powerErr];
				if (powerErr) {
					BS_LOG_ERROR(kComponent, "setPower failed: {}",
						powerErr.localizedDescription.UTF8String);
					return false;
				}
			}

			NSString* nsSsid = toNs(ssid);
			NSString* nsPsk  = toNs(psk);

			CWNetwork* net = findNetwork(iface, nsSsid);
			if (!net) {
				BS_LOG_ERROR(kComponent, "SSID '{}' not visible after scans", ssid);
				return false;
			}

			NSError* assocErr = nil;
			BOOL ok = [iface associateToNetwork:net password:nsPsk error:&assocErr];
			if (!ok) {
				BS_LOG_ERROR(kComponent, "associateToNetwork failed: {}",
					assocErr ? assocErr.localizedDescription.UTF8String : "unknown");
				return false;
			}
			joinedSsid_ = ssid;
			BS_LOG_INFO(kComponent, "Joined '{}'", ssid);
			return true;
		}
	}

	void stop() override {
		if (joinedSsid_.empty()) return;
		@autoreleasepool {
			CWInterface* iface = [[CWWiFiClient sharedWiFiClient] interface];
			if (iface) {
				// disassociate is silent and idempotent. We don't power Wi-Fi
				// off — the user may rely on it for everything else.
				[iface disassociate];
				BS_LOG_INFO(kComponent, "Disassociated from '{}'", joinedSsid_);
			}
		}
		joinedSsid_.clear();
	}

private:
	std::string joinedSsid_;
};

std::unique_ptr<IConnectionBroker> makeConnectionBroker() {
	return std::make_unique<MacWifiClientBroker>();
}

} // namespace BetterSend
