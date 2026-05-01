//
// Created by robertvokac on 5/28/25.
//

#pragma once

#include "SharpRuntime/Prop.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework {

    using System::TimeSpan;

    /**
     * @brief Holds the time state of a Game.
     *
     * This is a C++ counterpart of the MonoGame/XNA
     * Microsoft.Xna.Framework.GameTime class.
     *
     * GameTime stores:
     * - total time since the start of the game
     * - elapsed time since the last update
     * - whether the game is currently running slowly
     *
     * @note Status: Implemented.
     */
    class GameTime {
    public:
        /**
         * @brief Gets or sets the total game time since the start of the game.
         */
        DEF_PROP(TimeSpan, TotalGameTime, getter1, setter1, member1, static0, constret1, ref1, constmet1)

    private:
        TimeSpan ElapsedGameTime_;

    public:
        /**
         * @brief Gets the elapsed game time since the last update.
         *
         * @return Time elapsed since the last update.
         */
        [[nodiscard]] const TimeSpan& getElapsedGameTimeProperty() const;

        /**
         * @brief Sets the elapsed game time since the last update.
         *
         * @param v New elapsed game time.
         */
        void setElapsedGameTimeProperty(const TimeSpan& v);

        /**
         * @brief Sets the elapsed game time since the last update using move semantics.
         *
         * @param v New elapsed game time.
         */
        void setElapsedGameTimeProperty(TimeSpan&& v);

        /**
         * @brief Gets or sets whether the game is running slowly.
         *
         * This flag is typically used when the game loop falls behind its target timing.
         */
        DEF_PROP(bool, IsRunningSlowly, getter1, setter1, member1, static0, constret1, ref1, constmet1)

    public:
        /**
         * @brief Initializes a new instance with TotalGameTime and ElapsedGameTime set to zero.
         *
         * IsRunningSlowly is initialized to false.
         */
        GameTime();

        /**
         * @brief Initializes a new instance with the specified total and elapsed game time.
         *
         * @param totalGameTime Total time since the start of the game.
         * @param elapsedGameTime Time elapsed since the last update.
         */
        GameTime(TimeSpan totalGameTime, TimeSpan elapsedGameTime);

        /**
         * @brief Initializes a new instance with the specified total and elapsed game time.
         *
         * @param totalGameTime Total time since the start of the game.
         * @param elapsedGameTime Time elapsed since the last update.
         * @param isRunningSlowly Indicates whether the game is running slowly.
         */
        GameTime(TimeSpan totalGameTime, TimeSpan elapsedGameTime, bool isRunningSlowly);

        /**
         * @brief Destroys the GameTime instance.
         */
        ~GameTime() = default;
    };

} // namespace Microsoft::Xna::Framework