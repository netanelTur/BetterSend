#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "IDiscovery.h"
#include <future>
#include <chrono>

// ── test_discovery.cpp ────────────────────────────────────────────────────────
// Tests for MdnsDiscovery.
//
// Note on real mDNS tests:
//   Full mDNS discovery requires two processes on the same network segment.
//   Unit tests here focus on lifecycle (no crash/deadlock) and self-discovery
//   on loopback where the OS mDNS stack supports it.
//
// MockDiscovery below is intended for tests in higher-level layers
// (e.g. testing the C API or Flutter bridge logic) that need a controlled
// discovery source without real network I/O.

using namespace BetterSend;
using namespace std::chrono_literals;

// ── MockDiscovery ─────────────────────────────────────────────────────────────
// Use in higher-level tests that depend on IDiscovery.
//
// class MockDiscovery : public IDiscovery {
// public:
//     MOCK_METHOD(void, startAdvertising, (const std::string&, int), (override));
//     MOCK_METHOD(void, startDiscovery,
//                 (std::function<void(Device)>), (override));
//     MOCK_METHOD(void, stop, (), (override));
//
//     // Drive the callback manually from test code:
//     void emitDevice(Device d) { capturedCallback_(std::move(d)); }
//
// private:
//     std::function<void(Device)> capturedCallback_;
// };

// ─────────────────────────────────────────────────────────────────────────────

TEST(DiscoveryTest, StartAndStop_NoCrashOrDeadlock) {
    // Arrange + Act + Assert (smoke test)
    // auto disc = std::make_unique<MdnsDiscovery>();
    // disc->startAdvertising("TestDevice", 9001);
    // std::this_thread::sleep_for(100ms);
    // disc->stop();  // must return, no deadlock
    GTEST_SKIP() << "MdnsDiscovery not implemented yet";
}

TEST(DiscoveryTest, SelfDiscovery_LoopbackFindsOwnDevice) {
    // Works reliably on macOS; Linux requires avahi-daemon.
    //
    // Arrange
    // auto disc = std::make_unique<MdnsDiscovery>();
    // std::promise<Device> promise;
    // auto future = promise.get_future();
    //
    // disc->startAdvertising("LoopbackDevice", 9002);
    // disc->startDiscovery([&promise](Device d) {
    //     if (d.name == "LoopbackDevice") promise.set_value(d);
    // });
    //
    // Assert
    // ASSERT_EQ(future.wait_for(3s), std::future_status::ready);
    // EXPECT_EQ(future.get().port, 9002);
    GTEST_SKIP() << "MdnsDiscovery not implemented yet";
}

TEST(DiscoveryTest, StopBeforeStart_NoCrash) {
    // Calling stop() on a never-started discovery must be safe.
    // auto disc = std::make_unique<MdnsDiscovery>();
    // EXPECT_NO_THROW(disc->stop());
    GTEST_SKIP() << "MdnsDiscovery not implemented yet";
}
