//
// Created by robertvokac on 5/28/25.
//

#pragma once

#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework {

    class Game;

    using System::TimeSpan;

    /**
     * @brief Stores timing information for the current game update.
     *
     * GameTime stores the total game time, elapsed time since the previous
     * update, and whether the game loop is currently running slowly.
     *
     * @note Property setters are intentionally not public. In XNA these setters
     * are internal; in CNA the Game class updates them.
     */
    class GameTime {
        friend class Game;

    public:
        /**
         * @brief Initializes TotalGameTime and ElapsedGameTime to zero and IsRunningSlowly to false.
         */
        GameTime();

        /**
         * @brief Initializes a new instance with the specified total and elapsed game time.
         *
         * @param totalGameTime Total time since the start of the game.
         * @param elapsedGameTime Time elapsed since the previous update.
         */
        GameTime(TimeSpan totalGameTime, TimeSpan elapsedGameTime);

        /**
         * @brief Initializes a new instance with the specified time values and slow-running flag.
         *
         * @param totalGameTime Total time since the start of the game.
         * @param elapsedGameTime Time elapsed since the previous update.
         * @param isRunningSlowly Indicates whether the game loop is running slowly.
         */
        GameTime(TimeSpan totalGameTime, TimeSpan elapsedGameTime, bool isRunningSlowly);

        ~GameTime() = default;

        /**
         * @brief Gets the total game time since the start of the game.
         */
        [[nodiscard]] const TimeSpan& getTotalGameTimeProperty() const;

        /**
         * @brief Gets the elapsed game time since the previous update.
         */
        [[nodiscard]] const TimeSpan& getElapsedGameTimeProperty() const;

        /**
         * @brief Gets whether the game loop is currently running slowly.
         */
        [[nodiscard]] bool getIsRunningSlowlyProperty() const;

        void setTotalGameTimeProperty(const TimeSpan& value);
        void setTotalGameTimeProperty(TimeSpan&& value);

        void setElapsedGameTimeProperty(const TimeSpan& value);
        void setElapsedGameTimeProperty(TimeSpan&& value);

        void setIsRunningSlowlyProperty(bool value);

    private:

        TimeSpan TotalGameTime_;
        TimeSpan ElapsedGameTime_;
        bool IsRunningSlowly_;
    };

} // namespace Microsoft::Xna::Framework
