// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class ModelMeshPart;
    class VertexBuffer;
    class IndexBuffer;

    /**
     * @brief A single keyframe within an animation bone track.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. CNA extension supporting
     * SkinnedModelEXT's real-rendering animation playback.
     */
    CNAEXT struct KeyframeEXT
    {
        /** @brief Time of this keyframe, relative to the start of the clip. */
        System::TimeSpan Time;
        /** @brief Bone-local translation at this keyframe. */
        Microsoft::Xna::Framework::Vector3 Translation;
        /** @brief Bone-local rotation at this keyframe. */
        Microsoft::Xna::Framework::Quaternion Rotation = Microsoft::Xna::Framework::Quaternion::Identity;
        /** @brief Bone-local scale at this keyframe. */
        Microsoft::Xna::Framework::Vector3 Scale{1.0f, 1.0f, 1.0f};
    };

    /**
     * @brief A sequence of keyframes driving a single bone within an animation clip.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. CNA extension.
     */
    CNAEXT struct BoneTrackEXT
    {
        /** @brief Index of the bone this track drives, into SkinnedModelEXT's bone arrays. */
        int BoneIndex = -1;
        /** @brief Keyframes for this track, in ascending Time order. */
        std::vector<KeyframeEXT> Keys;
    };

    /**
     * @brief A named animation clip: a fixed duration and a set of per-bone keyframe tracks.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. CNA extension. Clip names conventionally
     * match Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset enumerator names
     * (see AvatarAnimationPresetToClipNameEXT), but SkinnedModelEXT itself has no dependency
     * on that enum.
     */
    /**
     * @brief Which index space a clip's `BoneTrackEXT::BoneIndex` values live in.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. The two are deliberately distinct and must never
     * be silently interchanged (plan_gltf.md §15.1.2): a joint's palette slot has nothing to do
     * with its position in the scene, and a rigid scene node has no palette slot at all. A clip
     * that stated neither would let a reader apply one as the other, which is a silent corruption
     * in place of the silent drop `GLTF-293` removed.
     */
    CNAEXT enum class ClipTargetSpaceEXT
    {
        /** @brief A slot in one skin's GPU joint palette — what `SkinningData` indexes. */
        JointPalette,
        /** @brief A node's index in `Model::Bones` — what rigid node animation drives. */
        SceneNode,
    };

    /**
     * @brief A named animation clip: a fixed duration and a set of per-bone keyframe tracks.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. CNA extension. Clip names conventionally
     * match Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset enumerator names
     * (see AvatarAnimationPresetToClipNameEXT), but SkinnedModelEXT itself has no dependency
     * on that enum.
     */
    CNAEXT struct AnimationClipEXT
    {
        /** @brief Total playback duration of this clip. */
        System::TimeSpan Duration;
        /** @brief Per-bone keyframe tracks. Bones with no track hold their bind pose. */
        std::vector<BoneTrackEXT> Tracks;
        /**
         * @brief Which index space @ref Tracks' bone indices are in.
         *
         * @note CNAEXT — plan_gltf.md `GLTF-294`. Defaults to `JointPalette`, which is what every
         * clip was before rigid node animation existed, so no existing consumer's meaning changes.
         */
        ClipTargetSpaceEXT TargetSpace = ClipTargetSpaceEXT::JointPalette;
    };

    /**
     * @brief A real, GPU-skinnable mesh + skeleton + animation clip set.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. CNA extension used by
     * AvatarRenderer::EnableRealRenderingEXT for real avatar rendering. Deliberately not built
     * on Model/ModelBone/ModelMesh, which encode a *rigid* per-mesh parent-bone-transform
     * hierarchy (real XNA's multi-part model animation) rather than per-vertex GPU skinning.
     * This type's bone count/hierarchy is entirely independent of the real Xbox Avatar's
     * 71-bone arrays exposed by AvatarRenderer::ParentBones/IAvatarAnimation::BoneTransforms —
     * no attempt is made to align indices, counts, or semantics between the two.
     */
    CNAEXT class SkinnedModelEXT
    {
    public:
        /**
         * @brief Names a single renderable part of the model.
         */
        CNAEXT struct PartEXT
        {
            /**
             * @brief Part name, used for appearance tinting (AvatarRenderer::PartTintEXT
             * matches by substring, e.g. "Hair"/"Shirt"/"Pants"/"Shoes" — not exact
             * equality, since this is the source Blender object name baked straight
             * through by the content pipeline, e.g. "CNAAvatarHair").
             */
            std::string Name;
            /** @brief The part's geometry; SkinnedModelEXT owns the referenced buffers. */
            ModelMeshPart* Part = nullptr;
            /** @brief The part's diffuse texture, or nullptr if none was supplied. */
            Texture2D* Texture = nullptr;
        };

        /** @brief Constructs an empty skinned model with no bones, parts, or clips. */
        SkinnedModelEXT();

        /** @brief Destructor. */
        ~SkinnedModelEXT();

        /** @brief Copying is not allowed (owns GPU buffer resources). */
        SkinnedModelEXT(const SkinnedModelEXT&) = delete;
        /** @brief Copy-assignment is not allowed. */
        SkinnedModelEXT& operator=(const SkinnedModelEXT&) = delete;
        /** @brief Move-constructs a SkinnedModelEXT, transferring resource ownership. */
        SkinnedModelEXT(SkinnedModelEXT&&) noexcept;
        /** @brief Move-assigns a SkinnedModelEXT, transferring resource ownership. */
        SkinnedModelEXT& operator=(SkinnedModelEXT&&) noexcept;

        /** @brief Number of bones in this model's skeleton (independent of the Xbox 71-bone arrays). */
        int BoneCount = 0;
        /** @brief Parent bone index for each bone (-1 for a root bone). */
        std::vector<int> ParentBoneIndices;
        /** @brief Bind-pose local transform for each bone, relative to its parent. */
        std::vector<Matrix> BindPoseLocal;
        /** @brief Inverse of each bone's bind-pose *global* (world) transform. */
        std::vector<Matrix> InverseBindPoseGlobal;
        /** @brief Renderable parts making up this model. */
        std::vector<PartEXT> Parts;
        /** @brief Animation clips, keyed by clip name. */
        std::unordered_map<std::string, AnimationClipEXT> Clips;

        /**
         * @brief Samples an animation clip at a given position and computes final,
         * skinning-ready world bone matrices (already multiplied by each bone's
         * InverseBindPoseGlobal).
         *
         * @param clipName Name of a clip present in Clips.
         * @param position Playback position within the clip.
         * @param loop     Whether to wrap @p position around Duration instead of clamping.
         * @param outWorldBones Receives BoneCount skinning matrices, ready for
         * SkinnedEffect::SetBoneTransforms.
         * @throws System::ArgumentException if clipName is not present in Clips.
         */
        void ComputeBoneTransformsEXT(const std::string& clipName,
                                       System::TimeSpan position,
                                       bool loop,
                                       std::vector<Matrix>& outWorldBones) const;

        /**
         * @brief Adds a renderable part, taking ownership of its vertex/index buffers.
         *
         * Used by SkinnedModelTypeReader while loading content; not typically called by game code.
         *
         * @param name         Part name (see PartEXT::Name).
         * @param vertexBuffer Vertex buffer for this part; ownership transfers to this model.
         * @param indexBuffer  Index buffer for this part; ownership transfers to this model.
         * @param part         Mesh part referencing the above buffers; ownership transfers to this model.
         * @param texture      Optional diffuse texture for this part; pass a default-constructed
         *                     Texture2D if the part has none.
         */
        void AddPartEXT(std::string name,
                         std::unique_ptr<VertexBuffer> vertexBuffer,
                         std::unique_ptr<IndexBuffer> indexBuffer,
                         std::unique_ptr<ModelMeshPart> part,
                         Texture2D texture = Texture2D());

        /**
         * @brief Attaches every part from another, independently-loaded SkinnedModelEXT
         * onto this model, taking ownership of its buffers.
         *
         * @note CNAEXT — CNA extension (Task 11.21). Lets a standalone-converted wardrobe
         * piece (`tools/avatar_builder/generate_wardrobe.py` + `convert_avatar.py`, Task
         * 11.14) actually be worn by an already-loaded, running avatar — until this
         * method existed, such a piece could be converted but never attached at
         * runtime. Requires @p other to share this model's exact bone count and index
         * order (guaranteed for any two models built from the same canonical skeleton —
         * `tools/avatar_builder/generate_skeleton.py`'s `BONES` table, via
         * `convert_avatar.py`'s deterministic topological bone sort — since a wardrobe
         * piece's per-vertex joint indices are then already correct for this model's own
         * `ParentBoneIndices`/`BindPoseLocal` arrays with no remapping needed). Does not
         * support attaching a model built from a different skeleton (see @throws).
         *
         * Task 11.4: replace-by-name — any part(s) of this model already sharing a name
         * with an incoming part are removed (via RemovePartEXT, freeing their owned GPU
         * resources) before the incoming part is appended. Without this, re-attaching a
         * same-named replacement (e.g. swapping hairstyles at runtime) silently left both
         * the old and new part rendered simultaneously, since this method previously only
         * ever appended.
         *
         * @param other Another SkinnedModelEXT sharing this model's exact bone layout;
         *              left with no parts of its own afterward (moved-from).
         * @throws System::ArgumentException if @p other's BoneCount differs from this
         *         model's — the one cheap, always-available check that a mismatched
         *         skeleton wasn't passed; it cannot detect a same-count-but-different
         *         skeleton, so callers are still responsible for only attaching pieces
         *         built from the same canonical skeleton as this model.
         */
        void AttachPartEXT(SkinnedModelEXT&& other);

        /**
         * @brief Removes every part with the given name, freeing its owned GPU resources.
         *
         * @note CNAEXT — CNA extension (Task 11.4/11.5). Unlike erasing directly from the
         * public Parts vector (the previous only available approach, and the direct cause
         * of Task 11.5's GPU-resource leak — Parts merely holds non-owning descriptors),
         * this also removes the matching entries from this model's own owned-resource
         * vectors, actually releasing the underlying VertexBuffer/IndexBuffer/
         * ModelMeshPart/Texture2D.
         *
         * @param name Part name to remove (see PartEXT::Name); every matching part is
         *             removed, not just the first.
         */
        void RemovePartEXT(const std::string& name);

        /** @brief Returns the number of owned vertex buffers, for testing (Task 11.5). */
        CNAEXT [[nodiscard]] std::size_t GetOwnedVertexBufferCountForTesting() const { return vertexBuffers_.size(); }
        /** @brief Returns the number of owned index buffers, for testing (Task 11.5). */
        CNAEXT [[nodiscard]] std::size_t GetOwnedIndexBufferCountForTesting() const { return indexBuffers_.size(); }
        /** @brief Returns the number of owned mesh parts, for testing (Task 11.5). */
        CNAEXT [[nodiscard]] std::size_t GetOwnedPartCountForTesting() const { return ownedParts_.size(); }
        /** @brief Returns the number of owned textures, for testing (Task 11.5). */
        CNAEXT [[nodiscard]] std::size_t GetOwnedTextureCountForTesting() const { return textures_.size(); }

    private:
        std::vector<std::unique_ptr<VertexBuffer>> vertexBuffers_;
        std::vector<std::unique_ptr<IndexBuffer>> indexBuffers_;
        std::vector<std::unique_ptr<ModelMeshPart>> ownedParts_;
        std::vector<std::unique_ptr<Texture2D>> textures_;
    };
}
