#pragma once
#include <string>

namespace BetterSend {

// ── IClipboard ────────────────────────────────────────────────────────────────
// Strategy interface for reading and writing the system clipboard.
//
// Each platform requires a distinct implementation:
//   - macOS / iOS : NSPasteboard / UIPasteboard  (Objective-C++)
//   - Android     : JNI → ClipboardManager
//   - Windows     : Win32 OpenClipboard / SetClipboardData
//
// File locations:
//   cpp_core/src/platform/ClipboardApple.mm   (macOS + iOS)
//   cpp_core/src/platform/ClipboardAndroid.cpp
//   cpp_core/src/platform/ClipboardWindows.cpp
//
// Selected at build time via CMake platform detection.
// Tests use MockClipboard (in-memory string).
//
// Phase: implement after file transfer (Phase 2).

class IClipboard {
public:
    virtual ~IClipboard() = default;

    // Read current text from the system clipboard.
    // Returns empty string if the clipboard is empty or contains non-text data.
    [[nodiscard]] virtual std::string read() = 0;

    // Write text to the system clipboard.
    virtual void write(const std::string& text) = 0;
};

} // namespace BetterSend
