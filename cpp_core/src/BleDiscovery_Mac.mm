// ── BleDiscovery_Mac.mm ──────────────────────────────────────────────────────
// macOS implementation of BetterSend::IDiscovery on top of BLE (CoreBluetooth).
// Built only when the host platform is Apple-desktop (see CMakeLists.txt guard).
//
// Stack: Objective-C++ over CoreBluetooth.
//   - CBPeripheralManager  → broadcast ManufacturerData=[companyId][magic][name]
//   - CBCentralManager     → scan for peers carrying that same payload
//
// macOS quirk: CBAdvertisementDataManufacturerDataKey expects the raw bytes
// **after** the BLE AD type byte, meaning the first 2 bytes ARE the
// little-endian company ID. So the payload below begins with {0xFF,0xFF} to
// match the Windows publisher's CompanyId=0xFFFF + Data=[magic][name].
//
// CoreBluetooth delivers all delegate callbacks on a dispatch queue we own,
// matching IDiscovery's "callbacks may come from a background thread" contract.

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
@property (nonatomic, copy)   NSData*             advertPayload;
@property (nonatomic, assign) BOOL                wantAdvertise;
@property (nonatomic, assign) BOOL                wantScan;
@property (nonatomic, assign) BetterSend::BleDiscoveryMac* owner;
@end

namespace BetterSend {

// Build the manufacturer-data payload as macOS expects it:
//   [companyId LE][magic][name UTF-8 trimmed to kBleMaxNameLen]
static NSData* makeAdvertPayloadNS(const std::string& deviceName) {
	NSMutableData* d = [NSMutableData dataWithCapacity:2 + sizeof(kBleMagicBytes) + kBleMaxNameLen];
	const uint8_t cid[2] = {
		static_cast<uint8_t>(kBleCompanyId & 0xFF),
		static_cast<uint8_t>((kBleCompanyId >> 8) & 0xFF),
	};
	[d appendBytes:cid length:2];
	[d appendBytes:kBleMagicBytes length:sizeof(kBleMagicBytes)];
	int n = static_cast<int>(deviceName.size());
	if (n > kBleMaxNameLen) n = kBleMaxNameLen;
	if (n > 0) [d appendBytes:deviceName.data() length:static_cast<NSUInteger>(n)];
	return d;
}

// Decode the peer name from incoming ManufacturerData. Returns empty string if
// the company ID or magic prefix don't match BetterSend.
static std::string extractPeerName(NSData* mfgData) {
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
			delegate_       = [[BSBleMacDelegate alloc] init];
			delegate_.owner = this;
			queue_          = dispatch_queue_create("com.bettersend.ble", DISPATCH_QUEUE_SERIAL);
		}
	}

	~BleDiscoveryMac() override { stop(); }

	void startAdvertising(const std::string& deviceName, int /*port*/) override {
		BS_LOG_INFO(kComponent, "BLE advertise: name='{}' companyId={:#06x}",
			deviceName, kBleCompanyId);
		@autoreleasepool {
			delegate_.advertPayload = makeAdvertPayloadNS(deviceName);
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
		BS_LOG_INFO(kComponent, "BLE discovery: companyId={:#06x}", kBleCompanyId);
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

	void onPeerDiscovered(NSString* peerId, const std::string& peerName) {
		if (!peerId || peerName.empty()) return;
		const std::string idStr(peerId.UTF8String);
		{
			std::lock_guard<std::mutex> lock(seenMu_);
			if (!seen_.insert(idStr).second) return;
		}
		BS_LOG_INFO(kComponent, "Found peer: name='{}' id={}", peerName, idStr);
		if (onFound_) {
			onFound_(Device{peerName, std::string("ble:") + idStr, kDefaultPort});
		}
	}

private:
	void kickAdvertise() {
		if (!delegate_.wantAdvertise || !delegate_.advertPayload) return;
		if (delegate_.peripheral.isAdvertising) return;
		NSDictionary* opts = @{
			(NSString*)CBAdvertisementDataManufacturerDataKey : delegate_.advertPayload,
		};
		[delegate_.peripheral startAdvertising:opts];
	}

	void kickScan() {
		if (!delegate_.wantScan) return;
		if (delegate_.central.isScanning) return;
		// services:nil delivers every advertisement; we filter by manufacturer data
		// in the receive path. macOS doesn't expose a hardware-level manufacturer
		// filter (only Service UUID), so software-side filtering it is.
		[delegate_.central scanForPeripheralsWithServices:nil options:nil];
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
	NSData* mfgData = advertisementData[CBAdvertisementDataManufacturerDataKey];
	const std::string name = extractPeerName(mfgData);
	if (name.empty()) return; // not a BetterSend peer

	if (self.owner) {
		self.owner->onPeerDiscovered(peripheral.identifier.UUIDString, name);
	}
}

@end
