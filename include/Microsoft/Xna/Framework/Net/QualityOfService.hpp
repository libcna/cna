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

        /** @brief Creates a QualityOfService for CNA internal use. */
        NOXNA static QualityOfService CreateInternal();

    private:
        QualityOfService();

        System::TimeSpan averageRoundtripTime_;
        int bytesPerSecondDownstream_{0};
        int bytesPerSecondUpstream_{0};
        bool isAvailable_{true};
        System::TimeSpan minimumRoundtripTime_;
    };
}
