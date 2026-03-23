#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "ITransport.h"
#include "IProtocol.h"
#include "Constants.h"
#include <future>
#include <chrono>

// ── test_transport.cpp ────────────────────────────────────────────────────────
// Unit + integration tests for TcpTransport.
//
// Strategy:
//   - MockProtocol isolates TcpTransport from JSON encoding.
//   - Localhost round-trip tests verify the full send → receive flow.
//
// For round-trip tests use std::promise/future to synchronize across threads
// without sleeping. Timeout: 3 seconds per test.

using namespace BetterSend;
using namespace std::chrono_literals;

// ── MockProtocol ──────────────────────────────────────────────────────────────
// Uncomment and fill in when gmock is available in the build.
//
// class MockProtocol : public IProtocol {
// public:
//     MOCK_METHOD(std::vector<uint8_t>, encodeHeader, (const MessageHeader&), (override));
//     MOCK_METHOD(MessageHeader, decodeHeader, (std::span<const uint8_t>), (override));
// };

// ─────────────────────────────────────────────────────────────────────────────

TEST(TransportTest, SendAndReceiveClipboard_LocalhostRoundTrip) {
    // Arrange
    // auto proto     = std::make_shared<TransferProtocol>();
    // auto transport = std::make_unique<TcpTransport>(proto, "TestDevice");
    //
    // std::promise<Transfer> promise;
    // auto future = promise.get_future();
    //
    // constexpr int kTestPort = 19001;
    // transport->startServer(kTestPort, [&promise](Transfer t) {
    //     promise.set_value(std::move(t));
    // });
    //
    // Act
    // transport->sendClipboard("127.0.0.1", kTestPort, "hello world");
    //
    // Assert
    // ASSERT_EQ(future.wait_for(3s), std::future_status::ready);
    // auto received = future.get();
    // EXPECT_EQ(received.type, Transfer::Type::Clipboard);
    // EXPECT_EQ(received.data, "hello world");
    GTEST_SKIP() << "TcpTransport not implemented yet";
}

TEST(TransportTest, SendAndReceiveFile_LocalhostRoundTrip) {
    // Write a temp file with known content, send it, verify data arrives intact.
    // Use std::filesystem::temp_directory_path() for the temp file path.
    //
    // ASSERT checksum/content equality after receive.
    GTEST_SKIP() << "TcpTransport not implemented yet";
}

TEST(TransportTest, MultipleSequentialTransfers_AllArrive) {
    // Send 3 clipboard messages sequentially.
    // Verify the onReceive callback fires exactly 3 times with correct data.
    GTEST_SKIP() << "TcpTransport not implemented yet";
}

TEST(TransportTest, StopServer_NoCrashAfterStop) {
    // Start server, call stop(), then attempt a sendClipboard.
    // Must not crash and must not invoke onReceive.
    GTEST_SKIP() << "TcpTransport not implemented yet";
}
