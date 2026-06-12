// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Input/Touch/GestureType.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "System/TimeSpan.hpp"
#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Input::Touch
{
    /// Represents one gesture event returned by TouchPanel::ReadGesture.
    struct GestureSample
    {
        [[nodiscard]] GestureType getGestureTypeProperty() const;
        [[nodiscard]] System::TimeSpan getTimestampProperty() const;
        [[nodiscard]] const Microsoft::Xna::Framework::Vector2& getPositionProperty() const;
        [[nodiscard]] const Microsoft::Xna::Framework::Vector2& getPosition2Property() const;
        [[nodiscard]] const Microsoft::Xna::Framework::Vector2& getDeltaProperty() const;
        [[nodiscard]] const Microsoft::Xna::Framework::Vector2& getDelta2Property() const;

        /// Gets the finger id of the primary touch point (FNA extension).
        NOXNA [[nodiscard]] int getFingerIdEXTProperty() const;
        /// Gets the finger id of the secondary touch point (FNA extension).
        NOXNA [[nodiscard]] int getFingerId2EXTProperty() const;

        /// Constructs an empty gesture sample.
        GestureSample();

        /// Constructs a gesture sample; finger ids are set to NO_FINGER.
        GestureSample(GestureType gestureType,
                      System::TimeSpan timestamp,
                      Microsoft::Xna::Framework::Vector2 position,
                      Microsoft::Xna::Framework::Vector2 position2,
                      Microsoft::Xna::Framework::Vector2 delta,
                      Microsoft::Xna::Framework::Vector2 delta2);

        /// Constructs a gesture sample with explicit finger ids (internal use).
        NOXNA GestureSample(GestureType gestureType,
                             System::TimeSpan timestamp,
                             Microsoft::Xna::Framework::Vector2 position,
                             Microsoft::Xna::Framework::Vector2 position2,
                             Microsoft::Xna::Framework::Vector2 delta,
                             Microsoft::Xna::Framework::Vector2 delta2,
                             int fingerId,
                             int fingerId2);

    private:
        GestureType gestureType_;
        System::TimeSpan timestamp_;
        Microsoft::Xna::Framework::Vector2 position_;
        Microsoft::Xna::Framework::Vector2 position2_;
        Microsoft::Xna::Framework::Vector2 delta_;
        Microsoft::Xna::Framework::Vector2 delta2_;
        int fingerIdEXT_;
        int fingerId2EXT_;
    };
}
