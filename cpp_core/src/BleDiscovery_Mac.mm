// ── BleDiscovery_Mac.mm ──────────────────────────────────────────────────────
// macOS implementation of BetterSend::IDiscovery on top of BLE (CoreBluetooth).
// Built only when the host platform is Apple-desktop (see CMakeLists.txt guard).
//
// Stack: Objective-C++ over CoreBluetooth.
//   - CBPeripheralManager  → advertise Service UUID + LocalName
//   - CBCentralManager     → scan with services:nil, recognise BOTH the
//                            Apple-style ServiceUUID advert and the Windows
//                            ManufacturerData advert in software.
//
// Apple-only quirk: CBPeripheralManager.startAdvertising: silently drops
// every advertisement key except CBAdvertisementDataServiceUUIDsKey and
// CBAdvertisementDataLocalNameKey. ManufacturerData never leaves the device,
// so the wire payload is asymmetric and the scanner has to know both shapes:
//   Mac→Windows: 128-bit Service UUID kBleServiceUuid + Local Name = device name
//   Windows→Mac: CompanyId 0xFFFF + magic prefix + Local Name = device name
//
// scanForPeripheralsWithServices: filters at the BLE controller — passing
// the BetterSend UUID would block every Windows packet (Windows can't put
// arbitrary 128-bit Service UUIDs into legacy advertisements at the same
// time as ManufacturerData; advertisement byte budget is 31). So the Mac
// scans unfiltered and does the signature check itself.
//
// CoreBluetooth delivers all delegate callbacks on a serial dispatch queue
// we own, matching IDiscovery's "callbacks may come from a background
// thread" contract.

#import <Foundation/Foundation.h>
#import <CoreBluetooth/CoreBluetooth.h>

#include "BleDiscovery.h"
#include "Constants.h"
#include "Logger.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace BetterSend {
	constexpr const char* kComponent = "BleDisc";
	class BleDiscoveryMac;
}

// ── ObjC delegate ─────────────────────────────────────────────────────────────
// Single object plays both CBPeripheralManagerDelegate and CBCentralManagerDelegate.
// Forwards state-changes and discoveries back to the owning BleDiscoveryMac.

@interface BSBleMacDelegate : NSObject <CBPeripheralManagerDelegate, CBCentralManagerDelegate>
@property (nonatomic, strong) CBPeripheralManager* peripheral;
@property (nonatomic, strong) CBCentralManager*   central;
@property (nonatomic, strong) CBUUID*             serviceUuid;
@property (nonatomic, copy)   NSString*           advertName;
@property (nonatomic, assign) BOOL                wantAdvertise;
@property (nonatomic, assign) BOOL                wantScan;
@property (atomic,    assign) uint64_t            scanDutyGen;
@property (nonatomic, assign) BetterSend::BleDiscoveryMac* owner;
@end

namespace BetterSend {

static NSString* makeAdvertName(const std::string& deviceName) {
	std::string trimmed = deviceName;
	if (trimmed.size() > static_cast<size_t>(kBleMaxNameLen)) {
		trimmed.resize(kBleMaxNameLen);
	}
	return [NSString stringWithUTF8String:trimmed.c_str()];
}

// Recognise the Windows ManufacturerData signature:
//   [companyId LE = 0xFF 0xFF][magic 4B][UTF-8 name...]
// Returns the trailing name on a match, empty string otherwise.
static std::string toLowerAscii(const std::string& s) {
	std::string out;
	out.reserve(s.size());
	for (unsigned char c : s) out.push_back(static_cast<char>(std::tolower(c)));
	return out;
}

static std::string extractWindowsPeerName(NSData* mfgData) {
	if (!mfgData) return {};
	const NSUInteger headLen = 2 + sizeof(kBleMagicBytes);
	if (mfgData.length < headLen) return {};
	const uint8_t* p = static_cast<const uint8_t*>(mfgData.bytes);
	if (p[0] != (kBleCompanyId & 0xFF)) return {};
	if (p[1] != ((kBleCompanyId >> 8) & 0xFF)) return {};
	for (size_t i = 0; i < sizeof(kBleMagicBytes); ++i) {
		if (p[2 + i] != kBleMagicBytes[i]) return {};
	}
	return std::string(reinterpret_cast<const char*>(p + headLen),
		mfgData.length - headLen);
}

class BleDiscoveryMac : public IDiscovery {
public:
	BleDiscoveryMac() {
		@autoreleasepool {
			delegate_             = [[BSBleMacDelegate alloc] init];
			delegate_.owner       = this;
			delegate_.serviceUuid = [CBUUID UUIDWithString:@(kBleServiceUuid)];
			queue_                = dispatch_queue_create("com.bettersend.ble", DISPATCH_QUEUE_SERIAL);
		}
	}

	~BleDiscoveryMac() override { stop(); }

	void startAdvertising(const std::string& deviceName, int /*port*/) override {
		BS_LOG_INFO(kComponent, "BLE advertise: name='{}' serviceUuid={}",
			deviceName, kBleServiceUuid);
		@autoreleasepool {
			delegate_.advertName    = makeAdvertName(deviceName);
			delegate_.wantAdvertise = YES;
			advertising_.store(true);
			if (!delegate_.peripheral) {
				delegate_.peripheral = [[CBPeripheralManager alloc]
					initWithDelegate:delegate_ queue:queue_];
			} else if (delegate_.peripheral.state == CBManagerStatePoweredOn) {
				kickAdvertise();
			}
		}
	}

	void startDiscovery(std::function<void(Device)> onFound) override {
		BS_LOG_INFO(kComponent, "BLE discovery: serviceUuid={}", kBleServiceUuid);
		onFound_ = std::move(onFound);
		@autoreleasepool {
			delegate_.wantScan = YES;
			scanning_.store(true);
			if (!delegate_.central) {
				delegate_.central = [[CBCentralManager alloc]
					initWithDelegate:delegate_ queue:queue_];
			} else if (delegate_.central.state == CBManagerStatePoweredOn) {
				startScanDutyCycle();
			}
		}
	}

	// Pause / resume the BLE roles around an external operation that needs
	// the shared radio (today: future programmatic Wi-Fi join via
	// NEHotspotConfiguration — Phase 1 Path B leaves the user to join the
	// hotspot from the Wi-Fi menu and does not call these). Just stop/start
	// the scan or advertise without tearing down the manager objects: the
	// over-engineered "nil out CBCentralManager / CBPeripheralManager
	// during the handoff" trick was an attempt to clear CoreWLAN's
	// "Resource busy" on Apple silicon, but `scanForNetworksWithName:` is a
	// dead-end API in 2026 regardless. The principled fix is
	// NEHotspotConfiguration, which doesn't need BLE to release the radio.
	void pauseScan() override {
		@autoreleasepool {
			delegate_.scanDutyGen = ++dutyGenCounter_;  // cancel the duty cycle
			if (delegate_.central && delegate_.central.isScanning) {
				[delegate_.central stopScan];
				BS_LOG_DEBUG(kComponent, "Scan paused");
			}
		}
	}

	void resumeScan() override {
		@autoreleasepool {
			if (delegate_.wantScan
			    && delegate_.central
			    && delegate_.central.state == CBManagerStatePoweredOn) {
				startScanDutyCycle();
				BS_LOG_DEBUG(kComponent, "Scan resumed");
			}
		}
	}

	void pauseAdvertise() override {
		@autoreleasepool {
			if (delegate_.peripheral && delegate_.peripheral.isAdvertising) {
				[delegate_.peripheral stopAdvertising];
				BS_LOG_DEBUG(kComponent, "Advertise paused");
			}
		}
	}

	void resumeAdvertise() override {
		@autoreleasepool {
			if (delegate_.wantAdvertise
			    && delegate_.peripheral
			    && delegate_.peripheral.state == CBManagerStatePoweredOn) {
				kickAdvertise();
				BS_LOG_DEBUG(kComponent, "Advertise resumed");
			}
		}
	}

	void stop() override {
		if (advertising_.exchange(false)) {
			@autoreleasepool {
				delegate_.wantAdvertise = NO;
				if (delegate_.peripheral && delegate_.peripheral.isAdvertising) {
					[delegate_.peripheral stopAdvertising];
				}
			}
			BS_LOG_DEBUG(kComponent, "Advertise stopped");
		}
		if (scanning_.exchange(false)) {
			@autoreleasepool {
				delegate_.wantScan    = NO;
				delegate_.scanDutyGen = ++dutyGenCounter_;  // cancel the duty cycle
				if (delegate_.central) [delegate_.central stopScan];
			}
			BS_LOG_DEBUG(kComponent, "Scan stopped");
		}
	}

	// ── Called by the ObjC delegate on the Bluetooth queue ──────────────────
	void onPeripheralReady() { kickAdvertise();      }
	void onCentralReady()    { startScanDutyCycle(); }

	void onPeerDiscovered(const std::string& peerName, NSString* fallbackId) {
		if (peerName.empty()) return;
		// Throttle + canonical-name dedupe. BLE adverts fire 10–30 Hz; we
		// surface a single event every kPeerHeartbeatSec so the Flutter side
		// can prune dead peers. The key is lowercased so the Windows peer's
		// two advertisements (BluetoothLEAdvertisementPublisher carrying our
		// magic+name in UPPERCASE; GattServiceProvider carrying the system
		// Bluetooth friendly name in mixed case) collapse to a single entry.
		// First name seen wins as the canonical display string.
		const std::string key = toLowerAscii(peerName);
		const auto now = std::chrono::steady_clock::now();
		std::string canonical;
		{
			std::lock_guard<std::mutex> lock(seenMu_);
			auto& entry = throttle_[key];
			if (entry.canonical.empty()) entry.canonical = peerName;
			if (entry.lastEmit.time_since_epoch().count() != 0 &&
			    now - entry.lastEmit < std::chrono::seconds(kPeerHeartbeatSec)) {
				return;
			}
			entry.lastEmit = now;
			canonical = entry.canonical;
		}
		const std::string idStr = fallbackId ? std::string(fallbackId.UTF8String) : canonical;
		BS_LOG_INFO(kComponent, "Found peer: name='{}' id={}", canonical, idStr);
		if (onFound_) {
			onFound_(Device{canonical, std::string("ble:") + idStr, kDefaultPort});
		}
	}

private:
	void kickAdvertise() {
		if (!delegate_.wantAdvertise || !delegate_.advertName) return;
		if (delegate_.peripheral.isAdvertising) return;
		NSDictionary* opts = @{
			CBAdvertisementDataServiceUUIDsKey : @[ delegate_.serviceUuid ],
			CBAdvertisementDataLocalNameKey    : delegate_.advertName,
		};
		[delegate_.peripheral startAdvertising:opts];
	}

	// Start (or restart) the scan duty-cycle. Apple silicon shares one antenna
	// between BT and Wi-Fi, and a continuous AllowDuplicates scan keeps the
	// radio in RX — starving this Mac's own advertise so the Windows watcher
	// never catches a packet (asymmetric: Mac sees Windows, Windows never sees
	// Mac). So we scan for kBleScanWindowMs, then stop the scan for
	// kBleAdvertiseWindowMs to give the advertise a clean TX window, and loop.
	// Discovery still catches the continuously-advertising Windows peer during
	// the scan windows.
	void startScanDutyCycle() {
		if (!delegate_.wantScan) return;
		const uint64_t gen = ++dutyGenCounter_;
		delegate_.scanDutyGen = gen;
		scheduleScanWindow(delegate_, queue_, gen);
	}

	// Recurring window driver. Captures the delegate (ARC-retained, so it
	// outlives this C++ object) and a generation token — NEVER `this` — so a
	// pending block can't dereference a freed BleDiscoveryMac after stop().
	// A stale generation (bumped by stop/pauseScan/restart) makes the block a
	// no-op and ends the chain.
	static void scheduleScanWindow(BSBleMacDelegate* d, dispatch_queue_t q, uint64_t gen) {
		if (!d.wantScan || d.scanDutyGen != gen) return;
		// services:nil — accept every peripheral; the signature check in
		// didDiscoverPeripheral admits both the Apple ServiceUUID advert and
		// the Windows ManufacturerData advert. AllowDuplicates=YES keeps the
		// heartbeat throttle in onPeerDiscovered fed with fresh callbacks.
		if (d.central
		    && d.central.state == CBManagerStatePoweredOn
		    && !d.central.isScanning) {
			NSDictionary* opts = @{ CBCentralManagerScanOptionAllowDuplicatesKey : @YES };
			[d.central scanForPeripheralsWithServices:nil options:opts];
		}
		dispatch_after(
			dispatch_time(DISPATCH_TIME_NOW, (int64_t)kBleScanWindowMs * NSEC_PER_MSEC),
			q, ^{
				if (!d.wantScan || d.scanDutyGen != gen) return;
				if (d.central.isScanning) [d.central stopScan];   // advertise window
				dispatch_after(
					dispatch_time(DISPATCH_TIME_NOW, (int64_t)kBleAdvertiseWindowMs * NSEC_PER_MSEC),
					q, ^{ scheduleScanWindow(d, q, gen); });
			});
	}

	struct ThrottleEntry {
		std::chrono::steady_clock::time_point lastEmit;
		std::string                           canonical;
	};

	BSBleMacDelegate*                                delegate_{nil};
	dispatch_queue_t                                 queue_{nullptr};
	std::function<void(Device)>                      onFound_;
	std::atomic<bool>                                advertising_{false};
	std::atomic<bool>                                scanning_{false};
	std::atomic<uint64_t>                            dutyGenCounter_{0};
	std::mutex                                       seenMu_;
	std::unordered_map<std::string, ThrottleEntry>   throttle_;
};

// ── Factory ───────────────────────────────────────────────────────────────────

std::unique_ptr<IDiscovery> makeBleDiscovery() {
	return std::make_unique<BleDiscoveryMac>();
}

// Cross-platform makeDiscovery() lives in one .cpp/.mm per platform. On macOS
// the production discovery is BLE; BonjourDiscovery.cpp is excluded from the
// Apple build (see CMakeLists.txt) and stays in the tree for the future
// Apple↔Apple (AWDL) pair.
std::unique_ptr<IDiscovery> makeDiscovery() {
	return makeBleDiscovery();
}

} // namespace BetterSend

// ── ObjC delegate impl ────────────────────────────────────────────────────────

@implementation BSBleMacDelegate

- (void)peripheralManagerDidUpdateState:(CBPeripheralManager *)peripheral {
	using namespace BetterSend;
	switch (peripheral.state) {
		case CBManagerStatePoweredOn:
			BS_LOG_INFO(kComponent, "Peripheral powered on");
			if (self.owner) self.owner->onPeripheralReady();
			break;
		case CBManagerStateUnauthorized:
			BS_LOG_ERROR(kComponent,
				"Peripheral unauthorized — Info.plist NSBluetoothAlwaysUsageDescription missing or user denied");
			break;
		case CBManagerStateUnsupported:
			BS_LOG_ERROR(kComponent, "Peripheral unsupported on this hardware");
			break;
		case CBManagerStatePoweredOff:
			BS_LOG_WARN(kComponent, "Peripheral powered off");
			break;
		default:
			BS_LOG_DEBUG(kComponent, "Peripheral state: {}", static_cast<int>(peripheral.state));
			break;
	}
}

- (void)peripheralManagerDidStartAdvertising:(CBPeripheralManager *)peripheral
                                       error:(NSError *)error {
	using namespace BetterSend;
	if (error) {
		BS_LOG_ERROR(kComponent, "Advertise start failed: {}",
			error.localizedDescription.UTF8String);
	} else {
		BS_LOG_INFO(kComponent, "Advertise started");
	}
}

- (void)centralManagerDidUpdateState:(CBCentralManager *)central {
	using namespace BetterSend;
	switch (central.state) {
		case CBManagerStatePoweredOn:
			BS_LOG_INFO(kComponent, "Central powered on");
			if (self.owner) self.owner->onCentralReady();
			break;
		case CBManagerStateUnauthorized:
			BS_LOG_ERROR(kComponent,
				"Central unauthorized — Info.plist NSBluetoothAlwaysUsageDescription missing or user denied");
			break;
		case CBManagerStateUnsupported:
			BS_LOG_ERROR(kComponent, "Central unsupported on this hardware");
			break;
		case CBManagerStatePoweredOff:
			BS_LOG_WARN(kComponent, "Central powered off");
			break;
		default:
			BS_LOG_DEBUG(kComponent, "Central state: {}", static_cast<int>(central.state));
			break;
	}
}

- (void)centralManager:(CBCentralManager *)central
 didDiscoverPeripheral:(CBPeripheral *)peripheral
     advertisementData:(NSDictionary<NSString *,id> *)advertisementData
                  RSSI:(NSNumber *)RSSI {
	using namespace BetterSend;

	// Path A — Apple peer: BetterSend Service UUID is in the advert list.
	std::string name;
	NSArray<CBUUID*>* uuids = advertisementData[CBAdvertisementDataServiceUUIDsKey];
	BOOL isBetterSendApple = NO;
	if (uuids) {
		for (CBUUID* u in uuids) {
			if ([u isEqual:self.serviceUuid]) { isBetterSendApple = YES; break; }
		}
	}
	if (isBetterSendApple) {
		// Only trust LocalName from the live advertisement. peripheral.name
		// is the OS-cached Bluetooth Classic friendly name, which surfaces
		// even when the peer isn't running BetterSend and double-emits the
		// same Windows machine (once via the Windows GattServiceProvider
		// advert that carries our ServiceUUID without a LocalName, once via
		// the BluetoothLEAdvertisementPublisher with our ManufacturerData).
		NSString* localName = advertisementData[CBAdvertisementDataLocalNameKey];
		if (localName.length > 0) name = std::string(localName.UTF8String);
	}

	// Path B — Windows peer: ManufacturerData carries [0xFF 0xFF][magic][name].
	if (name.empty()) {
		NSData* mfgData = advertisementData[CBAdvertisementDataManufacturerDataKey];
		name = extractWindowsPeerName(mfgData);
	}

	if (name.empty()) return; // neither signature — not a BetterSend peer

	if (self.owner) {
		self.owner->onPeerDiscovered(name, peripheral.identifier.UUIDString);
	}
}

@end
