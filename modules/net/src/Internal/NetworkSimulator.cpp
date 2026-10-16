// SPDX-License-Identifier: MS-PL
#include "NetworkSimulator.hpp"

#include "System/ArgumentOutOfRangeException.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>

using Microsoft::Xna::Framework::Net::LocalNetworkGamer;
using Microsoft::Xna::Framework::Net::NetworkGamer;
using Microsoft::Xna::Framework::Net::SendDataOptions;

namespace CNA::Internal::Net {
namespace {
    using TimeSpanTicks = std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;

    std::optional<NetworkSimulator::Clock::time_point>& ClockOverride()
    {
        static std::optional<NetworkSimulator::Clock::time_point> time;
        return time;
    }

    std::function<double()>& RandomOverride()
    {
        static std::function<double()> nextDouble;
        return nextDouble;
    }

    NetworkSimulator::Clock::time_point Now()
    {
        return ClockOverride() ? *ClockOverride() : NetworkSimulator::Clock::now();
    }
}

void NetworkSimulator::setSimulatedPacketLoss(float value)
{
    // NaN passes, as it does XNA's own comparisons.
    if (value < 0.0f || value > 1.0f) throw System::ArgumentOutOfRangeException("value");
    loss_ = value;
}

void NetworkSimulator::setSimulatedLatency(System::TimeSpan value)
{
    if (value < System::TimeSpan::Zero) throw System::ArgumentOutOfRangeException("value");
    latency_ = value;
}

bool NetworkSimulator::SendData(std::vector<SharpRuntime::bytecs>& data, SendDataOptions options,
                                LocalNetworkGamer* sender, NetworkGamer* recipient)
{
    if (loss_ > 0.0f && (options & SendDataOptions::Reliable) == SendDataOptions::None &&
        NextDouble() <= loss_)
        return true;
    if (latency_.getTicksProperty() <= 0) return false;

    LatentPacket packet{Now() + NormallyDistributedRandomLatency(), std::move(data), options, sender,
                        recipient};
    if ((options & SendDataOptions::InOrder) != SendDataOptions::None)
    {
        auto& queue = (options & SendDataOptions::Chat) != SendDataOptions::None ? unencryptedOrdered_
                                                                                 : encryptedOrdered_;
        queue.push_back(std::move(packet));
        return true;
    }
    const auto later = std::upper_bound(
        unordered_.begin(), unordered_.end(), packet.SendTime,
        [](Clock::time_point time, const LatentPacket& held) { return time < held.SendTime; });
    unordered_.insert(later, std::move(packet));
    return true;
}

std::vector<NetworkSimulator::LatentPacket> NetworkSimulator::TakeDuePackets()
{
    std::vector<LatentPacket> due;
    if (unordered_.empty() && encryptedOrdered_.empty() && unencryptedOrdered_.empty()) return due;
    const Clock::time_point now = Now();
    const auto firstLater = std::find_if(unordered_.begin(), unordered_.end(),
                                         [&](const LatentPacket& held) { return held.SendTime > now; });
    std::move(unordered_.begin(), firstLater, std::back_inserter(due));
    unordered_.erase(unordered_.begin(), firstLater);
    for (auto* queue : {&encryptedOrdered_, &unencryptedOrdered_})
    {
        while (!queue->empty() && queue->front().SendTime <= now)
        {
            due.push_back(std::move(queue->front()));
            queue->pop_front();
        }
    }
    return due;
}

void NetworkSimulator::Clear()
{
    unordered_.clear();
    encryptedOrdered_.clear();
    unencryptedOrdered_.clear();
}

void NetworkSimulator::SetClockForTesting(Clock::time_point time) { ClockOverride() = time; }
void NetworkSimulator::ResetClockForTesting() { ClockOverride().reset(); }
void NetworkSimulator::SetRandomForTesting(std::function<double()> nextDouble)
{
    RandomOverride() = std::move(nextDouble);
}
void NetworkSimulator::ResetRandomForTesting() { RandomOverride() = nullptr; }

double NetworkSimulator::NextDouble()
{
    if (RandomOverride()) return RandomOverride()();
    return static_cast<double>(random_() >> 11) * 0x1.0p-53;
}

NetworkSimulator::Clock::duration NetworkSimulator::NormallyDistributedRandomLatency()
{
    // Box-Muller, in TimeSpan ticks. The cap keeps an enormous TimeSpan representable as a due time.
    constexpr std::int64_t kMaximumTicks =
        std::chrono::duration_cast<TimeSpanTicks>(Clock::duration::max() / 4).count();
    const std::int64_t mean = std::min(latency_.getTicksProperty(), kMaximumTicks / 2);
    const std::int64_t maximum = mean * 2;
    const double u1 = NextDouble();
    const double u2 = NextDouble();
    const double z = std::sqrt(-2.0 * std::log(u1)) * std::cos(6.2831853071795862 * u2);
    const double latency = z * static_cast<double>(mean / 4) + static_cast<double>(mean);
    std::int64_t ticks = 0;
    if (latency >= static_cast<double>(maximum)) ticks = maximum;
    else if (latency > 0.0) ticks = static_cast<std::int64_t>(latency);
    return std::chrono::duration_cast<Clock::duration>(TimeSpanTicks(ticks));
}
}
