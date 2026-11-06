// SPDX-License-Identifier: MS-PL
#include "../../../../../gamer-services/src/Internal/Protocol/CnaService/QosProtocol.hpp"
#include "../../../../src/Internal/OnlineQosHost.hpp"
#include "../../../../src/Internal/OnlineQosProbe.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <vector>
using namespace CNA::Internal::Net;
namespace {
// Simulated links carry actual CNQ bytes through the production probe and host
// cores. The service leg forwards only. Native Service/QosHub authorization is
// tested in the server.
class Path {
public:
  std::int64_t now = 0, hostExtra = 0, lastUp = -1000000, lastDown = -1000000;
  bool hostLive = true, loss = false, jitter = false;
  int hostReceipts = 0, relayReceipts = 0;
  std::multimap<std::int64_t, std::function<void()>> events;
  std::unique_ptr<OnlineQosProbe> probe;
  std::unique_ptr<OnlineQosHost> host;
  Path() {
    QosProbeId id{};
    id[0] = 1;
    probe = std::make_unique<OnlineQosProbe>(
        id, 7, [this] { return now; },
        [this](auto bytes) { fromClient(std::move(bytes)); });
    host = std::make_unique<OnlineQosHost>(
        id, [this] { return now; },
        [this](auto bytes) { fromHost(std::move(bytes)); });
  }
  void fromClient(std::vector<unsigned char> bytes) {
    const auto view = CnaService::parseQosFrame(bytes);
    const auto kind = view.type;
    const auto index = view.sequence;
    if (kind == CnaService::QosType::Ping)
      probe->sent(bytes, now);
    events.emplace(now + 10000,
                   [this] { ++relayReceipts; }); // searcher -> service10ms
    if (!hostLive)
      return;
    auto arrival =
        now + 30000 + hostExtra; // service -> host20ms (+controlled delay)
    if (kind == CnaService::QosType::Up) {
      arrival = std::max(arrival, lastUp + 10000);
      lastUp = arrival;
    }
    if (loss && kind == CnaService::QosType::Up && index != 0 && index != 1 &&
        index != 3 && index != 7)
      return;
    if (kind == CnaService::QosType::Cancel)
      return; // consumed by the service, never game/host data
    events.emplace(arrival, [this, bytes = std::move(bytes)] {
      ++hostReceipts;
      host->receive(bytes, now);
    });
  }
  void fromHost(std::vector<unsigned char> bytes) {
    const auto view = CnaService::parseQosFrame(bytes);
    const auto kind = view.type;
    const auto index = view.sequence;
    auto arrival =
        now + 30000 + hostExtra; // host -> service20ms -> searcher10ms
    if (kind == CnaService::QosType::Down) {
      arrival = std::max(arrival, lastDown + 20000);
      lastDown = arrival;
    }
    if (loss && kind == CnaService::QosType::Down && index != 0 && index != 1 &&
        index != 3 && index != 7)
      return;
    if (jitter && kind == CnaService::QosType::Pong) {
      const int delays[]{0, 20000, 40000, 20000};
      arrival += delays[index];
    }
    events.emplace(arrival, [this, bytes = std::move(bytes)] {
      probe->receive(bytes, now);
    });
  }
  void run(std::int64_t until) {
    // Begin originates at service, reaches host after20ms. No host => no
    // Begin/echo/train.
    if (now == 0) {
      probe->start();
      if (hostLive)
        events.emplace(20000 + hostExtra, [this] { host->begin(now); });
    }
    for (; now <= until; now += 1000) {
      while (!events.empty() && events.begin()->first <= now) {
        auto work = std::move(events.begin()->second);
        events.erase(events.begin());
        work();
      }
      probe->tick();
      if (hostLive)
        host->tick();
    }
  }
};
} // namespace
TEST(OnlineQosPathTest, FullPathIncludesBothHostLegsAndAsymmetricRates) {
  Path path;
  path.jitter = true;
  path.run(600000);
  // AR: RTT60/80/100/80ms => mean80/min60; delivered train spans70/140ms.
  ASSERT_EQ(OnlineQosStatus::Available, path.probe->status());
  EXPECT_EQ(80, path.probe->values().averageMs);
  EXPECT_EQ(60, path.probe->values().minimumMs);
  EXPECT_EQ(120000, path.probe->values().upstream);
  EXPECT_EQ(60000, path.probe->values().downstream);
  EXPECT_GT(path.hostReceipts, 0);
  EXPECT_GT(path.relayReceipts, 0);
  Path slow;
  slow.hostExtra = 15000;
  slow.run(600000);
  ASSERT_EQ(OnlineQosStatus::Available, slow.probe->status());
  // AR:10+35+35+10=90ms, rather than client-to-service20ms.
  EXPECT_EQ(90, slow.probe->values().averageMs);
  EXPECT_EQ(90, slow.probe->values().minimumMs);
}
TEST(OnlineQosPathTest, HealthyRelayWithoutHostNeverPublishes) {
  Path path;
  path.hostLive = false;
  path.run(3000000);
  EXPECT_GT(path.relayReceipts, 0);
  EXPECT_EQ(0, path.hostReceipts);
  EXPECT_EQ(OnlineQosStatus::Failed, path.probe->status());
  const auto value = path.probe->values();
  EXPECT_EQ(0, value.upstream);
  EXPECT_EQ(0, value.downstream);
  EXPECT_EQ(0, value.averageMs);
  EXPECT_EQ(0, value.minimumMs);
}
TEST(OnlineQosPathTest,
     ControlledLossCountsReceivedBytesAndCancelDropsLateTraffic) {
  Path lossy;
  lossy.loss = true;
  lossy.run(3000000);
  ASSERT_EQ(OnlineQosStatus::Available, lossy.probe->status());
  // AR: only sequences0,1,3,7 delivered, but dropped frames still occupy the
  // link: count4 spans70/140ms; floor(3600/.070)=51428, floor(3600/.140)=25714.
  EXPECT_EQ(51428, lossy.probe->values().upstream);
  EXPECT_EQ(25714, lossy.probe->values().downstream);
  Path canceled;
  canceled.run(40000);
  canceled.probe->cancel();
  canceled.run(3000000);
  EXPECT_EQ(OnlineQosStatus::Canceled, canceled.probe->status());
  EXPECT_EQ(0, canceled.probe->values().upstream);
  EXPECT_EQ(0, canceled.probe->values().downstream);
}
