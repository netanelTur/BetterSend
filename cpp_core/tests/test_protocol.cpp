#include <gtest/gtest.h>
#include "IProtocol.h"
#include "Constants.h"

// ── test_protocol.cpp ─────────────────────────────────────────────────────────
// Unit tests for TransferProtocol.
// No mocks needed — TransferProtocol is pure logic with no I/O.
//
// How to wire up the implementation when ready:
//   Option A: include the .cpp directly (fast for small files):
//     #include "../src/TransferProtocol.cpp"
//   Option B: link bettersend_core in CMakeLists and use the class normally.
//
// All tests follow Arrange / Act / Assert structure.

using namespace BetterSend;

// Helper: build a default file header for reuse across tests
static MessageHeader makeFileHeader() {
	return {
		.type       = MessageHeader::Type::File,
		.name       = "photo.jpg",
		.size       = 1024,
		.senderName = "iPhone-Yoni",
	};
}

// ─────────────────────────────────────────────────────────────────────────────

TEST(ProtocolTest, EncodeDecodeFileHeader_RoundTrip) {
	// Arrange
	// TODO: auto proto = std::make_unique<TransferProtocol>();
	// auto original = makeFileHeader();

	// Act
	// auto bytes   = proto->encodeHeader(original);
	// auto decoded = proto->decodeHeader(bytes);

	// Assert
	// EXPECT_EQ(decoded.type,       original.type);
	// EXPECT_EQ(decoded.name,       original.name);
	// EXPECT_EQ(decoded.size,       original.size);
	// EXPECT_EQ(decoded.senderName, original.senderName);
	GTEST_SKIP() << "TransferProtocol not implemented yet";
}

TEST(ProtocolTest, EncodeDecodeClipboardHeader_RoundTrip) {
	// Arrange
	// MessageHeader original{MessageHeader::Type::Clipboard, "", 42, "Pixel-6"};

	// Act + Assert: same round-trip check as above
	GTEST_SKIP() << "TransferProtocol not implemented yet";
}

TEST(ProtocolTest, LengthPrefixIsBigEndian) {
	// The first 4 bytes of the encoded output must be a big-endian uint32
	// representing the JSON length.
	//
	// Arrange + Act
	// auto bytes = proto->encodeHeader(makeFileHeader());
	//
	// Assert
	// uint32_t len = (bytes[0]<<24)|(bytes[1]<<16)|(bytes[2]<<8)|bytes[3];
	// EXPECT_EQ(bytes.size(), kHeaderLengthBytes + len);
	GTEST_SKIP() << "TransferProtocol not implemented yet";
}

TEST(ProtocolTest, DecodeInvalidData_ThrowsRuntimeError) {
	// Passing garbage bytes must throw, not crash.
	//
	// EXPECT_THROW(proto->decodeHeader(std::span<const uint8_t>{}),
	//              std::runtime_error);
	GTEST_SKIP() << "TransferProtocol not implemented yet";
}

TEST(ProtocolTest, UnicodeFileName_SurvivesRoundTrip) {
	// Filenames with UTF-8 (Hebrew, emoji, etc.) must survive encode→decode.
	//
	// MessageHeader hdr{MessageHeader::Type::File, "תמונה🎉.png", 512, "Test"};
	// auto decoded = proto->decodeHeader(proto->encodeHeader(hdr));
	// EXPECT_EQ(decoded.name, hdr.name);
	GTEST_SKIP() << "TransferProtocol not implemented yet";
}
