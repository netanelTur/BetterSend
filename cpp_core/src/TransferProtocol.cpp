#include "TransferProtocol.h"
#include "Logger.h"
#include "Constants.h"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

// ── TransferProtocol ──────────────────────────────────────────────────────────
// Wire format (per IProtocol.h):
//   [ 4 bytes big-endian uint32 N: JSON length ]
//   [ N bytes UTF-8 JSON                       ]
//   [ M bytes payload — streamed by ITransport ]
//
// JSON schema:
//   { "type": "file"|"clipboard", "name": "...", "size": N, "sender": "..." }

namespace BetterSend {

namespace {

constexpr std::string_view kComponent     = "Protocol";
constexpr std::string_view kTypeFile      = "file";
constexpr std::string_view kTypeClipboard = "clipboard";

void writeBigEndianU32(uint8_t* out, uint32_t v) {
	out[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
	out[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
	out[2] = static_cast<uint8_t>((v >>  8) & 0xFF);
	out[3] = static_cast<uint8_t>( v        & 0xFF);
}

uint32_t readBigEndianU32(const uint8_t* in) {
	return (static_cast<uint32_t>(in[0]) << 24)
	     | (static_cast<uint32_t>(in[1]) << 16)
	     | (static_cast<uint32_t>(in[2]) <<  8)
	     |  static_cast<uint32_t>(in[3]);
}

} // namespace

std::vector<uint8_t> TransferProtocol::encodeHeader(const MessageHeader& header) {
	nlohmann::json j;
	j["type"]   = (header.type == MessageHeader::Type::File)
	                  ? std::string(kTypeFile)
	                  : std::string(kTypeClipboard);
	j["name"]   = header.name;
	j["size"]   = header.size;
	j["sender"] = header.senderName;

	const std::string body = j.dump();
	if (body.size() > kMaxHeaderSizeBytes) {
		BS_LOG_ERROR(kComponent, "encodeHeader: JSON too large ({} bytes)", body.size());
		throw std::runtime_error("Header JSON exceeds kMaxHeaderSizeBytes");
	}

	std::vector<uint8_t> out(kHeaderLengthBytes + body.size());
	writeBigEndianU32(out.data(), static_cast<uint32_t>(body.size()));
	std::copy(body.begin(), body.end(), out.begin() + kHeaderLengthBytes);

	BS_LOG_DEBUG(kComponent, "encodeHeader: type={} name='{}' size={} -> {} bytes",
		j["type"].get<std::string>(), header.name, header.size, out.size());
	return out;
}

MessageHeader TransferProtocol::decodeHeader(std::span<const uint8_t> data) {
	if (data.size() < kHeaderLengthBytes) {
		BS_LOG_ERROR(kComponent, "decodeHeader: prefix too short ({} bytes)", data.size());
		throw std::runtime_error("Header data too short");
	}

	const uint32_t jsonLen = readBigEndianU32(data.data());
	if (jsonLen == 0 || jsonLen > kMaxHeaderSizeBytes) {
		BS_LOG_ERROR(kComponent, "decodeHeader: invalid jsonLen={}", jsonLen);
		throw std::runtime_error("Header length out of range");
	}
	if (data.size() < kHeaderLengthBytes + jsonLen) {
		BS_LOG_ERROR(kComponent, "decodeHeader: truncated, need {} have {}",
			kHeaderLengthBytes + jsonLen, data.size());
		throw std::runtime_error("Incomplete header");
	}

	const std::string body(
		reinterpret_cast<const char*>(data.data() + kHeaderLengthBytes), jsonLen);

	nlohmann::json j;
	try {
		j = nlohmann::json::parse(body);
	} catch (const nlohmann::json::parse_error& e) {
		BS_LOG_ERROR(kComponent, "decodeHeader: invalid JSON: {}", e.what());
		throw std::runtime_error(std::string("Invalid header JSON: ") + e.what());
	}

	MessageHeader hdr;
	const std::string typeStr = j.value("type", std::string{});
	hdr.type = (typeStr == kTypeFile)
	               ? MessageHeader::Type::File
	               : MessageHeader::Type::Clipboard;
	hdr.name       = j.value("name",   std::string{});
	hdr.size       = j.value("size",   std::size_t{0});
	hdr.senderName = j.value("sender", std::string{});

	BS_LOG_DEBUG(kComponent, "decodeHeader: type={} name='{}' size={} sender='{}'",
		typeStr, hdr.name, hdr.size, hdr.senderName);
	return hdr;
}

} // namespace BetterSend
