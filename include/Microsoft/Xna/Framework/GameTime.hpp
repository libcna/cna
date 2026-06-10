// SPDX-License-Identifier: MS-PL

#pragma once

#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework
{
    class Game;

    using System::TimeSpan;

    /// Stores timing information for the current game update.
    class GameTime
    {
        friend class Game;

    public:
        /// Initializes TotalGameTime and ElapsedGameTime to zero and IsRunningSlowly to false.
        GameTime();

        /// Initializes a new instance with the specified total and elapsed game time.
        GameTime(TimeSpan totalGameTime, TimeSpan elapsedGameTime);

        /// Initializes a new instance with the specified time values and slow-running flag.
        GameTime(TimeSpan totalGameTime, TimeSpan elapsedGameTime, bool isRunningSlowly);

        /// Gets the total game time since the start of the game.
        [[nodiscard]] const TimeSpan& getTotalGameTimeProperty() const;

        /// Gets the elapsed game time since the previous update.
        [[nodiscard]] const TimeSpan& getElapsedGameTimeProperty() const;

        /// Gets whether the game loop is currently running slowly.
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
