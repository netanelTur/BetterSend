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
| **iPhone** | ✅ AWDL | 🚧 Phase 1 | ✅ AWDL | 🔜 Phase 3 |
| **Android** | 🚧 Phase 1 | ✅ Wi-Fi Direct | 🔜 Phase 3 | 🔜 Phase 3 |
| **Mac** | ✅ AWDL | 🔜 Phase 3 | ✅ AWDL | 🔜 Phase 3 |
| **Windows** | 🔜 Phase 3 | 🔜 Phase 3 | 🔜 Phase 3 | 🔜 Phase 3 |

## Roadmap

| Phase | Goal | Status |
|-------|------|--------|
| **1** | File transfer iOS ↔ Android, no internet | 🚧 Active |
| **2** | Clipboard (copied text) transfer | ⏳ Queued |
| **3** | Full OS matrix — Windows, Mac, iOS, Android | ⏳ Queued |
| **+** | Cloud room: shared workspace via code, works across distance | 💡 Nice-to-have |

## Architecture

```
Flutter UI  (Dart)
     │  dart:ffi → bettersend_api.cpp (extern "C" Facade)
     ▼
bettersend_core.so/.dylib/.dll   (C++20, namespace BetterSend)
     ├── IDiscovery    ← BonjourDiscovery (Apple/AWDL) | MdnsDiscovery (other) | BleDiscovery (future)
     ├── ITransport    ← TcpTransport
     ├── IProtocol     ← TransferProtocol   [4B len][JSON header][payload]
     ├── ITransferable ← FileTransferable, TextTransferable
     ├── IClipboard    ← platform impls (Phase 2)
     └── Logger        Singleton
```

Each device pair uses the best available P2P technology via the **Strategy pattern** — new platform support = new `IDiscovery`/`ITransport` implementation, zero changes to existing code.

## Transport strategy per pair

| Pair | Discovery | Transport |
|------|-----------|-----------|
| Apple ↔ Apple | Bonjour + AWDL (`kDNSServiceFlagsIncludeAWDL`) | TCP over AWDL |
| Android ↔ Android | mDNS | TCP over Wi-Fi Direct |
| iOS ↔ Android | BLE → hotspot handshake | TCP over hotspot |
| Any ↔ Windows | mDNS + Wi-Fi Direct | TCP |
| Remote (future) | Cloud signaling + shared code | WebRTC relay |

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
# 1. Build native library (from project root, x64 Release)
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# 2. Run Flutter app
cd flutter_app
flutter run -d windows
```
The Windows CMake runner automatically copies `build/cpp_core/Release/bettersend_core.dll` into the build output.

> **Both computers need to be on the same LAN for discovery to work (Mac ↔ Windows via mDNS).**

## Project structure

```
BetterSend/
├── cpp_core/
│   ├── include/          # Interfaces + data types
│   │   ├── IDiscovery.h  ITransport.h  IProtocol.h  ITransferable.h
│   │   ├── BonjourDiscovery.h  MdnsDiscovery.h
│   │   ├── FileTransferable.h  TextTransferable.h
│   │   ├── Device.h  Constants.h  Logger.h
│   │   └── mdns.h            # Single-header mDNS (mjansson, public domain)
│   ├── src/
│   │   ├── BonjourDiscovery.cpp   # Apple only: dns_sd.h + AWDL
│   │   ├── MdnsDiscovery.cpp      # Windows/Linux/Android: raw mDNS (cross-platform)
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
