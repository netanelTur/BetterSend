#pragma once
#include <cstddef>
#include <string>

// ── BetterSend — UTF-8 helpers ───────────────────────────────────────────────
// Small header-only utilities for byte-safe handling of UTF-8 text.

namespace BetterSend {

// Truncate a UTF-8 string to at most maxBytes BYTES without splitting a
// multi-byte code point. A blind resize() at a fixed byte index can slice
// through the middle of a Hebrew letter or emoji and produce an invalid
// sequence that corrupts the BLE advert / TCP header or renders as garbage on
// the peer. This walks the cut point back to the nearest code-point boundary.
//
// kBleMaxNameLen is a byte budget (31-byte legacy advertisement), so every
// device-name path that feeds the radio runs through here.
inline std::string clampUtf8(const std::string& s, int maxBytes) {
	if (maxBytes <= 0) return {};
	if (s.size() <= static_cast<std::size_t>(maxBytes)) return s;

	std::size_t cut = static_cast<std::size_t>(maxBytes);
	// A continuation byte matches 0b10xxxxxx. While the byte at `cut` is one,
	// the cut would land inside a code point — back up to its lead byte and
	// drop the whole (incomplete) code point.
	while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) {
		--cut;
	}
	return s.substr(0, cut);
}

} // namespace BetterSend
