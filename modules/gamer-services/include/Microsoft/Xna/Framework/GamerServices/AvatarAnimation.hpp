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

        /**
         * @brief Gets the current bone transform matrices for the animation.
         *
         * @return A read-only collection of 71 bone transform matrices (always zero-valued —
         * see the class remarks).
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
         * @brief Gets the total length of the animation.
         *
         * @return The animation length (always TimeSpan::Zero() — see the class remarks).
         */
        [[nodiscard]] System::TimeSpan getLengthProperty() const override;

        /**
         * @brief Gets the facial expression at the current playback position.
         *
         * @return The current facial expression (always a default-constructed, all-Neutral
         * AvatarExpression — nothing in this class ever changes it).
         */
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

        /**
         * @brief Sets the real-rendering clip name paired with this preset.
         *
         * @note CNAEXT — CNA extension. Defaults to AvatarAnimationPresetToClipNameEXT(preset)
         * at construction; this setter allows overriding it (e.g. to a documented best-effort
         * substitute clip name — see tools/avatar_asset_pipeline/README.md).
         * @param clipName The clip name to look up in a SkinnedModelEXT's Clips.
         */
        CNAEXT void SetRealClipNameEXT(const std::string& clipName);

        /**
         * @brief Gets the real-rendering clip name paired with this preset.
         *
         * @note CNAEXT — CNA extension.
         * @return The current clip name.
         */
        CNAEXT [[nodiscard]] const std::string& GetRealClipNameEXT() const;

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
        std::string realClipName_;
    };
}
