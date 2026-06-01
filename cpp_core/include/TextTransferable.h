#pragma once
#include "ITransferable.h"
#include <string>

namespace BetterSend {

// ── TextTransferable ──────────────────────────────────────────────────────────
// ITransferable implementation for plain text (clipboard or short messages).
//
// Wire type: MessageHeader::Type::Clipboard
// Payload  : UTF-8 bytes of the text string

class TextTransferable : public ITransferable {
public:
	explicit TextTransferable(std::string text) : text_{std::move(text)} {}

	[[nodiscard]] MessageHeader makeHeader(std::string_view senderName) const override {
		return MessageHeader{
			.type       = MessageHeader::Type::Clipboard,
			.name       = {},
			.size       = text_.size(),
			.senderName = std::string(senderName),
		};
	}

	[[nodiscard]] std::vector<uint8_t> payload() const override {
		return std::vector<uint8_t>(text_.begin(), text_.end());
	}

private:
	std::string text_;
};

} // namespace BetterSend
