#include <gtest/gtest.h>
#include "TransferProtocol.h"
#include "Constants.h"

#include <cstdint>
#include <stdexcept>
#include <string>

// ── test_protocol.cpp ─────────────────────────────────────────────────────────
// Unit tests for TransferProtocol.
// Pure logic; no I/O. All tests follow Arrange / Act / Assert.

using namespace BetterSend;

static MessageHeader makeFileHeader() {
	return {
		.type       = MessageHeader::Type::File,
		.name       = "photo.jpg",
		.size       = 1024,
		.senderName = "iPhone-Yoni",
	};
}

TEST(ProtocolTest, EncodeDecodeFileHeader_RoundTrip) {
	TransferProtocol proto;
	const auto original = makeFileHeader();

	const auto bytes   = proto.encodeHeader(original);
	const auto decoded = proto.decodeHeader(bytes);

	EXPECT_EQ(decoded.type,       original.type);
	EXPECT_EQ(decoded.name,       original.name);
	EXPECT_EQ(decoded.size,       original.size);
	EXPECT_EQ(decoded.senderName, original.senderName);
}

TEST(ProtocolTest, EncodeDecodeClipboardHeader_RoundTrip) {
	TransferProtocol proto;
	MessageHeader original{
		.type       = MessageHeader::Type::Clipboard,
		.name       = {},
		.size       = 42,
		.senderName = "Pixel-6",
	};

	const auto bytes   = proto.encodeHeader(original);
	const auto decoded = proto.decodeHeader(bytes);

	EXPECT_EQ(decoded.type,       MessageHeader::Type::Clipboard);
	EXPECT_EQ(decoded.name,       "");
	EXPECT_EQ(decoded.size,       42u);
	EXPECT_EQ(decoded.senderName, "Pixel-6");
}

TEST(ProtocolTest, LengthPrefixIsBigEndian) {
	TransferProtocol proto;
	const auto bytes = proto.encodeHeader(makeFileHeader());

	ASSERT_GE(bytes.size(), kHeaderLengthBytes);
	const uint32_t len =
		(static_cast<uint32_t>(bytes[0]) << 24) |
		(static_cast<uint32_t>(bytes[1]) << 16) |
		(static_cast<uint32_t>(bytes[2]) <<  8) |
		 static_cast<uint32_t>(bytes[3]);
	EXPECT_EQ(bytes.size(), kHeaderLengthBytes + len);
}

TEST(ProtocolTest, DecodeTooShort_Throws) {
	TransferProtocol proto;
	const std::vector<uint8_t> two{0xAB, 0xCD};
	EXPECT_THROW({ (void)proto.decodeHeader(two); }, std::runtime_error);
}

TEST(ProtocolTest, DecodeInvalidJson_Throws) {
	TransferProtocol proto;
	// length prefix = 5, then 5 bytes of invalid JSON ('{:::}')
	const std::vector<uint8_t> garbage{
		0x00, 0x00, 0x00, 0x05,
		'{', ':', ':', ':', '}'
	};
	EXPECT_THROW({ (void)proto.decodeHeader(garbage); }, std::runtime_error);
}

TEST(ProtocolTest, UnicodeFileName_SurvivesRoundTrip) {
	TransferProtocol proto;
	MessageHeader hdr{
		.type       = MessageHeader::Type::File,
		.name       = "תמונה🎉.png",
		.size       = 512,
		.senderName = "Test",
	};

	const auto decoded = proto.decodeHeader(proto.encodeHeader(hdr));
	EXPECT_EQ(decoded.name,       hdr.name);
	EXPECT_EQ(decoded.size,       hdr.size);
	EXPECT_EQ(decoded.senderName, hdr.senderName);
}
