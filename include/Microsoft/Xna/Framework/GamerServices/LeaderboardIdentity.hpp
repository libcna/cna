// SPDX-License-Identifier: MS-PL
#pragma once
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardKey.hpp"
#include <string>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Identifies a leaderboard by key and optional game mode.
     */
    struct LeaderboardIdentity
    {
        /**
         * @brief Gets or sets the leaderboard key string.
         *
         * @return The key string.
         */
        [[nodiscard]] std::string getKeyProperty() const;

        /**
         * @brief Sets the leaderboard key string.
         *
         * @param value The key string.
         */
        void setKeyProperty(const std::string& value);

        /**
         * @brief Gets or sets the game mode index.
         *
         * @return The game mode.
         */
        [[nodiscard]] int getGameModeProperty() const;

        /**
         * @brief Sets the game mode index.
         *
         * @param value The game mode.
         */
        void setGameModeProperty(int value);

        /**
         * @brief Creates a LeaderboardIdentity for the given key with game mode 0.
         *
         * @param key The leaderboard key.
         * @return A new LeaderboardIdentity.
         */
        static LeaderboardIdentity Create(LeaderboardKey key);

        /**
         * @brief Creates a LeaderboardIdentity for the given key and game mode.
         *
         * @param key      The leaderboard key.
         * @param gameMode The game mode index.
         * @return A new LeaderboardIdentity.
         */
        static LeaderboardIdentity Create(LeaderboardKey key, int gameMode);

    private:
        std::string key_;
        int gameMode_{0};
    };
}
