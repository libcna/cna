// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPresenceMode.hpp"
#include <array>
#include <string>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Describes what a signed-in gamer is currently doing in a game.
     */
    class GamerPresence
    {
    public:
        /**
         * @brief Gets the current presence mode.
         *
         * @return The GamerPresenceMode.
         */
        [[nodiscard]] GamerPresenceMode getPresenceModeProperty() const;

        /**
         * @brief Sets the current presence mode.
         *
         * Setting this property updates the presence string and notifies the platform.
         *
         * @param value The new GamerPresenceMode.
         */
        void setPresenceModeProperty(GamerPresenceMode value);

        /**
         * @brief Gets the integer value embedded in the presence string.
         *
         * @return The presence value.
         */
        [[nodiscard]] int getPresenceValueProperty() const;

        /**
         * @brief Sets the integer value embedded in the presence string.
         *
         * @param value The new presence value.
         */
        void setPresenceValueProperty(int value);

        /**
         * @brief Sets the platform presence string directly.
         *
         * This is an FNA extension point; calling it has no effect on non-Xbox platforms.
         *
         * @param mode The presence mode string.
         */
        CNAEXT void SetPresenceModeStringEXT(const std::string& mode);

        /** @brief Creates a GamerPresence instance for CNA internal use. */
        CNAEXT static GamerPresence CreateInternal();

    private:
        GamerPresence();

        std::string presence_;
        GamerPresenceMode presenceMode_;
        int presenceValue_;

        static const std::array<std::string, 60> presenceModeStrings_;
    };
}
