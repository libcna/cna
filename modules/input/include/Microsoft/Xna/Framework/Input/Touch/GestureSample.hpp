// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Input/Touch/GestureType.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "System/TimeSpan.hpp"
#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Input::Touch
{
    /**
     * @brief Represents one gesture event returned by TouchPanel::ReadGesture.
     */
    struct GestureSample
    {
        /**
         * @brief Gets the type of gesture represented by this sample.
         * @return The gesture type.
         */
        [[nodiscard]] GestureType getGestureTypeProperty() const;

        /**
         * @brief Gets the timestamp of the gesture.
         * @return The timestamp.
         */
        [[nodiscard]] System::TimeSpan getTimestampProperty() const;

        /**
         * @brief Gets the position of the primary touch point.
         * @return The primary position.
         */
        [[nodiscard]] const Microsoft::Xna::Framework::Vector2& getPositionProperty() const;

        /**
         * @brief Gets the position of the secondary touch point.
         * @return The secondary position.
         */
        [[nodiscard]] const Microsoft::Xna::Framework::Vector2& getPosition2Property() const;

        /**
         * @brief Gets the delta of the primary touch point since the last sample.
         * @return The primary delta.
         */
        [[nodiscard]] const Microsoft::Xna::Framework::Vector2& getDeltaProperty() const;

        /**
         * @brief Gets the delta of the secondary touch point since the last sample.
         * @return The secondary delta.
         */
        [[nodiscard]] const Microsoft::Xna::Framework::Vector2& getDelta2Property() const;

        /**
         * @brief Gets the finger id of the primary touch point (FNA extension).
         * @return The primary finger id.
         */
        NOXNA [[nodiscard]] int getFingerIdEXTProperty() const;

        /**
         * @brief Gets the finger id of the secondary touch point (FNA extension).
         * @return The secondary finger id.
         */
        NOXNA [[nodiscard]] int getFingerId2EXTProperty() const;

        /** @brief Constructs an empty gesture sample. */
        NOXNA GestureSample();

        /**
         * @brief Constructs a gesture sample; finger ids are set to NO_FINGER.
         * @param gestureType The type of gesture.
         * @param timestamp The timestamp of the gesture.
         * @param position The primary touch position.
         * @param position2 The secondary touch position.
         * @param delta The primary touch delta.
         * @param delta2 The secondary touch delta.
         */
        GestureSample(GestureType gestureType,
                      System::TimeSpan timestamp,
                      Microsoft::Xna::Framework::Vector2 position,
                      Microsoft::Xna::Framework::Vector2 position2,
                      Microsoft::Xna::Framework::Vector2 delta,
                      Microsoft::Xna::Framework::Vector2 delta2);

        /**
         * @brief Constructs a gesture sample with explicit finger ids (internal use).
         * @param gestureType The type of gesture.
         * @param timestamp The timestamp of the gesture.
         * @param position The primary touch position.
         * @param position2 The secondary touch position.
         * @param delta The primary touch delta.
         * @param delta2 The secondary touch delta.
         * @param fingerId The primary finger id.
         * @param fingerId2 The secondary finger id.
         */
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
