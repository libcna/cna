// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimationPreset.hpp"
#include "Microsoft/Xna/Framework/GamerServices/IAvatarAnimation.hpp"
#include "System/IDisposable.hpp"
#include <vector>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Provides a preset avatar animation.
     *
     * The real XNA implementation's constructor never actually reads its @p animationPreset
     * argument — every instance ends up with the same 71 zero-valued bone transforms and a
     * zero-length animation, regardless of which preset is requested. That behavior is
     * preserved here exactly (see @ref AvatarAnimation(AvatarAnimationPreset)).
     */
    class AvatarAnimation : public IAvatarAnimation, public System::IDisposable
    {
    public:
        /**
         * @brief Initializes a new instance of AvatarAnimation for the specified preset.
         *
         * @param animationPreset The animation preset (not read by the real XNA implementation;
         * every instance behaves identically regardless of this argument).
         */
        explicit AvatarAnimation(AvatarAnimationPreset animationPreset);

        [[nodiscard]] System::Collections::ObjectModel::ReadOnlyCollection<Microsoft::Xna::Framework::Matrix>
        getBoneTransformsProperty() const override;

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

        [[nodiscard]] System::TimeSpan getLengthProperty() const override;

        [[nodiscard]] AvatarExpression getExpressionProperty() const override;

        /**
         * @brief Advances the playback position of the animation.
         *
         * Adds @p elapsedAnimationTime to the current position, then clamps it back into
         * [TimeSpan::Zero(), Length]: if @p loop is true and Length is non-zero, wraps around
         * by repeatedly adding/subtracting Length; otherwise clamps to the nearer bound.
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
        std::vector<Microsoft::Xna::Framework::Matrix> avatarBones_;
        AvatarExpression currentExpression_;
        System::TimeSpan currentPosition_;
        System::TimeSpan length_;
        bool isDisposed_{false};
    };
}
