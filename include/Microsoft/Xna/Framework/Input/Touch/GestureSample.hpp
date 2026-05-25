#pragma once

#include "Microsoft/Xna/Framework/Input/Touch/GestureType.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::Input::Touch
{
    /// Represents one gesture event returned by TouchPanel::ReadGesture.
    struct GestureSample
    {
        /// Creates an empty gesture sample.
        GestureSample();

        /// Creates a gesture sample.
        GestureSample(
            GestureType gestureType,
            System::TimeSpan timestamp,
            Microsoft::Xna::Framework::Vector2 position,
            Microsoft::Xna::Framework::Vector2 position2,
            Microsoft::Xna::Framework::Vector2 delta,
            Microsoft::Xna::Framework::Vector2 delta2
        );

        [[nodiscard]] GestureType getGestureTypeProperty() const;
        [[nodiscard]] System::TimeSpan getTimestampProperty() const;
        [[nodiscard]] Microsoft::Xna::Framework::Vector2 getPositionProperty() const;
        [[nodiscard]] Microsoft::Xna::Framework::Vector2 getPosition2Property() const;
        [[nodiscard]] Microsoft::Xna::Framework::Vector2 getDeltaProperty() const;
        [[nodiscard]] Microsoft::Xna::Framework::Vector2 getDelta2Property() const;

    private:
        GestureType gestureType_;
        System::TimeSpan timestamp_;
        Microsoft::Xna::Framework::Vector2 position_;
        Microsoft::Xna::Framework::Vector2 position2_;
        Microsoft::Xna::Framework::Vector2 delta_;
        Microsoft::Xna::Framework::Vector2 delta2_;
    };
}
