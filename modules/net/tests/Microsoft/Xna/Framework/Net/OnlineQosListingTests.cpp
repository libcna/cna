// SPDX-License-Identifier: MS-PL
#include "../../../../../src/Internal/OnlineQosResult.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSessionCollection.hpp"
#include <gtest/gtest.h>
#include <utility>
using namespace Microsoft::Xna::Framework::Net;
using CNA::Internal::Net::OnlineQosResult;
namespace {
AvailableNetworkSession listing() {
  return AvailableNetworkSession::CreateInternal(
      2, "Host", 0, 6, {}, QualityOfService::CreateInternal(), "", 0,
      NetworkSessionType::PlayerMatch);
}
void empty(const QualityOfService &value) {
  // Refresh QualityOfService::.ctor() IL_0000-0006: CLR zero fields.
  EXPECT_FALSE(value.getIsAvailableProperty());
  EXPECT_EQ(0, value.getBytesPerSecondUpstreamProperty());
  EXPECT_EQ(0, value.getBytesPerSecondDownstreamProperty());
  EXPECT_EQ(System::TimeSpan::Zero, value.getAverageRoundtripTimeProperty());
  EXPECT_EQ(System::TimeSpan::Zero, value.getMinimumRoundtripTimeProperty());
}
void measured(const QualityOfService &value) {
  // Refresh measured ctor IL_0006-0025; hand-derived train/RTT fixture in
  // GAP-17 design.
  EXPECT_TRUE(value.getIsAvailableProperty());
  EXPECT_EQ(120000, value.getBytesPerSecondUpstreamProperty());
  EXPECT_EQ(60000, value.getBytesPerSecondDownstreamProperty());
  EXPECT_EQ(System::TimeSpan::FromMilliseconds(80),
            value.getAverageRoundtripTimeProperty());
  EXPECT_EQ(System::TimeSpan::FromMilliseconds(60),
            value.getMinimumRoundtripTimeProperty());
}
} // namespace
TEST(OnlineQosListingTest, PollReplacesSnapshotAndRetainsOldReference) {
  auto item = listing();
  auto state = std::make_shared<OnlineQosResult>();
  OnlineQosResult::attach(item, state);
  auto found = AvailableNetworkSessionCollection::CreateInternal({item});
  const auto &result = std::as_const(found)[0];
  const auto &old = result.getQualityOfServiceProperty();
  empty(old);
  EXPECT_TRUE(state->complete(120000, 60000, 80, 60));
  const auto &current = result.getQualityOfServiceProperty();
  measured(current);
  // Refresh GetQualityOfService IL_00cb-00d0 replaces the object, never mutates
  // the old one.
  EXPECT_NE(&old, &current);
  empty(old);
  EXPECT_EQ(&current, &result.getQualityOfServiceProperty());
}
TEST(OnlineQosListingTest, DisposeCancelsCopiesAndRejectsLatePublication) {
  auto stopped = std::make_shared<int>(0);
  auto state = std::make_shared<OnlineQosResult>([stopped] { ++*stopped; });
  auto item = listing();
  OnlineQosResult::attach(item, state);
  auto found = AvailableNetworkSessionCollection::CreateInternal({item});
  auto copy = std::as_const(found)[0];
  found.Dispose();
  found.Dispose();
  EXPECT_EQ(1, *stopped);
  EXPECT_FALSE(state->complete(120000, 60000, 80, 60));
  empty(copy.getQualityOfServiceProperty());
  empty(std::as_const(found)[0].getQualityOfServiceProperty());
}
TEST(OnlineQosListingTest, ReadyButUnpolledIsNotAdoptedAfterDispose) {
  auto state = std::make_shared<OnlineQosResult>();
  auto item = listing();
  OnlineQosResult::attach(item, state);
  auto found = AvailableNetworkSessionCollection::CreateInternal({item});
  ASSERT_TRUE(state->complete(120000, 60000, 80, 60));
  found.Dispose();
  empty(std::as_const(found)[0].getQualityOfServiceProperty());
}
TEST(OnlineQosListingTest,
     PublishedSnapshotSurvivesDisposeAndRepeatedCompletion) {
  auto state = std::make_shared<OnlineQosResult>();
  auto item = listing();
  OnlineQosResult::attach(item, state);
  auto found = AvailableNetworkSessionCollection::CreateInternal({item});
  ASSERT_TRUE(state->complete(120000, 60000, 80, 60));
  const auto &snapshot = std::as_const(found)[0].getQualityOfServiceProperty();
  measured(snapshot);
  EXPECT_FALSE(state->complete(1, 2, 99, 90));
  found.Dispose();
  measured(snapshot);
  measured(std::as_const(found)[0].getQualityOfServiceProperty());
}
TEST(OnlineQosListingTest, DefaultAndSystemLinkRemainStatic) {
  auto pending = listing();
  empty(pending.getQualityOfServiceProperty());
  auto lan = AvailableNetworkSession::CreateInternal(
      1, "LAN", 0, 7, {},
      QualityOfService::CreateInternal(System::TimeSpan::FromMilliseconds(42)));
  auto found = AvailableNetworkSessionCollection::CreateInternal({lan});
  found.Dispose();
  const auto &qos = std::as_const(found)[0].getQualityOfServiceProperty();
  EXPECT_TRUE(qos.getIsAvailableProperty());
  EXPECT_EQ(System::TimeSpan::FromMilliseconds(42),
            qos.getMinimumRoundtripTimeProperty());
  EXPECT_EQ(System::TimeSpan::FromMilliseconds(42),
            qos.getAverageRoundtripTimeProperty());
  EXPECT_EQ(0, qos.getBytesPerSecondUpstreamProperty());
  EXPECT_EQ(0, qos.getBytesPerSecondDownstreamProperty());
}
TEST(OnlineQosListingTest, LastResultLifetimeCancelsWithoutWorkerRetention) {
  auto stopped = std::make_shared<int>(0);
  std::weak_ptr<OnlineQosResult> weak;
  {
    auto state = std::make_shared<OnlineQosResult>([stopped] { ++*stopped; });
    weak = state;
    auto item = listing();
    OnlineQosResult::attach(item, state);
    state.reset();
    EXPECT_FALSE(weak.expired());
    EXPECT_EQ(0, *stopped);
  }
  EXPECT_TRUE(weak.expired());
  EXPECT_EQ(1, *stopped);
}
