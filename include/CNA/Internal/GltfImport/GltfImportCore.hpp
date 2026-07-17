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
    };

    /** @brief A group of glTF mesh instances sharing the same skin (or no skin at all). */
    struct MeshGroup
    {
        /** @brief The shared skin, or nullptr for an unskinned (static) group. */
        const cgltf_skin* skin = nullptr;
        /** @brief The glTF meshes belonging to this group. */
        std::vector<const cgltf_mesh*> meshes;
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
}
