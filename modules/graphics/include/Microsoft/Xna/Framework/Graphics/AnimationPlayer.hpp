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
     * @note CNAEXT — not part of the XNA 4.0 API. Mirrors the well-known "Keyframe" class from
     * Microsoft's own XNA Skinned Model Sample, which real XNA never shipped as framework
     * code — almost every XNA game needing skeletal animation copy-pasted an identical class
     * into its own project. A direct alias of KeyframeEXT (SkinnedModelEXT's own identical
     * concept, used by the separate Avatar-rendering path) rather than a duplicate type, so
     * this real-Model-facing path and the Avatar-facing SkinnedModelEXT path share the exact
     * same keyframe representation and content-loading code.
     */
    CNAEXT using Keyframe = KeyframeEXT;

    /**
     * @brief A named animation clip: a fixed duration and a set of per-bone keyframe tracks.
     *
     * @note CNAEXT — see Keyframe above. A direct alias of AnimationClipEXT.
     */
    CNAEXT using AnimationClip = AnimationClipEXT;

    /**
     * @brief The skeleton and animation clip data needed to play back skeletal animation for
     * a real Model (as opposed to SkinnedModelEXT's own separate, Avatar-specific data model).
     *
     * @note CNAEXT — not part of the XNA 4.0 API. Mirrors the well-known "SkinningData" class
     * from Microsoft's own XNA Skinned Model Sample. `ModelTypeReader::Read()` attaches an
     * instance of this to a loaded Model's own `Tag` property whenever a `.model.json`
     * descriptor supplies a `"skeleton"` field — game code retrieves it via
     * `static_cast<SkinningData*>(model.getTagProperty())`, matching the real sample's own
     * convention of stashing this on `Model.Tag` (real XNA's `Model` class has no dedicated
     * skinning-data property of its own). Inherits `System::Object` so it can be pointed to
     * by `Model.Tag`'s own `System::Object*` type.
     */
    CNAEXT struct SkinningData : public System::Object
    {
        /** @brief Returns the fully-qualified .NET-style type name of this object. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Number of bones in this model's skeleton. */
        int BoneCount = 0;
        /** @brief Parent bone index for each bone (-1 for a root bone), in topological order. */
        std::vector<int> SkeletonHierarchy;
        /** @brief Bind-pose local transform for each bone, relative to its parent. */
        std::vector<Matrix> BindPose;
        /** @brief Inverse of each bone's bind-pose *global* (world) transform. */
        std::vector<Matrix> InverseBindPose;
        /**
         * @brief Transform each **root** bone's own local transform composes against; identity for
         * a bone with a parent, and identity throughout for a skeleton that has no such context.
         *
         * @note CNAEXT — plans/plan_gltf.md GLTF-245/GLTF-247 (Phase 5). A skeleton imported from glTF
         * hangs somewhere in a larger scene: its root joints may have scene ancestors that are not
         * themselves joints, and the skinned mesh's own node transform has to be cancelled rather
         * than applied. Both are properties of the space *above* the skeleton, so they cannot be
         * expressed as a bone-local transform — and folding them into @ref BindPose would be undone
         * the instant a clip replaced that root bone's local transform. Kept separately, an
         * animated root joint substitutes only its own local transform, exactly like any other
         * bone. May be left empty, which is read as all-identity.
         */
        std::vector<Matrix> SkeletonRootPrefix;
        /**
         * @brief The scene-node index of the rig root the file declares, or -1 when it declares none.
         *
         * @note CNAEXT — plans/plan_gltf.md GLTF-249. glTF's `skin.skeleton` names the rig's semantic
         * root: the node an editor would show as "the armature", and the one to attach a prop or a
         * whole character to. It is carried here purely so an application can *find* that node,
         * indexed into `Model::Bones` like every other scene node (§15.1.2's `sceneNodeIndex`).
         *
         * It has **no** effect on any transform CNA computes, and must never acquire one:
         * plans/plan_gltf.md §15.1.1 records that truncating a joint's ancestry walk at this node
         * reproduces defect D8 — one dropped ancestor translation displaced every skinned vertex
         * by 100 units. Joint global transforms come from the complete scene ancestry regardless
         * of where this points.
         */
        int SkeletonRootNodeIndexEXT = -1;
        /** @brief The declared rig root's node name; empty when the file declares none. */
        std::string SkeletonRootNameEXT;
        /** @brief Animation clips, keyed by clip name. */
        std::unordered_map<std::string, AnimationClip> AnimationClips;
    };

    /**
     * @brief Advances a skeletal animation clip by elapsed time and produces per-bone
     * transform arrays each frame, interpolating between keyframes.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. Mirrors the well-known "AnimationPlayer"
     * class from Microsoft's own XNA Skinned Model Sample — most ported XNA samples needing
     * skeletal animation include a copy of this exact class and call into it the same way:
     * construct once with the model's SkinningData, `StartClip()` when playback begins,
     * `Update()` every frame, then feed `GetSkinTransforms()` straight into
     * `SkinnedEffect::SetBoneTransforms()` before drawing the mesh.
     */
    CNAEXT class AnimationPlayer
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

    class Model;

    /**
     * @brief Animation clips whose tracks drive a `Model`'s own bones rather than a joint palette.
     *
     * @note CNAEXT — not part of the XNA 4.0 API (plans/plan_gltf.md `GLTF-294`). Attached to
     * `Model::Tag`, the same place `SkinningData` and `MorphTargetDataEXT` already live. This is
     * rigid (non-joint) animation: a door, a turntable, a clock hand — motion glTF expresses by
     * animating an ordinary scene node, which CNA silently dropped before `GLTF-293` because
     * `ExtractClips` resolved every channel against a skin's joint set.
     *
     * Every clip here carries `ClipTargetSpaceEXT::SceneNode`, so a consumer that also handles
     * palette clips can tell the two apart rather than having to know by context.
     *
     * @note `Model::Tag` holds one object, and a skinned model already uses it for `SkinningData`.
     * A file with **both** a skin and rigid node animation therefore has nowhere to put these
     * today; the importer reports that by name rather than dropping it (`GLTF-295`).
     */
    CNAEXT struct ModelAnimationsEXT : public System::Object
    {
        /** @brief Returns the fully-qualified .NET-style type name of this object. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Clips, keyed by clip name. */
        std::unordered_map<std::string, AnimationClip> Clips;
    };

    /**
     * @brief Poses a model's bones from a scene-node clip at a point in time.
     *
     * @note CNAEXT — not part of the XNA 4.0 API (plans/plan_gltf.md `GLTF-294`). Writes each track's
     * interpolated local transform onto the `ModelBone` its index names, leaving every untracked
     * bone at whatever transform it already has — so a clip animating one node of a large model
     * does not disturb the rest. `Model::CopyAbsoluteBoneTransformsTo` then composes the hierarchy
     * exactly as it does for a static model.
     *
     * Keyframes are interpolated the same way `AnimationPlayer` interpolates a palette clip:
     * linearly in translation and scale, spherically in rotation, clamped at both ends.
     *
     * @param model The model whose bones are posed.
     * @param clip The clip to evaluate; must carry `ClipTargetSpaceEXT::SceneNode`.
     * @param time The time to evaluate at, clamped to `[0, clip.Duration]`.
     * @throws std::invalid_argument if @p clip is not a scene-node clip, since applying a
     * joint-palette clip's indices to `Model::Bones` would pose the wrong bones silently.
     */
    CNAEXT void ApplyClipToBonesEXT(Model& model, const AnimationClip& clip,
                                    System::TimeSpan time);

    /**
     * @brief Poses a skinned model in its bind pose, so it is drawable before any clip plays.
     *
     * @note CNAEXT — not part of the XNA 4.0 API (plans/plan_gltf.md `GLTF-262`). A skinned effect's
     * bone palette defaults to `MaxBones` identity matrices, which is not a neutral value: it
     * means "every joint matrix is the identity", so a mesh drawn that way is rendered in joint
     * space rather than in its bind pose, and glTF's own `inverse(globalTransform(meshNode))`
     * cancellation term (§3.7.3) never applies. A skinned model that is loaded and drawn without
     * game code first calling `AnimationPlayer::Update` and `SetBoneTransforms` therefore renders
     * visibly wrong rather than merely unanimated.
     *
     * This computes the palette the same way an application would — an `AnimationPlayer` over
     * @p skinningData with no clip started, which leaves every bone at its bind pose — and pushes
     * it onto every matching skinned effect the model carries. When @p model exposes a non-empty
     * `Model::SkinsEXT` mapping, only the meshes mapped to @p skinningData are touched; this keeps
     * independent glTF skins from overwriting one another's mutable effect palettes (`GLTF-265`).
     * Models from older or non-glTF paths have no mapping and retain the historical apply-to-all
     * behavior. Calling this again, or animating afterwards, simply overwrites the selected
     * palette; it holds no state of its own.
     *
     * @param model The model whose parts carry `SkinnedEffect` / `SkinnedPbrEffect` instances.
     * @param skinningData The model's skeleton, normally the object on `Model::Tag`.
     * @return The number of skinned effects that were posed; 0 for a model with none.
     */
    CNAEXT std::size_t ApplyBindPoseBoneTransformsEXT(Model& model,
                                                       const SkinningData& skinningData);

}
