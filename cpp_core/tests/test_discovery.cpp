#include <gtest/gtest.h>

#include "IDiscovery.h"
#include "MdnsDiscovery.h"

#include <chrono>
#include <memory>
#include <thread>

// ── test_discovery.cpp ────────────────────────────────────────────────────────
// Smoke tests for the platform-selected discovery impl exposed via
// makeDiscovery() (Constants.h). Phase 1 platforms wire that factory to
// BleDiscovery_{Mac,Windows}; the dev fallback (other platforms) returns
// MdnsDiscovery. We only test lifecycle here — peer discovery itself
// requires two devices and is exercised by the end-to-end manual test.

using namespace BetterSend;
using namespace std::chrono_literals;

TEST(DiscoveryTest, StartAndStop_NoCrashOrDeadlock) {
	auto disc = makeDiscovery();
	ASSERT_NE(disc, nullptr);
	disc->startAdvertising("TestDevice", 9001);
	std::this_thread::sleep_for(100ms);
	disc->stop();
}

TEST(DiscoveryTest, StopBeforeStart_NoCrash) {
	auto disc = makeDiscovery();
	ASSERT_NE(disc, nullptr);
	EXPECT_NO_THROW(disc->stop());
}
