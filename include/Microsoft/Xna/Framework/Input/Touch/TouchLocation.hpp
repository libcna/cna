// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include <string>

namespace Microsoft::Xna::Framework::Input::Touch
{
    /// Represents one touch point snapshot, including an optional previous location.
    struct TouchLocation
    {
        [[nodiscard]] int getIdProperty() const;
        [[nodiscard]] TouchLocationState getStateProperty() const;
        [[nodiscard]] const Microsoft::Xna::Framework::Vector2& getPositionProperty() const;

        /// Constructs an invalid touch location.
        TouchLocation();

        /// Constructs with id, state, and position. Previous state is Invalid.
        TouchLocation(int id, TouchLocationState state,
                      const Microsoft::Xna::Framework::Vector2& position);

        /// Constructs with full previous location information.
        TouchLocation(int id, TouchLocationState state,
                      const Microsoft::Xna::Framework::Vector2& position,
                      TouchLocationState previousState,
                      const Microsoft::Xna::Framework::Vector2& previousPosition);

        /// Tries to retrieve the previous location. Returns false if previous state is Invalid.
        bool TryGetPreviousLocation(TouchLocation& previousLocation) const;

        [[nodiscard]] bool Equals(const TouchLocation& other) const;
        [[nodiscard]] int GetHashCode() const;
        [[nodiscard]] std::string ToString() const;

        friend bool operator==(const TouchLocation& value1, const TouchLocation& value2);
        friend bool operator!=(const TouchLocation& value1, const TouchLocation& value2);

    private:
        int id_;
        TouchLocationState state_;
        Microsoft::Xna::Framework::Vector2 position_;
        TouchLocationState prevState_;
        Microsoft::Xna::Framework::Vector2 prevPosition_;
    };
}
