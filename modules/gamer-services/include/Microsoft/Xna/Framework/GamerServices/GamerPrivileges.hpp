// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeSetting.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Represents the set of privileges a signed-in gamer has on the platform.
     */
    class GamerPrivileges
    {
    public:
        /**
         * @brief Gets the communication privilege setting.
         *
         * @return The AllowCommunication privilege.
         */
        [[nodiscard]] GamerPrivilegeSetting getAllowCommunicationProperty() const;

        /**
         * @brief Gets whether the gamer is allowed to participate in online sessions.
         *
         * @return true if online sessions are permitted.
         */
        [[nodiscard]] bool getAllowOnlineSessionsProperty() const;

        /**
         * @brief Gets whether the gamer is allowed to access premium content.
         *
         * @return true if premium content access is permitted.
         */
        [[nodiscard]] bool getAllowPremiumContentProperty() const;

        /**
         * @brief Gets the profile-viewing privilege setting.
         *
         * @return The AllowProfileViewing privilege.
         */
        [[nodiscard]] GamerPrivilegeSetting getAllowProfileViewingProperty() const;

        /**
         * @brief Gets whether the gamer is allowed to purchase content.
         *
         * @return true if content purchases are permitted.
         */
        [[nodiscard]] bool getAllowPurchaseContentProperty() const;

        /**
         * @brief Gets whether the gamer is allowed to trade content.
         *
         * @return true if content trading is permitted.
         */
        [[nodiscard]] bool getAllowTradeContentProperty() const;

        /**
         * @brief Gets the user-created-content privilege setting.
         *
         * @return The AllowUserCreatedContent privilege.
         */
        [[nodiscard]] GamerPrivilegeSetting getAllowUserCreatedContentProperty() const;

        /** @brief Creates a GamerPrivileges instance for CNA internal use. */
        NOXNA static GamerPrivileges CreateInternal();

    private:
        GamerPrivileges();

        GamerPrivilegeSetting allowCommunication_;
        bool allowOnlineSessions_;
        bool allowPremiumContent_;
        GamerPrivilegeSetting allowProfileViewing_;
        bool allowPurchaseContent_;
        bool allowTradeContent_;
        GamerPrivilegeSetting allowUserCreatedContent_;
    };
}
