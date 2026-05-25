#include "Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp"

namespace Microsoft::Xna::Framework::Input::Touch
{
    GestureSample::GestureSample()
        : gestureType_(GestureType::None),
          timestamp_(System::TimeSpan::Zero),
          position_(Microsoft::Xna::Framework::Vector2::Zero),
          position2_(Microsoft::Xna::Framework::Vector2::Zero),
          delta_(Microsoft::Xna::Framework::Vector2::Zero),
          delta2_(Microsoft::Xna::Framework::Vector2::Zero)
    {
    }

    GestureSample::GestureSample(
        GestureType gestureType,
        System::TimeSpan timestamp,
        Microsoft::Xna::Framework::Vector2 position,
        Microsoft::Xna::Framework::Vector2 position2,
        Microsoft::Xna::Framework::Vector2 delta,
        Microsoft::Xna::Framework::Vector2 delta2
    )
        : gestureType_(gestureType),
          timestamp_(timestamp),
          position_(position),
          position2_(position2),
          delta_(delta),
          delta2_(delta2)
    {
    }

    GestureType GestureSample::getGestureTypeProperty() const
    {
        return gestureType_;
    }

    System::TimeSpan GestureSample::getTimestampProperty() const
    {
        return timestamp_;
    }

    Microsoft::Xna::Framework::Vector2 GestureSample::getPositionProperty() const
    {
        return position_;
    }

    Microsoft::Xna::Framework::Vector2 GestureSample::getPosition2Property() const
    {
        return position2_;
    }

    Microsoft::Xna::Framework::Vector2 GestureSample::getDeltaProperty() const
    {
        return delta_;
    }

    Microsoft::Xna::Framework::Vector2 GestureSample::getDelta2Property() const
    {
        return delta2_;
    }
}
