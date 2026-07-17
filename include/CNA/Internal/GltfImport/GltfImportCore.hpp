// SPDX-License-Identifier: MS-PL
#pragma once

#include "cgltf.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

namespace CNA::Internal::GltfImport
{
    /**
     * @brief One topologically-reordered skeleton bone, extracted from a glTF skin.
     *
     * @note NOXNA — not part of the XNA 4.0 API. Shared parsing core used by both
     * `tools/gltf_to_cnj` (offline `.cnj` export) and `GltfModelTypeReader` (runtime
     * `ContentManager` loading, plan_cnj.md CNB-70/71).
     */
    struct BoneOut
    {
        /** @brief The bone's name, taken from the glTF node (or a generated placeholder). */
        std::string name;
        /** @brief Index of this bone's parent within the same skeleton, or -1 for a root bone. */
        int parentIndex = -1;
        /** @brief Bone-local bind-pose transform (glTF's node-local transform, unit-scaled). */
        Microsoft::Xna::Framework::Matrix bindPoseLocal =
            Microsoft::Xna::Framework::Matrix::getIdentityProperty();
        /** @brief World-space inverse bind matrix (glTF's own authored value, unit-scaled). */
        Microsoft::Xna::Framework::Matrix inverseBindGlobal =
            Microsoft::Xna::Framework::Matrix::getIdentityProperty();
    };

    /** @brief The result of topologically reordering one glTF skin's joints. */
    struct SkeletonResult
    {
        /** @brief Bones in new (parent-before-child) order. */
        std::vector<BoneOut> bones;
        /** @brief Maps an original `skin.joints[]` index to its reordered index. */
        std::vector<int> oldToNew;
        /** @brief Maps a joint's glTF node pointer directly to its reordered index. */
        std::unordered_map<const cgltf_node*, int> nodeToNewIndex;
    };

    /** @brief One animation keyframe: a bone-local translation/rotation/scale at a point in time. */
    struct KeyframeOut
    {
        /** @brief Time of this keyframe, in seconds relative to the start of the clip. */
        double time = 0.0;
        /** @brief Bone-local translation at this keyframe. */
        Microsoft::Xna::Framework::Vector3 translation;
        /** @brief Bone-local rotation at this keyframe. */
        Microsoft::Xna::Framework::Quaternion rotation = Microsoft::Xna::Framework::Quaternion::Identity;
        /** @brief Bone-local scale at this keyframe. */
        Microsoft::Xna::Framework::Vector3 scale{1.0f, 1.0f, 1.0f};
    };

    /** @brief A sequence of keyframes driving one bone within an animation clip. */
    struct TrackOut
    {
        /** @brief Index of the bone this track drives, into the owning `SkeletonResult::bones`. */
        int boneIndex = -1;
        /** @brief Keyframes for this track, in ascending time order. */
        std::vector<KeyframeOut> keys;
    };

    /** @brief One named animation clip: a duration and a set of per-bone keyframe tracks. */
    struct ClipOut
    {
        /** @brief The clip's name, taken from the glTF animation (or a generated placeholder). */
        std::string name;
        /** @brief Total playback duration, in seconds. */
        double duration = 0.0;
        /** @brief Per-bone keyframe tracks. Bones with no track hold their bind pose. */
        std::vector<TrackOut> tracks;
    };

    /** @brief A texture's raw encoded (PNG/JPEG) image bytes, plus its file extension. */
    struct ExtractedImage
    {
        /** @brief The image's raw encoded bytes, exactly as stored/referenced by the glTF file. */
        std::vector<std::uint8_t> bytes;
        /** @brief Lowercase file extension without a leading dot (e.g. "png", "jpg"). */
        std::string extension;
    };

    /** @brief One extracted mesh primitive's vertex/index bytes plus its effect-relevant flags. */
    struct MeshOut
    {
        /** @brief The mesh part's name (from the glTF mesh, or a generated placeholder). */
        std::string name;
        /** @brief Tightly-packed vertex bytes, `vertexBytes.size() / stride` vertices. */
        std::vector<std::uint8_t> vertexBytes;
        /** @brief Byte stride of one vertex (16/20/24/32/52/56 — see CLAUDE.md's stride table). */
        int stride = 32;
        /** @brief Tightly-packed index bytes (16- or 32-bit, per `use32BitIndices`). */
        std::vector<std::uint8_t> indexBytes;
        /** @brief True when `indexBytes` holds 32-bit indices (vertex count exceeded 65535). */
        bool use32BitIndices = false;
        /** @brief True when this mesh has GPU-skinning data (JOINTS_0/WEIGHTS_0 attributes). */
        bool skinned = false;
        /** @brief True when this mesh has a per-vertex COLOR_0 attribute. */
        bool colored = false;
        /** @brief True when this mesh should be imported through DualTextureEffect (CNB-72/73). */
        bool useDualTexture = false;
        /** @brief The material's base-color texture image, or nullptr if none. */
        const cgltf_image* baseColorImage = nullptr;
        /** @brief The material's occlusion texture image, or nullptr if none. */
        const cgltf_image* occlusionImage = nullptr;
        /**
         * @brief Per-target, per-vertex position deltas: morphPositionDeltas[target][vertex],
         * already unit-scaled. Empty if the primitive has no morph targets.
         */
        std::vector<std::vector<Microsoft::Xna::Framework::Vector3>> morphPositionDeltas;
        /**
         * @brief Per-target, per-vertex normal deltas: morphNormalDeltas[target][vertex], or an
         * empty inner vector for a target with no NORMAL delta. Only meaningful for strides with
         * a Normal slot (32/52/56) -- see SetMorphWeightsEXT's own doc comment.
         */
        std::vector<std::vector<Microsoft::Xna::Framework::Vector3>> morphNormalDeltas;
        /**
         * @brief True when this primitive is imported through PbrEffect (stride 48,
         * VertexPositionNormalTangentTexture) instead of BasicEffect/DualTextureEffect --
         * unskinned, uncolored, and has a normal map or metallic-roughness map (see
         * ExtractMesh's own doc comment for the exact eligibility rule).
         */
        bool usePbr = false;
        /** @brief The material's normal map image, or nullptr if none. */
        const cgltf_image* normalImage = nullptr;
        /** @brief The material's metallic-roughness map image, or nullptr if none. */
        const cgltf_image* metallicRoughnessImage = nullptr;
        /** @brief The material's emissive map image, or nullptr if none. */
        const cgltf_image* emissiveImage = nullptr;
        /** @brief The material's metallic factor [0,1] (glTF default 1.0). */
        float metallicFactor = 1.0f;
        /** @brief The material's roughness factor [0,1] (glTF default 1.0). */
        float roughnessFactor = 1.0f;
        /** @brief The material's emissive factor (glTF default black/zero). */
        Microsoft::Xna::Framework::Vector3 emissiveFactor;
    };

    /** @brief A group of glTF mesh instances sharing the same skin (or no skin at all). */
    struct MeshGroup
    {
        /** @brief The shared skin, or nullptr for an unskinned (static) group. */
        const cgltf_skin* skin = nullptr;
        /** @brief The glTF meshes belonging to this group. */
        std::vector<const cgltf_mesh*> meshes;
    };

    /** @brief One keyframe of a morph-weight animation track: a full weight vector at a point in time. */
    struct MorphWeightKeyframeOut
    {
        /** @brief Time of this keyframe, in seconds relative to the start of the clip. */
        double time = 0.0;
        /** @brief Weight for each morph target at this keyframe. */
        std::vector<float> weights;
    };

    /**
     * @brief A morph-weight animation track extracted from a glTF "weights" animation channel.
     *
     * @note NOXNA — not part of the XNA 4.0 API. Independent of ExtractClips' own bone-track
     * extraction: glTF's "weights" channel targets a mesh-instance node directly, not a skeleton
     * joint, so a mesh can have morph weight animation with no skin at all -- see
     * ExtractMorphWeightTrack's own doc comment.
     */
    struct MorphWeightTrackOut
    {
        /** @brief Keyframes for this track, in ascending time order. */
        std::vector<MorphWeightKeyframeOut> keys;
        /** @brief True if the source channel used STEP interpolation (hold last value, no lerp). */
        bool stepInterpolation = false;
    };

    /**
     * @brief Topologically reorders a glTF skin's joints (parent-before-child) and extracts each
     * bone's name, parent index, and bind-pose/inverse-bind-pose transforms.
     *
     * @param skin The glTF skin to process.
     * @param unitScale Uniform scale applied to every bone's translation (see `ScaleTranslation`).
     * @return The reordered skeleton, plus the old-to-new joint index remap.
     */
    SkeletonResult BuildSkeleton(const cgltf_skin* skin, float unitScale);

    /**
     * @brief Extracts every animation in a glTF file as a resampled, per-bone keyframe clip.
     *
     * @param data The parsed glTF file.
     * @param skel The skeleton the clips animate (bone indices are resolved against it).
     * @param unitScale Uniform scale applied to translation channel values/tangents.
     * @param warnings Appended with a human-readable note for each skipped, unsupported channel
     *                 target (e.g. morph target weights).
     * @return One `ClipOut` per glTF animation.
     */
    std::vector<ClipOut> ExtractClips(const cgltf_data* data, const SkeletonResult& skel,
                                       float unitScale, std::vector<std::string>& warnings);

    /**
     * @brief Extracts a glTF image's raw encoded bytes, resolving whichever of the three glTF
     * image-storage methods it uses (embedded bufferView, external file, or base64 data: URI).
     *
     * @param image The glTF image to extract, or nullptr.
     * @param gltfDir The directory containing the source .gltf/.glb file (for external file URIs).
     * @return The extracted bytes, or std::nullopt if @p image is nullptr.
     */
    std::optional<ExtractedImage> ExtractImage(const cgltf_image* image,
                                                const std::filesystem::path& gltfDir);

    /**
     * @brief Returns a primitive's material's base-color texture image, honoring its own
     * TEXCOORD-set selection, or nullptr if the primitive has no material or base color texture.
     */
    const cgltf_image* FindBaseColorImage(const cgltf_primitive& prim);

    /**
     * @brief Returns a primitive's material's occlusion texture image, or nullptr if the
     * primitive has no material or occlusion texture.
     */
    const cgltf_image* FindOcclusionImage(const cgltf_primitive& prim);

    /**
     * @brief Returns a primitive's material's normal map image, or nullptr if the primitive has
     * no material or normal map (plan_cnj.md CNB-59, Phase 13A).
     */
    const cgltf_image* FindNormalImage(const cgltf_primitive& prim);

    /**
     * @brief Returns a primitive's material's metallic-roughness map image, or nullptr if the
     * primitive has no material or metallic-roughness map.
     */
    const cgltf_image* FindMetallicRoughnessImage(const cgltf_primitive& prim);

    /**
     * @brief Returns a primitive's material's emissive map image, or nullptr if the primitive has
     * no material or emissive map.
     */
    const cgltf_image* FindEmissiveImage(const cgltf_primitive& prim);

    /**
     * @brief Extracts one glTF mesh primitive's vertex/index bytes, selecting the vertex stride
     * and effect-relevant flags from its attributes and material (see `MeshOut`).
     *
     * @param prim The glTF primitive to extract.
     * @param name The mesh part's name (used only in error messages and `MeshOut::name`).
     * @param skel The mesh's skeleton (already topologically reordered), or nullptr if unskinned.
     * @param unitScale Uniform scale applied to every vertex position.
     * @return The extracted mesh bytes and flags.
     */
    MeshOut ExtractMesh(const cgltf_primitive& prim, const std::string& name,
                         const SkeletonResult* skel, float unitScale);

    /**
     * @brief Groups every mesh-bearing node reachable from the file's default scene by which skin
     * (if any) it references, so a file combining multiple independent skinned characters (or a
     * mix of skinned + static scenery) produces one group per skin rather than merging them.
     *
     * @param data The parsed glTF file.
     * @return One `MeshGroup` per distinct skin (plus one for unskinned meshes, if any exist).
     */
    std::vector<MeshGroup> CollectMeshGroups(const cgltf_data* data);

    /**
     * @brief Returns a mesh's default morph target weights (its own "weights" array), zero-filled
     * up to @p targetCount if the mesh's own array is shorter or absent.
     *
     * @param mesh The glTF mesh.
     * @param targetCount The primitive's own morph target count (MeshOut::morphPositionDeltas.size()).
     * @return The default weight vector, exactly @p targetCount entries long.
     */
    std::vector<float> GetMeshDefaultWeights(const cgltf_mesh* mesh, std::size_t targetCount);

    /**
     * @brief Extracts a mesh's morph-weight animation track, if one exists.
     *
     * glTF's "weights" animation channel targets the *node* instancing a mesh, not the mesh (or a
     * skeleton joint) directly -- unlike bone channels (see ExtractClips), this works for a mesh
     * with morph targets and no skin at all. A mesh referenced by more than one node resolves to
     * the first node found (a documented simplification for a rare edge case).
     *
     * @param data The parsed glTF file.
     * @param mesh The glTF mesh to find a weight animation channel for.
     * @param targetCount The primitive's own morph target count.
     * @return The extracted track, or std::nullopt if the mesh has no weight animation channel.
     */
    std::optional<MorphWeightTrackOut> ExtractMorphWeightTrack(const cgltf_data* data, const cgltf_mesh* mesh,
                                                                std::size_t targetCount);
}
