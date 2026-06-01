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

## Key Learnings

- **Project:** BetterSend — cross-platform AirDrop alternative (C++20 core + Flutter UI)
- **Transfer type design:** Each transferable item (file, text) implements `ITransferable` — `makeHeader()` + `payload()`. Transport layer is closed to new types.
- **FFI mock mode:** `ffi_bridge.dart` falls back to mock mode (`_mockMode = true`) when native library isn't built. `echo()` returns `'[mock] $text'` in mock mode.
- **ITransport unified send:** `ITransport::send(ip, port, ITransferable&)` replaces separate `sendFile`/`sendClipboard` — new types don't require modifying the transport interface.
- **bettersend_echo:** First FFI smoke test. Uses `thread_local std::string buf` to avoid allocation. Caller does NOT free the returned pointer (static buffer).
- **Splash screen:** `SplashScreen` in `flutter_app/lib/screens/splash_screen.dart`. "Better Send Than Sorry". Navigates via `onDone` callback after 2.2s.

## Do-Not-Repeat

<!-- Format: [YYYY-MM-DD] Description of what went wrong and what to do instead. -->
- [2026-06-01] Do NOT use spaces for indentation. Always use tabs — even in Dart files despite `dart format` convention.

## Decision Log

- [2026-06-01] `ITransport::send(ITransferable&)` chosen over separate `sendFile`/`sendClipboard`. OCP: adding new types requires no changes to `ITransport`.
- [2026-06-01] `ITransferable` defined in `ITransferable.h`, includes `IProtocol.h` for `MessageHeader`. Simple dependency, no circular issues.
- [2026-06-01] Mock mode in `ffi_bridge.dart` via try/catch on library load. Enables Flutter UI development without building C++ first.
- [2026-06-01] `bettersend_echo` uses `thread_local static std::string` — no malloc/free required across FFI boundary for this demo function.
