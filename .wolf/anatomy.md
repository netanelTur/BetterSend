# anatomy.md

> Auto-maintained by OpenWolf. Last scanned: 2026-06-01T15:26:40.318Z
> Files: 33 tracked | Anatomy hits: 0 | Misses: 0

## ./

- `.DS_Store` (~1640 tok)
- `.gitignore` — Git ignore rules (~609 tok)
- `CLAUDE.md` — OpenWolf (~987 tok)
- `CMakeLists.txt` — CMake build configuration (~150 tok)
- `plan.md` — BetterSend — Development Plan (~1676 tok)

## .claude/

- `settings.json` (~441 tok)

## .claude/rules/

- `openwolf.md` (~313 tok)

## cpp_core/

- `CMakeLists.txt` — CMake build configuration (~432 tok)

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
- `TextTransferable.h` — `ITransferable` impl for plain text/clipboard; wire type=Clipboard (~244 tok)

## cpp_core/src/

- `bettersend_api.cpp` — C API Facade (extern "C"); `bettersend_echo` FFI smoke test; stub impls for all API functions (~2093 tok)
- `MdnsDiscovery.cpp` — `IDiscovery` stub; advertise/discovery thread loops (TODO: mdns.h integration) (~790 tok)
- `TcpTransport.cpp` — `ITransport` stub; `send(ITransferable&)` (TODO: Asio async TCP) (~693 tok)
- `TransferProtocol.cpp` — `IProtocol` stub; JSON encode/decode (TODO: nlohmann/json) (~872 tok)

## cpp_core/tests/

- `CMakeLists.txt` — CMake build configuration (~200 tok)
- `test_discovery.cpp` — include <gtest/gtest.h> (~703 tok)
- `test_protocol.cpp` — include <gtest/gtest.h> (~749 tok)
- `test_transport.cpp` — include <gtest/gtest.h> (~714 tok)

## flutter_app/

- `pubspec.yaml` — Dart/Flutter package manifest (~169 tok)

## flutter_app/lib/

- `ffi_bridge.dart` — FFI bridge; mock mode fallback when lib absent; `BetterSendBridge` + `echo()` smoke test (~1367 tok)
- `main.dart` — Entry point; splash → HomeScreen transition; initializes BetterSendBridge (~342 tok)

## flutter_app/lib/screens/

- `device_list_screen.dart` — Device picker; lists discovered peers + Send buttons (~672 tok)
- `home_screen.dart` — Main screen; FFI echo test card (hello world demo) + device list + FAB (~1126 tok)
- `splash_screen.dart` — Opening screen; "Better Send Than Sorry" fade-in, 2.2s then onDone callback (~443 tok)
- `transfer_screen.dart` — Transfer progress screen; upload/download icon + LinearProgressIndicator (~769 tok)
