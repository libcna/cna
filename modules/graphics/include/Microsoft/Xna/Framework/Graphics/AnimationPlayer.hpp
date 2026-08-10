// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief A single keyframe within an animation bone track.
     *
     * @note NOXNA — not part of the XNA 4.0 API. Mirrors the well-known "Keyframe" class from
     * Microsoft's own XNA Skinned Model Sample, which real XNA never shipped as framework
     * code — almost every XNA game needing skeletal animation copy-pasted an identical class
     * into its own project. A direct alias of KeyframeEXT (SkinnedModelEXT's own identical
     * concept, used by the separate Avatar-rendering path) rather than a duplicate type, so
     * this real-Model-facing path and the Avatar-facing SkinnedModelEXT path share the exact
     * same keyframe representation and content-loading code.
     */
    NOXNA using Keyframe = KeyframeEXT;

    /**
     * @brief A named animation clip: a fixed duration and a set of per-bone keyframe tracks.
     *
     * @note NOXNA — see Keyframe above. A direct alias of AnimationClipEXT.
     */
    NOXNA using AnimationClip = AnimationClipEXT;

    /**
     * @brief The skeleton and animation clip data needed to play back skeletal animation for
     * a real Model (as opposed to SkinnedModelEXT's own separate, Avatar-specific data model).
     *
     * @note NOXNA — not part of the XNA 4.0 API. Mirrors the well-known "SkinningData" class
     * from Microsoft's own XNA Skinned Model Sample. `ModelTypeReader::Read()` attaches an
     * instance of this to a loaded Model's own `Tag` property whenever a `.model.json`
     * descriptor supplies a `"skeleton"` field — game code retrieves it via
     * `static_cast<SkinningData*>(model.getTagProperty())`, matching the real sample's own
     * convention of stashing this on `Model.Tag` (real XNA's `Model` class has no dedicated
     * skinning-data property of its own). Inherits `System::Object` so it can be pointed to
     * by `Model.Tag`'s own `System::Object*` type.
     */
    NOXNA struct SkinningData : public System::Object
    {
        /** @brief Returns the fully-qualified .NET-style type name of this object. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Number of bones in this model's skeleton. */
        int BoneCount = 0;
        /** @brief Parent bone index for each bone (-1 for a root bone), in topological order. */
        std::vector<int> SkeletonHierarchy;
        /** @brief Bind-pose local transform for each bone, relative to its parent. */
        std::vector<Matrix> BindPose;
        /** @brief Inverse of each bone's bind-pose *global* (world) transform. */
        std::vector<Matrix> InverseBindPose;
        /** @brief Animation clips, keyed by clip name. */
        std::unordered_map<std::string, AnimationClip> AnimationClips;
    };

    /**
     * @brief Advances a skeletal animation clip by elapsed time and produces per-bone
     * transform arrays each frame, interpolating between keyframes.
     *
     * @note NOXNA — not part of the XNA 4.0 API. Mirrors the well-known "AnimationPlayer"
     * class from Microsoft's own XNA Skinned Model Sample — most ported XNA samples needing
     * skeletal animation include a copy of this exact class and call into it the same way:
     * construct once with the model's SkinningData, `StartClip()` when playback begins,
     * `Update()` every frame, then feed `GetSkinTransforms()` straight into
     * `SkinnedEffect::SetBoneTransforms()` before drawing the mesh.
     */
    NOXNA class AnimationPlayer
    {
    public:
        /**
         * @brief Constructs a player bound to a specific model's skeleton.
         * @param skinningData The skeleton/bind-pose/clip data to animate. Must outlive this player.
         */
        explicit AnimationPlayer(const SkinningData& skinningData);

        /**
         * @brief Starts playing a new animation clip from the beginning.
         * @param clip The clip to play.
         */
        void StartClip(const AnimationClip& clip);

        /**
         * @brief Advances the current clip's playback position and recomputes bone transforms.
         *
         * If no clip is currently playing (StartClip() was never called), every bone is left
         * at its bind pose.
         *
         * @param time                  Amount of time to advance the playback position by (or,
         *                              if @p relativeToCurrentTime is false, the absolute
         *                              position to seek to).
         * @param relativeToCurrentTime True to add @p time to the current position; false to
         *                              treat @p time as an absolute position.
         * @param loop                  True to wrap the resulting position around the clip's
         *                              Duration instead of clamping to it.
         */
        void Update(System::TimeSpan time, bool relativeToCurrentTime, bool loop = true);

        /**
         * @brief Returns the current playback position within the active clip.
         * @return Elapsed time since the clip started, as of the last Update() call.
         */
        [[nodiscard]] System::TimeSpan getCurrentPositionProperty() const { return currentPosition_; }

        /**
         * @brief Returns the currently-playing clip.
         * @return Pointer to the active AnimationClip, or nullptr if StartClip() was never called.
         */
        [[nodiscard]] const AnimationClip* getCurrentClipProperty() const { return currentClip_; }

        /**
         * @brief Returns each bone's local transform (relative to its parent) at the current
         * playback position.
         * @return A BoneCount-length array of local bone transforms.
         */
        [[nodiscard]] const std::vector<Matrix>& GetBoneTransforms() const { return boneTransforms_; }

        /**
         * @brief Returns each bone's absolute (model-space) transform at the current playback
         * position.
         * @return A BoneCount-length array of world-space bone transforms.
         */
        [[nodiscard]] const std::vector<Matrix>& GetWorldTransforms() const { return worldTransforms_; }

        /**
         * @brief Returns each bone's final skin transform, ready to pass directly to
         * SkinnedEffect::SetBoneTransforms().
         * @return A BoneCount-length array of skin transforms (InverseBindPose * worldTransform).
         */
        [[nodiscard]] const std::vector<Matrix>& GetSkinTransforms() const { return skinTransforms_; }

    private:
        const SkinningData* skinningData_;
        const AnimationClip* currentClip_ = nullptr;
        System::TimeSpan currentPosition_;
        std::vector<Matrix> boneTransforms_;
        std::vector<Matrix> worldTransforms_;
        std::vector<Matrix> skinTransforms_;

        void RecomputeTransforms();
    };
}
