// SPDX-License-Identifier: MS-PL
#include "../../../../src/Internal/OnlineQosMeasurement.hpp"
#include <gtest/gtest.h>
#include <limits>
using namespace CNA::Internal::Net;
namespace {
QosProbeId id() {
  QosProbeId value{};
  value[0] = 1;
  return value;
}
OnlineQosMeasurement make() { return OnlineQosMeasurement(id(), 7, 0); }
void pongs(OnlineQosMeasurement &m, int extraMs = 0, unsigned count = 4) {
  // AR: searcher-relay10 + relay-host20 + host-relay20 + relay-searcher10
  // =60ms. Remaining samples have +20, +40 and +20ms controlled host delay.
  const int times[]{60, 80, 100, 80};
  for (unsigned i = 0; i < count; ++i) {
    ASSERT_TRUE(m.sentPing(i, 42 + i, i * 100000));
    ASSERT_TRUE(
        m.pong(id(), 7, i, 42 + i, i * 100000 + (times[i] + extraMs) * 1000));
  }
}
void trains(OnlineQosMeasurement &m) {
  // AR: upstream 7*1200/.070=120000; downstream 7*1200/.140=60000 B/s.
  for (unsigned i = 0; i < 8; ++i)
    ASSERT_TRUE(m.downstream(i, 200000 + i * 20000));
  ASSERT_TRUE(m.upstreamReport(8, 70000, 350000));
}
void zero(const OnlineQosMeasurement &m) {
  const auto v = m.values();
  EXPECT_EQ(0, v.upstream);
  EXPECT_EQ(0, v.downstream);
  EXPECT_EQ(0, v.averageMs);
  EXPECT_EQ(0, v.minimumMs);
}
} // namespace
TEST(OnlineQosMeasurementTest, HostLegChangesRoundtrip) {
  auto baseline = make();
  pongs(baseline);
  trains(baseline);
  baseline.finish(400000);
  ASSERT_EQ(OnlineQosStatus::Available, baseline.status());
  EXPECT_EQ(80, baseline.values().averageMs);
  EXPECT_EQ(60, baseline.values().minimumMs);
  auto slower = make();
  pongs(slower, 30);
  trains(slower);
  slower.finish(450000);
  ASSERT_EQ(OnlineQosStatus::Available, slower.status());
  EXPECT_EQ(110, slower.values().averageMs);
  EXPECT_EQ(90, slower.values().minimumMs);
}
TEST(OnlineQosMeasurementTest, BothDirectionsUseReceiverTimes) {
  auto m = make();
  pongs(m);
  zero(m);
  m.finish(399999);
  EXPECT_EQ(OnlineQosStatus::Pending, m.status());
  trains(m);
  m.finish(400000);
  ASSERT_EQ(OnlineQosStatus::Available, m.status());
  EXPECT_EQ(120000, m.values().upstream);
  EXPECT_EQ(60000, m.values().downstream);
}
TEST(OnlineQosMeasurementTest, LossAndDuplicatesCountOnlyDeliveredBytes) {
  auto m = make();
  pongs(m);
  ASSERT_TRUE(m.downstream(0, 0));
  ASSERT_TRUE(m.downstream(1, 10000));
  EXPECT_FALSE(m.downstream(1, 11000));
  ASSERT_TRUE(m.downstream(3, 30000));
  EXPECT_FALSE(m.downstream(2, 40000));
  ASSERT_TRUE(m.downstream(7, 70000));
  ASSERT_TRUE(m.upstreamReport(4, 70000, 350000));
  m.finish(3000000);
  ASSERT_EQ(OnlineQosStatus::Available, m.status());
  // AR: three received frames beyond baseline, 3600/.070 => floor51428,
  // not120000.
  EXPECT_EQ(51428, m.values().downstream);
  EXPECT_EQ(51428, m.values().upstream);
}
TEST(OnlineQosMeasurementTest, PendingPartialAndExpiredAreNeverAvailable) {
  auto m = make();
  pongs(m, 0, 2);
  trains(m);
  m.finish(2999999);
  EXPECT_EQ(OnlineQosStatus::Pending, m.status());
  zero(m);
  m.finish(3000000);
  EXPECT_EQ(OnlineQosStatus::Failed, m.status());
  zero(m);
  EXPECT_FALSE(m.sentPing(2, 99, 3000000));
  EXPECT_FALSE(m.upstreamReport(8, 70000, 3000000));
  m.finish(4000000);
  zero(m);
  auto partial = make();
  pongs(partial);
  for (unsigned i = 0; i < 3; ++i)
    ASSERT_TRUE(partial.downstream(i, i * 10000));
  ASSERT_TRUE(partial.upstreamReport(8, 70000, 350000));
  partial.finish(3000000);
  EXPECT_EQ(OnlineQosStatus::Failed, partial.status());
  zero(partial);
}
TEST(OnlineQosMeasurementTest, ZeroSpanInvalidReportAndOverflow) {
  auto invalid = make();
  EXPECT_FALSE(invalid.upstreamReport(-1, 100, 350000));
  EXPECT_FALSE(invalid.upstreamReport(9, 100, 350000));
  EXPECT_FALSE(invalid.upstreamReport(8, 0, 350000));
  EXPECT_FALSE(invalid.upstreamReport(8, 3000001, 350000));
  EXPECT_FALSE(invalid.upstreamReport(1, 1, 350000));
  EXPECT_FALSE(invalid.upstreamReport(8, -1, 350000));
  auto zeroSpan = make();
  pongs(zeroSpan);
  for (unsigned i = 0; i < 8; ++i)
    ASSERT_TRUE(zeroSpan.downstream(i, 100));
  ASSERT_TRUE(zeroSpan.upstreamReport(8, 70000, 350000));
  zeroSpan.finish(3000000);
  EXPECT_EQ(OnlineQosStatus::Failed, zeroSpan.status());
  zero(zeroSpan);
  auto fast = make();
  pongs(fast);
  for (unsigned i = 0; i < 4; ++i)
    ASSERT_TRUE(fast.downstream(i, i == 3 ? 1 : 0));
  ASSERT_TRUE(fast.upstreamReport(4, 1, 350000));
  fast.finish(3000000);
  ASSERT_EQ(OnlineQosStatus::Available, fast.status());
  EXPECT_EQ(std::numeric_limits<int>::max(), fast.values().upstream);
  EXPECT_EQ(std::numeric_limits<int>::max(), fast.values().downstream);
}
TEST(OnlineQosMeasurementTest, NonceAndGenerationPreventLateCompletion) {
  auto m = make();
  auto wrong = id();
  wrong[0] = 2;
  EXPECT_FALSE(m.pong(id(), 7, 0, 42, 100));
  ASSERT_TRUE(m.sentPing(0, 42, 0));
  EXPECT_FALSE(m.pong(wrong, 7, 0, 42, 60000));
  EXPECT_FALSE(m.pong(id(), 8, 0, 42, 60000));
  EXPECT_FALSE(m.pong(id(), 7, 0, 43, 60000));
  ASSERT_TRUE(m.pong(id(), 7, 0, 42, 60000));
  EXPECT_FALSE(m.pong(id(), 7, 0, 42, 70000));
  m.cancel();
  EXPECT_FALSE(m.sentPing(1, 43, 100000));
  EXPECT_FALSE(m.upstreamReport(8, 70000, 350000));
  m.finish(3000000);
  EXPECT_EQ(OnlineQosStatus::Canceled, m.status());
  zero(m);
}
TEST(OnlineQosMeasurementTest, IntegerMillisecondsRoundOnlyAfterMean) {
  auto m = make();
  const int times[]{60000, 61000, 61000};
  for (unsigned i = 0; i < 3; ++i) {
    ASSERT_TRUE(m.sentPing(i, 42 + i, i * 100000));
    ASSERT_TRUE(m.pong(id(), 7, i, 42 + i, i * 100000 + times[i]));
  }
  trains(m);
  m.finish(3000000);
  ASSERT_EQ(OnlineQosStatus::Available, m.status());
  EXPECT_EQ(61, m.values().averageMs);
  EXPECT_EQ(60, m.values().minimumMs);
}

TEST(OnlineQosMeasurementTest, DeadlineRejectsLateEvidenceBeforeFinalization) {
  auto boundary = make();
  pongs(boundary, 0, 2);
  trains(boundary);
  ASSERT_TRUE(boundary.sentPing(2, 44, 200000));
  // CNA deadline policy is closed at now==3s, even before the timer callback
  // runs.
  EXPECT_FALSE(boundary.pong(id(), 7, 2, 44, 3000000));
  boundary.finish(3000000);
  EXPECT_EQ(OnlineQosStatus::Failed, boundary.status());
  zero(boundary);
  auto report = make();
  pongs(report);
  for (unsigned i = 0; i < 8; ++i)
    ASSERT_TRUE(report.downstream(i, 200000 + i * 20000));
  EXPECT_FALSE(report.upstreamReport(8, 70000, 3000000));
  report.finish(3000000);
  EXPECT_EQ(OnlineQosStatus::Failed, report.status());
  zero(report);
}
