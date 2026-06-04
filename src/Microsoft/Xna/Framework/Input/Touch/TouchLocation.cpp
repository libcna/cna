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

    int              TouchLocation::getIdProperty()       const { return id_; }
    TouchLocationState TouchLocation::getStateProperty()  const { return state_; }
    const Microsoft::Xna::Framework::Vector2& TouchLocation::getPositionProperty() const { return position_; }

    bool TouchLocation::TryGetPreviousLocation(TouchLocation& previousLocation) const
    {
        if (prevState_ == TouchLocationState::Invalid)
            return false;
        previousLocation = TouchLocation(id_, prevState_, prevPosition_);
        return true;
    }

    bool TouchLocation::Equals(const TouchLocation& other) const
    {
        return id_    == other.id_    &&
               state_ == other.state_ &&
               position_ == other.position_;
    }

    int TouchLocation::GetHashCode() const
    {
        return id_ ^ (static_cast<int>(state_) * 31);
    }

    std::string TouchLocation::ToString() const
    {
        return "[TouchLocation Id=" + std::to_string(id_) + "]";
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
