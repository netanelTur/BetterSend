# BetterSend — Development Plan

Cross-platform AirDrop alternative: iOS ↔ Android ↔ macOS ↔ Windows.
No internet required. C++20 backend + Flutter UI.

---

## Architecture Overview

```
Flutter UI  (Dart, thin)
     │  dart:ffi calls
     ▼
bettersend_core.so/.dylib/.dll   (C++20)
     ├── Logger          (Singleton)
     ├── IDiscovery      (Strategy) ← MdnsDiscovery
     ├── ITransport      (Strategy) ← TcpTransport
     ├── IProtocol       (Strategy) ← TransferProtocol
     ├── IClipboard      (Strategy) ← platform impls (Phase 2)
     └── bettersend_api  (Facade, C API)
```

---

## Phase 1 — iOS ↔ Android File Transfer

**Goal:** Send a file from iPhone to Android on the same Wi-Fi. No internet.

### Steps

| # | Task | File(s) | What to learn |
|---|------|---------|---------------|
| 1 | Verify CMake builds on your machine | `CMakeLists.txt` | [CMake tutorial](https://cmake.org/cmake/help/latest/guide/tutorial/) |
| 2 | Implement `TransferProtocol` (JSON encode/decode) | `TransferProtocol.cpp` | [nlohmann/json docs](https://json.nlohmann.me/) · Big-endian byte manipulation |
| 3 | Pass `test_protocol.cpp` — all 5 tests green | `tests/test_protocol.cpp` | [GoogleTest primer](https://google.github.io/googletest/primer.html) |
| 4 | Implement `TcpTransport` (Asio async server + client) | `TcpTransport.cpp` | [Asio async TCP server example](https://think-async.com/Asio/asio-1.28.0/doc/asio/tutorial/tutdaytime3.html) |
| 5 | Pass `test_transport.cpp` — localhost round-trip | `tests/test_transport.cpp` | `std::promise` / `std::future` for test synchronization |
| 6 | Download `mdns.h`, implement `MdnsDiscovery` | `MdnsDiscovery.cpp` | [mdns.h README](https://github.com/mjansson/mdns) · RFC 6762 (skim) |
| 7 | Pass `test_discovery.cpp` — start/stop + self-discovery | `tests/test_discovery.cpp` | macOS: enable Local Network permission |
| 8 | Wire up `bettersend_api.cpp` — connect all three | `bettersend_api.cpp` | C++ `extern "C"` linkage |
| 9 | Build `.so` for Android, `.dylib` for iOS via CMake | `CMakeLists.txt` | [Flutter + CMake FFI template](https://docs.flutter.dev/platform-integration/android/c-interop) |
| 10 | Implement `ffi_bridge.dart` — all typedefs + `BetterSendBridge` | `ffi_bridge.dart` | [dart:ffi docs](https://dart.dev/guides/libraries/c-interop) · `NativeCallable` |
| 11 | Build basic Flutter UI (HomeScreen + DeviceList + Transfer) | `screens/` | Ask Claude using the prompts at the top of each screen file |
| 12 | End-to-end test: iPhone → Android, real file | Manual | Android Studio + Xcode simulator or real devices |

**Milestone:** Send a 10 MB photo from iPhone to Android. Works on Wi-Fi, no internet.

---

## Phase 2 — Clipboard Sync

**Goal:** Copy text on one device, paste it on another.

### Steps

| # | Task | File(s) | What to learn |
|---|------|---------|---------------|
| 13 | Implement `ClipboardApple.mm` (macOS + iOS) | `src/platform/ClipboardApple.mm` | Objective-C++ (`NSPasteboard` / `UIPasteboard`) |
| 14 | Implement `ClipboardAndroid.cpp` (JNI) | `src/platform/ClipboardAndroid.cpp` | Android JNI · `ClipboardManager` Java API |
| 15 | Write `MockClipboard` for tests | `tests/test_clipboard.cpp` | gmock `MOCK_METHOD` |
| 16 | Add `type=clipboard` flow through existing Transport + Protocol | `bettersend_api.cpp` | Existing code — no new concepts |
| 17 | Flutter: clipboard read via `bettersend_clipboard_read` | `ffi_bridge.dart` | `Pointer<Utf8>` + `StringBuffer` in Dart |
| 18 | UI: 'Send Clipboard' button in HomeScreen | `home_screen.dart` | Ask Claude using prompt at top of file |

**Milestone:** Copy text on iPhone, paste it on Android. Works on Wi-Fi.

---

## Phase 3 — All Platforms + Drag & Drop

**Goal:** Works on macOS, Windows, Linux. Desktop supports drag & drop.

### Steps

| # | Task | File(s) | What to learn |
|---|------|---------|---------------|
| 19 | Implement `ClipboardWindows.cpp` | `src/platform/ClipboardWindows.cpp` | Win32 `OpenClipboard` / `SetClipboardData` |
| 20 | macOS entitlements (Local Network, Bonjour) | `flutter_app/macos/` | Apple entitlements `.plist` |
| 21 | Windows firewall rule for port 9000 | CMake install script | Windows Defender firewall via PowerShell |
| 22 | Drag & drop on desktop | `flutter_app/lib/` | [`desktop_drop`](https://pub.dev/packages/desktop_drop) Flutter package |
| 23 | Progress callbacks in `ITransport::sendFile` | `ITransport.h`, `TcpTransport.cpp` | Asio async_write with byte counting |
| 24 | CI: GitHub Actions — build + ctest on macOS + Ubuntu | `.github/workflows/` | GitHub Actions YAML · CMake in CI |

**Milestone:** Drag a file from macOS Finder, drop into BetterSend window, it arrives on Android.

---

## Nice to Have — Cloud Room (Remote Transfers)

**Goal:** Transfer files between devices on different networks using a relay server.

### Concept
- Device A and B both connect to a cloud relay via WebSocket.
- They share a 6-digit room code (like Snapdrop).
- Data is relayed (not stored) through the server.

### Steps (rough)

| # | Task | What to learn |
|---|------|---------------|
| 25 | Relay server (Node.js or Go, minimal) | WebSocket server · Room concept |
| 26 | Add `ITransport` implementation: `WebSocketTransport` | [libwebsockets](https://libwebsockets.org/) or Asio WebSocket |
| 27 | Room code UI: 6-digit input screen | Flutter `TextField` + code sharing |
| 28 | End-to-end encryption (optional, strong nice-to-have) | libsodium or OpenSSL · ECDH key exchange |

---

## Key Resources

| Topic | Link |
|-------|------|
| CMake docs | https://cmake.org/cmake/help/latest/ |
| Asio standalone | https://think-async.com/Asio/ |
| nlohmann/json | https://json.nlohmann.me/ |
| mdns.h | https://github.com/mjansson/mdns |
| GoogleTest | https://google.github.io/googletest/ |
| dart:ffi | https://dart.dev/guides/libraries/c-interop |
| Flutter C interop | https://docs.flutter.dev/platform-integration/android/c-interop |
| Objective-C++ | https://developer.apple.com/documentation/foundation |

---

## Design Patterns Used

| Pattern | Where | Why |
|---------|-------|-----|
| Strategy | `IDiscovery`, `ITransport`, `IProtocol`, `IClipboard` | Swap implementations for tests or platforms |
| Singleton | `Logger` | One log stream, accessible from anywhere |
| Facade | `bettersend_api.cpp` | Single C API over the C++ subsystems |
| RAII | All resource handles (sockets, files, threads) | Exception-safe cleanup |

---

## How to Run Tests

```bash
cd BetterSend
cmake -B build -DBUILD_TESTS=ON
cmake --build build
cd build && ctest --verbose
```

Expected output while skeleton-only: all tests `SKIPPED` (not failed).
Each test becomes green as you implement the corresponding class.
