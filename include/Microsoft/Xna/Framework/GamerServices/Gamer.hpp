// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include <any>
#include <string>

namespace Microsoft::Xna::Framework::GamerServices
{
    class SignedInGamerCollection;
    class LeaderboardWriter;

    /**
     * @brief Represents a gamer profile on the platform.
     *
     * Abstract base class for all gamer types in the XNA GamerServices API.
     */
    class Gamer
    {
    public:
        virtual ~Gamer() = default;

        /**
         * @brief Gets the display name of the gamer.
         *
         * @return The display name string.
         */
        [[nodiscard]] const std::string& getDisplayNameProperty() const;

        /**
         * @brief Sets the display name of the gamer.
         *
         * @param value The new display name.
         */
        void setDisplayNameProperty(const std::string& value);

        /**
         * @brief Gets the gamertag of the gamer.
         *
         * @return The gamertag string.
         */
        [[nodiscard]] const std::string& getGamertagProperty() const;

        /**
         * @brief Gets whether this gamer has been disposed.
         *
         * @return true if disposed.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets the user-defined tag object attached to this gamer.
         *
         * @return Reference to the tag value.
         */
        [[nodiscard]] std::any& getTagProperty();

        /**
         * @brief Sets the user-defined tag object attached to this gamer.
         *
         * @param value The tag value.
         */
        void setTagProperty(const std::any& value);

        /**
         * @brief Gets the collection of currently signed-in gamers.
         *
         * @return Pointer to the signed-in gamer collection.
         */
        static SignedInGamerCollection* getSignedInGamersProperty();

    protected:
        /**
         * @brief Constructs a Gamer with the given gamertag and display name.
         *
         * @param gamertag    The gamertag string.
         * @param displayName The display name string.
         */
        Gamer(const std::string& gamertag, const std::string& displayName);

        std::string displayName_;
        std::string gamertag_;
        bool isDisposed_{false};
        std::any tag_;

    private:
        static SignedInGamerCollection* signedInGamers_;
    };
}
