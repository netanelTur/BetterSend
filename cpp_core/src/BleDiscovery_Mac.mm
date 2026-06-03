// ── BleDiscovery_Mac.mm ──────────────────────────────────────────────────────
// macOS implementation of BetterSend::IDiscovery on top of BLE (CoreBluetooth).
// Built only when the host platform is Apple-desktop (see CMakeLists.txt guard).
//
// Stack: Objective-C++ over CoreBluetooth.
//   - CBPeripheralManager  → advertise Service UUID + LocalName
//   - CBCentralManager     → scan filtered on the same Service UUID
//
// Apple-only quirk: CBPeripheralManager.startAdvertising: silently drops
// every advertisement key except CBAdvertisementDataServiceUUIDsKey and
// CBAdvertisementDataLocalNameKey. ManufacturerData never leaves the device,
// so the Windows side (which advertises ManufacturerData and also listens
// for our Service UUID) and the Mac side use distinct payload shapes that
// the peer recognises:
//   Mac→Windows: 128-bit Service UUID kBleServiceUuid + Local Name = device name
//   Windows→Mac: CompanyId 0xFFFF + magic prefix + Local Name = device name
//
// CoreBluetooth delivers all delegate callbacks on a serial dispatch queue
// we own, matching IDiscovery's "callbacks may come from a background
// thread" contract.

#import <Foundation/Foundation.h>
#import <CoreBluetooth/CoreBluetooth.h>

#include "BleDiscovery.h"
#include "Constants.h"
#include "Logger.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_set>

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
				kickScan();
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
				delegate_.wantScan = NO;
				if (delegate_.central) [delegate_.central stopScan];
			}
			BS_LOG_DEBUG(kComponent, "Scan stopped");
		}
	}

	// ── Called by the ObjC delegate on the Bluetooth queue ──────────────────
	void onPeripheralReady() { kickAdvertise(); }
	void onCentralReady()    { kickScan();      }

	void onPeerDiscovered(const std::string& peerName, NSString* fallbackId) {
		if (peerName.empty()) return;
		// Dedupe by name: Windows rotates its BLE MAC every restart, so
		// peripheral.identifier.UUIDString shifts under us. The device name is
		// stable for the lifetime of the BetterSend session on each side.
		{
			std::lock_guard<std::mutex> lock(seenMu_);
			if (!seen_.insert(peerName).second) return;
		}
		const std::string idStr = fallbackId ? std::string(fallbackId.UTF8String) : peerName;
		BS_LOG_INFO(kComponent, "Found peer: name='{}' id={}", peerName, idStr);
		if (onFound_) {
			onFound_(Device{peerName, std::string("ble:") + idStr, kDefaultPort});
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

	void kickScan() {
		if (!delegate_.wantScan) return;
		if (delegate_.central.isScanning) return;
		// Hardware-level filter on the BetterSend Service UUID. AllowDuplicates
		// stays off — we'd just throw the dupes away, and the OS already filters
		// re-emissions of the same peer to once-per-discovery cycle.
		NSDictionary* opts = @{ CBCentralManagerScanOptionAllowDuplicatesKey : @NO };
		[delegate_.central scanForPeripheralsWithServices:@[ delegate_.serviceUuid ]
		                                          options:opts];
	}

	BSBleMacDelegate*               delegate_{nil};
	dispatch_queue_t                queue_{nullptr};
	std::function<void(Device)>     onFound_;
	std::atomic<bool>               advertising_{false};
	std::atomic<bool>               scanning_{false};
	std::mutex                      seenMu_;
	std::unordered_set<std::string> seen_;
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

	// Prefer the LocalName carried in the advertisement payload; some peers
	// only publish the name in the scan response that CoreBluetooth surfaces
	// here as peripheral.name. Fall back to that, then to the system-issued
	// peripheral identifier so we never call back with an empty name.
	NSString* localName = advertisementData[CBAdvertisementDataLocalNameKey];
	if (localName.length == 0) localName = peripheral.name;

	std::string name;
	if (localName.length > 0) {
		name = std::string(localName.UTF8String);
	} else {
		// No name at all — skip. Without a stable name we can't dedup across
		// MAC-rotations on the peer side either.
		return;
	}

	if (self.owner) {
		self.owner->onPeerDiscovered(name, peripheral.identifier.UUIDString);
	}
}

@end
