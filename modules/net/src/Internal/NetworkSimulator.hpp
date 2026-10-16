// SPDX-License-Identifier: MS-PL
#pragma once
#include "Microsoft/Xna/Framework/Net/SendDataOptions.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/TimeSpan.hpp"
#include <chrono>
#include <deque>
#include <functional>
#include <random>
#include <vector>

namespace Microsoft::Xna::Framework::Net
{
    class LocalNetworkGamer;
    class NetworkGamer;
}

namespace CNA::Internal::Net {
/**
 * @brief NetworkSession.SimulatedPacketLoss and SimulatedLatency, applied where XNA applies them:
 * on the sending machine, to each SendData before it reaches a transport or a local recipient.
 *
 * Loss drops a packet sent without Reliable with the set probability. Latency holds a packet for a
 * normally distributed time with the set value as mean and a quarter of it as standard deviation,
 * clamped to [0, twice the value]. Unordered packets leave by due time and can overtake each other;
 * InOrder packets wait in a queue (one for Chat, one for the rest) and are delayed without being
 * reordered. Measured from the XNA 4.0 Net assembly's NetworkSimulator.
 */
class NetworkSimulator {
public:
    using Clock = std::chrono::steady_clock;

    /** @brief A packet held by the simulated latency. */
    struct LatentPacket {
        /** @brief When the packet is due. */
        Clock::time_point SendTime;
        /** @brief The packet's bytes. */
        std::vector<SharpRuntime::bytecs> Data;
        /** @brief The options it was sent with. */
        Microsoft::Xna::Framework::Net::SendDataOptions Options{};
        /** @brief The sending gamer. */
        Microsoft::Xna::Framework::Net::LocalNetworkGamer* Sender{nullptr};
        /** @brief The recipient, or nullptr for every gamer in the session. */
        Microsoft::Xna::Framework::Net::NetworkGamer* Recipient{nullptr};
    };

    /** @brief Gets the loss probability. @return From 0 to 1. */
    [[nodiscard]] float getSimulatedPacketLoss() const { return loss_; }
    /** @brief Sets the loss probability.
     * @param value From 0 to 1; anything else throws ArgumentOutOfRangeException. */
    void setSimulatedPacketLoss(float value);
    /** @brief Gets the mean latency. @return The latency. */
    [[nodiscard]] System::TimeSpan getSimulatedLatency() const { return latency_; }
    /** @brief Sets the mean latency.
     * @param value Zero or more; a negative value throws ArgumentOutOfRangeException. */
    void setSimulatedLatency(System::TimeSpan value);

    /**
     * @brief Applies the simulation to one send.
     *
     * @param data The packet; moved from when it is held.
     * @param options The options it is sent with.
     * @param sender The sending gamer.
     * @param recipient The recipient, or nullptr for every gamer.
     * @return Whether the simulator took the packet (dropped or held); false means send it now.
     */
    bool SendData(std::vector<SharpRuntime::bytecs>& data,
                  Microsoft::Xna::Framework::Net::SendDataOptions options,
                  Microsoft::Xna::Framework::Net::LocalNetworkGamer* sender,
                  Microsoft::Xna::Framework::Net::NetworkGamer* recipient);

    /** @brief Removes the held packets now due: unordered ones first, then each InOrder queue.
     * @return The packets, in the order they are to be sent. */
    std::vector<LatentPacket> TakeDuePackets();

    /** @brief Discards every held packet. */
    void Clear();

    /** @brief Fixes the time every simulator reads. @param time The time. */
    static void SetClockForTesting(Clock::time_point time);
    /** @brief Restores the real clock. */
    static void ResetClockForTesting();
    /** @brief Replaces every simulator's random source.
     * @param nextDouble Returns values in [0, 1), as System.Random.NextDouble does. */
    static void SetRandomForTesting(std::function<double()> nextDouble);
    /** @brief Restores each simulator's own random source. */
    static void ResetRandomForTesting();

private:
    double NextDouble();
    Clock::duration NormallyDistributedRandomLatency();

    float loss_{0.0f};
    System::TimeSpan latency_{System::TimeSpan::Zero};
    std::mt19937_64 random_{std::random_device{}()};
    std::vector<LatentPacket> unordered_;
    std::deque<LatentPacket> encryptedOrdered_;
    std::deque<LatentPacket> unencryptedOrdered_;
};
}
