# BetterSend — Development Plan

Cross-platform AirDrop alternative: Mac ↔ Windows first, then iOS ↔ Android,
then full matrix. No internet required, no shared Wi-Fi router required, no
user network setup. C++20 backend + Flutter UI.

---

## Architecture Overview

```
Flutter UI  (Dart, thin)
     │  dart:ffi calls
     ▼
bettersend_core.so/.dylib/.dll   (C++20)
     ├── Logger              (Singleton)
     ├── IDiscovery          (Strategy) ← BleDiscovery_{Mac,Windows}
     │                                   BonjourDiscovery (later, Apple↔Apple)
     │                                   MdnsDiscovery    (dev fallback)
     ├── IConnectionBroker   (Strategy) ← WindowsHotspotBroker, MacWifiClientBroker
     ├── ITransport          (Strategy) ← TcpTransport
     ├── IProtocol           (Strategy) ← TransferProtocol
     ├── IClipboard          (Strategy) ← platform impls (Phase 3)
     └── bettersend_api      (Facade, extern "C" API)
```

The same Strategy interfaces absorb every future device pair. New pair = new
implementations, zero changes to existing files.

---

## Phase 1 — Mac ↔ Windows File Transfer

**Goal:** Send a file from Windows → Mac (and back) with **no shared Wi-Fi,
no internet, no user network setup**. Only requirement: BetterSend installed
on both devices.

**Why Mac↔Windows first:** Dev pair (Windows + Mac in the team). BLE
discovery + hotspot-bring-up + TCP pattern is identical to every other
no-shared-network pair, so the work unlocks all later phases through the
Strategy interfaces.

### End-to-end flow

1. **BLE advertise + scan** — both apps broadcast the BetterSend signature;
   both scan; peers see each other by device name. Cross-platform via a
   shared wire format (Windows ManufacturerData + Mac ServiceUUID/LocalName).
2. **GATT handshake** — once peers see each other, the Windows side
   (deterministic host: macOS programmatic hotspot is restricted) exposes a
   GATT characteristic with `{SSID, PSK, port}` that the Mac reads.
3. **Connection bring-up** — Windows calls
   `WindowsHotspotBroker::start()` (WinRT `NetworkOperatorTetheringManager`);
   Mac calls `MacWifiClientBroker::joinSsid(...)` (CoreWLAN
   `associateToNetwork:password:error:`). Both ends are now on the same
   ad-hoc subnet.
4. **TCP transfer** — `TcpTransport` (Asio standalone) runs on the
   hotspot subnet. Wire format: `[4B BE len][JSON header][payload]`.

### Status

| # | Task | File(s) | Status |
|---|------|---------|--------|
| 1 | Logger + Constants + interfaces (IDiscovery / IProtocol / ITransport / ITransferable / IClipboard) | `cpp_core/include/*.h` | done |
| 2 | Splash + Home + Device list + Transfer screens (skeleton) | `flutter_app/lib/screens/` | done |
| 3 | FFI bridge (load lib + echo + create/destroy + advertise + discovery callback) | `flutter_app/lib/ffi_bridge.dart` | done |
| 4 | C API skeleton (`bettersend_create`, `bettersend_echo`, advertise + discovery) | `cpp_core/src/bettersend_api.cpp` | done |
| 5 | MdnsDiscovery (dev fallback / future Android) | `cpp_core/src/MdnsDiscovery.cpp` | done |
| 6 | BonjourDiscovery (future Apple↔Apple AWDL; excluded from Phase 1 builds) | `cpp_core/src/BonjourDiscovery.cpp` | done |
| 7 | BleDiscovery (Windows) — `BluetoothLEAdvertisementPublisher/Watcher`, cross-format peer recognition | `cpp_core/src/BleDiscovery_Windows.cpp` | done |
| 8 | BleDiscovery (Mac) — CoreBluetooth `CBPeripheralManager/CBCentralManager`, cross-format peer recognition | `cpp_core/src/BleDiscovery_Mac.mm` | done |
| 9 | TransferProtocol — nlohmann/json + 4-byte BE length prefix | `cpp_core/src/TransferProtocol.cpp` + `include/TransferProtocol.h` | in progress |
| 10 | TcpTransport — Asio standalone async server + client, streaming send for large files | `cpp_core/src/TcpTransport.cpp` + `include/TcpTransport.h` | todo |
| 11 | `IConnectionBroker` interface — `start()` (host), `joinSsid(ssid, psk)` (client), `stop()` | `cpp_core/include/IConnectionBroker.h` | todo |
| 12 | WindowsHotspotBroker — WinRT `NetworkOperatorTetheringManager::StartTetheringAsync` | `cpp_core/src/WindowsHotspotBroker.cpp` | todo |
| 13 | MacWifiClientBroker — CoreWLAN `CWInterface associateToNetwork:password:error:` | `cpp_core/src/MacWifiClientBroker.mm` | todo |
| 14 | BLE GATT handshake — add a single characteristic carrying `{SSID, PSK, port}`; Windows side serves, Mac side reads | `BleDiscovery_Windows.cpp` + `BleDiscovery_Mac.mm` | todo |
| 15 | Wire full pipeline in `bettersend_api.cpp` — `start_server`, `send_file` (discover → GATT read → broker bring-up → TCP) | `cpp_core/src/bettersend_api.cpp` | todo |
| 16 | FFI: `startServer` + `sendFile` + `sendClipboard` callbacks | `flutter_app/lib/ffi_bridge.dart` | todo |
| 17 | UI: file picker + send button + live progress; received-file open action | `flutter_app/lib/screens/` | todo |
| 18 | Tests green: `test_protocol`, `test_transport` (localhost round-trip) | `cpp_core/tests/` | todo |
| 19 | End-to-end: 10 MB photo Windows → Mac, zero shared infrastructure | manual | milestone |

**Milestone:** 10 MB photo Windows → Mac → 10 MB photo back Mac → Windows.
No router, no internet, no manual SSID setup on the receiving side.

### Future-platform safety (must hold before merging Phase 1)

- Nothing Mac/Windows-specific leaks into shared headers (`IDiscovery.h`,
  `IConnectionBroker.h`, `ITransport.h`, `IProtocol.h`, the C surface in
  `bettersend_api.cpp`). Platform code lives only in its own `.cpp/.mm`
  files behind the interface.
- `BleDiscovery` keeps using the cross-platform wire format already
  shared by `BleDiscovery_Windows.cpp` / `BleDiscovery_Mac.mm` so
  Android/iOS impls can drop in without changes.
- `IConnectionBroker` is an interface, not a class hierarchy — easy to add
  `AndroidWifiDirectBroker`, `IosHotspotBroker`, etc., without touching
  existing impls.
- Wire format and protocol stay unchanged across all phases.

---

## Phase 2 — iOS ↔ Android File Transfer

**Goal:** Same milestone as Phase 1, but for the mobile pair. No internet,
no shared Wi-Fi router, no user setup.

### Steps (rough — refined when we get there)

| # | Task | File(s) |
|---|------|---------|
| 20 | BleDiscovery_Android (JNI → `BluetoothLeAdvertiser` / `BluetoothLeScanner`) | `cpp_core/src/BleDiscovery_Android.cpp` |
| 21 | BleDiscovery_iOS (`CoreBluetooth` — same code as `BleDiscovery_Mac.mm` minus the framework rules) | `cpp_core/src/BleDiscovery_iOS.mm` |
| 22 | AndroidHotspotBroker — `WifiManager.setSoftApConfiguration` via JNI (Android 11+) | `cpp_core/src/AndroidHotspotBroker.cpp` |
| 23 | iOSWifiClientBroker — `NEHotspotConfiguration` for joining the Android hotspot | `cpp_core/src/iOSWifiClientBroker.mm` |
| 24 | Mobile-specific FFI plumbing + permissions (Bluetooth, Location for BLE on Android, Local Network on iOS) | `flutter_app/` |

**Milestone:** 10 MB photo iPhone → Android, no shared infrastructure.

---

## Phase 3 — Clipboard Sync

**Goal:** Copy text on one device, paste on another, across pairs that
already support file transfer.

### Steps

| # | Task | File(s) |
|---|------|---------|
| 25 | ClipboardApple.mm (macOS + iOS) — `NSPasteboard` / `UIPasteboard` | `cpp_core/src/ClipboardApple.mm` |
| 26 | ClipboardWindows.cpp — Win32 `OpenClipboard` / `SetClipboardData` | `cpp_core/src/ClipboardWindows.cpp` |
| 27 | ClipboardAndroid.cpp — JNI → `ClipboardManager` | `cpp_core/src/ClipboardAndroid.cpp` |
| 28 | Wire `type=clipboard` through the existing Transport + Protocol (already supported by `TextTransferable`) | `cpp_core/src/bettersend_api.cpp` |
| 29 | UI: 'Send Clipboard' button on HomeScreen + 'Copy on receive' action | `flutter_app/lib/screens/` |

**Milestone:** Copy text on iPhone, paste on Windows.

---

## Phase 4 — Full Matrix + Desktop Drag & Drop

**Goal:** Every supported pair, every direction. Desktop supports drag &
drop into the BetterSend window.

### Steps

| # | Task | File(s) |
|---|------|---------|
| 30 | Drag & drop on desktop (`desktop_drop` Flutter package) | `flutter_app/lib/` |
| 31 | Progress callbacks in `ITransport::send` — bytes streamed → Dart Stream | `ITransport.h`, `TcpTransport.cpp`, `ffi_bridge.dart` |
| 32 | CI: GitHub Actions — build + ctest on macOS + Ubuntu + Windows | `.github/workflows/` |
| 33 | Resume-after-disconnect (optional) | protocol, TcpTransport |

**Milestone:** Drag a file from Finder, drop into BetterSend, it lands on
the Windows partner's Downloads folder.

---

## Nice to Have — Cloud Room (Remote Transfers)

**Goal:** Transfer files between devices on different networks via a relay.

### Concept

- Both devices connect to a cloud signaling server via WebSocket.
- They share a 6-digit room code.
- Data is relayed (not stored) through the server — or upgraded to WebRTC
  data channels once both peers are connected.

### Steps (rough)

| # | Task |
|---|------|
| 34 | Relay server (Node.js or Go, minimal) — WebSocket + room concept |
| 35 | New `ITransport` impl: `WebSocketTransport` (or `WebRtcTransport`) |
| 36 | Room code UI: 6-digit input screen, code sharing |
| 37 | End-to-end encryption (optional, strong nice-to-have) — libsodium ECDH |

---

## Coding conventions (recap; full version in CLAUDE.md)

- **Indentation:** tabs only, never spaces. Applies to C++, Dart, CMake.
- **Complexity:** keep code minimal. No unused helpers, no premature
  abstractions. Three concrete cases beat one premature abstraction.
- **Step-by-step:** smallest working thing first; each iteration runnable
  or observable.
- **No mock mode:** real library or fail loudly. Never fake data in the UI.
- **OCP:** add new platforms / transports / discoveries via new
  Strategy implementations, not edits to existing files.

---

## Key Resources

| Topic | Link |
|-------|------|
| CMake docs | https://cmake.org/cmake/help/latest/ |
| Asio standalone | https://think-async.com/Asio/ |
| nlohmann/json | https://json.nlohmann.me/ |
| WinRT — `NetworkOperatorTetheringManager` | https://learn.microsoft.com/en-us/uwp/api/windows.networking.networkoperators.networkoperatortetheringmanager |
| WinRT — BLE Advertisement | https://learn.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.advertisement |
| CoreBluetooth | https://developer.apple.com/documentation/corebluetooth |
| CoreWLAN — `CWInterface associateToNetwork` | https://developer.apple.com/documentation/corewlan/cwinterface |
| dart:ffi | https://dart.dev/guides/libraries/c-interop |
| GoogleTest | https://google.github.io/googletest/ |

---

## Design Patterns Used

| Pattern | Where | Why |
|---------|-------|-----|
| Strategy | `IDiscovery`, `ITransport`, `IProtocol`, `IConnectionBroker`, `IClipboard`, `ITransferable` | Swap implementations per pair/platform without modifying existing code |
| Singleton | `Logger` | One log stream, accessible from anywhere |
| Facade | `bettersend_api.cpp` | Single C API over the C++ subsystems |
| RAII | All resource handles (sockets, files, threads) | Exception-safe cleanup |

---

## How to Build & Test

```bash
# Build core + library
cmake -B build && cmake --build build

# Build with unit tests
cmake -B build -DBUILD_TESTS=ON && cmake --build build && cd build && ctest --verbose
```

Skeleton-only tests show `SKIPPED` — that is the expected state until each
class is implemented. A test goes green only when its implementation lands.
