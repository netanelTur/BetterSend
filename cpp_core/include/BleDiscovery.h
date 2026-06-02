#pragma once
#include "IDiscovery.h"
#include <memory>

namespace BetterSend {

// ── BleDiscovery ──────────────────────────────────────────────────────────────
// Cross-platform Bluetooth Low Energy peer discovery. Each platform implements
// its own .cpp behind this single factory:
//   - BleDiscovery_Windows.cpp   (C++/WinRT, BluetoothLEAdvertisementPublisher/Watcher)
//   - BleDiscovery_Mac.mm        (CoreBluetooth, CBPeripheralManager/CBCentralManager)
//   - BleDiscovery_Android.cpp   (JNI → BluetoothLeAdvertiser/Scanner)  (later)
//   - BleDiscovery_iOS.mm        (CoreBluetooth)                         (later)
//
// All implementations advertise + scan for kBleServiceUuid (Constants.h). The
// LocalName in the advertising payload carries the human-readable device name.
// Discovery::Device.ip is "ble:<address>" until the connection broker brings
// up an actual IP-level transport; Discovery::Device.port stays kDefaultPort.

std::unique_ptr<IDiscovery> makeBleDiscovery();

} // namespace BetterSend
