// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimationPreset.hpp"
#include "Microsoft/Xna/Framework/GamerServices/IAvatarAnimation.hpp"
#include "System/IDisposable.hpp"
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Provides methods and properties for animating an avatar using standard animations.
     *
     * Each preset is an original CNA clip on the 71-bone avatar skeleton with a real length,
     * bone transforms that change over time and a keyed facial expression.
     */
    class AvatarAnimation : public IAvatarAnimation, public System::IDisposable
    {
    public:
        /**
         * @brief Creates a new instance of AvatarAnimation, initialized with the specified animation.
         *
         * @param animationPreset The standard animation. A value outside AvatarAnimationPreset
         * yields a zero-length animation holding the bind pose.
         * @throws System::InvalidOperationException if the built-in animation assets cannot be read.
         */
        explicit AvatarAnimation(AvatarAnimationPreset animationPreset);

        /**
         * @brief Gets the position of the bones at the time specified by CurrentPosition.
         *
         * @return 71 transforms in local bone space relative to their parent bones.
         */
        [[nodiscard]] System::Collections::ObjectModel::ReadOnlyCollection<Microsoft::Xna::Framework::Matrix>
        getBoneTransformsProperty() const override;

        /**
         * @brief Gets the current playback position within the animation.
         *
         * @return The current position.
         */
        [[nodiscard]] System::TimeSpan getCurrentPositionProperty() const override;

        /**
         * @brief Sets the current playback position within the animation.
         *
         * Immediately re-clamps the new position via Update(TimeSpan::Zero(), false), matching
         * the real XNA implementation.
         *
         * @param value The new position.
         */
        void setCurrentPositionProperty(System::TimeSpan value) override;

        /**
         * @brief Gets the length of the animation.
         *
         * @return The animation length.
         */
        [[nodiscard]] System::TimeSpan getLengthProperty() const override;

        /**
         * @brief Gets the expression of the animation at the current time position.
         *
         * @return The current facial expression.
         */
        [[nodiscard]] AvatarExpression getExpressionProperty() const override;

        /**
         * @brief Advances the playback position of the animation.
         *
         * Adds @p elapsedAnimationTime to the current position (a negative value plays backward),
         * then brings it back into [0, Length]: wrapped by the remainder of Length when @p loop is
         * true and Length is non-zero, otherwise clamped to the nearer bound.
         *
         * @param elapsedAnimationTime The amount of time to advance by.
         * @param loop Whether the animation should loop.
         * @throws System::ObjectDisposedException if this instance has been disposed.
         */
        void Update(System::TimeSpan elapsedAnimationTime, bool loop) override;

        /** @brief Gets a value indicating whether this instance has been disposed. */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /** @brief Releases all resources used by this instance. */
        void Dispose() override;

    protected:
        /**
         * @brief Releases the unmanaged resources used by this instance.
         *
         * @param disposing true to release both managed and unmanaged resources; false to
         * release only unmanaged resources. Idempotent.
         */
        void Dispose(bool disposing);

    private:
        void Sample();

        int preset_{-1};
        std::vector<Microsoft::Xna::Framework::Matrix> avatarBones_;
        AvatarExpression currentExpression_;
        System::TimeSpan currentPosition_;
        System::TimeSpan length_;
        bool isDisposed_{false};
    };
}
