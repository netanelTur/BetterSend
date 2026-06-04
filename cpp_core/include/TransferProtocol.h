#pragma once
#include "IProtocol.h"

namespace BetterSend {

// ── TransferProtocol ──────────────────────────────────────────────────────────
// Concrete IProtocol — encodes/decodes message headers as JSON wrapped in a
// 4-byte big-endian length prefix. Single source of truth for the on-wire
// header format; payload bytes are streamed separately by ITransport.

class TransferProtocol : public IProtocol {
public:
	[[nodiscard]] std::vector<uint8_t> encodeHeader(
		const MessageHeader& header) override;

	[[nodiscard]] MessageHeader decodeHeader(
		std::span<const uint8_t> data) override;
};

} // namespace BetterSend
