// SPDX-License-Identifier: MS-PL
#pragma once
#include "Microsoft/Xna/Framework/GamerServices/AvatarExpression.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "System/Collections/ObjectModel/ReadOnlyCollection.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Provides access to an avatar animation.
     */
    class IAvatarAnimation
    {
    public:
        /** @brief Virtual destructor for safe polymorphic destruction. */
        virtual ~IAvatarAnimation() = default;

        /**
         * @brief Gets the current bone transform matrices for the animation.
         *
         * @return A read-only collection of 71 bone transform matrices.
         */
        [[nodiscard]] virtual System::Collections::ObjectModel::ReadOnlyCollection<Microsoft::Xna::Framework::Matrix>
        getBoneTransformsProperty() const = 0;

        /**
         * @brief Gets the current playback position within the animation.
         *
         * @return The current position.
         */
        [[nodiscard]] virtual System::TimeSpan getCurrentPositionProperty() const = 0;

        /**
         * @brief Sets the current playback position within the animation.
         *
         * @param value The new position.
         */
        virtual void setCurrentPositionProperty(System::TimeSpan value) = 0;

        /**
         * @brief Gets the total length of the animation.
         *
         * @return The animation length.
         */
        [[nodiscard]] virtual System::TimeSpan getLengthProperty() const = 0;

        /**
         * @brief Gets the facial expression at the current playback position.
         *
         * @return The current facial expression.
         */
        [[nodiscard]] virtual AvatarExpression getExpressionProperty() const = 0;

        /**
         * @brief Advances the playback position of the animation.
         *
         * @param elapsedAnimationTime The amount of time to advance by.
         * @param loop Whether the animation should loop.
         */
        virtual void Update(System::TimeSpan elapsedAnimationTime, bool loop) = 0;
    };
}
