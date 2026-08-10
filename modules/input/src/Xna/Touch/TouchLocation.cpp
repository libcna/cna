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
        // DEC-12: matches FNA exactly — the out-param is written on every path (a C# out-param must be assigned
        // before the method returns), then the return value reports whether that previous location is
        // valid. On the false path this yields TouchLocation(id_, Invalid, prevPosition_).
        previousLocation = TouchLocation(id_, prevState_, prevPosition_);
        return prevState_ != TouchLocationState::Invalid;
    }

    bool TouchLocation::Equals(const TouchLocation& other) const
    {
        return id_           == other.id_           &&
               position_     == other.position_     &&
               state_        == other.state_        &&
               prevPosition_ == other.prevPosition_ &&
               prevState_    == other.prevState_;
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
        return value1.Equals(value2);
    }

    bool operator!=(const TouchLocation& value1, const TouchLocation& value2)
    {
        return !(value1 == value2);
    }
}
