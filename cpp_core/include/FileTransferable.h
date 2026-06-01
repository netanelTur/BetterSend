#pragma once
#include "ITransferable.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace BetterSend {

// ── FileTransferable ──────────────────────────────────────────────────────────
// ITransferable implementation for binary files.
//
// Wire type: MessageHeader::Type::File
// Payload  : raw file bytes (read lazily in payload())
//
// Note: payload() reads the entire file into memory.
// For large files, TcpTransport should stream in chunks instead of calling
// payload() — this impl is correct for the protocol contract but not
// memory-optimal for multi-GB files.

class FileTransferable : public ITransferable {
public:
	explicit FileTransferable(std::filesystem::path path) : path_{std::move(path)} {}

	[[nodiscard]] MessageHeader makeHeader(std::string_view senderName) const override {
		return MessageHeader{
			.type       = MessageHeader::Type::File,
			.name       = path_.filename().string(),
			.size       = std::filesystem::file_size(path_),
			.senderName = std::string(senderName),
		};
	}

	[[nodiscard]] std::vector<uint8_t> payload() const override {
		std::ifstream f(path_, std::ios::binary);
		if (!f) throw std::runtime_error("Cannot open file: " + path_.string());
		return std::vector<uint8_t>(
			std::istreambuf_iterator<char>(f),
			std::istreambuf_iterator<char>()
		);
	}

private:
	std::filesystem::path path_;
};

} // namespace BetterSend
