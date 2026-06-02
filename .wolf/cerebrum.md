# Cerebrum

> OpenWolf's learning memory. Updated automatically as the AI learns from interactions.
> Do not edit manually unless correcting an error.
> Last updated: 2026-05-27

## User Preferences

- **Indentation:** Tabs only — no spaces anywhere (C++, Dart, CMake, all files).
- **Code simplicity:** Remove dead/unused code. Keep it minimal — no unnecessary helpers or premature abstractions.
- **Step-by-step approach:** Build incrementally from smallest working piece. Each iteration should be runnable/observable.
- **Collaboration:** User wants to be a partner in the build process, not handed a finished product. Show progress at each step.
- **OCP focus:** Apply Open-Closed Principle. Use design patterns (Strategy, Factory, etc.) when they make extension easier without modifying existing code.
- **Code docs HTML:** `docs/codebase-explained.html` must be updated **immediately after every file change** — not at the end of the task. Add sections for new files, update changed sections, update the date. No exceptions.

## Key Learnings

- **Project:** BetterSend — AirDrop upgraded, fully cross-platform. Transfer files/clipboard between ANY two devices with the app. No internet, no account, no shared WiFi. Only dependency: BetterSend on both sides.
- **Target pairs:** iOS↔Android, iOS↔Mac, iOS↔Windows, Android↔Mac, Android↔Windows, Mac↔Windows, and all others.
- **Transport strategy per pair:** Apple↔Apple = AWDL (Bonjour). Android↔Android = Wi-Fi Direct. iOS↔Android = BLE discovery + hotspot. Any↔Windows = Wi-Fi Direct. Remote (future) = cloud signaling + WebRTC.
- **IDiscovery + ITransport Strategy pattern absorbs all pair complexity** — new pairs = new implementations, zero changes to existing code.
- **Transfer type design:** Each transferable item (file, text) implements `ITransferable` — `makeHeader()` + `payload()`. Transport layer is closed to new types.
- **FFI mock mode:** `ffi_bridge.dart` falls back to mock mode (`_mockMode = true`) when native library isn't built. `echo()` returns `'[mock] $text'` in mock mode. `startDiscovery` fires 2 fake devices after 1-2s in mock mode.
- **ITransport unified send:** `ITransport::send(ip, port, ITransferable&)` replaces separate `sendFile`/`sendClipboard` — new types don't require modifying the transport interface.
- **bettersend_echo:** First FFI smoke test. Uses `thread_local std::string buf` to avoid allocation. Caller does NOT free the returned pointer (static buffer).
- **Splash screen:** `SplashScreen` in `flutter_app/lib/screens/splash_screen.dart`. "Better Send Than Sorry". Navigates via `onDone` callback after 2.2s.
- **MdnsDiscovery pattern:** Factory `makeDiscovery()` in `MdnsDiscovery.h` hides concrete class from callers. `bettersend_api.cpp` never includes `MdnsDiscovery.cpp` internals.
- **mdns.h sockets:** All sockets are non-blocking (O_NONBLOCK set by mdns.h). Must use `select()` with timeout in listen loops — never raw blocking recv.
- **Discovery IP trick:** Use `from` sockaddr in the mDNS callback instead of parsing A record — sender IP is the device's IP. Simplifies callback by ~30 lines.
- **Two buffers for advertise:** `recvBuf` for `mdns_socket_listen`, `sendBuf` for `mdns_query_answer_*` — same buffer cannot be used for both (would corrupt received data).
- **NativeCallable.listener:** Required for FFI callbacks that come from background C threads. Posts to Dart isolate automatically. Keep reference alive (`_discoveryCb`), close in `dispose()`.
- **Windows C++ compat:** `getifaddrs` is POSIX-only — Windows needs `GetAdaptersAddresses` from `<iphlpapi.h>`. Use `#ifdef _WIN32` for all platform-specific headers. CMake: add `iphlpapi` to WIN32 link libs.
- **Flutter multi-platform:** `ffi_bridge.dart` loads `.dylib` (macOS), `.dll` (Windows), `.so` (Android), `DynamicLibrary.process()` (iOS). Each platform's runner copies the library to the correct location.
- **Flutter Windows runner:** `flutter_app/windows/CMakeLists.txt` bundles `bettersend_core.dll`. MSVC puts DLL in `build/cpp_core/<Config>/`, MinGW puts it in `build/cpp_core/` — both paths covered with OPTIONAL install rules.

## Do-Not-Repeat

<!-- Format: [YYYY-MM-DD] Description of what went wrong and what to do instead. -->
- [2026-06-01] Do NOT use spaces for indentation. Always use tabs — even in Dart files despite `dart format` convention.
- [2026-06-01] Do NOT add mock mode, fake devices, or simulated data to the UI. User explicitly rejected this. Real library + real mDNS only. If lib not built → app fails with clear error, not fake results.
- [2026-06-01] Update docs/codebase-explained.html IMMEDIATELY after each file change — not at the end of the session. User explicitly required this. Never skip or defer.
- [2026-06-02] Update README.md alongside HTML docs — both must stay in sync. README updated when: new files added, architecture changes, new platform support, phase status changes.
- [2026-06-02] MdnsDiscovery.cpp must use `#ifdef _WIN32` for all POSIX-only headers. Never write `#include <ifaddrs.h>` or `<arpa/inet.h>` at top level — always conditional. `getifaddrs` → `GetAdaptersAddresses` on Windows.

## Decision Log

- [2026-06-01] `ITransport::send(ITransferable&)` chosen over separate `sendFile`/`sendClipboard`. OCP: adding new types requires no changes to `ITransport`.
- [2026-06-01] `ITransferable` defined in `ITransferable.h`, includes `IProtocol.h` for `MessageHeader`. Simple dependency, no circular issues.
- [2026-06-01] Mock mode REMOVED — real library or crash. User rejected fake data completely.
- [2026-06-01] `bettersend_echo` uses `thread_local static std::string` — no malloc/free required across FFI boundary for this demo function.
- [2026-06-01] BonjourDiscovery uses `kDNSServiceFlagsIncludeAWDL | kDNSServiceFlagsIncludeP2P` — enables peer-to-peer discovery without shared WiFi (user requirement: no WiFi dependency, like AirDrop).
- [2026-06-01] Apple platforms use BonjourDiscovery (dns_sd.h), others use MdnsDiscovery (raw mdns.h) — CMakeLists.txt selects at compile time. Same factory name `makeDiscovery()`.
