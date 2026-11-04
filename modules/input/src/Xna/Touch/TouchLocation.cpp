// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp"

#include <string>

namespace Microsoft::Xna::Framework::Input::Touch
{
    TouchLocation::TouchLocation()
        : id_(0),
          state_(TouchLocationState::Invalid),
          position_(),
          prevState_(TouchLocationState::Invalid),
          prevPosition_()
    {
    }

    TouchLocation::TouchLocation(int id, TouchLocationState state,
                                 const Microsoft::Xna::Framework::Vector2& position)
        : id_(id),
          state_(state),
          position_(position),
          prevState_(TouchLocationState::Invalid),
          prevPosition_()
    {
    }

    TouchLocation::TouchLocation(int id, TouchLocationState state,
                                 const Microsoft::Xna::Framework::Vector2& position,
                                 TouchLocationState previousState,
                                 const Microsoft::Xna::Framework::Vector2& previousPosition)
        : id_(id),
          state_(state),
          position_(position),
          prevState_(previousState),
          prevPosition_(previousPosition)
    {
    }

    TouchLocation::TouchLocation(int id, TouchLocationState state,
                                 const Microsoft::Xna::Framework::Vector2& position,
                                 float pressure)
        : id_(id),
          state_(state),
          position_(position),
          prevState_(TouchLocationState::Invalid),
          prevPosition_(),
          pressure_(pressure)
    {
    }

    TouchLocation::TouchLocation(int id, TouchLocationState state,
                                 const Microsoft::Xna::Framework::Vector2& position,
                                 TouchLocationState previousState,
                                 const Microsoft::Xna::Framework::Vector2& previousPosition,
                                 float pressure)
        : id_(id),
          state_(state),
          position_(position),
          prevState_(previousState),
          prevPosition_(previousPosition),
          pressure_(pressure)
    {
    }

    int              TouchLocation::getIdProperty()       const { return id_; }
    TouchLocationState TouchLocation::getStateProperty()  const { return state_; }
    const Microsoft::Xna::Framework::Vector2& TouchLocation::getPositionProperty() const { return position_; }
    float            TouchLocation::getPressureEXT()      const { return pressure_; }

    bool TouchLocation::TryGetPreviousLocation(TouchLocation& previousLocation) const
    {
        // XNA TryGetPreviousLocation IL_0000-004a writes (-1, Invalid, Zero) on failure,
        // overriding FNA's retention of the current id and stored previous coordinates.
        if (prevState_ == TouchLocationState::Invalid)
        {
            previousLocation = TouchLocation(-1, TouchLocationState::Invalid,
                                             Microsoft::Xna::Framework::Vector2::Zero);
            return false;
        }
        previousLocation = TouchLocation(id_, prevState_, prevPosition_);
        return true;
    }

    bool TouchLocation::Equals(const std::any& obj) const
    {
        const TouchLocation* other = std::any_cast<TouchLocation>(&obj);
        return other != nullptr && Equals(*other);
    }

    bool TouchLocation::Equals(const TouchLocation& other) const
    {
        // XNA Equals(TouchLocation) IL_0000-004d compares ID and coordinates, omitting both states.
        return id_           == other.id_           &&
               position_     == other.position_     &&
               prevPosition_ == other.prevPosition_;
    }

    int TouchLocation::GetHashCode() const
    {
        // Unsigned wraparound avoids signed-overflow UB (INPUT-BUILD-006); result is unchanged.
        return static_cast<int>(static_cast<unsigned>(id_)
                                + static_cast<unsigned>(position_.GetHashCode()));
    }

    std::string TouchLocation::ToString() const
    {
        return "{Position:" + position_.ToString() + "}";
    }

    bool operator==(const TouchLocation& value1, const TouchLocation& value2)
    {
        // XNA op_Equality IL_0010-001e,0040-004e includes both states;
        // typed Equals IL_0000-004d deliberately omits them.
        return value1.getStateProperty() == value2.getStateProperty() &&
               value1.prevState_ == value2.prevState_ && value1.Equals(value2);
    }

    bool operator!=(const TouchLocation& value1, const TouchLocation& value2)
    {
        return !(value1 == value2);
    }
}
