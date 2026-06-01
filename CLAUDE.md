# OpenWolf

@.wolf/OPENWOLF.md

This project uses OpenWolf for context management. Read and follow .wolf/OPENWOLF.md every session. Check .wolf/cerebrum.md before generating code. Check .wolf/anatomy.md before reading files.

---

# BetterSend — Project Rules & Guidelines

Cross-platform AirDrop alternative. C++20 shared library (`bettersend_core`) + Flutter thin UI via `dart:ffi`. Local Wi-Fi only.

```
Flutter UI  (Dart)
     │  dart:ffi → bettersend_api.cpp (extern "C" Facade)
     ▼
bettersend_core.so/.dylib/.dll   (namespace BetterSend)
     ├── Logger        Singleton — BS_LOG_* macros only
     ├── IDiscovery    ← MdnsDiscovery   service: _bettersend._tcp.local.
     ├── ITransport    ← TcpTransport    port: kDefaultPort (9000)
     ├── IProtocol     ← TransferProtocol  wire: [4B big-endian len][JSON header][payload]
     ├── ITransferable ← FileTransferable, TextTransferable
     └── IClipboard    ← platform impls (Phase 2 only)
```

## Coding conventions

**Indentation:** Tabs only — never spaces. Apply to all new and modified files (C++, Dart, CMake).

**Complexity:** Keep code minimal. No unnecessary code (unused helpers, premature abstractions). Three concrete cases beat one premature abstraction.

**Development approach:** Step by step — start with the most minimal working thing and build incrementally. Each step should produce something observable/runnable.

## Design principles

**Open-Closed Principle (OCP):** Classes are open for extension, closed for modification.
- Adding a new transfer type → create new `ITransferable` subclass, no existing code changes.
- Adding a new transport → create new `ITransport` impl, no existing code changes.
- Adding a new discovery → create new `IDiscovery` impl, no existing code changes.
- Use Strategy, Factory, and similar patterns where they make extension easier.

**Uniform contract per transferable type:** Each transferable item (file, text, …) implements `ITransferable` — `makeHeader()` + `payload()`. The transport and protocol layers work with `ITransferable` and stay closed to new types.

**Design patterns in use:**

| Pattern | Where | Why |
|---------|-------|-----|
| Strategy | `IDiscovery`, `ITransport`, `IProtocol`, `IClipboard`, `ITransferable` | Swap impls without touching callers |
| Singleton | `Logger` | One log stream everywhere |
| Facade | `bettersend_api.cpp` | Single C API over all subsystems |
| RAII | Sockets, files, threads | Exception-safe cleanup |

## Non-obvious rules

**C API boundary (`bettersend_api.cpp`):** No C++ exceptions across `extern "C"` — every function is try/catch. Signatures: `const char*`, `int`, `long long`, `void*` only. No `std::string` in signatures.

**Logging:** `BS_LOG_DEBUG/INFO/WARN/ERROR("ComponentName", fmt, ...)` everywhere. No `printf`, `cout`, `fprintf`.

**Constants:** Every magic number lives in `cpp_core/include/Constants.h`. Never hardcode `9000`, `5353`, or `"_bettersend._tcp.local."` inline.

**Wire format:** `[4-byte big-endian uint32 header length][JSON: type/name/size/sender][binary payload]`. See `IProtocol.h`.

**FFI callbacks:** `onReceive` and `onFound` arrive on a C background thread. Use `NativeCallable.listener` — never call `setState` directly from the callback. All `dart:ffi` lookups stay in `ffi_bridge.dart`, never in screen files.

**Comments:** English only. No Hebrew in committed code.

## Verification

```bash
# Build + test (run this after any C++ change)
cmake -B build -DBUILD_TESTS=ON && cmake --build build && cd build && ctest --verbose

# Skeleton-only: tests show SKIPPED — that is correct, not a failure
# A test goes green only when its implementation is complete
```

## Phase discipline

**Currently: Phase 1** — file transfer iOS ↔ Android over Wi-Fi.  
Don't implement Phase 2 (clipboard) or Phase 3 (desktop/CI) until Phase 1 milestone passes (10 MB photo iPhone → Android).  
`IClipboard` stubs are fine to keep; full platform impls wait.
