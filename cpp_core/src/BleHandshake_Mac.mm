// ── BleHandshake_Mac.mm ──────────────────────────────────────────────────────
// macOS implementation of BetterSend::IPeerHandshake — client role only.
// Built only when the host platform is Apple-desktop (CMakeLists.txt guard).
//
// Flow inside fetchPayload(peerId, timeoutSeconds):
//   1. Spin up our own CBCentralManager on a private serial dispatch queue.
//   2. Once it powers on, scan filtered by service UUID. Windows runs two
//      concurrent BLE publishers: a BluetoothLEAdvertisementPublisher
//      (manufacturer data only — NOT connectable) and a GattServiceProvider
//      (advertises the service UUID — connectable). They show up as two
//      separate CBPeripherals with different identifiers; if we scanned with
//      services:nil and matched by peripheral.identifier, we'd often target
//      the non-connectable channel and connectPeripheral would hang forever
//      (CoreBluetooth has no built-in connect timeout). Filtering the scan
//      by service UUID guarantees we only ever see the connectable peer.
//   3. On match, connect → discover BetterSend service → discover the
//      handshake characteristic → read its value.
//   4. Stash the UTF-8 bytes as an NSString, signal a dispatch_semaphore,
//      tear down. fetchPayload returns the string (or empty on timeout).
//
// Host role isn't supported here — macOS Phase 1 is always the client.
// publishPayload() throws to fail loudly on misuse.

#import <Foundation/Foundation.h>
#import <CoreBluetooth/CoreBluetooth.h>

#include "IPeerHandshake.h"
#include "Constants.h"
#include "Logger.h"

#include <atomic>
#include <stdexcept>
#include <string>

namespace BetterSend {
	constexpr const char* kHandshakeComponent = "BleHand";
	class BleHandshakeMac;
}

@interface BSHandshakeMacDelegate : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate>
@property (nonatomic, strong) CBCentralManager*   central;
@property (nonatomic, strong) CBPeripheral*       target;
@property (nonatomic, strong) NSString*           targetUuid;
@property (nonatomic, strong) CBUUID*             serviceUuid;
@property (nonatomic, strong) CBUUID*             charUuid;
@property (nonatomic, strong) NSString*           result;
@property (nonatomic, strong) dispatch_semaphore_t done;
@property (nonatomic, assign) BOOL                wantScan;
@property (nonatomic, assign) BetterSend::BleHandshakeMac* owner;
@end

namespace BetterSend {

class BleHandshakeMac : public IPeerHandshake {
public:
	BleHandshakeMac() {
		@autoreleasepool {
			queue_              = dispatch_queue_create("com.bettersend.ble.hand",
				DISPATCH_QUEUE_SERIAL);
			delegate_           = [[BSHandshakeMacDelegate alloc] init];
			delegate_.owner     = this;
			delegate_.serviceUuid = [CBUUID UUIDWithString:@(kBleServiceUuid)];
			delegate_.charUuid    = [CBUUID UUIDWithString:@(kBleHandshakeCharUuid)];
		}
	}

	~BleHandshakeMac() override { stop(); }

	void publishPayload(const std::string& /*payload*/) override {
		throw std::runtime_error(
			"BleHandshake_Mac: host role not supported. "
			"macOS Phase 1 is always the client; Windows publishes.");
	}

	[[nodiscard]] std::string fetchPayload(const std::string& peerId,
	                                       int timeoutSeconds) override {
		// peerId arrives shaped as "ble:<CBPeripheral.identifier.UUIDString>"
		// from BleDiscovery_Mac.mm. Strip the prefix.
		std::string target = peerId;
		if (target.rfind("ble:", 0) == 0) target.erase(0, 4);

		BS_LOG_INFO(kHandshakeComponent, "fetchPayload: peerId='{}' timeout={}s",
			target, timeoutSeconds);

		@autoreleasepool {
			delegate_.targetUuid = [NSString stringWithUTF8String:target.c_str()];
			delegate_.result     = nil;
			delegate_.done       = dispatch_semaphore_create(0);
			delegate_.wantScan   = YES;
			if (!delegate_.central) {
				delegate_.central = [[CBCentralManager alloc]
					initWithDelegate:delegate_ queue:queue_];
			} else if (delegate_.central.state == CBManagerStatePoweredOn) {
				kickScan();
			}
		}

		dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW,
			static_cast<int64_t>(timeoutSeconds) * NSEC_PER_SEC);
		long rc = dispatch_semaphore_wait(delegate_.done, deadline);

		std::string out;
		@autoreleasepool {
			if (rc == 0 && delegate_.result) {
				out = std::string(delegate_.result.UTF8String);
			}
			delegate_.wantScan = NO;
			if (delegate_.central && delegate_.central.isScanning) {
				[delegate_.central stopScan];
			}
			if (delegate_.target) {
				[delegate_.central cancelPeripheralConnection:delegate_.target];
				delegate_.target = nil;
			}
		}

		if (out.empty()) {
			BS_LOG_ERROR(kHandshakeComponent, "fetchPayload timeout/empty for '{}'", target);
		} else {
			BS_LOG_INFO(kHandshakeComponent, "fetchPayload got {} bytes", out.size());
		}
		return out;
	}

	void stop() override {
		@autoreleasepool {
			if (delegate_) {
				delegate_.wantScan = NO;
				if (delegate_.central && delegate_.central.isScanning) {
					[delegate_.central stopScan];
				}
				if (delegate_.target) {
					[delegate_.central cancelPeripheralConnection:delegate_.target];
					delegate_.target = nil;
				}
			}
		}
	}

	// ── Called by the ObjC delegate on the BLE queue ────────────────────────
	void onCentralReady() { kickScan(); }

private:
	void kickScan() {
		if (!delegate_.wantScan) return;
		if (delegate_.central.isScanning) return;
		// Filter scan by the BetterSend service UUID so CoreBluetooth only
		// surfaces peripherals whose advert includes that UUID — i.e., the
		// connectable GattServiceProvider on Windows — and silently drops
		// the non-connectable manufacturer-data advert that shares the same
		// name. See file header for the full rationale.
		NSDictionary* opts = @{ CBCentralManagerScanOptionAllowDuplicatesKey : @NO };
		[delegate_.central scanForPeripheralsWithServices:@[ delegate_.serviceUuid ]
		                                          options:opts];
	}

	BSHandshakeMacDelegate*  delegate_{nil};
	dispatch_queue_t         queue_{nullptr};
};

std::unique_ptr<IPeerHandshake> makePeerHandshake() {
	return std::make_unique<BleHandshakeMac>();
}

} // namespace BetterSend

// ── ObjC delegate impl ───────────────────────────────────────────────────────

@implementation BSHandshakeMacDelegate

- (void)centralManagerDidUpdateState:(CBCentralManager *)central {
	using namespace BetterSend;
	if (central.state == CBManagerStatePoweredOn) {
		BS_LOG_INFO(kHandshakeComponent, "Handshake central powered on");
		if (self.owner) self.owner->onCentralReady();
	} else if (central.state == CBManagerStateUnauthorized) {
		BS_LOG_ERROR(kHandshakeComponent,
			"Handshake central unauthorized — check Info.plist + entitlements");
	}
}

- (void)centralManager:(CBCentralManager *)central
 didDiscoverPeripheral:(CBPeripheral *)peripheral
     advertisementData:(NSDictionary<NSString *,id> *)advertisementData
                  RSSI:(NSNumber *)RSSI {
	using namespace BetterSend;
	// Scan is filtered by serviceUuid in kickScan, so every peripheral
	// fired here already advertises the BetterSend service and is
	// connectable. Connect to the first one seen. In a single-host Phase 1
	// pair this is unambiguous; future multi-host scenarios will add a
	// name-based secondary filter.
	if (self.target) return; // already connecting
	NSString* uuidStr = peripheral.identifier.UUIDString;
	BS_LOG_INFO(kHandshakeComponent,
		"Found target peripheral id={} name={}, connecting",
		std::string(uuidStr.UTF8String),
		std::string(peripheral.name ? peripheral.name.UTF8String : "(nil)"));
	[central stopScan];
	self.target          = peripheral;
	self.target.delegate = self;
	[central connectPeripheral:peripheral options:nil];
}

- (void)centralManager:(CBCentralManager *)central
  didConnectPeripheral:(CBPeripheral *)peripheral {
	using namespace BetterSend;
	BS_LOG_INFO(kHandshakeComponent, "Connected; discovering services");
	[peripheral discoverServices:@[ self.serviceUuid ]];
}

- (void)centralManager:(CBCentralManager *)central
 didFailToConnectPeripheral:(CBPeripheral *)peripheral
                  error:(NSError *)error {
	using namespace BetterSend;
	BS_LOG_ERROR(kHandshakeComponent, "Connect failed: {}",
		error ? error.localizedDescription.UTF8String : "unknown");
	dispatch_semaphore_signal(self.done);
}

- (void)peripheral:(CBPeripheral *)peripheral
 didDiscoverServices:(NSError *)error {
	using namespace BetterSend;
	if (error) {
		BS_LOG_ERROR(kHandshakeComponent, "discoverServices error: {}",
			error.localizedDescription.UTF8String);
		dispatch_semaphore_signal(self.done);
		return;
	}
	for (CBService* svc in peripheral.services) {
		if ([svc.UUID isEqual:self.serviceUuid]) {
			[peripheral discoverCharacteristics:@[ self.charUuid ] forService:svc];
			return;
		}
	}
	BS_LOG_ERROR(kHandshakeComponent, "BetterSend service not found");
	dispatch_semaphore_signal(self.done);
}

- (void)peripheral:(CBPeripheral *)peripheral
 didDiscoverCharacteristicsForService:(CBService *)service
                   error:(NSError *)error {
	using namespace BetterSend;
	if (error) {
		BS_LOG_ERROR(kHandshakeComponent, "discoverCharacteristics error: {}",
			error.localizedDescription.UTF8String);
		dispatch_semaphore_signal(self.done);
		return;
	}
	for (CBCharacteristic* ch in service.characteristics) {
		if ([ch.UUID isEqual:self.charUuid]) {
			[peripheral readValueForCharacteristic:ch];
			return;
		}
	}
	BS_LOG_ERROR(kHandshakeComponent, "Handshake characteristic not found");
	dispatch_semaphore_signal(self.done);
}

- (void)peripheral:(CBPeripheral *)peripheral
 didUpdateValueForCharacteristic:(CBCharacteristic *)characteristic
                   error:(NSError *)error {
	using namespace BetterSend;
	if (error) {
		BS_LOG_ERROR(kHandshakeComponent, "readValue error: {}",
			error.localizedDescription.UTF8String);
		dispatch_semaphore_signal(self.done);
		return;
	}
	NSData* data = characteristic.value;
	if (data.length > 0) {
		self.result = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
		BS_LOG_INFO(kHandshakeComponent, "Read {} bytes from peer", (unsigned long)data.length);
	}
	dispatch_semaphore_signal(self.done);
}

@end
