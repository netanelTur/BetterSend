# BetterSend 🛰️☄️🚀

> AirDrop — upgraded. Cross-platform.

Transfer files and clipboard between any two devices with BetterSend installed.  
No internet. No shared WiFi. No account.  
**The only dependency: both devices run BetterSend.**

---

## Why

AirDrop is great — but Apple only. BetterSend does the same thing across every platform, every device pair, using peer-to-peer connections with zero shared infrastructure.

## Target pairs

| From \ To | iPhone | Android | Mac | Windows |
|-----------|--------|---------|-----|---------|
| **iPhone** | ✅ AWDL | 🔜 Phase 2 | ✅ AWDL | 🔜 Phase 4 |
| **Android** | 🔜 Phase 2 | ✅ Wi-Fi Direct | 🔜 Phase 4 | 🔜 Phase 4 |
| **Mac** | ✅ AWDL | 🔜 Phase 4 | ✅ AWDL | 🚧 **Phase 1** |
| **Windows** | 🔜 Phase 4 | 🔜 Phase 4 | 🚧 **Phase 1** | 🔜 Phase 4 |

## Roadmap

| Phase | Goal | Status |
|-------|------|--------|
| **1** | File transfer **Mac ↔ Windows**, no internet, no shared WiFi | 🚧 Active |
| **2** | iOS ↔ Android (same BLE → hotspot pattern) | ⏳ Queued |
| **3** | Clipboard (copied text) transfer | ⏳ Queued |
| **4** | Full OS matrix — every pair, every direction | ⏳ Queued |
| **+** | Cloud room: shared workspace via code, works across distance | 💡 Nice-to-have |

> **Why Mac↔Windows first:** it's the dev pair — daily dogfooding. The BLE-discovery + hotspot-bring-up pattern is identical for every no-shared-network pair, so getting it right here unlocks all the rest through the existing Strategy interfaces.

## Architecture

```
Flutter UI  (Dart)
     │  dart:ffi → bettersend_api.cpp (extern "C" Facade)
     ▼
bettersend_core.so/.dylib/.dll   (C++20, namespace BetterSend)
     ├── IDiscovery          ← BleDiscovery (Phase 1, cross-platform via SimpleBLE)
     │                         BonjourDiscovery (Apple-pair, later phase)
     │                         MdnsDiscovery (legacy / dev fallback over existing Wi-Fi)
     ├── IConnectionBroker   ← WindowsHotspotBroker, MacWifiClientBroker
     │                         (brings up the local network the transport runs on)
     ├── ITransport          ← TcpTransport
     ├── IProtocol           ← TransferProtocol   [4B len][JSON header][payload]
     ├── ITransferable       ← FileTransferable, TextTransferable
     ├── IClipboard          ← platform impls (Phase 3)
     └── Logger              Singleton
```

Each device pair uses the best available P2P technology via the **Strategy pattern** — new platform support = new `IDiscovery` / `IConnectionBroker` / `ITransport` implementation, zero changes to existing code.

## Transport strategy per pair

The same **BLE-discover → bring-up-network → TCP-transfer** pattern repeats for every no-shared-infrastructure pair; only the platform glue differs.

| Pair | Discovery | Bring-up | Transport |
|------|-----------|----------|-----------|
| **Mac ↔ Windows (Phase 1)** | BLE (SimpleBLE) | Windows starts Mobile Hotspot via WinRT; Mac auto-joins via CoreWLAN | TCP over hotspot |
| Apple ↔ Apple | Bonjour + AWDL | — (AWDL always-on) | TCP over AWDL |
| Android ↔ Android | mDNS + Wi-Fi Direct | WifiP2pManager | TCP over Wi-Fi Direct |
| iOS ↔ Android (Phase 2) | BLE | one side creates hotspot, other connects | TCP over hotspot |
| Any ↔ Windows | BLE + hotspot | same as Mac↔Windows | TCP |
| Remote (future) | Cloud signaling + shared code | — | WebRTC relay |

## Build

**Prerequisites:** CMake ≥ 3.20, Flutter SDK. Xcode on macOS, Visual Studio 2022 on Windows.

### macOS
```bash
# 1. Build native library (from project root)
cmake -B build && cmake --build build

# 2. Run Flutter app
cd flutter_app && flutter run -d macos
```
The Xcode build phase automatically copies `build/cpp_core/libbettersend_core.dylib` into the app bundle.

### Windows
```bash
# 1. Build native library (from project root) — uses mingw64 + Ninja
cmake -B build -G Ninja
cmake --build build

# 2. Run Flutter app
cd flutter_app
flutter run -d windows
```
**One-time:** after the first `flutter run`, copy `bettersend_core.dll` plus the three MinGW runtime DLLs (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`) into `flutter_app/build/windows/x64/runner/Debug/`. TODO: a post-build CMake step to automate this, matching the Xcode build phase on macOS.

> **Phase 1 discovery (BLE advertise + scan) is live on Windows.** Mac side (`BleDiscovery_Mac.mm`, CoreBluetooth) and the post-discovery connection broker (Mobile Hotspot ↔ CoreWLAN join) are the next pieces. Until the broker lands, peers found via BLE expose `Device.ip = "ble:<addr>"` as a placeholder — no actual file transfer yet.

## Project structure

```
BetterSend/
├── cpp_core/
│   ├── include/          # Interfaces + data types
│   │   ├── IDiscovery.h  ITransport.h  IProtocol.h  ITransferable.h
│   │   ├── BonjourDiscovery.h  MdnsDiscovery.h  BleDiscovery.h
│   │   ├── FileTransferable.h  TextTransferable.h
│   │   ├── Device.h  Constants.h  Logger.h
│   │   └── mdns.h            # Single-header mDNS (mjansson, public domain)
│   ├── src/
│   │   ├── BonjourDiscovery.cpp   # Apple only: dns_sd.h + AWDL (Apple-pair, later)
│   │   ├── MdnsDiscovery.cpp      # Linux/Android: raw mDNS (dev fallback)
│   │   ├── BleDiscovery_Windows.cpp  # Phase 1 Windows side: WinRT BLE advertise + scan
│   │   ├── TcpTransport.cpp
│   │   ├── TransferProtocol.cpp
│   │   └── bettersend_api.cpp     # extern "C" Facade for Flutter FFI
│   └── tests/
├── flutter_app/
│   ├── lib/
│   │   ├── ffi_bridge.dart        # FFI bridge — real library required
│   │   ├── main.dart
│   │   └── screens/
│   │       ├── home_screen.dart   # Discovery + device list
│   │       ├── splash_screen.dart
│   │       ├── transfer_screen.dart
│   │       └── device_list_screen.dart
│   ├── macos/                     # macOS runner + entitlements
│   └── windows/                   # Windows runner + DLL bundling
└── docs/
    └── codebase-explained.html    # Line-by-line code walkthrough (Hebrew)
```

## Code docs

`docs/codebase-explained.html` — detailed line-by-line explanation of the entire codebase. Open in any browser.

---

Built by Yonatan & Nathaniel.
