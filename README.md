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
| **1** | File transfer **Mac ↔ Windows**, no internet, no shared WiFi | ✅ Code complete (E2E test pending) |
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
| **Mac ↔ Windows (Phase 1)** | BLE (SimpleBLE) | Windows starts Mobile Hotspot via WinRT; Mac joins via CoreWLAN **on tap** (Mac tap joins directly; Windows tap broadcasts a connect-request marker so the Mac joins from its end) | TCP over hotspot |
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

> **Phase 1 end-to-end pipeline is wired**: BLE discovery (Windows + Mac) → Windows Mobile Hotspot bring-up (WinRT, eager) → Mac joins on a user tap (GATT credential handshake → CoreWLAN join → Hello) → TCP file transfer via Asio. Connection is symmetric tap-to-connect — a Mac tap joins directly, a Windows tap broadcasts a connect-request marker so the Mac initiates the join (only the Mac can join the hotspot). Both sides run a TCP receive server on port 9000 and surface received files / clipboard via the `onReceive` FFI callback.
>
> **Send flow is gated by user consent.** When you press Send, the sender first transmits a `Request` control message; the receiver sees an "Incoming file" dialog with `Accept` / `Decline` buttons. The actual file bytes are pushed only after the receiver accepts. Control messages ride on the existing Clipboard wire format with a `"BS\t"` JSON prefix; no protocol changes.
>
> **Live transfer progress (both sides).** `TcpTransport::setProgressCallback` reports byte progress while a **file** transfer is in flight — `dir 0` sending / `dir 1` receiving, throttled to integer-percent changes (≤101 calls/transfer; control/clipboard/Hello are skipped). It flows `bettersend_set_progress_callback` → `bridge.onProgress(TransferProgress)` → a "Transfers" section on the home screen with a `LinearProgressIndicator` per active transfer (cleared shortly after 100%). The send path is chunked for this; `payload()` is still fully in memory (fine for Phase 1; true streaming send is a Phase 4 follow-up).
>
> **Custom display name.** A Settings screen (gear icon on the home app bar) lets you pick the name other devices see; it persists via `shared_preferences` (falls back to the OS hostname). Changing it applies **live** — `bridge.setDeviceName` → `bettersend_set_device_name` re-advertises over BLE and updates the name stamped into outgoing TCP headers, no restart. The name is byte-capped to `kBleMaxNameLen` (20) on a UTF-8 boundary (`clampUtf8` in `Utf8.h`) so a Hebrew/emoji name is never split mid-codepoint; the input field validates the same budget in bytes. Caveat: a renamed **Mac** still shows as `BetterSend-<hex>` in the Windows advert list until its first TCP Hello (the same CoreBluetooth SCAN_RSP limitation) — a renamed **Windows** machine is visible to the Mac immediately.
>
> **Receiver picks the save folder.** The Settings screen has a "Save received files to" card; it defaults to the OS Downloads directory and a Change button opens a native folder picker. The choice now persists via `shared_preferences` (key `save_dir`) and flows `bridge.setSaveDir` → `bettersend_set_save_dir` → `TcpTransport::setSaveDirectory`; incoming files land in `<chosen-dir>/<timestamp>_<name>`. Empty selection falls back to a `bettersend_incoming` folder under the OS temp dir.
>
> **Live peer list** — `kPeerHeartbeatSec=2` and `kPeerStaleSec=10` in `Constants.h` define how the UI keeps the device list fresh: native re-emits each peer every 2s; Flutter prunes peers not seen for 10s. Both sides dedupe by case-insensitive name (Windows can advertise under both `NETANELTUR` and `NetanelTur` simultaneously; we surface a single canonical entry).
>
> macOS requires Bluetooth permission. `Info.plist` carries `NSBluetoothAlwaysUsageDescription`; both entitlements files declare `com.apple.security.device.bluetooth` **and** `com.apple.security.files.user-selected.read-only` (the second is needed for the file picker to actually return a usable path under the App Sandbox).
>
> Windows side requires an active `InternetConnectionProfile` (any adapter, online or not) for `NetworkOperatorTetheringManager` to start the hotspot. Configure the hotspot SSID + passphrase once in Windows Settings → Network → Mobile Hotspot; BetterSend reads them via WinRT and publishes via GATT.
>
> **Symmetric tap-to-connect (no auto-join).** Tap a peer on **either** device → connect → transfer. Tapping calls `bettersend_connect_peer`, reporting progress via `ConnectStatusCallback` (`0` connecting → `1` joined → `2` ready, or `3` failed) to drive the "Connecting…" spinner. Because only the Mac can join the Windows hotspot, the two sides differ under the hood: a **Mac** tap joins directly (GATT read → `joinNetwork` → Hello); a **Windows** tap can't join, so it broadcasts a *connect-request* marker in its BLE advert (`kBleConnectMagicBytes`, the normal magic with its last byte flipped `D0`→`D1`, same length) via `IDiscovery::setConnectRequested(true)` — the Mac already parses that advert, sees the marker, and initiates the join from its end; Windows then polls until the Mac's Hello reveals its IP and reverts the marker. There is **no blanket auto-join**: the Mac joins only when locally tapped or explicitly invited, so multiple hosts don't race and a tap can't hang (`connect_peer` polls an in-flight join rather than no-op'ing). *Auto-join was tried and reverted — it raced the tap (cb=nullptr) and hung the spinner.*
>
> macOS has no public API to join Wi-Fi without scanning (`associateToNetwork:` needs a `CWNetwork*` from a scan; `NEHotspotConfiguration` is iOS-only). `MacWifiClientBroker` uses an open scan (`scanForNetworksWithSSID:nil`) + `associateToNetwork:`, with retry backoff `kJoinBackoffMs=8000` — airportd rate-limits back-to-back open scans in a ~5-6s window (`"Resource busy"`), so retries must clear that window. BLE is intentionally **not** silenced around the scan: the rate-limit persists even with the CB managers released, and the nil-out machinery was a prior bug-loop source.
>
> **Windows seeing the Mac.** The Windows watcher *hears* the Mac's advert, but CoreBluetooth doesn't reliably deliver a usable `LocalName` (the 128-bit ServiceUuid saturates the primary advert and Apple often skips the scan-response name). So `BleDiscovery_Windows` surfaces a stable synthetic name `BetterSend-<last4hex>` when the BetterSend ServiceUuid matches but no name is available; the real device name arrives later via the TCP Hello on first connect (per-address commit-then-reuse avoids double-listing). The earlier Mac scan duty-cycle was tried and reverted — it targeted the Mac advert (which arrives fine), not the missing scan-response.

## Project structure

```
BetterSend/
├── cpp_core/
│   ├── include/          # Interfaces + data types
│   │   ├── IDiscovery.h  ITransport.h  IProtocol.h  ITransferable.h
│   │   ├── IConnectionBroker.h  IPeerHandshake.h
│   │   ├── BonjourDiscovery.h  MdnsDiscovery.h  BleDiscovery.h
│   │   ├── TcpTransport.h  TransferProtocol.h
│   │   ├── FileTransferable.h  TextTransferable.h
│   │   ├── Device.h  Constants.h  Logger.h  Utf8.h
│   │   └── mdns.h            # Single-header mDNS (mjansson, public domain)
│   ├── src/
│   │   ├── BonjourDiscovery.cpp        # Apple-pair future phase (excluded from Phase 1 build)
│   │   ├── MdnsDiscovery.cpp           # Dev fallback (Linux/Android future)
│   │   ├── BleDiscovery_Windows.cpp    # Phase 1 Windows BLE advertise + scan (WinRT)
│   │   ├── BleDiscovery_Mac.mm         # Phase 1 macOS BLE advertise + scan (CoreBluetooth)
│   │   ├── BleHandshake_Windows.cpp    # Phase 1 GATT server — publishes hotspot creds
│   │   ├── BleHandshake_Mac.mm         # Phase 1 GATT client — reads peer's creds
│   │   ├── WindowsHotspotBroker.cpp    # Phase 1 host — brings up Windows Mobile Hotspot
│   │   ├── MacWifiClientBroker.mm      # Phase 1 client — CoreWLAN associateToNetwork
│   │   ├── TcpTransport.cpp            # Asio async server + sync send, streaming file recv
│   │   ├── TransferProtocol.cpp        # nlohmann/json header + 4B BE length prefix
│   │   └── bettersend_api.cpp          # extern "C" Facade for Flutter FFI
│   └── tests/
├── flutter_app/
│   ├── lib/
│   │   ├── ffi_bridge.dart        # FFI bridge — real library required
│   │   ├── main.dart
│   │   └── screens/
│   │       ├── home_screen.dart   # Discovery + device list
│   │       ├── settings_screen.dart # Display name + save folder (persisted)
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
