// ── MacWifiClientBroker.mm ───────────────────────────────────────────────────
// macOS implementation of BetterSend::IConnectionBroker — client role only.
// Built only when the host platform is Apple-desktop (CMakeLists.txt guard).
//
// Why this file is shaped like it is:
//   - Apple's modern Wi-Fi join API, `NEHotspotConfiguration`, is iOS-only
//     (`API_UNAVAILABLE(macos, tvos)` in the SDK headers). It does not exist
//     on macOS, period. Anyone who tells you otherwise is wrong; the SDK
//     header is the ground truth.
//   - The legacy CoreWLAN path — `CWInterface scanForNetworksWithName:` +
//     `associateToNetwork:password:error:` — is a dead-end on macOS 11+.
//     The scan call returns "Resource busy" indefinitely regardless of BLE
//     state. We tried three rounds of BLE-side mitigation (pause scan,
//     pause advertise, nil-out both CBManagers + 4 s sleep) — none of them
//     made `scanForNetworksWithName:` succeed.
//   - The actually-working programmatic path on macOS is the
//     `/usr/sbin/networksetup -setairportnetwork <iface> <ssid> <psk>`
//     command. It is documented, ships with macOS, and bypasses the broken
//     scan API entirely. We drive it via `NSTask` so we get exit status +
//     stdout/stderr in the log.
//
// Sandbox / signing notes:
//   - Launching `/usr/sbin/networksetup` from a hardened sandboxed app is
//     blocked. The Flutter Mac runner ships with sandbox=YES out of the
//     box. We drop `com.apple.security.app-sandbox` in
//     `DebugProfile.entitlements` + `Release.entitlements` so `NSTask` can
//     execute the system binary. macOS may still surface a one-shot
//     keychain / "Allow this app to control Wi-Fi" prompt on first use —
//     subsequent joins are silent.
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

// Resolve the active Wi-Fi interface name. On most Macs it's `en0`,
// but Mac Pros / docks can shift it. `CWWiFiClient interfaceNames`
// returns every adapter the OS recognises; the first one is the
// primary Wi-Fi interface in practice.
NSString* wifiInterfaceName() {
	NSArray<NSString*>* names = [CWWiFiClient interfaceNames];
	if (names.count > 0) return names.firstObject;
	return @"en0";
}

// Drive `/usr/sbin/networksetup -setairportnetwork`. Returns true if the
// command exits with status 0 AND its stdout does not contain a known
// failure marker (`networksetup` exits 0 even on join failure and prints
// the actual outcome to stdout — classic POSIX-shell mistake on Apple's
// part, but we work around it).
bool runNetworksetupJoin(NSString* iface, NSString* ssid, NSString* psk) {
	NSTask* task = [[NSTask alloc] init];
	task.launchPath = @"/usr/sbin/networksetup";
	task.arguments  = @[ @"-setairportnetwork", iface, ssid, psk ];
	NSPipe* outPipe = [NSPipe pipe];
	NSPipe* errPipe = [NSPipe pipe];
	task.standardOutput = outPipe;
	task.standardError  = errPipe;

	NSError* launchErr = nil;
	if (@available(macOS 10.13, *)) {
		[task launchAndReturnError:&launchErr];
	} else {
		@try { [task launch]; }
		@catch (NSException* e) {
			launchErr = [NSError errorWithDomain:NSPOSIXErrorDomain code:1
				userInfo:@{ NSLocalizedDescriptionKey: e.reason ?: @"launch failed" }];
		}
	}
	if (launchErr) {
		BS_LOG_ERROR(kComponent, "NSTask launch failed: {}",
			launchErr.localizedDescription.UTF8String);
		return false;
	}
	[task waitUntilExit];

	NSString* stdoutStr = [[NSString alloc]
		initWithData:[outPipe.fileHandleForReading readDataToEndOfFile]
		    encoding:NSUTF8StringEncoding] ?: @"";
	NSString* stderrStr = [[NSString alloc]
		initWithData:[errPipe.fileHandleForReading readDataToEndOfFile]
		    encoding:NSUTF8StringEncoding] ?: @"";

	BS_LOG_INFO(kComponent,
		"networksetup exit={} stdout='{}' stderr='{}'",
		static_cast<long>(task.terminationStatus),
		stdoutStr.UTF8String, stderrStr.UTF8String);

	if (task.terminationStatus != 0) return false;

	NSString* combined = [stdoutStr stringByAppendingString:stderrStr];
	NSArray<NSString*>* failureMarkers = @[
		@"Failed to join",
		@"Could not find",
		@"not find network",
		@"Error:",
	];
	for (NSString* m in failureMarkers) {
		if ([combined rangeOfString:m options:NSCaseInsensitiveSearch].location != NSNotFound) {
			return false;
		}
	}
	return true;
}

// Warm the OS's known-network list with a fresh active scan.
// `networksetup -setairportnetwork` only joins SSIDs the OS currently
// believes are in range; if the host's beacon hasn't been seen in the
// last few seconds, the join fails with "Could not find network …".
// Unlike the SSID-filtered `scanForNetworksWithName:` (which returns
// "Resource busy" indefinitely on Apple silicon and was the root cause
// of the original Path B fallback), a nil-SSID scan is an open scan
// that returns every visible network and succeeds reliably here.
void primeScan() {
	@autoreleasepool {
		CWInterface* iface = [[CWWiFiClient sharedWiFiClient] interface];
		if (!iface) return;
		NSError* scanErr = nil;
		NSSet* results = [iface scanForNetworksWithSSID:nil error:&scanErr];
		if (scanErr) {
			BS_LOG_DEBUG(kComponent, "Prime scan error: {}",
				scanErr.localizedDescription.UTF8String);
		} else {
			BS_LOG_DEBUG(kComponent, "Prime scan: {} networks visible",
				static_cast<unsigned long>(results.count));
		}
	}
}

// Poll CWWiFiClient until the active SSID matches `expected`. DHCP and
// the interface activation can lag the `networksetup` exit by a second
// or two on cold starts.
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
					"Already on '{}', skipping networksetup join", ssid);
				return true;
			}

			NSString* iface = wifiInterfaceName();
			BS_LOG_INFO(kComponent,
				"networksetup -setairportnetwork {} '{}' (psk redacted)",
				iface.UTF8String, ssid);

			// Retry the join a few times. The first attempt right after
			// the BLE handshake often races: the OS has not yet seen the
			// host's beacon, networksetup prints "Could not find network",
			// then a fresh scan + retry succeeds within a few seconds.
			constexpr int kJoinAttempts = 5;
			constexpr int kJoinBackoffMs = 2000;
			bool joined = false;
			for (int attempt = 1; attempt <= kJoinAttempts; ++attempt) {
				primeScan();
				if (runNetworksetupJoin(iface, nsSsid, nsPsk)) {
					joined = true;
					break;
				}
				BS_LOG_WARN(kComponent,
					"networksetup join attempt {} for '{}' failed; retrying in {} ms",
					attempt, ssid, kJoinBackoffMs);
				std::this_thread::sleep_for(
					std::chrono::milliseconds(kJoinBackoffMs));
			}
			if (!joined) {
				BS_LOG_ERROR(kComponent,
					"networksetup join of '{}' failed after {} attempts",
					ssid, kJoinAttempts);
				return false;
			}

			if (!waitForSsid(nsSsid, 10000)) {
				BS_LOG_ERROR(kComponent,
					"networksetup reported success but interface.ssid never matched '{}' within 10 s",
					ssid);
				return false;
			}

			joinedSsid_ = ssid;
			BS_LOG_INFO(kComponent, "Joined '{}' via networksetup", ssid);
			return true;
		}
	}

	void stop() override {
		if (joinedSsid_.empty()) return;
		@autoreleasepool {
			CWInterface* iface = [[CWWiFiClient sharedWiFiClient] interface];
			if (iface) {
				// Disassociate is silent + idempotent. We don't power Wi-Fi
				// off — the user may rely on it for everything else.
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
