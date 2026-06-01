# anatomy.md

> Auto-maintained by OpenWolf. Last scanned: 2026-05-27T07:28:13.294Z
> Files: 29 tracked | Anatomy hits: 0 | Misses: 0

## ./

- `.DS_Store` (~1640 tok)
- `.gitignore` — Git ignore rules (~609 tok)
- `CLAUDE.md` — OpenWolf (~57 tok)
- `CMakeLists.txt` — CMake build configuration (~150 tok)
- `plan.md` — BetterSend — Development Plan (~1676 tok)

## .claude/

- `settings.json` (~441 tok)

## .claude/rules/

- `openwolf.md` (~313 tok)

## cpp_core/

- `CMakeLists.txt` — CMake build configuration (~432 tok)

## cpp_core/include/

- `Constants.h` — pragma once (~384 tok)
- `Device.h` — pragma once (~251 tok)
- `IClipboard.h` — pragma once (~328 tok)
- `IDiscovery.h` — pragma once (~398 tok)
- `IProtocol.h` — pragma once (~546 tok)
- `ITransport.h` — pragma once (~571 tok)
- `Logger.h` — pragma once (~1370 tok)

## cpp_core/src/

- `bettersend_api.cpp` — include "IDiscovery.h" (~2243 tok)
- `MdnsDiscovery.cpp` — include "IDiscovery.h" (~916 tok)
- `TcpTransport.cpp` — include "ITransport.h" (~1024 tok)
- `TransferProtocol.cpp` — include "IProtocol.h" (~1046 tok)

## cpp_core/tests/

- `CMakeLists.txt` — CMake build configuration (~200 tok)
- `test_discovery.cpp` — include <gtest/gtest.h> (~776 tok)
- `test_protocol.cpp` — include <gtest/gtest.h> (~842 tok)
- `test_transport.cpp` — include <gtest/gtest.h> (~792 tok)

## flutter_app/

- `pubspec.yaml` — Dart/Flutter package manifest (~169 tok)

## flutter_app/lib/

- `ffi_bridge.dart` — ── ffi_bridge.dart ─────────────────────────────────────────────────────────── (~1369 tok)
- `main.dart` — ── main.dart ───────────────────────────────────────────────────────────────── (~279 tok)

## flutter_app/lib/screens/

- `device_list_screen.dart` — ── ASK CLAUDE FOR UI ───────────────────────────────────────────────────────── (~768 tok)
- `home_screen.dart` — ── ASK CLAUDE FOR UI ───────────────────────────────────────────────────────── (~866 tok)
- `transfer_screen.dart` — ── ASK CLAUDE FOR UI ───────────────────────────────────────────────────────── (~885 tok)
