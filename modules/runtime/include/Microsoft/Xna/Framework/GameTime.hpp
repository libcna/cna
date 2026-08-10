// SPDX-License-Identifier: MS-PL

#pragma once

#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework
{
    class Game;

    using System::TimeSpan;

    /** @brief Stores timing information for the current game update. */
    class GameTime
    {
        friend class Game;

    public:
        /** @brief Initializes TotalGameTime and ElapsedGameTime to zero and IsRunningSlowly to false. */
        GameTime();

        /**
         * @brief Initializes a new instance with the specified total and elapsed game time.
         * @param totalGameTime Total elapsed game time since the game started.
         * @param elapsedGameTime Time elapsed since the last update.
         */
        GameTime(TimeSpan totalGameTime, TimeSpan elapsedGameTime);

        /**
         * @brief Initializes a new instance with the specified time values and slow-running flag.
         * @param totalGameTime Total elapsed game time since the game started.
         * @param elapsedGameTime Time elapsed since the last update.
         * @param isRunningSlowly true if the game loop is currently running slower than the target rate.
         */
        GameTime(TimeSpan totalGameTime, TimeSpan elapsedGameTime, bool isRunningSlowly);

        /**
         * @brief Gets the total game time since the start of the game.
         * @return A const reference to the total game time.
         */
        [[nodiscard]] const TimeSpan& getTotalGameTimeProperty() const;

        /**
         * @brief Gets the elapsed game time since the previous update.
         * @return A const reference to the elapsed game time.
         */
        [[nodiscard]] const TimeSpan& getElapsedGameTimeProperty() const;

        /**
         * @brief Gets whether the game loop is currently running slowly.
         * @return true if the game loop is falling behind the target update rate.
         */
        [[nodiscard]] bool getIsRunningSlowlyProperty() const;

    private:
        void setTotalGameTimeProperty(const TimeSpan& value);
        void setElapsedGameTimeProperty(const TimeSpan& value);
        void setIsRunningSlowlyProperty(bool value);

        TimeSpan TotalGameTime_;
        TimeSpan ElapsedGameTime_;
        bool IsRunningSlowly_;
    };
}
