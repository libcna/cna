// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "../../../../src/Internal/NetworkSimulator.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <chrono>
#include <cmath>
#include <deque>
#include <limits>
#include <vector>

using CNA::Internal::Net::NetworkSimulator;
using Microsoft::Xna::Framework::Net::SendDataOptions;
using namespace std::chrono_literals;

// XNA 4.0's NetworkSimulator, read from Microsoft.Xna.Framework.Net.dll: a packet without Reliable
// is lost when NextDouble() <= SimulatedPacketLoss; a held packet waits mean + z * mean / 4, with
// z = sqrt(-2 ln u1) cos(2 pi u2) and the result clamped to [0, 2 * mean].
namespace {
    // Feeds each simulator draw from a list and counts them; u1 = e^-2 makes |z| = 2, so u2 = 0
    // holds a packet 1.5 * mean and u2 = 0.5 holds it 0.5 * mean.
    struct ScriptedSimulator {
        NetworkSimulator simulator;
        NetworkSimulator::Clock::time_point start{};
        std::deque<double> draws;
        int drawn = 0;

        ScriptedSimulator() {
            NetworkSimulator::SetClockForTesting(start);
            NetworkSimulator::SetRandomForTesting([this] {
                ++drawn;
                EXPECT_FALSE(draws.empty()) << "the simulator drew more random numbers than scripted";
                if (draws.empty()) return 0.25;
                const double value = draws.front();
                draws.pop_front();
                return value;
            });
        }
        ~ScriptedSimulator() {
            NetworkSimulator::ResetClockForTesting();
            NetworkSimulator::ResetRandomForTesting();
        }

        bool Send(SharpRuntime::bytecs value, SendDataOptions options) {
            std::vector<SharpRuntime::bytecs> data{value};
            return simulator.SendData(data, options, nullptr, nullptr);
        }

        std::vector<SharpRuntime::bytecs> DueAt(NetworkSimulator::Clock::duration elapsed) {
            NetworkSimulator::SetClockForTesting(start + elapsed);
            std::vector<SharpRuntime::bytecs> values;
            for (const auto& packet : simulator.TakeDuePackets()) values.push_back(packet.Data.at(0));
            return values;
        }
    };

    const double kUnitDeviate = std::exp(-2.0);
    using Bytes = std::vector<SharpRuntime::bytecs>;
}

TEST(NetworkSimulatorTest, SettersRefuseValuesXnaRefuses) {
    NetworkSimulator simulator;
    EXPECT_THROW(simulator.setSimulatedPacketLoss(-0.01f), System::ArgumentOutOfRangeException);
    EXPECT_THROW(simulator.setSimulatedPacketLoss(1.01f), System::ArgumentOutOfRangeException);
    EXPECT_THROW(simulator.setSimulatedLatency(System::TimeSpan::FromTicks(-1)),
                 System::ArgumentOutOfRangeException);
    simulator.setSimulatedPacketLoss(1.0f);
    EXPECT_EQ(simulator.getSimulatedPacketLoss(), 1.0f);
    simulator.setSimulatedPacketLoss(0.0f);
    EXPECT_EQ(simulator.getSimulatedPacketLoss(), 0.0f);
    // XNA's own comparisons let NaN through.
    simulator.setSimulatedPacketLoss(std::numeric_limits<float>::quiet_NaN());
    EXPECT_TRUE(std::isnan(simulator.getSimulatedPacketLoss()));
    simulator.setSimulatedLatency(System::TimeSpan::Zero);
    EXPECT_EQ(simulator.getSimulatedLatency(), System::TimeSpan::Zero);
}

TEST(NetworkSimulatorTest, LossDropsWhenTheDrawIsAtMostTheLossAndNeverTouchesReliable) {
    ScriptedSimulator s;
    s.simulator.setSimulatedPacketLoss(0.5f);
    s.draws = {0.5, 0.75};
    EXPECT_TRUE(s.Send(1, SendDataOptions::None)) << "a draw equal to the loss drops the packet";
    EXPECT_FALSE(s.Send(2, SendDataOptions::InOrder)) << "a larger draw lets it through";
    for (const auto options : {SendDataOptions::Reliable, SendDataOptions::ReliableInOrder,
                               SendDataOptions::Chat | SendDataOptions::Reliable}) {
        EXPECT_FALSE(s.Send(3, options));
    }
    EXPECT_EQ(s.drawn, 2) << "Reliable packets never draw";

    s.simulator.setSimulatedPacketLoss(0.0f);
    EXPECT_FALSE(s.Send(4, SendDataOptions::None));
    EXPECT_EQ(s.drawn, 2) << "no loss, no draw";
}

TEST(NetworkSimulatorTest, WithoutLatencyAPacketIsSentAtOnceAndKeptByTheCaller) {
    ScriptedSimulator s;
    std::vector<SharpRuntime::bytecs> data{9, 8};
    EXPECT_FALSE(s.simulator.SendData(data, SendDataOptions::Reliable, nullptr, nullptr));
    EXPECT_EQ(data, (Bytes{9, 8}));
    EXPECT_TRUE(s.DueAt(0s).empty());
}

TEST(NetworkSimulatorTest, UnorderedPacketsLeaveByDueTimeAndCanOvertake) {
    ScriptedSimulator s;
    s.simulator.setSimulatedLatency(System::TimeSpan::FromMilliseconds(100));
    s.draws = {kUnitDeviate, 0.0, kUnitDeviate, 0.5};
    EXPECT_TRUE(s.Send(1, SendDataOptions::Reliable)); // held 150 ms
    EXPECT_TRUE(s.Send(2, SendDataOptions::None));     // held 50 ms
    EXPECT_TRUE(s.DueAt(49ms).empty());
    EXPECT_EQ(s.DueAt(51ms), (Bytes{2}));
    EXPECT_TRUE(s.DueAt(149ms).empty());
    EXPECT_EQ(s.DueAt(151ms), (Bytes{1}));
}

TEST(NetworkSimulatorTest, InOrderPacketsAreDelayedWithoutReorderingPerQueue) {
    ScriptedSimulator s;
    s.simulator.setSimulatedLatency(System::TimeSpan::FromMilliseconds(100));
    s.draws = {kUnitDeviate, 0.0, kUnitDeviate, 0.5, kUnitDeviate, 0.5};
    EXPECT_TRUE(s.Send(1, SendDataOptions::InOrder));                         // due at 150 ms
    EXPECT_TRUE(s.Send(2, SendDataOptions::ReliableInOrder));                 // due at 50 ms
    EXPECT_TRUE(s.Send(3, SendDataOptions::Chat | SendDataOptions::InOrder)); // due at 50 ms
    EXPECT_EQ(s.DueAt(51ms), (Bytes{3})) << "Chat packets have their own queue";
    EXPECT_TRUE(s.DueAt(149ms).empty()) << "an InOrder packet waits for the one sent before it";
    EXPECT_EQ(s.DueAt(151ms), (Bytes{1, 2}));
}

TEST(NetworkSimulatorTest, LatencyIsClampedToBetweenZeroAndTwiceTheMean) {
    ScriptedSimulator s;
    s.simulator.setSimulatedLatency(System::TimeSpan::FromMilliseconds(100));
    s.draws = {1e-300, 0.5, 1e-300, 0.0};
    EXPECT_TRUE(s.Send(1, SendDataOptions::None)); // z far below -4: held for no time at all
    EXPECT_TRUE(s.Send(2, SendDataOptions::None)); // z far above 4: held twice the mean
    EXPECT_EQ(s.DueAt(0ms), (Bytes{1}));
    EXPECT_TRUE(s.DueAt(199ms).empty());
    EXPECT_EQ(s.DueAt(200ms), (Bytes{2}));
}

TEST(NetworkSimulatorTest, ClearDiscardsEveryHeldPacket) {
    ScriptedSimulator s;
    s.simulator.setSimulatedLatency(System::TimeSpan::FromMilliseconds(10));
    s.draws = {0.25, 0.25, 0.25, 0.25};
    EXPECT_TRUE(s.Send(1, SendDataOptions::None));
    EXPECT_TRUE(s.Send(2, SendDataOptions::InOrder));
    s.simulator.Clear();
    EXPECT_TRUE(s.DueAt(1s).empty());
}
