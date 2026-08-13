// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"

#include <algorithm>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "System/ArgumentException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const std::string& SkinningData::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Graphics.SkinningData";
        return typeName;
    }

    namespace
    {
        // Mirrors SkinnedModelEXT.cpp's own identical SampleTrack helper (same keyframe
        // interpolation math, same BoneTrackEXT/KeyframeEXT types via the Keyframe alias) --
        // deliberately a separate, small, self-contained copy rather than a shared function
        // across the two systems, matching SkinnedModelEXT's own documented design decision to
        // stay independent of Model/ModelBone/ModelMesh (see SkinnedModelEXT.hpp).
        Matrix SampleTrack(const BoneTrackEXT& track, System::TimeSpan pos)
        {
            const auto& keys = track.Keys;
            if (keys.size() == 1 || pos <= keys.front().Time)
            {
                const auto& k = keys.front();
                return Matrix::CreateScale(k.Scale) * Matrix::CreateFromQuaternion(k.Rotation)
                     * Matrix::CreateTranslation(k.Translation);
            }
            if (pos >= keys.back().Time)
            {
                const auto& k = keys.back();
                return Matrix::CreateScale(k.Scale) * Matrix::CreateFromQuaternion(k.Rotation)
                     * Matrix::CreateTranslation(k.Translation);
            }

            std::size_t next = 1;
            while (next < keys.size() && keys[next].Time < pos)
            {
                ++next;
            }
            const auto& a = keys[next - 1];
            const auto& b = keys[next];

            const double spanSeconds = (b.Time - a.Time).getTotalSecondsProperty();
            const float amount = spanSeconds > 0.0
                ? static_cast<float>((pos - a.Time).getTotalSecondsProperty() / spanSeconds)
                : 0.0f;

            const Vector3 translation = Vector3::Lerp(a.Translation, b.Translation, amount);
            const Vector3 scale = Vector3::Lerp(a.Scale, b.Scale, amount);
            const Quaternion rotation = Quaternion::Slerp(a.Rotation, b.Rotation, amount);

            return Matrix::CreateScale(scale) * Matrix::CreateFromQuaternion(rotation)
                 * Matrix::CreateTranslation(translation);
        }
    }

    AnimationPlayer::AnimationPlayer(const SkinningData& skinningData)
        : skinningData_(&skinningData)
    {
        boneTransforms_  = skinningData_->BindPose;
        worldTransforms_.assign(static_cast<std::size_t>(skinningData_->BoneCount), Matrix::getIdentityProperty());
        skinTransforms_.assign(static_cast<std::size_t>(skinningData_->BoneCount), Matrix::getIdentityProperty());
        RecomputeTransforms();
    }

    void AnimationPlayer::StartClip(const AnimationClip& clip)
    {
        currentClip_ = &clip;
        currentPosition_ = System::TimeSpan::Zero;
        RecomputeTransforms();
    }

    void AnimationPlayer::Update(System::TimeSpan time, bool relativeToCurrentTime, bool loop)
    {
        System::TimeSpan pos = relativeToCurrentTime ? (currentPosition_ + time) : time;

        if (currentClip_ != nullptr && currentClip_->Duration > System::TimeSpan::Zero)
        {
            if (loop)
            {
                // Same floor-mod-via-ticks approach as SkinnedModelEXT::ComputeBoneTransformsEXT
                // (Task 11.1) -- avoids an unbounded per-frame iterative wraparound cost for a
                // long-running session with a short clip.
                const auto durationTicks = currentClip_->Duration.getTicksProperty();
                auto posTicks = pos.getTicksProperty() % durationTicks;
                if (posTicks < 0) { posTicks += durationTicks; }
                pos = System::TimeSpan::FromTicks(posTicks);
            }
            else
            {
                if (pos > currentClip_->Duration) { pos = currentClip_->Duration; }
                if (pos < System::TimeSpan::Zero) { pos = System::TimeSpan::Zero; }
            }
        }
        else
        {
            pos = System::TimeSpan::Zero;
        }

        currentPosition_ = pos;
        RecomputeTransforms();
    }

    void AnimationPlayer::RecomputeTransforms()
    {
        const int boneCount = skinningData_->BoneCount;
        if (static_cast<int>(skinningData_->SkeletonHierarchy.size()) != boneCount
            || static_cast<int>(skinningData_->BindPose.size()) != boneCount
            || static_cast<int>(skinningData_->InverseBindPose.size()) != boneCount)
        {
            throw System::ArgumentException(
                "AnimationPlayer's SkinningData BoneCount (" + std::to_string(boneCount)
                    + ") does not match SkeletonHierarchy.size() ("
                    + std::to_string(skinningData_->SkeletonHierarchy.size())
                    + "), BindPose.size() (" + std::to_string(skinningData_->BindPose.size())
                    + "), or InverseBindPose.size() ("
                    + std::to_string(skinningData_->InverseBindPose.size()) + ")",
                "skinningData");
        }

        boneTransforms_ = skinningData_->BindPose;
        if (currentClip_ != nullptr)
        {
            for (const auto& track : currentClip_->Tracks)
            {
                if (!track.Keys.empty() && track.BoneIndex >= 0
                    && track.BoneIndex < static_cast<int>(boneTransforms_.size()))
                {
                    boneTransforms_[static_cast<std::size_t>(track.BoneIndex)] =
                        SampleTrack(track, currentPosition_);
                }
            }
        }

        // Bones are stored in topological (breadth-first) order, so each parent's world
        // transform is already finalized by the time a child bone is processed -- matches
        // SkinnedModelEXT::ComputeBoneTransformsEXT's own identical convention.
        worldTransforms_.assign(static_cast<std::size_t>(boneCount), Matrix::getIdentityProperty());
        for (int i = 0; i < boneCount; ++i)
        {
            const int parent = skinningData_->SkeletonHierarchy[static_cast<std::size_t>(i)];
            if (parent >= i)
            {
                throw System::ArgumentException(
                    "SkinningData::SkeletonHierarchy is not topologically ordered: bone "
                        + std::to_string(i) + " has parent index " + std::to_string(parent)
                        + ", which must be less than the bone's own index",
                    "skinningData");
            }
            // A root bone composes against SkeletonRootPrefix rather than nothing: that is where
            // the scene ancestry above the joint set and the skinned mesh node's cancellation live
            // (see SkinningData::SkeletonRootPrefix). An empty array reads as all-identity, so a
            // skeleton built without that context behaves exactly as before.
            const bool hasPrefix =
                skinningData_->SkeletonRootPrefix.size() == static_cast<std::size_t>(boneCount);
            worldTransforms_[static_cast<std::size_t>(i)] = parent < 0
                ? (hasPrefix ? boneTransforms_[static_cast<std::size_t>(i)] *
                                   skinningData_->SkeletonRootPrefix[static_cast<std::size_t>(i)]
                             : boneTransforms_[static_cast<std::size_t>(i)])
                : boneTransforms_[static_cast<std::size_t>(i)] * worldTransforms_[static_cast<std::size_t>(parent)];
        }

        skinTransforms_.assign(static_cast<std::size_t>(boneCount), Matrix::getIdentityProperty());
        for (int i = 0; i < boneCount; ++i)
        {
            skinTransforms_[static_cast<std::size_t>(i)] =
                skinningData_->InverseBindPose[static_cast<std::size_t>(i)] * worldTransforms_[static_cast<std::size_t>(i)];
        }
    }

    const std::string& ModelAnimationsEXT::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.ModelAnimationsEXT";
        return name;
    }

    void ApplyClipToBonesEXT(Model& model, const AnimationClip& clip, System::TimeSpan time)
    {
        // plan_gltf.md GLTF-294. Refusing a palette clip here is the whole reason
        // AnimationClipEXT carries a target space at all: its indices name joint slots, and
        // applying them to Model::Bones would pose the wrong bones with no symptom but wrong
        // motion -- a silent corruption in place of the silent drop GLTF-293 removed.
        if (clip.TargetSpace != ClipTargetSpaceEXT::SceneNode)
        {
            throw std::invalid_argument(
                "ApplyClipToBonesEXT: this clip's tracks index a joint palette, not Model::Bones. "
                "Play it through AnimationPlayer instead -- applying palette indices to bones "
                "would pose the wrong bones silently.");
        }

        if (time < System::TimeSpan::Zero) { time = System::TimeSpan::Zero; }
        if (time > clip.Duration) { time = clip.Duration; }

        // The collection is const, but a bone it hands back is not: posing a model changes its
        // bones' transforms, not which bones it has.
        const ModelBoneCollection& bones = model.getBonesProperty();
        for (const BoneTrackEXT& track : clip.Tracks)
        {
            if (track.Keys.empty()) { continue; }
            if (track.BoneIndex < 0 || track.BoneIndex >= bones.getCountProperty()) { continue; }
            // Only tracked bones are written. A clip animating one node of a large model must not
            // reset every other bone to a pose it never mentioned.
            bones[track.BoneIndex]->setTransformProperty(SampleTrack(track, time));
        }
    }


    std::size_t ApplyBindPoseBoneTransformsEXT(Model& model, const SkinningData& skinningData)
    {
        // No clip started, so RecomputeTransforms() leaves every bone at its bind pose -- which is
        // exactly the palette an application would push on its first frame.
        AnimationPlayer player(skinningData);
        player.Update(System::TimeSpan::Zero, false, false);
        const std::vector<Matrix>& palette = player.GetSkinTransforms();
        if (palette.empty()) { return 0; }

        std::size_t posed = 0;
        const ModelMeshCollection& meshes = model.getMeshesProperty();
        for (int mi = 0; mi < meshes.getCountProperty(); ++mi)
        {
            const ModelMesh* mesh = meshes[mi];
            if (mesh == nullptr) { continue; }

            // GLTF-265: a runtime glTF Model can carry several independent skins. When its
            // explicit mapping is present, applying skin A must not overwrite skin B's effect
            // palette merely because both effects live in the same Model. Models built by older
            // or non-glTF paths have no mapping and retain the historical apply-to-all behavior.
            const std::vector<ModelSkinEXT>& skins = model.getSkinsEXTProperty();
            if (!skins.empty())
            {
                bool belongsToSkin = false;
                for (const ModelSkinEXT& skin : skins)
                {
                    if (skin.Data != &skinningData) { continue; }
                    belongsToSkin = std::find(skin.Meshes.begin(), skin.Meshes.end(), mesh) !=
                                    skin.Meshes.end();
                    if (belongsToSkin) { break; }
                }
                if (!belongsToSkin) { continue; }
            }
            const ModelMeshPartCollection& parts = mesh->getMeshPartsProperty();
            for (int pi = 0; pi < parts.getCountProperty(); ++pi)
            {
                ModelMeshPart* part = parts[pi];
                if (part == nullptr) { continue; }
                Effect* effect = part->getEffectProperty();
                if (auto* skinnedPbr = dynamic_cast<SkinnedPbrEffect*>(effect))
                {
                    skinnedPbr->SetBoneTransforms(palette);
                    ++posed;
                }
                else if (auto* skinned = dynamic_cast<SkinnedEffect*>(effect))
                {
                    skinned->SetBoneTransforms(palette);
                    ++posed;
                }
            }
        }
        return posed;
    }

}
