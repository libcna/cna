// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "Microsoft/Xna/Framework/Net/QualityOfService.hpp"
#include <cstdint>
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

        /**
         * @brief Gets the network address to connect to in order to join this session.
         *
         * Not part of FNA's original design (FNA's Find() never actually discovers anything to
         * connect to); populated by ENetDiscoveryService from the real LAN discovery reply.
         *
         * @return The host's address, or empty if this instance wasn't built from a real
         *         discovery reply.
         */
        NOXNA [[nodiscard]] const std::string& GetConnectAddress() const;

        /**
         * @brief Gets the network port to connect to in order to join this session.
         *
         * @return The host's real ENet connect port, or 0 if this instance wasn't built from a
         *         real discovery reply.
         */
        NOXNA [[nodiscard]] uint16_t GetConnectPort() const;

        /** @brief Creates an AvailableNetworkSession for CNA internal use. */
        NOXNA static AvailableNetworkSession CreateInternal(
            int numGamers,
            const std::string& host,
            int privateSlots,
            int publicSlots,
            NetworkSessionProperties properties,
            QualityOfService qos,
            const std::string& hostAddress = "",
            uint16_t hostPort = 0
        );

    private:
        AvailableNetworkSession(
            int numGamers,
            const std::string& host,
            int privateSlots,
            int publicSlots,
            NetworkSessionProperties properties,
            QualityOfService qos,
            const std::string& hostAddress = "",
            uint16_t hostPort = 0
        );

        int currentGamerCount_;
        std::string hostGamertag_;
        int openPrivateGamerSlots_;
        int openPublicGamerSlots_;
        NetworkSessionProperties sessionProperties_;
        QualityOfService qualityOfService_;
        std::string hostAddress_;
        uint16_t hostPort_{0};
    };
}
