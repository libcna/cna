// SPDX-License-Identifier: MS-PL
#pragma once

#include "cgltf.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

namespace CNA::Internal::GltfImport
{
    /**
     * @brief One topologically-reordered skeleton bone, extracted from a glTF skin.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. Shared parsing core used by both
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
        /**
         * @brief The transform a **root** bone's own local transform composes against; identity
         * for any bone whose parent is inside the skin's joint set.
         *
         * @note CNAEXT — plan_gltf.md GLTF-245/GLTF-247 (Phase 5). glTF's joint matrix is
         * `inverse(globalTransform(meshNode)) * globalTransform(joint) * inverseBindMatrix`, and
         * both leading terms live *above* the joint set: `globalTransform(joint)` includes every
         * scene ancestor -- joints or not, and regardless of `skin.skeleton` -- while
         * `inverse(globalTransform(meshNode))` cancels the skinned mesh node's own placement.
         * Neither can be expressed by a bone-local transform, and folding them into
         * `bindPoseLocal` would be silently undone the moment an animation clip replaced that
         * bone's local transform. Carrying them separately keeps both correct:
         *
         *     world(root) = bindPoseLocal(root) * parentWorldPrefix(root)
         *     world(bone) = bindPoseLocal(bone) * world(parent)
         *
         * so an animated root joint substitutes only its own local transform, exactly as a
         * non-root joint does.
         */
        Microsoft::Xna::Framework::Matrix parentWorldPrefix =
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

    /**
     * @brief Which index space a clip's `TrackOut::boneIndex` values live in (§15.1.2).
     *
     * @note CNAEXT — not part of the XNA 4.0 API. The two spaces are deliberately distinct and
     * must never be silently interchanged: a joint's palette slot has nothing to do with its
     * position in the scene, and a rigid node has no palette slot at all.
     */
    enum class ClipTargetSpace
    {
        /**
         * @brief `paletteIndex` — a slot in one skin's GPU joint palette (`SkeletonResult::bones`).
         *
         * What `SkinningData`, `BlendIndices` and `uBones[]` index. Meaningful only relative to the
         * skin the clip was extracted for.
         */
        JointPalette,
        /**
         * @brief `sceneNodeIndex` — a node's index in `SceneGraphOut::nodes`, and hence its
         * `ModelBone` index, since both loaders mirror the scene graph one-for-one.
         *
         * What rigid (non-joint) node animation drives (`GLTF-293`).
         */
        SceneNode,
    };

    /** @brief A sequence of keyframes driving one bone within an animation clip. */
    struct TrackOut
    {
        /**
         * @brief Index of the bone this track drives, in the owning clip's `targetSpace`.
         *
         * A `JointPalette` clip indexes `SkeletonResult::bones`; a `SceneNode` clip indexes
         * `SceneGraphOut::nodes` (equivalently `Model::Bones`).
         */
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
        /**
         * @brief Which index space this clip's track bone indices are in.
         *
         * @note CNAEXT — not part of the XNA 4.0 API. Defaults to `JointPalette`, which is what
         * every clip was before `GLTF-293`, so an existing consumer keeps its meaning unchanged.
         */
        ClipTargetSpace targetSpace = ClipTargetSpace::JointPalette;
    };

    /** @brief A texture's raw encoded (PNG/JPEG) image bytes, plus its file extension. */
    struct ExtractedImage
    {
        /** @brief The image's raw encoded bytes, exactly as stored/referenced by the glTF file. */
        std::vector<std::uint8_t> bytes;
        /** @brief Lowercase file extension without a leading dot (e.g. "png", "jpg"). */
        std::string extension;
    };

    /**
     * @brief A glTF primitive's declared topology — `mesh.primitive.mode`, specification §3.7.2.1.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. The enumerator values are glTF's own `mode`
     * numbers, so a value round-trips through the file format unchanged. This is deliberately the
     * *source* topology rather than an XNA `PrimitiveType`: three of the seven modes have no XNA
     * equivalent at all, and deciding what each becomes is a separate concern from reading what
     * the file actually declares (plan_gltf.md §10.1).
     */
    enum class PrimitiveTopology
    {
        /** @brief `mode` 0 — an unconnected point per vertex. */
        Points = 0,
        /** @brief `mode` 1 — an independent line segment per index pair. */
        Lines = 1,
        /** @brief `mode` 2 — a connected line strip, closed by a segment back to the first vertex. */
        LineLoop = 2,
        /** @brief `mode` 3 — a connected, open line strip. */
        LineStrip = 3,
        /** @brief `mode` 4 — an independent triangle per index triple. glTF's own default. */
        Triangles = 4,
        /** @brief `mode` 5 — a triangle strip; odd triangles have reversed winding. */
        TriangleStrip = 5,
        /** @brief `mode` 6 — a triangle fan around the first vertex. */
        TriangleFan = 6,
    };

    /** @brief One extracted mesh primitive's vertex/index bytes plus its effect-relevant flags. */
    struct MeshOut
    {
        /** @brief The mesh part's name (from the glTF mesh, or a generated placeholder). */
        std::string name;
        /**
         * @brief The topology `indexBytes` is actually in, after any import-time conversion.
         *
         * Always `Triangles` on a `MeshOut` that `ExtractMesh` returned, and that is a guarantee
         * rather than a coincidence: a triangle strip or fan is converted to a triangle list here
         * (plan_gltf.md §10.1, `GLTF-072`) and every remaining topology is rejected. Compare with
         * @ref sourceTopology to see whether a conversion happened.
         */
        PrimitiveTopology topology = PrimitiveTopology::Triangles;
        /**
         * @brief The topology the source primitive declared (`mesh.primitive.mode`), unconverted.
         *
         * @note CNAEXT — not part of the XNA 4.0 API. Kept distinct from @ref topology so a
         * conversion is visible rather than lossy: `sourceTopology != topology` means the index
         * list was rewritten at import, which is what `GLTF-082` reports and what a consumer
         * needs in order to map a drawn triangle back to the primitive the file authored.
         */
        PrimitiveTopology sourceTopology = PrimitiveTopology::Triangles;
        /** @brief Tightly-packed vertex bytes, `vertexBytes.size() / stride` vertices. */
        std::vector<std::uint8_t> vertexBytes;
        /** @brief Byte stride of one vertex (16/20/24/32/48/52/56/68 — see CLAUDE.md's stride table). */
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
         * VertexPositionNormalTangentTexture, unskinned) or SkinnedPbrEffect (stride 68,
         * VertexPositionNormalTangentTextureSkinned, skinned) instead of BasicEffect/
         * DualTextureEffect/SkinnedEffect -- uncolored, and has a normal map or
         * metallic-roughness map (see ExtractMesh's own doc comment for the exact eligibility
         * rule).
         */
        bool usePbr = false;
        /** @brief The material's normal map image, or nullptr if none. */
        const cgltf_image* normalImage = nullptr;
        /** @brief The material's metallic-roughness map image, or nullptr if none. */
        const cgltf_image* metallicRoughnessImage = nullptr;
        /** @brief The material's emissive map image, or nullptr if none. */
        const cgltf_image* emissiveImage = nullptr;
        /**
         * @brief The material's base colour factor, RGBA (glTF default `(1,1,1,1)`).
         *
         * @note CNAEXT — not part of the XNA 4.0 API. Multiplies the base-colour texture when
         * there is one, and stands alone when there is not (`GLTF-216`/`GLTF-218`). Never read at
         * all before `GLTF-216`: a gold factor-only material rendered as opaque white, which was
         * half of defect D7.
         */
        Microsoft::Xna::Framework::Vector4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
        /** @brief The material's metallic factor [0,1] (glTF default 1.0). */
        float metallicFactor = 1.0f;
        /** @brief The material's roughness factor [0,1] (glTF default 1.0). */
        float roughnessFactor = 1.0f;
        /** @brief The material's emissive factor (glTF default black/zero). */
        Microsoft::Xna::Framework::Vector3 emissiveFactor;
        /**
         * @brief The material's alpha-coverage mode (glTF default `OPAQUE`).
         *
         * @note CNAEXT — not part of the XNA 4.0 API (`GLTF-228`). `MeshOut` had no field for this
         * at all, which was the last part of defect D7: an `alphaMode BLEND` material imported as
         * opaque with nothing anywhere recording that it had asked not to be.
         */
        Microsoft::Xna::Framework::Graphics::AlphaModeEXT alphaMode =
            Microsoft::Xna::Framework::Graphics::AlphaModeEXT::Opaque;
        /** @brief The alpha threshold a `Mask` material is cut at (glTF default 0.5, `GLTF-229`). */
        float alphaCutoff = 0.5f;
        /** @brief Whether the material asks for its back faces to be drawn (`GLTF-231`). */
        bool doubleSided = false;
        /**
         * @brief True when `usePbr` is true and at least one present PBR map (normal,
         * metallic-roughness, emissive, or occlusion) references a different glTF TEXCOORD set
         * than the one actually baked into this primitive's vertex buffer (always the base-color
         * texture's own TEXCOORD set, or TEXCOORD_0 if there is no base-color texture) -- CNA's
         * PbrEffect/SkinnedPbrEffect currently sample every map from a single shared UV channel,
         * so a mismatched map will be sampled with the wrong UV data. `ExtractMesh`'s caller
         * (`gltf_to_cnj.cpp`) surfaces this as a warning rather than silently mis-rendering it;
         * true multi-UV-channel support is tracked as separate future work (plan_cnj.md Phase
         * 14B), not implemented here.
         */
        bool pbrUv2Mismatch = false;
    };

    /**
     * @brief One node of the imported scene graph — the `sceneNodeIndex` identity space.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. plan_gltf.md GLTF-103/GLTF-113 (Phase 5): glTF
     * places a mesh through the node that instantiates it, and a node's world transform is the
     * product of its ancestors' local transforms with its own. Reproducing that graph as CNA
     * `ModelBone`s (rather than baking it into vertex positions) is what preserves mesh instancing,
     * rigid node animation, node-attached cameras/lights, and a hierarchy game code can walk.
     *
     * This is the **scene-node identity space**, and it is deliberately distinct from a skin's own
     * GPU bone-palette index space (see `SkeletonResult::oldToNew`, and plan_gltf.md §15.1.2): the
     * scene graph is ordered by the glTF node graph and must never be reordered to suit a palette.
     */
    struct SceneNodeOut
    {
        /** @brief The node's name, from the glTF node, or a generated placeholder. */
        std::string name;
        /** @brief Index of this node's parent within the same `SceneGraphOut`, or -1 for the root. */
        int parentIndex = -1;
        /** @brief The node's own local transform (glTF `matrix`, or its TRS), in XNA row-vector form. */
        Microsoft::Xna::Framework::Matrix localTransform =
            Microsoft::Xna::Framework::Matrix::getIdentityProperty();
        /** @brief The node's scene-root-relative transform: `local * parentWorld`, in XNA row-vector form. */
        Microsoft::Xna::Framework::Matrix worldTransform =
            Microsoft::Xna::Framework::Matrix::getIdentityProperty();
        /** @brief The source glTF node, or nullptr for the synthetic scene root at index 0. */
        const cgltf_node* node = nullptr;
        /** @brief Index of the source node in `cgltf_data::nodes`, or -1 for the synthetic root. */
        int gltfNodeIndex = -1;
    };

    /**
     * @brief The default scene's node graph, flattened parent-before-child.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. Index 0 is always a synthetic identity root, so a
     * scene with several root nodes still maps onto CNA's single-`Root` `Model` shape without
     * inventing a transform.
     */
    struct SceneGraphOut
    {
        /** @brief Every node, parent-before-child; index 0 is the synthetic identity root. */
        std::vector<SceneNodeOut> nodes;
        /** @brief Maps a glTF node to its index in `nodes`. */
        std::unordered_map<const cgltf_node*, int> indexOfNode;
    };

    /**
     * @brief One placement of one glTF mesh by one glTF node.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. plan_gltf.md GLTF-113. A mesh referenced by
     * several nodes produces one instance per node, each with its own transform — the shape that
     * makes real instancing expressible, which baking world transforms into vertices would destroy.
     */
    struct MeshInstanceOut
    {
        /** @brief The instancing node, or nullptr in the "no scene references any mesh" fallback. */
        const cgltf_node* node = nullptr;
        /** @brief The mesh this node instantiates. */
        const cgltf_mesh* mesh = nullptr;
        /** @brief Index of the instancing node in the owning `SceneGraphOut::nodes` (0 = root). */
        int sceneNodeIndex = 0;
        /** @brief The instancing node's scene-root-relative transform, in XNA row-vector form. */
        Microsoft::Xna::Framework::Matrix worldTransform =
            Microsoft::Xna::Framework::Matrix::getIdentityProperty();
        /**
         * @brief True when the instancing node also references a skin.
         *
         * glTF requires a skinned mesh's own node transform to be ignored — joint transforms alone
         * place the geometry — so a caller building CNA bones must parent a skinned instance to the
         * identity scene root rather than to its own node's bone. Completing that rule (the
         * `inverse(globalTransform(meshNode))` term, and the joint ancestry `BuildSkeleton` still
         * drops) is plan_gltf.md GLTF-245/GLTF-247/GLTF-260, not this type.
         */
        bool skinned = false;
    };

    /** @brief A group of glTF mesh instances sharing the same skin (or no skin at all). */
    struct MeshGroup
    {
        /** @brief The shared skin, or nullptr for an unskinned (static) group. */
        const cgltf_skin* skin = nullptr;
        /** @brief The mesh placements belonging to this group, in glTF node order. */
        std::vector<MeshInstanceOut> instances;
    };

    /**
     * @brief A `KHR_lights_punctual` light, already approximated down to CNA's own
     * `DirectionalLight`-only shape (see `ExtractPunctualLightsEXT`'s own doc comment).
     *
     * @note CNAEXT — not part of the XNA 4.0 API.
     */
    struct LightOut
    {
        /** @brief World-space direction the light travels in, normalized. */
        Microsoft::Xna::Framework::Vector3 direction;
        /** @brief `color * intensity`, clamped to [0,1] per channel. */
        Microsoft::Xna::Framework::Vector3 diffuseColor;
    };

    /** @brief One keyframe of a morph-weight animation track: a full weight vector at a point in time. */
    struct MorphWeightKeyframeOut
    {
        /** @brief Time of this keyframe, in seconds relative to the start of the clip. */
        double time = 0.0;
        /** @brief Weight for each morph target at this keyframe. */
        std::vector<float> weights;
        /**
         * @brief CUBICSPLINE in-tangent, one per morph target (same order as `weights`), or empty
         * when the source channel was not CUBICSPLINE-interpolated.
         */
        std::vector<float> inTangent;
        /**
         * @brief CUBICSPLINE out-tangent, one per morph target (same order as `weights`), or empty
         * when the source channel was not CUBICSPLINE-interpolated.
         */
        std::vector<float> outTangent;
    };

    /**
     * @brief A morph-weight animation track extracted from a glTF "weights" animation channel.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. Independent of ExtractClips' own bone-track
     * extraction: glTF's "weights" channel targets a mesh-instance node directly, not a skeleton
     * joint, so a mesh can have morph weight animation with no skin at all -- see
     * ExtractMorphWeightTrack's own doc comment. CUBICSPLINE tangents are preserved (in
     * MorphWeightKeyframeOut::inTangent/outTangent) rather than baked down at import time, unlike
     * ExtractClips' own bone-channel resampling -- a single "weights" channel has no sibling
     * channel to derive extra union sample points from, so evaluating the real Hermite curve
     * lazily at playback time (see EvaluateMorphWeightsEXT) is both simpler and exact, not a
     * piecewise-linear approximation.
     */
    struct MorphWeightTrackOut
    {
        /** @brief Keyframes for this track, in ascending time order. */
        std::vector<MorphWeightKeyframeOut> keys;
        /** @brief True if the source channel used STEP interpolation (hold last value, no lerp). */
        bool stepInterpolation = false;
        /** @brief True if the source channel used CUBICSPLINE interpolation (real Hermite tangents). */
        bool cubicSpline = false;
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
     * @brief Topologically reorders a glTF skin's joints and resolves the two coordinate spaces the
     * one-argument overload cannot see: the joints' full scene ancestry and the skinned mesh node's
     * own placement (plan_gltf.md GLTF-245/GLTF-247, Phase 5).
     *
     * A joint's global transform includes **every** scene ancestor, whether or not that ancestor is
     * itself a joint and whether or not it lies above `skin.skeleton` — the declared skeleton root
     * is a naming/locating hint, never a traversal stop. Dropping any of that ancestry while
     * retaining the file's own `inverseBindMatrices` is defect D8: the joint matrix ends up
     * multiplied by the inverse of whatever was dropped.
     *
     * The skinned mesh node's transform is separately *cancelled*, not applied: glTF places a
     * skinned mesh entirely through its joints. Both terms are returned on each root bone's
     * @ref BoneOut::parentWorldPrefix rather than folded into its bind pose, so animating a root
     * joint cannot undo them.
     *
     * @param skin The glTF skin to process.
     * @param scene The graph `BuildSceneGraph` produced for the same file.
     * @param meshNodeWorld World transform of the node instancing the skinned mesh, in XNA
     *        row-vector form. Pass the identity when no such node applies.
     * @param unitScale Uniform scale applied to every bone's translation (see `ScaleTranslation`).
     * @return The reordered skeleton, plus the old-to-new joint index remap.
     */
    SkeletonResult BuildSkeleton(const cgltf_skin* skin, const SceneGraphOut& scene,
                                  const Microsoft::Xna::Framework::Matrix& meshNodeWorld,
                                  float unitScale);

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
     * @brief Extracts every animation as a clip whose tracks target **scene nodes**, not joints.
     *
     * This is rigid (non-joint) node animation — a door, a turntable, a clock hand — which was
     * silently dropped before `GLTF-293`: `ExtractClips` resolves every channel against a skin's
     * joint set, so a channel targeting an ordinary mesh node matched nothing and was skipped
     * without a warning, and the offline tool called it only for a skinned group in the first
     * place.
     *
     * The returned clips carry `ClipTargetSpace::SceneNode`, so their `boneIndex` values index
     * `SceneGraphOut::nodes` — which both loaders mirror one-for-one as `Model::Bones`. A channel
     * whose target node is not in the default scene is skipped and reported: it drives nothing
     * that was imported.
     *
     * Joints are deliberately **not** excluded. A node can be both a skin joint and an ordinary
     * scene node, and which of the two clips should drive it is `GLTF-294`'s question, not this
     * function's; silently dropping it here would repeat D6 in the other direction.
     *
     * @note CNAEXT — not part of the XNA 4.0 API.
     *
     * @param data The parsed glTF file.
     * @param scene The default scene's flattened node graph (from `BuildSceneGraph`).
     * @param unitScale Uniform scale applied to translation channel values/tangents.
     * @param warnings Appended with a human-readable note per skipped channel, naming why.
     * @return One `ClipOut` per glTF animation that drives at least one imported scene node.
     */
    std::vector<ClipOut> ExtractSceneNodeClips(const cgltf_data* data, const SceneGraphOut& scene,
                                                float unitScale,
                                                std::vector<std::string>& warnings);

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
     * @brief Decodes an extracted occlusion image, halves every RGB channel (alpha left
     * unchanged), and re-encodes the result as PNG -- the real fix for `DualTextureEffect`'s own
     * occlusion-as-lightmap approximation (plan_cnj.md CNB-72/73).
     *
     * glTF's own occlusion texture convention is "1.0 = fully visible" (correctly consumed as-is
     * by `PbrEffect::OcclusionMap`), but `DualTextureEffect`'s real XNA blend shader
     * (`base.rgb *= 2.0; FragColor = base * texture(uTexture2, vUV) * uDiffuseColor;`) expects a
     * baked lightmap where "0.5 = neutral" -- a byte-for-byte passthrough of a real occlusion
     * texture therefore renders roughly 2x too bright wherever it is not fully occluded. Only call
     * this for an image being used as `DualTextureEffect::Texture2`, never for `PbrEffect::
     * OcclusionMap`, which needs the original, unmodified image.
     *
     * @param image The extracted occlusion image bytes (from ExtractImage), any format
     * stb_image.h can decode (PNG/JPEG/BMP/TGA/...).
     * @return The remapped image, re-encoded as PNG (extension "png"), or std::nullopt if the
     * source bytes could not be decoded.
     */
    std::optional<ExtractedImage> RemapOcclusionImageForDualTextureEXT(const ExtractedImage& image);

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
     * @brief Classifies a primitive's declared `mode` (specification §3.7.2.1).
     *
     * @note CNAEXT — not part of the XNA 4.0 API.
     *
     * @param prim The glTF primitive to classify.
     * @param name The mesh part's name, used only in the error message.
     * @return The declared topology.
     * @throws std::runtime_error if the primitive declares no recognizable mode.
     */
    PrimitiveTopology ClassifyPrimitiveTopology(const cgltf_primitive& prim, const std::string& name);

    /**
     * @brief The specification's own name for a topology, e.g. "TRIANGLE_STRIP".
     *
     * @note CNAEXT — not part of the XNA 4.0 API.
     *
     * @param topology The topology to name.
     * @return The glTF `mode` name, for diagnostics.
     */
    const char* PrimitiveTopologyName(PrimitiveTopology topology);

    /**
     * @brief The glTF `mode` number a topology corresponds to.
     *
     * @note CNAEXT — not part of the XNA 4.0 API.
     *
     * @param topology The topology to convert.
     * @return The `mesh.primitive.mode` value, 0…6.
     */
    int PrimitiveTopologyMode(PrimitiveTopology topology);

    /**
     * @brief Whether CNA's import path can currently carry a topology through to a draw.
     *
     * The three triangle-producing topologies are supported: `Triangles` passes through, and
     * `TriangleStrip` / `TriangleFan` are **converted to a triangle list at import**
     * (`ConvertToTriangleList`). That conversion is deliberately chosen over plumbing new
     * topologies through every renderer — it is provable at L3 and L5, needs no renderer change,
     * and cannot regress an existing renderer (plan_gltf.md §10.1).
     *
     * The line and point topologies are read, classified and **rejected with a named error**
     * rather than reinterpreted. They decode perfectly well; what they lack is a draw path, since
     * every loader still computes a triangle-list primitive count. Giving them one is `GLTF-073`
     * (a real `PrimitiveType` on `ModelMeshPart`) plus `GLTF-078` (a topology-aware primitive
     * count), and for points also `GLTF-077` (whether a point list is CNAEXT-supported or an
     * explicit per-renderer rejection). Importing them before that would move the original defect
     * from the import layer to the draw layer rather than fixing it.
     *
     * @note CNAEXT — not part of the XNA 4.0 API.
     *
     * @param topology The topology to test.
     * @return True when `ExtractMesh` will import a primitive of that topology.
     */
    bool IsPrimitiveTopologySupported(PrimitiveTopology topology);

    /**
     * @brief Rewrites a strip's or fan's index list as an equivalent triangle list (§3.7.2.1).
     *
     * A `Triangles` list is returned unchanged, so this is safe to apply unconditionally to any
     * supported topology. Winding is preserved exactly as the specification defines it: a strip's
     * odd triangles emit `(i+1, i, i+2)` rather than `(i, i+1, i+2)`, so every resulting triangle
     * faces the same way and back-face culling behaves as the author intended. A fan emits
     * `(0, i, i+1)` around its first vertex.
     *
     * An index run too short to describe a single triangle yields an empty list rather than a
     * partial one. A list already in `Triangles` is returned verbatim, trailing partial triple
     * included — what a malformed index count becomes is `GLTF-079`'s decision, not this
     * function's.
     *
     * @note CNAEXT — not part of the XNA 4.0 API.
     *
     * @param indices The source index list, already resolved (a non-indexed primitive's implicit
     * `[0, count)` counts as resolved).
     * @param topology The topology `indices` is in.
     * @return The equivalent triangle-list indices, three per triangle.
     * @throws std::runtime_error if `topology` describes no triangles at all (a point or line
     * topology), which has no triangle-list equivalent and must never be given one.
     */
    std::vector<std::uint32_t> ConvertToTriangleList(const std::vector<std::uint32_t>& indices,
                                                     PrimitiveTopology topology);

    /**
     * @brief How many primitives an index run of a given topology describes (§12.3, `GLTF-078`).
     *
     * `TriangleList` → `n / 3`, `LineList` → `n / 2`, `LineStrip` and `LineLoop` → `n - 1`,
     * `Points` → `n`. `TriangleStrip` and `TriangleFan` are converted to a triangle list at import
     * (`GLTF-072`), so asking for their count here means the caller is holding an unconverted run
     * and is answered as the strip/fan formula `n - 2` rather than being silently divided by three.
     *
     * An index run too short to describe a single primitive yields `0`, never a negative count —
     * `n - 1` and `n - 2` are the two formulas where that matters.
     *
     * This exists because all three loaders independently hardcoded `numIndices / 3`: right for a
     * triangle list, silently wrong for everything else, and stated three times so the three could
     * drift.
     *
     * @note CNAEXT — not part of the XNA 4.0 API.
     *
     * @param topology The topology the index run is in.
     * @param indexCount The number of indices in the run.
     * @return The draw-call primitive count.
     */
    int PrimitiveCountForTopology(PrimitiveTopology topology, std::size_t indexCount);

    /**
     * @brief Parses a topology from its specification name, for reading a serialised `.cnj` part.
     *
     * The inverse of `PrimitiveTopologyName`. An unrecognised or empty name yields `Triangles`,
     * which is what a `.cnj` written before `GLTF-073` means by omitting the field entirely — so an
     * older asset keeps loading with exactly the topology it always had.
     *
     * @note CNAEXT — not part of the XNA 4.0 API.
     *
     * @param name The specification name, e.g. "LINE_STRIP".
     * @return The matching topology, or `Triangles` when the name is unknown.
     */
    PrimitiveTopology PrimitiveTopologyFromName(const std::string& name);

    /**
     * @brief glTF's own spelling of an alpha mode, e.g. "BLEND" (`GLTF-228`).
     *
     * @note CNAEXT — not part of the XNA 4.0 API. The specification's names, so a `.cnj` field is
     * readable against the glTF file it came from without a lookup table.
     *
     * @param mode The alpha mode to name.
     * @return "OPAQUE", "MASK" or "BLEND".
     */
    const char* AlphaModeEXTName(Microsoft::Xna::Framework::Graphics::AlphaModeEXT mode);

    /**
     * @brief Parses an alpha mode from its glTF name; unknown or empty yields `Opaque`.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. `Opaque` is both glTF's default and what a `.cnj`
     * written before `GLTF-228` means by omitting the field, so an older asset is unaffected.
     *
     * @param name The glTF spelling.
     * @return The matching mode, or `Opaque`.
     */
    Microsoft::Xna::Framework::Graphics::AlphaModeEXT AlphaModeEXTFromName(const std::string& name);

    /**
     * @brief The XNA `PrimitiveType` a decoded glTF topology is drawn with.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. Three of the seven glTF modes have no XNA
     * equivalent: `TriangleStrip` and `TriangleFan` never reach a draw because `GLTF-072` converts
     * them to a triangle list at import, and `LineLoop` is converted to a line strip with its
     * closing segment appended (`GLTF-076`). `Points` maps to CNA's own `PointListEXT`, which real
     * XNA 4.0 removed — whether a renderer honours it is `GLTF-077`'s per-renderer question.
     *
     * @param topology The decoded topology.
     * @return The primitive type to draw it with.
     */
    Microsoft::Xna::Framework::Graphics::PrimitiveType PrimitiveTypeForTopology(
        PrimitiveTopology topology);

    /**
     * @brief Extracts one glTF mesh primitive's vertex/index bytes, selecting the vertex stride
     * and effect-relevant flags from its attributes and material (see `MeshOut`).
     *
     * @throws std::runtime_error if the primitive's `mode` is one CNA does not yet support (see
     * `IsPrimitiveTopologySupported`) — never silently reinterpreted as a triangle list.
     *
     * @param data The parsed glTF file (needed to resolve KHR_draco_mesh_compression attribute
     * unique IDs against `data->accessors`' own base pointer — see `FindDracoUniqueId`'s own doc
     * comment; unused for a non-Draco primitive).
     * @param prim The glTF primitive to extract.
     * @param name The mesh part's name (used only in error messages and `MeshOut::name`).
     * @param skel The mesh's skeleton (already topologically reordered), or nullptr if unskinned.
     * @param unitScale Uniform scale applied to every vertex position.
     * @return The extracted mesh bytes and flags.
     */
    MeshOut ExtractMesh(const cgltf_data* data, const cgltf_primitive& prim, const std::string& name,
                         const SkeletonResult* skel, float unitScale);

    /**
     * @brief Flattens the file's default scene into a parent-before-child node list with composed
     * world transforms (plan_gltf.md GLTF-113, Phase 5).
     *
     * Index 0 is always a synthetic identity root named "Root"; every node reachable from the
     * default scene (`data->scene`, or the first scene when that is unset) follows, each preceded
     * by its parent. A node's local transform is its glTF `matrix` when present and its TRS
     * otherwise — the two are mutually exclusive per the specification — converted into XNA's
     * row-vector convention, and its world transform is `local * parentWorld`. A file with no
     * scenes at all yields just the root, matching `CollectMeshGroups`' own fallback.
     *
     * @param data The parsed glTF file.
     * @return The flattened graph, plus a node-pointer lookup into it.
     */
    SceneGraphOut BuildSceneGraph(const cgltf_data* data);

    /**
     * @brief Groups every mesh **placement** reachable from the file's default scene by which skin
     * (if any) it references, so a file combining multiple independent skinned characters (or a
     * mix of skinned + static scenery) produces one group per skin rather than merging them.
     *
     * Each placement is a `MeshInstanceOut` carrying the instancing node and that node's composed
     * world transform, so a mesh instantiated by several nodes yields several distinct instances
     * rather than one anonymous duplicate (plan_gltf.md GLTF-113).
     *
     * @param data The parsed glTF file.
     * @param scene The graph `BuildSceneGraph` produced for the same file, used to resolve each
     * instance's own node index and world transform.
     * @return One `MeshGroup` per distinct skin (plus one for unskinned meshes, if any exist).
     */
    std::vector<MeshGroup> CollectMeshGroups(const cgltf_data* data, const SceneGraphOut& scene);

    /**
     * @brief Convenience overload that builds the scene graph internally.
     *
     * Prefer the two-argument form when the caller also needs the graph itself (both model loaders
     * do, to build their `ModelBone` hierarchies) — this overload exists so call sites that only
     * want the mesh placements do not have to thread a graph they never read.
     *
     * @param data The parsed glTF file.
     * @return One `MeshGroup` per distinct skin (plus one for unskinned meshes, if any exist).
     */
    std::vector<MeshGroup> CollectMeshGroups(const cgltf_data* data);

    /**
     * @brief Extracts up to 3 `KHR_lights_punctual` lights from the file's default scene,
     * approximated as directional lights.
     *
     * No CNA stock effect shader (`BasicEffect`/`SkinnedEffect`/`PbrEffect`/`SkinnedPbrEffect`)
     * supports point or spot lights at all — real XNA predates any such concept, and every one of
     * them hard-caps at exactly 3 `DirectionalLight`s + ambient, matching real XNA's own
     * `BasicEffect`/`SkinnedEffect`. A `directional` light's own world-space -Z axis (glTF's own
     * convention for the direction a light travels) is used directly; a `point`/`spot` light is
     * approximated as a directional light pointing from the light's own world position toward the
     * scene origin — a documented, deliberate approximation, not physically accurate falloff or
     * cone-angle behavior. `color * intensity` is clamped to [0,1] per channel — glTF's own
     * photometric units (lux for directional, candela for point/spot) have no defined mapping onto
     * XNA's own unitless `DiffuseColor` convention, so this is intentionally simple rather than a
     * false claim of photometric correctness. Only the first 3 lights found (in node-array order,
     * restricted to nodes reachable from the default scene) are used; any beyond that are silently
     * dropped — callers wanting to surface that should compare the returned count against
     * `data->lights_count`.
     *
     * @param data The parsed glTF file.
     * @return Up to 3 approximated directional lights, empty if the file has no `KHR_lights_punctual` lights.
     */
    std::vector<LightOut> ExtractPunctualLightsEXT(const cgltf_data* data);

    /**
     * @brief Whether CNA's importer implements the semantics of a glTF extension by name.
     *
     * "Implements" is stricter than "notices". `KHR_materials_unlit` and
     * `KHR_materials_pbrSpecularGlossiness` are both *detected* — they exclude a material from the
     * metallic-roughness path so it cannot be mis-shaded as PBR (`GLTF-215`) — but neither is
     * **implemented**: an unlit material still goes through a lit effect, and the
     * specular-glossiness parameters are dropped. A file that lists either in `extensionsRequired`
     * is asking for something CNA cannot deliver, so this returns false for them.
     *
     * `KHR_draco_mesh_compression` is supported only in a build configured with libdraco; the
     * answer therefore depends on `CNA_DRACO_AVAILABLE` rather than being a fixed list.
     *
     * @note CNAEXT — not part of the XNA 4.0 API.
     *
     * @param extension The extension's glTF name, e.g. "KHR_texture_transform".
     * @return True when the importer honours that extension's semantics.
     */
    bool IsGltfExtensionSupportedEXT(const std::string& extension);

    /**
     * @brief Container-level validation, run once per file before anything is decoded
     * (`GLTF-021` … `GLTF-024`).
     *
     * Three separate checks, in the order a reader must apply them:
     *
     * 1. **`cgltf_validate()`** — every structural constraint whose violation would make decoding
     *    unsafe: an accessor reaching past its `bufferView`, a `bufferView` past its buffer, a
     *    sparse index out of the accessor's own range, attribute counts that disagree within a
     *    primitive, an undefined component or primitive type. cgltf checks nothing outside that
     *    class, which is why failure here is always a **hard rejection** (`GLTF-022`): there is no
     *    "cosmetic" violation in its check set to warn about instead.
     * 2. **`extensionsRequired`** — an entry CNA does not implement is a hard rejection naming the
     *    extension (`GLTF-023`). Importing such a file "successfully" produces geometry the author
     *    explicitly said would be wrong without that extension.
     * 3. **`extensionsUsed`** — an entry CNA does not implement is *reported*, not rejected
     *    (`GLTF-024`): by definition the file is expected to load without it.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. Call after `cgltf_load_buffers`, since the
     * sparse index-bound check needs buffer data to run at all.
     *
     * @param data The parsed glTF file, with buffers already loaded.
     * @param sourceName The file name or asset name, used in diagnostics.
     * @param warnings Appended with one entry per ignored `extensionsUsed` extension.
     * @throws std::runtime_error if validation fails, or a required extension is unsupported.
     */
    void ValidateGltfEXT(const cgltf_data* data, const std::string& sourceName,
                         std::vector<std::string>& warnings);

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
