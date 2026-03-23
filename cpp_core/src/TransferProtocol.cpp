#include "IProtocol.h"
#include "Logger.h"
#include "Constants.h"
#include <stdexcept>
#include <bit>
// After first CMake run, nlohmann/json.hpp is available:
// #include <nlohmann/json.hpp>

// ── TransferProtocol ──────────────────────────────────────────────────────────
// Concrete IProtocol — encodes/decodes message headers using JSON.
//
// Wire format (see IProtocol.h for full spec):
//   [4 bytes big-endian uint32: JSON length N] [N bytes: UTF-8 JSON]
//
// Big-endian helpers (C++20 std::byteswap available on most compilers):
//   Write: store bytes [len>>24, len>>16, len>>8, len] into the prefix
//   Read:  reconstruct as (b0<<24)|(b1<<16)|(b2<<8)|b3
//
// JSON field names:
//   "type"   → "file" | "clipboard"
//   "name"   → filename string
//   "size"   → payload size uint64
//   "sender" → sender device name

namespace BetterSend {

static constexpr std::string_view kComponent   = "Protocol";
static constexpr std::string_view kTypeFile      = "file";
static constexpr std::string_view kTypeClipboard = "clipboard";

class TransferProtocol : public IProtocol {
public:
    [[nodiscard]] std::vector<uint8_t> encodeHeader(
        const MessageHeader& header) override
    {
        // TODO: using nlohmann::json:
        //   nlohmann::json j;
        //   j["type"]   = (header.type == MessageHeader::Type::File)
        //                     ? kTypeFile : kTypeClipboard;
        //   j["name"]   = header.name;
        //   j["size"]   = header.size;
        //   j["sender"] = header.senderName;
        //   std::string jsonStr = j.dump();
        //
        //   Build output: 4-byte big-endian length + json bytes
        //   auto len = static_cast<uint32_t>(jsonStr.size());
        //   std::vector<uint8_t> out(kHeaderLengthBytes + jsonStr.size());
        //   out[0] = (len >> 24) & 0xFF;  out[1] = (len >> 16) & 0xFF;
        //   out[2] = (len >>  8) & 0xFF;  out[3] =  len        & 0xFF;
        //   std::ranges::copy(jsonStr, out.begin() + kHeaderLengthBytes);
        //   return out;
        BS_LOG_DEBUG(kComponent, "encodeHeader: type={} name={} size={}",
            header.type == MessageHeader::Type::File ? "file" : "clipboard",
            header.name, header.size);
        return {}; // TODO: replace with real implementation
    }

    [[nodiscard]] MessageHeader decodeHeader(
        std::span<const uint8_t> data) override
    {
        if (data.size() < kHeaderLengthBytes) {
            BS_LOG_ERROR(kComponent, "decodeHeader: data too short ({} bytes)", data.size());
            throw std::runtime_error("Header data too short");
        }

        // TODO:
        //   uint32_t jsonLen = (data[0]<<24)|(data[1]<<16)|(data[2]<<8)|data[3];
        //   if (jsonLen > kMaxHeaderSizeBytes) throw std::runtime_error("Header too large");
        //   if (data.size() < kHeaderLengthBytes + jsonLen)
        //       throw std::runtime_error("Incomplete header");
        //
        //   auto jsonStr = std::string(
        //       reinterpret_cast<const char*>(data.data() + kHeaderLengthBytes), jsonLen);
        //   auto j = nlohmann::json::parse(jsonStr);
        //
        //   MessageHeader hdr;
        //   hdr.type = (j["type"] == kTypeFile)
        //                  ? MessageHeader::Type::File
        //                  : MessageHeader::Type::Clipboard;
        //   hdr.name       = j["name"];
        //   hdr.size       = j["size"];
        //   hdr.senderName = j["sender"];
        //   return hdr;

        BS_LOG_DEBUG(kComponent, "decodeHeader called ({} bytes)", data.size());
        return {}; // TODO: replace with real implementation
    }
};

} // namespace BetterSend
