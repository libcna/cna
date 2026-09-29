// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "../../../../src/Internal/TrafficRate.hpp"

using CNA::Internal::Net::TrafficRate;
using namespace std::chrono_literals;

// BytesPerSecondSent/Received come from the transport's cumulative totals: a rate needs a full
// second of samples, is the bytes moved since the last one divided by the time taken, and holds
// until the next second is complete.
TEST(TrafficRateTest, RatesAreBytesPerSecondOverEachCompletedSecond) {
    TrafficRate rate;
    const std::chrono::steady_clock::time_point start{};
    EXPECT_FALSE(rate.sample(start, 1000, 500));
    EXPECT_FALSE(rate.sample(start + 999ms, 9000, 9000));
    EXPECT_EQ(rate.sentPerSecond(), 0);
    ASSERT_TRUE(rate.sample(start + 2s, 5000, 2500));
    EXPECT_EQ(rate.sentPerSecond(), 2000);
    EXPECT_EQ(rate.receivedPerSecond(), 1000);
    EXPECT_FALSE(rate.sample(start + 2500ms, 6000, 2600));
    EXPECT_EQ(rate.sentPerSecond(), 2000);
    ASSERT_TRUE(rate.sample(start + 3s, 5000, 2500));
    EXPECT_EQ(rate.sentPerSecond(), 0);
    EXPECT_EQ(rate.receivedPerSecond(), 0);
}

TEST(TrafficRateTest, ATotalThatWrapsPastTwoToTheThirtySecondStaysCorrect) {
    TrafficRate rate;
    const std::chrono::steady_clock::time_point start{};
    (void)rate.sample(start, 0xFFFFFF00u, 0xFFFFFFF0u);
    ASSERT_TRUE(rate.sample(start + 1s, 0x100u, 0x10u));
    EXPECT_EQ(rate.sentPerSecond(), 0x200);
    EXPECT_EQ(rate.receivedPerSecond(), 0x20);
}
