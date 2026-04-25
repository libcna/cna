//
// Created by robertvokac on 5/25/25.
//

#pragma once

#include "TouchLocationState.hpp"
#include "CppDotNet/Prop.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

namespace Microsoft::Xna::Framework::Input::Touch {
    /**
     * @brief Represents one touch point snapshot.
     *
     * Includes public touch id, touch state and touch position in pixels.
     *
     * @note Status: PARTIAL
     */
    struct TouchLocation {
        DEF_PROP(int, Id, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(TouchLocationState, State, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(Vector2, Position, getter1, setter0, member1, static0, constret1, ref1, constmet1)

    public:
        /**
         * @brief Initializes an invalid touch location.
         */
        TouchLocation();

        /**
         * @brief Initializes touch location without explicit id.
         *
         * @note Status: PARTIAL
         */
        TouchLocation(TouchLocationState state, const Vector2& position);

        /**
         * @brief Initializes touch location with id, state and position.
         */
        TouchLocation(int id, TouchLocationState state, const Vector2& position);
    };
}
