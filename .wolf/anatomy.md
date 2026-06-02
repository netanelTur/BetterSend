# anatomy.md

> Auto-maintained by OpenWolf. Last scanned: 2026-06-02T08:13:21.308Z
> Files: 44 tracked | Anatomy hits: 0 | Misses: 0

## ../../.claude/projects/-Users-yonatanrotem-Desktop-BetterSend/memory/

- `feedback_docs_update_timing.md` (~189 tok)
- `MEMORY.md` — Memory Index (~96 tok)
- `project_bettersend.md` (~435 tok)

## ./

- `.DS_Store` (~1640 tok)
- `.gitignore` — Git ignore rules (~609 tok)
- `CLAUDE.md` — OpenWolf (~1694 tok)
- `CMakeLists.txt` — CMake build configuration (~150 tok)
- `plan.md` — BetterSend — Development Plan (~1676 tok)
- `README.md` — Project documentation (~1125 tok)

## .claude/

- `settings.json` (~441 tok)

## .claude/rules/

- `openwolf.md` (~313 tok)

## cpp_core/

- `CMakeLists.txt` (~497 tok)

## cpp_core/include/

- `Constants.h` — Compile-time constants: ports, buffer sizes, service type (~326 tok)
- `Device.h` — `struct Device { name, ip, port }` — mDNS peer value type (~230 tok)
- `FileTransferable.h` — `ITransferable` impl for binary files; `makeHeader()` + `payload()` reads file bytes (~375 tok)
- `IClipboard.h` — Strategy interface: `read()` / `write(text)` — platform impls Phase 2 (~302 tok)
- `IDiscovery.h` — Strategy interface: `startAdvertising` / `startDiscovery` / `stop` (~364 tok)
- `IProtocol.h` — Defines `MessageHeader` struct + encode/decode JSON header interface (~497 tok)
- `ITransferable.h` — Uniform OCP contract: `makeHeader(senderName)` + `payload()` for all sendable types (~307 tok)
- `ITransport.h` — Strategy interface: `startServer` / `send(ITransferable&)` / `stop` (~501 tok)
- `Logger.h` — Thread-safe singleton; BS_LOG_DEBUG/INFO/WARN/ERROR macros (~1182 tok)
- `MdnsDiscovery.h` — Factory declaration: `makeDiscovery()` returns `unique_ptr<IDiscovery>` (~81 tok)
- `TextTransferable.h` — `ITransferable` impl for plain text/clipboard; wire type=Clipboard (~244 tok)

## cpp_core/src/

- `bettersend_api.cpp` — C Facade; bettersend_create instantiates MdnsDiscovery; startAdvertising+startDiscovery wired; echo + stubs (~1439 tok)
- `BonjourDiscovery.cpp` — include "IDiscovery.h" (~1902 tok)
- `MdnsDiscovery.cpp` — IDiscovery via mdns.h; cross-platform: Win32 uses GetAdaptersAddresses, POSIX uses getifaddrs (~2921 tok)
- `TcpTransport.cpp` — `ITransport` stub; `send(ITransferable&)` (TODO: Asio async TCP) (~693 tok)
- `TransferProtocol.cpp` — `IProtocol` stub; JSON encode/decode (TODO: nlohmann/json) (~872 tok)

## cpp_core/tests/

- `CMakeLists.txt` — CMake build configuration (~200 tok)
- `test_discovery.cpp` — include <gtest/gtest.h> (~703 tok)
- `test_protocol.cpp` — include <gtest/gtest.h> (~749 tok)
- `test_transport.cpp` — include <gtest/gtest.h> (~714 tok)

## docs/

- `codebase-explained.html` — BetterSend — הסבר הקוד (~15465 tok)

## flutter_app/

- `pubspec.yaml` — Dart/Flutter package manifest (~169 tok)

## flutter_app/lib/

- `ffi_bridge.dart` — ── ffi_bridge.dart ─────────────────────────────────────────────────────────── (~1193 tok)
- `main.dart` — Entry point; splash → HomeScreen transition; initializes BetterSendBridge (~342 tok)

## flutter_app/lib/screens/

- `device_list_screen.dart` — Device picker; lists discovered peers + Send buttons (~672 tok)
- `home_screen.dart` — Main screen; calls startAdvertising+startDiscovery in initState; shows _DeviceCard list or scanning indicator; FFI echo test (~1148 tok)
- `splash_screen.dart` — Opening screen; "Better Send Than Sorry" fade-in, 2.2s then onDone callback (~443 tok)
- `transfer_screen.dart` — Transfer progress screen; upload/download icon + LinearProgressIndicator (~769 tok)

## flutter_app/macos/Runner.xcodeproj/

- `project.pbxproj` — !$*UTF8*$! (~7671 tok)

## flutter_app/macos/Runner/

- `DebugProfile.entitlements` (~108 tok)
- `Release.entitlements` (~94 tok)

## flutter_app/windows/

- `CMakeLists.txt` — Project-level configuration. (~1187 tok)
