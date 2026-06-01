#pragma once
#include "IProtocol.h"
#include <vector>
#include <cstdint>
#include <string_view>

namespace BetterSend {

// ── ITransferable ─────────────────────────────────────────────────────────────
// Uniform contract for anything that can be sent between devices.
//
// Each concrete type (file, text, clipboard) knows how to:
//   1. Build its own protocol header (name, size, type)
//   2. Serialize its payload to bytes
//
// ITransport::send() accepts an ITransferable so the transport layer
// is decoupled from the specific content type being sent.
//
// Implementations:
//   - FileTransferable    (cpp_core/include/FileTransferable.h)
//   - TextTransferable    (cpp_core/include/TextTransferable.h)

class ITransferable {
public:
	virtual ~ITransferable() = default;

	// Build the protocol header for this item.
	// senderName — this device's display name, embedded in the header.
	[[nodiscard]] virtual MessageHeader makeHeader(std::string_view senderName) const = 0;

	// Serialize the payload bytes that follow the header on the wire.
	[[nodiscard]] virtual std::vector<uint8_t> payload() const = 0;
};

} // namespace BetterSend
