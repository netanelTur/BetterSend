#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <span>

namespace BetterSend {

// ── MessageHeader ─────────────────────────────────────────────────────────────
// Logical representation of the protocol header.
// Encoded to/from the wire format by IProtocol.

struct MessageHeader {
	enum class Type : uint8_t { File, Clipboard };

	Type        type{};
	std::string name;        // Filename (empty for Clipboard)
	std::size_t size{};      // Payload size in bytes
	std::string senderName;
};

// ── Wire format ───────────────────────────────────────────────────────────────
// Every message on the wire:
//
//   [ 4 bytes, big-endian uint32 : JSON header length N ]
//   [ N bytes, UTF-8             : JSON header          ]
//   [ M bytes                   : binary payload        ]   ← sent separately
//
// JSON header schema:
//   { "type": "file"|"clipboard", "name": "photo.jpg", "size": 1048576,
//     "sender": "iPhone-Yoni" }

// ── IProtocol ─────────────────────────────────────────────────────────────────
// Strategy interface for encoding and decoding message headers.
//
// Implementations:
//   - TransferProtocol   (production — uses nlohmann/json)
//   - MockProtocol       (tests — trivial passthrough)
//
// Design note: payload bytes are streamed directly by ITransport;
// IProtocol only owns the framing header.

class IProtocol {
public:
	virtual ~IProtocol() = default;

	// Encode header → wire bytes (4-byte length prefix + JSON).
	[[nodiscard]] virtual std::vector<uint8_t> encodeHeader(
		const MessageHeader& header) = 0;

	// Decode wire bytes → MessageHeader.
	// data must contain at least the 4-byte prefix and the full JSON.
	// Throws std::runtime_error on malformed input.
	[[nodiscard]] virtual MessageHeader decodeHeader(
		std::span<const uint8_t> data) = 0;
};

} // namespace BetterSend
