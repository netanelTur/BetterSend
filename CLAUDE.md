# OpenWolf

@.wolf/OPENWOLF.md

This project uses OpenWolf for context management. Read and follow .wolf/OPENWOLF.md every session. Check .wolf/cerebrum.md before generating code. Check .wolf/anatomy.md before reading files.

---

# BetterSend 🛰️☄️🚀👾👽

## Vision

**AirDrop — upgraded. Cross-platform.**

Transfer files and clipboard between any two devices that have BetterSend installed.
No internet. No shared WiFi. No account. Just the app on both sides.

### Target device pairs (all combinations):
- iPhone ↔ Android
- iPhone ↔ Mac
- iPhone ↔ Windows PC
- Android ↔ Mac
- Android ↔ Windows PC
- Mac ↔ Windows PC
- + any future platform

**The only dependency: both devices run BetterSend.**

### Roadmap

| Phase | Goal | Milestone |
|-------|------|-----------|
| **1 — Now** | File transfer Mac ↔ Windows, **no internet, no shared WiFi** | 10 MB photo sent successfully |
| **2** | iOS ↔ Android file transfer (same BLE → hotspot pattern) | 10 MB photo sent successfully |
| **3** | Clipboard (copied text) transfer across already-supported pairs | Paste on other device |
| **4** | Full OS matrix — every pair, every direction | All pairs work |
| **Nice-to-have** | Cloud room: shared workspace via code, works across distance | Remote devices connect |

> **Why Mac↔Windows first:** It's the dev pair (user on Windows, partner on Mac), so dogfooding is constant. The discovery + transport-bring-up pattern is identical to every other no-shared-network pair, so getting it right here unlocks all the others through the Strategy interfaces.

### Transport strategy (per device pair)

Each pair uses the best available P2P technology — **no shared WiFi router required, no user network setup**:

| Pair | Discovery | Transport bring-up | Transport |
|------|-----------|-------------------|-----------|
| **Mac ↔ Windows (Phase 1)** | **BLE** advertise + scan, BetterSend service UUID | Windows side starts Mobile Hotspot programmatically (WinRT `NetworkOperatorTetheringManager`); Mac auto-joins using SSID+PSK exchanged over BLE GATT | TCP over the hotspot |
| iOS ↔ iOS / Mac ↔ Mac / iOS ↔ Mac | Bonjour + AWDL (`dns_sd.h`) | — (AWDL is always-on) | TCP over AWDL |
| Android ↔ Android | mDNS + Wi-Fi Direct | WifiP2pManager group | TCP over Wi-Fi Direct |
| iOS ↔ Android (Phase 2) | BLE | one side creates hotspot, other connects | TCP over hotspot |
| Any ↔ Windows | BLE + hotspot (same as Mac↔Windows) | same | TCP |
| Remote (nice-to-have) | Cloud signaling with shared code | — | WebRTC or relay |

`IDiscovery`, `ITransport`, and a new `IConnectionBroker` (handles hotspot/Wi-Fi Direct bring-up) Strategy interfaces absorb all of this — new pairs never touch existing code. The same BLE-discovery + hotspot-bring-up pattern repeats for every no-shared-network pair; only the platform glue differs.

```
Flutter UI  (Dart)
     │  dart:ffi → bettersend_api.cpp (extern "C" Facade)
     ▼
bettersend_core.so/.dylib/.dll   (namespace BetterSend)
     ├── Logger              Singleton — BS_LOG_* macros only
     ├── IDiscovery          ← BleDiscovery (Phase 1, cross-platform via SimpleBLE)
     │                         BonjourDiscovery (Apple-pair, later)
     │                         MdnsDiscovery (legacy/over-existing-Wi-Fi, dev fallback)
     ├── IConnectionBroker   ← WindowsHotspotBroker, MacWifiClientBroker
     │                         (brings up the actual network the transport runs on)
     ├── ITransport          ← TcpTransport    port: kDefaultPort (9000)
     ├── IProtocol           ← TransferProtocol  wire: [4B big-endian len][JSON header][payload]
     ├── ITransferable       ← FileTransferable, TextTransferable
     └── IClipboard          ← platform impls (Phase 3)
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

**No mock mode:** Never add fake/stub data to the UI (mock devices, simulated discovery, placeholder transfers). If the native library isn't built, the app should fail loudly — not silently show fake results. Real network behavior only.

**Build the library first:** `cmake -B build && cmake --build build` from project root. The Xcode build phase copies `build/cpp_core/libbettersend_core.dylib` into the app bundle automatically. Nathaniel must also run cmake after cloning.

## Code documentation

`docs/codebase-explained.html` — הסבר שורה-שורה של כל הקוד בפרויקט. **חובה לעדכן מיד בסוף כל שינוי — לא בסוף המשימה, אחרי כל קובץ שנשתנה.**

`README.md` — project overview, roadmap, architecture, build instructions. **עדכן במקביל ל-HTML** כשמשתנה: ארכיטקטורה, phases, קבצים חדשים, הנחיות build.

כשמעדכנים HTML:
- הוסף section חדש לכל קובץ שנוצר
- עדכן sections קיימות אם הקוד השתנה
- עדכן את תאריך העדכון האחרון בpage-header

כשמעדכנים README:
- עדכן טבלת device pairs אם platform חדש נוסף
- עדכן project structure אם קבצים נוספו/הוסרו
- עדכן roadmap status אם phase הושלם

## Verification

```bash
# Build + test (run this after any C++ change)
cmake -B build -DBUILD_TESTS=ON && cmake --build build && cd build && ctest --verbose

# Skeleton-only: tests show SKIPPED — that is correct, not a failure
# A test goes green only when its implementation is complete
```

## Phase discipline

**Currently: Phase 1** — file transfer Mac ↔ Windows, **no internet, no shared WiFi, no user network setup**.  
Milestone: 10 MB photo Windows → Mac, peer-to-peer, zero shared infrastructure.

Don't implement Phase 2 (iOS↔Android), Phase 3 (clipboard), or Phase 4 (full matrix) until Phase 1 milestone passes.  
`IClipboard` stubs and other-pair stubs are fine to keep; full impls wait.

**Mac ↔ Windows P2P path (Phase 1 core challenge):**  
1. **BLE discovery** — both apps advertise BetterSend service UUID + device name; both scan for the same UUID. Cross-platform via SimpleBLE in C++ core. Single `BleDiscovery` impl covers both sides.
2. **BLE GATT handshake** — once peers see each other, the device that will act as host (deterministic tie-break, e.g., MAC ordering) writes SSID + PSK + listen port into a GATT characteristic the other reads.
3. **Connection bring-up** — Windows host calls `WindowsHotspotBroker::start()` (WinRT `NetworkOperatorTetheringManager`); Mac client calls `MacWifiClientBroker::joinSsid(...)` (CoreWLAN `associateToNetwork`). macOS programmatic hotspot is restricted, so first version always picks Windows as host when one is present; Mac-as-host comes later.
4. **TCP transfer** — `TcpTransport` runs on the hotspot's local subnet exactly as today. The transfer code stays unchanged.

Strategy pattern means the only new C++ code is `BleDiscovery` + two `IConnectionBroker` impls — nothing else changes. **The same pattern repeats** for iOS↔Android and Any↔Windows pairs in later phases.

**Future-platform safety check (must hold before merging Phase 1 code):**
- Nothing Mac/Windows-specific leaks into shared headers (`IDiscovery.h`, `IConnectionBroker.h`, `ITransport.h`, `bettersend_api.cpp` C surface). Platform code lives only in its own `.cpp` files behind the interface.
- `BleDiscovery` uses a generic library (SimpleBLE or equivalent) that already supports Android/iOS — no rewrite needed when those land.
- `IConnectionBroker` is an interface, not a class hierarchy — easy to add `AndroidWifiDirectBroker`, `IosHotspotBroker`, etc. without touching existing impls.
- Wire format and protocol stay unchanged across all phases.
