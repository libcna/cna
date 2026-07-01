// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "Microsoft/Xna/Framework/Net/QualityOfService.hpp"
#include <string>

namespace Microsoft::Xna::Framework::Net
{
    /**
     * @brief Describes a network session discovered while searching for sessions to join.
     */
    class AvailableNetworkSession final
    {
    public:
        /**
         * @brief Gets the current number of gamers in the session.
         *
         * @return The current gamer count.
         */
        [[nodiscard]] int getCurrentGamerCountProperty() const;

        /**
         * @brief Gets the gamertag of the session's host.
         *
         * @return The host gamertag string.
         */
        [[nodiscard]] const std::string& getHostGamertagProperty() const;

        /**
         * @brief Gets the number of open private gamer slots.
         *
         * @return The open private slot count.
         */
        [[nodiscard]] int getOpenPrivateGamerSlotsProperty() const;

        /**
         * @brief Gets the number of open public gamer slots.
         *
         * @return The open public slot count.
         */
        [[nodiscard]] int getOpenPublicGamerSlotsProperty() const;

        /**
         * @brief Gets the measured quality-of-service for this session.
         *
         * @return Const reference to the QualityOfService.
         */
        [[nodiscard]] const QualityOfService& getQualityOfServiceProperty() const;

        /**
         * @brief Gets the custom session properties advertised for this session.
         *
         * @return Const reference to the NetworkSessionProperties.
         */
        [[nodiscard]] const NetworkSessionProperties& getSessionPropertiesProperty() const;

        /**
         * @brief Determines whether this session listing is structurally equal to another.
         *
         * FNA's AvailableNetworkSession has no custom equality; required by
         * ReadOnlyCollection<T>::IndexOf/Contains, whose virtual overrides get instantiated
         * regardless of use. Compares only the directly-comparable scalar fields — QualityOfService
         * and NetworkSessionProperties are not themselves equatable, so they're excluded.
         *
         * @param other The session to compare against.
         * @return true if the comparable fields are all equal.
         */
        NOXNA [[nodiscard]] bool operator==(const AvailableNetworkSession& other) const;

        /**
         * @brief Determines whether this session listing differs from another.
         *
         * @param other The session to compare against.
         * @return true if any comparable field differs.
         */
        NOXNA [[nodiscard]] bool operator!=(const AvailableNetworkSession& other) const;

        /** @brief Creates an AvailableNetworkSession for CNA internal use. */
        NOXNA static AvailableNetworkSession CreateInternal(
            int numGamers,
            const std::string& host,
            int privateSlots,
            int publicSlots,
            NetworkSessionProperties properties,
            QualityOfService qos
        );

    private:
        AvailableNetworkSession(
            int numGamers,
            const std::string& host,
            int privateSlots,
            int publicSlots,
            NetworkSessionProperties properties,
            QualityOfService qos
        );

        int currentGamerCount_;
        std::string hostGamertag_;
        int openPrivateGamerSlots_;
        int openPublicGamerSlots_;
        NetworkSessionProperties sessionProperties_;
        QualityOfService qualityOfService_;
    };
}
