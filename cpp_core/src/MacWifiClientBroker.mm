// ── MacWifiClientBroker.mm ───────────────────────────────────────────────────
// macOS implementation of BetterSend::IConnectionBroker — client role only.
// Built only when the host platform is Apple-desktop (CMakeLists.txt guard).
//
// Join strategy — direct CoreWLAN `associateToNetwork:password:error:`:
//
//   1. The OS does not give us an SSID-only join API on macOS:
//      `NEHotspotConfiguration` is iOS-only (`API_UNAVAILABLE(macos)` in
//      the SDK header), and `networksetup -setairportnetwork` silently
//      lies on macOS 14+ (`exit=0`, empty stdout, no actual association).
//   2. The classic `CWInterface associateToNetwork:password:error:` takes
//      a `CWNetwork*` and works fine — but the older code path obtained
//      that `CWNetwork` via `scanForNetworksWithName:<ssid>`, which is a
//      dead-end on Apple silicon: it returns `Resource busy` forever and
//      no amount of BLE-side teardown clears it.
//   3. A *nil-SSID* scan (`scanForNetworksWithSSID:nil`) is an open scan
//      that returns every visible network and does NOT hit the broken
//      busy state. We filter for our target SSID in code, hand the
//      resulting `CWNetwork` to `associateToNetwork:`, and the OS handles
//      auth + DHCP automatically. No user action needed.
//
// Host role isn't supported here — macOS Phase 1 is always the client.
// `startHost()` throws to fail loudly on misuse.

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

// Find the target SSID in a fresh open scan and `associateToNetwork:`.
// Returns true on a CoreWLAN-reported successful association. DHCP can
// still lag; the caller's `waitForSsid` poll covers that window.
bool joinViaCoreWLAN(NSString* ssid, NSString* psk) {
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

		NSError* scanErr = nil;
		// nil SSID = open scan, returns every visible network. Reliable.
		NSSet<CWNetwork*>* nets = [iface scanForNetworksWithSSID:nil error:&scanErr];
		if (scanErr) {
			BS_LOG_ERROR(kComponent, "Open scan failed: {}",
				scanErr.localizedDescription.UTF8String);
			return false;
		}
		BS_LOG_DEBUG(kComponent, "Open scan: {} networks visible",
			static_cast<unsigned long>(nets.count));

		CWNetwork* target = nil;
		for (CWNetwork* n in nets) {
			if ([n.ssid isEqualToString:ssid]) { target = n; break; }
		}
		if (!target) {
			BS_LOG_ERROR(kComponent,
				"SSID '{}' not in open-scan results", ssid.UTF8String);
			return false;
		}

		NSError* assocErr = nil;
		BOOL ok = [iface associateToNetwork:target
		                           password:psk
		                              error:&assocErr];
		if (!ok) {
			BS_LOG_ERROR(kComponent,
				"associateToNetwork failed: {} (code={})",
				assocErr ? assocErr.localizedDescription.UTF8String : "unknown",
				assocErr ? static_cast<long>(assocErr.code) : 0L);
			return false;
		}
		return true;
	}
}

// Poll CWWiFiClient until the active SSID matches `expected`. DHCP and
// interface activation can lag `associateToNetwork:` by a second or two
// on cold starts.
bool waitForSsid(NSString* expected, int timeoutMs) {
	constexpr int kPollMs = 250;
	int waited = 0;
	while (waited < timeoutMs) {
		CWInterface* iface = [[CWWiFiClient sharedWiFiClient] interface];
		if (iface && [iface.ssid isEqualToString:expected]) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
		waited += kPollMs;
	}
	return false;
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
			NSString* nsSsid = toNs(ssid);
			NSString* nsPsk  = toNs(psk);

			// Fast path: already on the target network.
			CWInterface* current = [[CWWiFiClient sharedWiFiClient] interface];
			if (current && [current.ssid isEqualToString:nsSsid]) {
				joinedSsid_ = ssid;
				BS_LOG_INFO(kComponent,
					"Already on '{}', skipping associateToNetwork", ssid);
				return true;
			}

			BS_LOG_INFO(kComponent,
				"associateToNetwork '{}' (psk redacted, len={})",
				ssid, static_cast<unsigned long>(nsPsk.length));

			// Retry the whole open-scan + associate cycle a few times. Since
			// the join now happens at user tap-time (the Windows host has been
			// beaconing for seconds), attempt 1 almost always wins and the
			// backoff never fires. The backoff MUST stay >= 8 s: airportd
			// rate-limits back-to-back open scans inside a ~5-6 s window
			// (returns "Resource busy"), so a shorter retry just burns a
			// rejected scan. 3 attempts fails fast to the connect spinner.
			constexpr int kJoinAttempts  = 3;
			constexpr int kJoinBackoffMs = 8000;
			bool associated = false;
			for (int attempt = 1; attempt <= kJoinAttempts; ++attempt) {
				if (joinViaCoreWLAN(nsSsid, nsPsk)) {
					associated = true;
					BS_LOG_INFO(kComponent,
						"associateToNetwork '{}' OK on attempt {}",
						ssid, attempt);
					break;
				}
				BS_LOG_WARN(kComponent,
					"Join attempt {} for '{}' failed; retrying in {} ms",
					attempt, ssid, kJoinBackoffMs);
				std::this_thread::sleep_for(
					std::chrono::milliseconds(kJoinBackoffMs));
			}
			if (!associated) {
				BS_LOG_ERROR(kComponent,
					"Join of '{}' failed after {} attempts",
					ssid, kJoinAttempts);
				return false;
			}

			if (!waitForSsid(nsSsid, 10000)) {
				BS_LOG_ERROR(kComponent,
					"associateToNetwork returned OK but interface.ssid never matched '{}' within 10 s",
					ssid);
				return false;
			}

			joinedSsid_ = ssid;
			BS_LOG_INFO(kComponent, "Joined '{}' via CoreWLAN", ssid);
			return true;
		}
	}

	void stop() override {
		if (joinedSsid_.empty()) return;
		@autoreleasepool {
			CWInterface* iface = [[CWWiFiClient sharedWiFiClient] interface];
			if (iface) {
				[iface disassociate];
				BS_LOG_INFO(kComponent,
					"Disassociated from '{}'", joinedSsid_);
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
