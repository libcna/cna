// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::Net
{
    /**
     * @brief Describes measured network quality between the local machine and a remote gamer.
     */
    class QualityOfService final
    {
    public:
        /**
         * @brief Gets the average measured round-trip time.
         *
         * @return The average round-trip time.
         */
        [[nodiscard]] System::TimeSpan getAverageRoundtripTimeProperty() const;

        /**
         * @brief Gets the measured downstream bandwidth in bytes per second.
         *
         * @return The downstream bandwidth.
         */
        [[nodiscard]] int getBytesPerSecondDownstreamProperty() const;

        /**
         * @brief Gets the measured upstream bandwidth in bytes per second.
         *
         * @return The upstream bandwidth.
         */
        [[nodiscard]] int getBytesPerSecondUpstreamProperty() const;

        /**
         * @brief Gets whether quality-of-service data is available.
         *
         * @return true if available.
         */
        [[nodiscard]] bool getIsAvailableProperty() const;

        /**
         * @brief Gets the minimum measured round-trip time.
         *
         * @return The minimum round-trip time.
         */
        [[nodiscard]] System::TimeSpan getMinimumRoundtripTimeProperty() const;

        /**
         * @brief Creates a QualityOfService for CNA internal use, with all-zero/unmeasured fields.
         *
         * `IsAvailable` is still `true` even though nothing is actually measured - this matches
         * FNA's own reference `internal QualityOfService()` constructor byte-for-byte (an
         * acknowledged upstream stub, its own source carries a "TODO: Everything below" comment),
         * not a CNA gap. Task 4.2: kept for callers with no real measurement to offer at all (e.g.
         * a session listing not built from a real discovery reply); real production discovery
         * replies use the measured overload below instead.
         */
        CNAEXT static QualityOfService CreateInternal();

        /**
         * @brief Task 4.2: creates a QualityOfService reflecting a real measurement, with
         * `IsAvailable = true`.
         *
         * `ENetDiscoveryService`'s only production call site measures the wall-clock round-trip
         * between sending a discovery `Query` and receiving each host's `Announce` reply -a real,
         * if connectionless-UDP-only, RTT sample (there's no established `ENetPeer` connection yet
         * at discovery time to measure real bandwidth from, so throughput stays unmeasured/zero;
         * see `ENetDiscoveryService.cpp`'s own comment for why).
         *
         * @param roundtripTime The measured round-trip time. Used for both the average and minimum
         * fields, since a single query/reply exchange yields exactly one sample, not a running
         * series to average or take a minimum over.
         */
        CNAEXT static QualityOfService CreateInternal(System::TimeSpan roundtripTime);

    private:
        QualityOfService();
        explicit QualityOfService(System::TimeSpan roundtripTime);

        System::TimeSpan averageRoundtripTime_;
        int bytesPerSecondDownstream_{0};
        int bytesPerSecondUpstream_{0};
        bool isAvailable_{true};
        System::TimeSpan minimumRoundtripTime_;
    };
}
