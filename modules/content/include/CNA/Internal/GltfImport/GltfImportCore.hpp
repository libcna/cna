// SPDX-License-Identifier: MS-PL
#pragma once

#include "cgltf.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
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
        /**
         * @brief The `sceneNodeIndex` of the node `skin.skeleton` names, or -1 when it names none.
         *
         * plan_gltf.md `GLTF-249`. This is a **hint**, recorded so an application can locate and
         * name the rig; it is deliberately **not** consulted anywhere in the transform arithmetic.
         * §15.1.1 is explicit that truncating the ancestry walk at `skin.skeleton` reproduces D8
         * in a new disguise, so the walk above stays driven by the scene graph alone and this
         * field exists to be *reported*, never to bound anything.
         *
         * -1 also when the skin declares a `skeleton` node that is not in the default scene.
         */
        int declaredSkeletonRootNodeIndex = -1;
        /** @brief The declared skeleton root's node name; empty when the skin declares none. */
        std::string declaredSkeletonRootName;
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
    /**
     * @brief The XNA sampler state one glTF `sampler` maps to (plan_gltf.md `GLTF-202`/`GLTF-203`).
     *
     * `cgltf_sampler` had **zero occurrences** in CNA before this: every imported texture was drawn
     * with whatever `SamplerState` the device happened to have, which defaults to `LinearWrap`. For
     * an asset authored `CLAMP_TO_EDGE` with UVs outside `[0,1]` — which `KHR_texture_transform`
     * routinely produces — that is a large, visible error.
     *
     * One of these per texture slot, because glTF attaches a sampler to a *texture*, not to a
     * material: a material may legitimately clamp its base colour and repeat its normal map.
     */
    struct SamplerOut
    {
        /** @brief The XNA filter the glTF min/mag pair maps to. */
        Microsoft::Xna::Framework::Graphics::TextureFilter filter =
            Microsoft::Xna::Framework::Graphics::TextureFilter::Linear;
        /** @brief The U address mode, from `wrapS`. */
        Microsoft::Xna::Framework::Graphics::TextureAddressMode addressU =
            Microsoft::Xna::Framework::Graphics::TextureAddressMode::Wrap;
        /** @brief The V address mode, from `wrapT`. */
        Microsoft::Xna::Framework::Graphics::TextureAddressMode addressV =
            Microsoft::Xna::Framework::Graphics::TextureAddressMode::Wrap;
        /**
         * @brief True when the file declared a sampler; false when these are glTF's own defaults.
         *
         * §3.8.4 makes an absent sampler mean "repeat, auto filter", which is exactly the default
         * above — so the values are the same either way and this records *why*, which is what an
         * import report needs to distinguish "the author chose repeat" from "the author said
         * nothing".
         */
        bool declared = false;
        /**
         * @brief True when the glTF `minFilter` asked for no mipmapping at all (`NEAREST`/`LINEAR`).
         *
         * XNA's `TextureFilter` has no "base level only" value — that is a property of the texture
         * having one mip level, not of the sampler — so the mip mode carried in @ref filter is
         * arbitrary for these two and `MipPoint` is chosen as the least-blending option. This flag
         * is what makes the approximation visible instead of implied (`GLTF-204`), and it becomes
         * observable the day `GLTF-206` starts generating mip levels.
         */
        bool minFilterHasNoMipStage = false;
    };

    /**
     * @brief The material texture slots CNA imports, and the index space `MeshOut::samplers` uses.
     *
     * Deliberately an enum rather than bare indices: a sampler array indexed by an untyped `int`
     * is exactly the kind of thing that silently acquires an off-by-one when a slot is added.
     */
    enum class TextureSlotEXT
    {
        /** @brief `pbrMetallicRoughness.baseColorTexture`. */
        BaseColor = 0,
        /** @brief `normalTexture`. */
        Normal = 1,
        /** @brief `pbrMetallicRoughness.metallicRoughnessTexture`. */
        MetallicRoughness = 2,
        /** @brief `emissiveTexture`. */
        Emissive = 3,
        /** @brief `occlusionTexture`. */
        Occlusion = 4,
    };

    /**
     * @brief Maps a glTF sampler's four fields onto XNA sampler state (plan_gltf.md §14.2).
     *
     * Takes raw glTF enum values rather than a `cgltf_sampler` so the whole table is testable
     * without a file. A zero value means "undefined", which §3.8.4 says to treat as the
     * implementation's own choice; CNA reads it as glTF's stated default of repeat + linear.
     *
     * XNA turns out to cover glTF's filter space **exactly**: its nine `TextureFilter` values
     * express all eight min×mag×mip combinations, so the four mixed cases need no approximation at
     * all. The one real approximation is the mip stage of a non-mipmapped `minFilter` — see
     * @ref SamplerOut::minFilterHasNoMipStage.
     *
     * @param magFilter glTF `magFilter` (9728 NEAREST, 9729 LINEAR, or 0 for undefined).
     * @param minFilter glTF `minFilter` (9728, 9729, 9984…9987, or 0 for undefined).
     * @param wrapS glTF `wrapS` (10497 REPEAT, 33071 CLAMP_TO_EDGE, 33648 MIRRORED_REPEAT, or 0).
     * @param wrapT glTF `wrapT`, same values as @p wrapS.
     * @return The mapped state. @ref SamplerOut::declared is left false; the caller sets it.
     */
    [[nodiscard]] SamplerOut MapGltfSamplerEXT(int magFilter, int minFilter, int wrapS, int wrapT);

    /**
     * @brief One glTF camera as instanced by a scene node (plan_gltf.md `GLTF-317`).
     *
     * `cgltf_camera` had **zero occurrences** in CNA: a file's cameras were dropped entirely, so an
     * asset that shipped its own framing had no way to express it and every viewer had to invent
     * one. glTF §3.10 puts the projection on the camera and the placement on the node, so both are
     * carried here rather than pre-combined -- an application animating the camera node needs them
     * apart.
     */
    struct CameraOut
    {
        /** @brief The camera's name, or the node's when the camera is unnamed; may be empty. */
        std::string name;
        /** @brief The instancing node's `sceneNodeIndex` (§15.1.2), so it indexes `Model::Bones`. */
        int sceneNodeIndex = -1;
        /** @brief True for a perspective camera, false for an orthographic one. */
        bool perspective = true;
        /** @brief Perspective vertical field of view, in radians. */
        float yfov = 0.0f;
        /**
         * @brief Perspective aspect ratio, or 0 when the file declares none.
         *
         * §3.10.3 says an undefined `aspectRatio` means "use the viewport's", which is a runtime
         * value the importer cannot know -- so it is carried as 0 rather than guessed.
         */
        float aspectRatio = 0.0f;
        /** @brief Orthographic half-width (`xmag`); the full width is twice this. */
        float xmag = 0.0f;
        /** @brief Orthographic half-height (`ymag`). */
        float ymag = 0.0f;
        /** @brief Near clip distance; required for both camera types. */
        float znear = 0.0f;
        /**
         * @brief Far clip distance, or 0 when a perspective camera declares none.
         *
         * An absent `zfar` means an **infinite** projection (§3.10.3). XNA has no such overload,
         * which is `GLTF-319`'s subject; 0 is the sentinel because a real `zfar` must be positive
         * and greater than `znear`.
         */
        float zfar = 0.0f;
        /** @brief The instancing node's world transform, already unit-scaled. */
        Microsoft::Xna::Framework::Matrix worldTransform =
            Microsoft::Xna::Framework::Matrix::getIdentityProperty();
    };

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
        /**
         * @brief The glTF material this primitive uses, or nullptr when it declares none.
         *
         * @note CNAEXT — not part of the XNA 4.0 API. plan_gltf.md `GLTF-238`: carried so a
         * loader can give two primitives of the *same* material one shared `Effect` instead of an
         * identical copy each. It is the material's identity that matters, not its contents, which
         * is why this is the pointer rather than a decoded record — two materials with identical
         * factors are still two materials, and a file that repeats one is the case worth sharing.
         */
        const cgltf_material* materialEXT = nullptr;
        /**
         * @brief Names the material model this primitive could not be imported with, or empty.
         *
         * plan_gltf.md `GLTF-241`. A primitive with `COLOR_0` **and** a metallic-roughness material
         * cannot be imported as PBR: no CNA vertex layout carries a colour alongside a tangent, and
         * no PBR shader reads a colour stream. It is imported through `BasicEffect` with its vertex
         * colours intact, and the material's factors and maps are **not applied**.
         *
         * That is a downgrade, and the whole point of this field is that it is no longer a *silent*
         * one: the loaders log it by name, and a test can assert it happened rather than inferring
         * it from a stride. Empty for every primitive that was imported as the file asked.
         */
        std::string unsupportedMaterialModelEXT;
        /**
         * @brief True when the chosen vertex layout has no Normal slot and an authored NORMAL was
         * therefore discarded.
         *
         * plan_gltf.md `GLTF-241`. Strides 24 and 20 carry no normal, so a primitive that lands on
         * one loses its authored normals entirely and cannot be lit at all -- not merely lit
         * without a PBR material. It is the same limitation one layer deeper, recorded here rather
         * than left for a reader to deduce from a stride.
         */
        bool droppedNormalForStrideEXT = false;
        /**
         * @brief Number of trailing indices dropped because they did not complete a primitive.
         *
         * plan_gltf.md `GLTF-079`. §3.7.2.1 requires the index count to be a whole number of
         * primitives for the declared `mode` — a multiple of 3 for `TRIANGLES`, of 2 for `LINES`,
         * and at least 3 (respectively 2) for a strip, fan or loop. `cgltf_validate` does not
         * check it, and neither reading the remainder as a further primitive (which runs off the
         * end of the index run) nor dropping it silently is acceptable: the first is wrong, the
         * second is undiagnosable. The incomplete tail is dropped and counted here, so the import
         * is deterministic and the loaders can say what was discarded. Zero for a well-formed
         * primitive.
         */
        std::size_t droppedIncompleteIndicesEXT = 0;
        /**
         * @brief True when the file authors `TANGENT` but the chosen vertex layout has no tangent
         * slot, so it was discarded.
         *
         * plan_gltf.md `GLTF-086`. Only strides 48 and 68 carry a tangent, and those are exactly
         * the PBR layouts — so an authored tangent basis on any other primitive is dropped. It
         * cannot be *carried*: there is nowhere to put it. `GLTF-086`'s acceptance allows the other
         * outcome, reported, and this is it. Worth reporting rather than shrugging at, because a
         * file that went to the trouble of authoring tangents did so for a reason.
         */
        bool droppedTangentForStrideEXT = false;
        /**
         * @brief One entry per material map whose image CNA could not read, naming the map and why.
         *
         * plan_gltf.md `GLTF-200` / `GLTF-350`. A texture can carry its pixels in a format CNA has
         * no decoder for — `KHR_texture_basisu` (KTX2/Basis) and `EXT_texture_webp` are the two the
         * ecosystem actually ships. Both are designed so a file may *also* declare a plain PNG/JPEG
         * `source` as a fallback, in which case CNA uses it and nothing is lost; both are also
         * routinely authored with **no** fallback, and then the map simply has no image CNA can
         * read.
         *
         * Until this field existed that map vanished without a word: the finder returned `nullptr`,
         * every downstream check read "no texture on this slot", and the model drew untextured as
         * though the author had never assigned one. Naming the map and the extension is the whole
         * difference between an unsupported feature and a bug report.
         *
         * Each entry reads like `"base color: KHR_texture_basisu"`. Empty for every primitive whose
         * maps were all readable.
         *
         * @note If the extension is listed in `extensionsRequired`, `ValidateGltfEXT` rejects the
         * file outright and nothing reaches here — this covers exactly the `extensionsUsed` case,
         * where the file claims it still loads without the extension.
         */
        std::vector<std::string> unsupportedTextureSourcesEXT;
        /**
         * @brief Maps whose `KHR_texture_transform` could not be applied (`GLTF-184`/`GLTF-336`).
         *
         * The transform is **baked into the UV data** at import, and there is exactly one UV
         * channel to bake it into — so exactly one transform can be honoured, and CNA honours the
         * base colour's. Any other map declaring a *different* transform is sampled with the base
         * colour's texture coordinates instead of its own, which for a tiled normal map or a
         * rotated emissive mask is a visible mis-registration.
         *
         * Distinct from `GLTF-181`'s single-UV-channel limit, and the difference decides how
         * expensive the real fix is: a second UV *channel* needs a vertex attribute, so a new
         * stride, and the stride it needs is already taken. A per-map *transform* needs only a
         * uniform — the shared UV transformed in the shader before each sample — so no ABI and no
         * `VertexDeclaration` change, just a shader and uniform change in every renderer with a
         * PBR program.
         *
         * Empty for the ordinary case: no transforms at all, or every map sharing one.
         */
        std::vector<std::string> unbakedTextureTransformsEXT;
        /**
         * @brief How many `JOINTS_n`/`WEIGHTS_n` sets beyond set 0 the primitive authored.
         *
         * plan_gltf.md `GLTF-095` / `GLTF-257`. glTF allows any number of influence sets, four
         * joints each; XNA's `BlendIndices`/`BlendWeight` carry exactly four. Every set past the
         * first is therefore dropped, and until this field existed it was dropped without a word —
         * a mesh authored for eight influences imported as though the author had asked for four.
         *
         * Zero for the overwhelming majority of files, which author one set.
         */
        std::size_t extraInfluenceSetsEXT = 0;
        /**
         * @brief True when the primitive authored no `NORMAL` and CNA computed one (`GLTF-173`).
         *
         * §3.7.2.1 requires a reader to calculate flat normals for a primitive without `NORMAL`.
         * CNA used to write a fabricated `(0,0,1)` for every vertex instead — a surface facing +Z
         * regardless of where it actually points — so a model lit from any other direction was
         * uniformly and silently wrong. This says the normals in `vertexBytes` are derived rather
         * than authored, which is worth knowing when comparing against another renderer.
         */
        bool generatedNormalsEXT = false;
        /**
         * @brief How many generated normals are averaged rather than truly flat (`GLTF-173`).
         *
         * Flat shading gives a vertex one normal *per face*, so a vertex shared between faces of
         * different orientation must be duplicated once per face. Duplication changes the vertex
         * count and every per-vertex stream including morph deltas, and this extraction produces
         * one vertex array — so such a vertex instead receives the area-weighted average of its
         * faces' normals, and is counted here.
         *
         * Zero for the case that matters most: a faceted mesh whose author already split its edges
         * gets exact flat normals, because no vertex is shared across differing faces.
         */
        std::size_t smoothedNormalVertexCountEXT = 0;
        /**
         * @brief The largest share of a single vertex's total influence that set truncation
         * discarded, in [0,1].
         *
         * plan_gltf.md `GLTF-095`. The count alone does not say whether the truncation matters: a
         * fifth influence weighted 0.002 is exporter noise, and one weighted 0.4 is a visibly
         * different pose. This is the number that tells them apart, measured before `GLTF-256`'s
         * renormalisation runs.
         *
         * Note what renormalisation then does: the retained four weights are rescaled to sum to 1,
         * so a truncated vertex is influenced by *four* joints rather than eight — it is **not**
         * dragged toward the origin by the missing weight. The degradation is a coarser skin, not a
         * collapsed one.
         */
        float worstDroppedInfluenceEXT = 0.0f;
        /**
         * @brief How many vertices had their joint weights renormalised (plan_gltf.md `GLTF-256`).
         *
         * §3.7.3.3 requires a vertex's weights to sum to 1, but a file is not guaranteed to honour
         * it. The failure is not cosmetic: the skin equation is a weighted sum of joint matrices,
         * so weights summing to 0.75 apply 0.75 of the vertex's transform — which for a joint near
         * the origin drags the vertex three-quarters of the way toward it. That is the audit's
         * **H12**, an independent collapse mechanism.
         *
         * CNA renormalises rather than refusing, because a slightly-off sum is what quantised
         * exporters routinely emit; this count is what keeps that from being silent. Vertices
         * within 1e-4 of 1 are not counted — that is float error, not a malformed file.
         */
        std::size_t renormalisedWeightVertexCountEXT = 0;
        /**
         * @brief How many vertices had joint weights summing to zero, and were left alone.
         *
         * plan_gltf.md `GLTF-256`. An all-zero weight set means the vertex is unweighted, and
         * `0/0` is not a normalisation — so these are counted and reported rather than "fixed" into
         * an arbitrary joint.
         */
        std::size_t zeroWeightVertexCountEXT = 0;
        /**
         * @brief The largest `|sum - 1|` seen before renormalisation; 0 when nothing was off.
         *
         * plan_gltf.md `GLTF-256`. Carried because the *size* of the deviation is what separates a
         * quantised exporter (a few 1e-3) from a genuinely broken file, and a count alone cannot
         * say which one a caller is looking at.
         */
        float worstWeightSumDeviationEXT = 0.0f;
        /** @brief True when this mesh should be imported through DualTextureEffect (CNB-72/73). */
        bool useDualTexture = false;
        /** @brief The material's base-color texture image, or nullptr if none. */
        const cgltf_image* baseColorImage = nullptr;
        /** @brief The material's occlusion texture image, or nullptr if none. */
        const cgltf_image* occlusionImage = nullptr;
        /**
         * @brief `normalTexture.scale` — how far the normal map perturbs the surface (§3.9.3).
         *
         * plan_gltf.md `GLTF-224`. Never read before: a material that dialled its normal map down
         * to a subtle 0.2 got the full-strength 1.0 instead, which is not a subtle difference.
         * Scales the sampled tangent-space normal's x and y before the basis is applied, so 0
         * flattens the map to the geometric normal and values above 1 exaggerate it — the
         * specification puts no upper bound on it.
         */
        float normalScale = 1.0f;
        /**
         * @brief `occlusionTexture.strength` — how far the occlusion map darkens (§3.9.3).
         *
         * plan_gltf.md `GLTF-225`. Never read before. Applied as
         * `1 + strength * (sampled - 1)`, the specification's own formula: at `strength = 0` the
         * result is 1 (no occlusion at all) whatever the map says, and at 1 it is the map.
         */
        float occlusionStrength = 1.0f;
        /**
         * @brief The sampler state of each material texture, by slot (plan_gltf.md `GLTF-202`).
         *
         * Indexed by @ref TextureSlotEXT. Per slot rather than per material because glTF attaches a
         * sampler to a *texture*: a material may legitimately clamp its base colour and repeat its
         * normal map, and one shared value could not express that.
         *
         * A slot whose texture is absent still carries glTF's default (repeat + linear) with
         * `declared` false, so a consumer never has to special-case a missing entry.
         */
        std::array<SamplerOut, 5> samplers{};
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
         * @brief Per-target, per-vertex tangent deltas, or an empty inner vector for a target with
         * no TANGENT delta. Only meaningful for strides with a Tangent slot (48/68).
         *
         * plan_gltf.md `GLTF-279`. glTF morph TANGENT deltas are `VEC3`, not `VEC4`: the
         * handedness `w` is a property of the UV winding and is **not** morphed, so it is carried
         * on the base vertex and left alone by the blend. Storing these as `Vector3` is what makes
         * that impossible to get wrong.
         */
        std::vector<std::vector<Microsoft::Xna::Framework::Vector3>> morphTangentDeltas;
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
         * @brief The material's `KHR_materials_transmission` factor, or 0 (`GLTF-339`).
         *
         * Read for any material. 1 is fully transmissive — clear glass — and 0 is the default,
         * meaning the extension is absent or explicitly neutral.
         */
        float transmissionFactorEXT = 0.0f;
        /**
         * @brief True when the transmission was approximated as alpha blending (`GLTF-339`).
         *
         * The approximation: `alpha = 1 - transmissionFactor`, multiplied into whatever alpha the
         * material already asked for, with `alphaMode` forced to `Blend`. Set only when the factor
         * is above 0, so a material that declares the extension neutrally is untouched.
         *
         * **Explicitly not physical**, and the ways it is wrong are worth naming rather than
         * discovering: there is no refraction, so nothing behind the surface is displaced; the blur
         * roughness would cause does not happen; alpha blending *darkens* what is behind a tinted
         * surface where transmission would *tint* it; and specular reflection, which a transmissive
         * surface keeps at full strength, fades out with the alpha. It is still far closer than the
         * fully opaque result CNA produced before, and it is reported every time.
         */
        bool transmissionApproximatedEXT = false;
        /**
         * @brief True when the material also declares a transmission **texture** (`GLTF-339`).
         *
         * Only the scalar factor is approximated; a per-texel transmission map has nowhere to go in
         * an `alphaMode`/`baseColorFactor` approximation, so a material that varies its
         * transmission across the surface is flattened to one value. Reported separately because
         * that is a materially worse approximation than the uniform case.
         */
        bool transmissionHasTextureEXT = false;
        /**
         * @brief The PBR maps that reference a different TEXCOORD set than the one baked, by name.
         *
         * plan_gltf.md `GLTF-181`/`GLTF-188`. `PbrEffect`/`SkinnedPbrEffect` sample every map from
         * **one** shared UV channel — the base-colour texture's own TEXCOORD set, or `TEXCOORD_0`
         * when there is none — so a map that selects a different set is sampled with the wrong
         * coordinates. This lists exactly which ones, so the report can name them instead of
         * saying only that something somewhere disagrees.
         *
         * `GLTF-188` narrowed it from a bare `bool` for two reasons. A single flag could not say
         * *which* map to go and look at, which is the only actionable part of the warning; and it
         * counted maps CNA never samples — an undecodable texture source (`GLTF-200`) is not
         * rendered from the wrong UV set, it is not rendered at all, and warning about its UVs
         * pointed at the wrong problem.
         *
         * Empty for a material whose maps agree, which is nearly all of them. Carrying a second UV
         * channel (which would make this list obsolete) is `GLTF-182`, a new vertex stride.
         */
        std::vector<std::string> uvSetMismatchedMapsEXT;
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
        /**
         * @brief True when this placement's composed world transform mirrors the geometry.
         *
         * plan_gltf.md `GLTF-116`/`GLTF-117`. The determinant of the world 3×3 is negative, so
         * §3.7.4 requires the triangle winding to be reversed for the primitive's front faces to
         * stay front-facing. The property belongs to the **composed** transform, never to the
         * instancing node's own scale: an odd number of mirroring ancestors mirrors, an even
         * number does not.
         *
         * It is recorded per instance rather than applied to the index buffer, because vertex
         * positions stay mesh-local (`GLTF-103` Option A) — one mesh may be instanced by both a
         * mirrored and an unmirrored node, and those two draws share one index buffer. Reversing
         * it at import would fix one placement by breaking the other. Applying it is a per-draw
         * `RasterizerState::CullMode` decision, the same boundary `GLTF-231` drew for
         * `doubleSided`.
         */
        bool mirroredEXT = false;
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
     * @brief What one file's node graph turned into (plan_gltf.md `GLTF-145`).
     *
     * @note CNAEXT — not part of the XNA 4.0 API, and deliberately **internal**: `GLTF-034`'s
     * programmatically reachable import report is deferred by the `GLTF-025` gate
     * (`docs/gltf-api-change-review.md` §2.3) for want of a consumer, so this is the same
     * information collected where it is computed and logged, rather than a second public surface
     * invented ahead of a caller for it.
     *
     * Every field answers a question that is otherwise only answerable by re-reading the file: how
     * much of it arrived, how deep it was, and whether its meshes are shared.
     */
    struct NodeGraphReportEXT
    {
        /** @brief Nodes imported from the default scene, excluding the synthetic root. */
        int nodeCount = 0;
        /** @brief Mesh placements produced — one per (node, mesh) pair, so instancing counts twice. */
        int meshInstanceCount = 0;
        /** @brief Distinct glTF meshes referenced by those placements. */
        int distinctMeshCount = 0;
        /** @brief Meshes placed by more than one node, i.e. genuinely instanced. */
        int sharedMeshCount = 0;
        /** @brief Longest root-to-leaf chain, counting the synthetic root as depth 0. */
        int maxDepth = 0;
        /** @brief Nodes that instance a camera. */
        int cameraNodeCount = 0;
        /** @brief Nodes that instance a `KHR_lights_punctual` light. */
        int lightNodeCount = 0;
        /** @brief Nodes carrying `EXT_mesh_gpu_instancing`, whose extra instances are not imported. */
        int gpuInstancedNodeCount = 0;
    };

    /**
     * @brief What one primitive's morph targets turned into (plan_gltf.md `GLTF-291`).
     *
     * @note CNAEXT — not part of the XNA 4.0 API, and internal for the same reason
     * @ref NodeGraphReportEXT is: `GLTF-034`'s public report is deferred by the `GLTF-025` gate.
     *
     * A morph target may carry any subset of `POSITION`, `NORMAL` and `TANGENT` deltas (§3.7.2.2),
     * and a target missing one is not an error — it simply does not move that stream. But a
     * *normal-mapped* surface whose targets carry positions and no tangents deforms with a
     * rest-pose tangent basis, which lights wrongly and looks like a material bug, so the counts
     * are worth having rather than inferring from a silently unchanged buffer.
     */
    struct MorphReportEXT
    {
        /** @brief Morph targets the primitive declares. */
        int targetCount = 0;
        /** @brief Targets carrying no `POSITION` deltas. */
        int targetsWithoutPositions = 0;
        /** @brief Targets carrying no `NORMAL` deltas. */
        int targetsWithoutNormals = 0;
        /** @brief Targets carrying no `TANGENT` deltas. */
        int targetsWithoutTangents = 0;
        /** @brief True when the mesh's own default weights are not all zero, so the rest pose is morphed. */
        bool hasNonZeroDefaultWeights = false;
    };

    /**
     * @brief Summarises one extracted primitive's morph targets and its applied default weights.
     *
     * @note CNAEXT — not part of the XNA 4.0 API.
     *
     * @param mesh The extracted primitive.
     * @param defaultWeights The default weights the loader is about to apply.
     * @return The counts described by @ref MorphReportEXT.
     */
    MorphReportEXT BuildMorphReportEXT(const MeshOut& mesh,
                                       const std::vector<float>& defaultWeights);

    /**
     * @brief Summarises an already-built scene graph and its mesh placements.
     *
     * @note CNAEXT — not part of the XNA 4.0 API.
     *
     * @param scene The graph `BuildSceneGraph` produced.
     * @param groups The placements `CollectMeshGroups` produced from the same graph.
     * @return The counts described by @ref NodeGraphReportEXT.
     */
    NodeGraphReportEXT BuildNodeGraphReportEXT(const SceneGraphOut& scene,
                                               const std::vector<MeshGroup>& groups);

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

    /**
     * @brief What `KHR_lights_punctual` import lost or approximated on one file (`GLTF-326`).
     *
     * XNA's stock effects light with **three directional lights** and nothing else. A glTF file may
     * declare any number of lights, of three kinds, with ranges and cone angles — so importing one
     * is a lossy operation by construction, and every entry here is a place that loss happens.
     * None of it was previously visible: a scene lit by six point lights imported as three
     * directionals aimed at the origin and said nothing at all.
     *
     * Every count is zero for a file already inside XNA's lighting model: at most three
     * directional lights whose `color * intensity` is in gamut.
     */
    struct LightReportEXT
    {
        /** @brief Lights in the default scene beyond the three XNA can bind. */
        std::size_t droppedLightCount = 0;
        /** @brief Point lights approximated as directional lights aimed at the scene origin. */
        std::size_t approximatedPointLightCount = 0;
        /** @brief Spot lights approximated the same way, additionally losing their cone entirely. */
        std::size_t approximatedSpotLightCount = 0;
        /**
         * @brief Lights whose `color * intensity` exceeded 1 on some channel and was clamped.
         *
         * glTF intensity is photometric and unbounded — lux for a directional light, candela for
         * the other two — while `DirectionalLight::DiffuseColor` is a [0,1] colour. An intensity of
         * 683 is an ordinary authored value and clamps to white, which is not a bug but is
         * absolutely something an author comparing renders deserves to be told.
         */
        std::size_t clampedIntensityLightCount = 0;
        /** @brief The largest pre-clamp channel value seen, or 0 when nothing was clamped. */
        float worstPreClampChannelEXT = 0.0f;
        /** @brief True when any of the above is non-zero. */
        [[nodiscard]] bool AnythingLost() const
        {
            return droppedLightCount > 0 || approximatedPointLightCount > 0 ||
                   approximatedSpotLightCount > 0 || clampedIntensityLightCount > 0;
        }
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
     * @brief True for the three modes whose primitives are triangles (§3.7.2.1).
     *
     * `TRIANGLES`, `TRIANGLE_STRIP` and `TRIANGLE_FAN`; false for the points and the three line
     * modes. Exported rather than file-local because two rules now depend on the same partition:
     * strip/fan conversion (`GLTF-072`) and the refusal of a Draco primitive that declares a mode
     * Draco's triangle-only encoder cannot mean (`GLTF-080`).
     *
     * @param topology The classified topology.
     * @return True when the mode's primitives are triangles.
     */
    bool ProducesTriangles(PrimitiveTopology topology);

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
     * @brief `ExtractPunctualLightsEXT`, additionally reporting what it lost (`GLTF-326`).
     *
     * The extraction is identical — this overload exists so a caller can *see* the approximation
     * rather than infer it. Prefer it on any path that can surface a diagnostic.
     *
     * @note CNAEXT — not part of the XNA 4.0 API.
     *
     * @param data The parsed glTF file.
     * @param report Filled with the dropped, approximated and clamped counts.
     * @return At most three directional lights, in scene-node order.
     */
    std::vector<LightOut> ExtractPunctualLightsEXT(const cgltf_data* data, LightReportEXT& report);

    /**
     * @brief Every camera the default scene instances, in scene-node order (`GLTF-317`).
     *
     * A camera on a node outside the default scene is not imported, on the same rule that governs
     * meshes: §3.5 makes the default scene the thing being rendered.
     *
     * @param data The parsed glTF file.
     * @param scene The flattened scene graph, for node placement and index identity.
     * @param unitScale Scale applied to the node world transform's translation, as elsewhere.
     * @return One record per camera-bearing node, in scene-node order.
     */
    [[nodiscard]] std::vector<CameraOut> ExtractCamerasEXT(
        const cgltf_data* data, const SceneGraphOut& scene, float unitScale);

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
     * @brief Resolves one external glTF URI against the asset's own directory, refusing anything
     * that escapes it (`GLTF-032` / `GLTF-198`).
     *
     * A glTF file names its external buffers and images by relative URI, and the only sane reading
     * of "relative" is *relative to the asset*. Nothing in the format stops an author -- or an
     * attacker who can get a `.gltf` opened -- from writing `../../../../etc/passwd`, and joining
     * that onto the asset directory resolves it happily. The refusal is deliberately not a warning:
     * a file asking for something outside its own directory is not a file with a cosmetic problem.
     *
     * Four separate rejections, because they fail for four different reasons and a caller reading
     * the message deserves to know which:
     *
     * 1. **A URI with a scheme** (`http:`, `file:`, …). CNA resolves relative file paths and
     *    `data:` only; a network URI silently treated as a file name would look like a missing
     *    file, which is a confusing way to say "unsupported".
     * 2. **An absolute path**, which by definition ignores the asset directory entirely.
     * 3. **A traversal that escapes**, checked lexically -- `a/../../b` escapes even though no
     *    single component looks suspicious.
     * 4. **A symlink that escapes**, checked again after resolving the existing prefix, because
     *    lexical normalisation cannot see through a link.
     *
     * Containment is compared component by component, never as a string prefix: `/asset-evil` must
     * not count as inside `/asset`.
     *
     * @note CNAEXT — not part of the XNA 4.0 API.
     *
     * @param gltfDir The directory holding the `.gltf` file.
     * @param uri The raw, still percent-encoded URI as authored in the file.
     * @param what What the URI names, used in diagnostics, e.g. "buffer" or "image".
     * @return The resolved path, guaranteed to be inside @p gltfDir.
     * @throws std::runtime_error if the URI is unsupported, absolute, or escapes @p gltfDir.
     */
    /**
     * @brief Cross-checks every accessor's decoded values against its own declared `min`/`max`
     * (`GLTF-061`).
     *
     * §3.6.2 makes `min` and `max` **required** on a `POSITION` accessor and optional elsewhere, and
     * they are the one piece of redundancy the format gives a reader: the author states the bounds,
     * and a decoder that produces values outside them has decoded something other than what was
     * written. Nothing in CNA read them, which is why `D4` — a sparse index accessor decoding to
     * all zeros — could collapse a quad to a point with every layer reporting success.
     *
     * A **warning**, not a rejection, and the asymmetry is deliberate. A file whose declared bounds
     * are merely stale is common and harmless; a decoder producing values outside them is a serious
     * signal, but the values themselves may still be exactly what the file contains. Refusing would
     * turn a diagnostic into a load failure for assets that render correctly today.
     *
     * Only `FLOAT` accessors are checked. An integer accessor's bounds are exact by construction,
     * and a normalized one's declared bounds are in raw units while the decode produces unit-range
     * values — comparing those would report every normalized accessor in every file.
     *
     * @note CNAEXT — not part of the XNA 4.0 API.
     *
     * @param data The parsed glTF file, with buffers loaded.
     * @param warnings Appended with one entry per accessor whose decoded values leave its bounds,
     *                 naming the accessor, the component, and both numbers.
     */
    void CrossCheckAccessorBoundsEXT(const cgltf_data* data, std::vector<std::string>& warnings);

    std::filesystem::path ResolveExternalUriEXT(const std::filesystem::path& gltfDir,
                                                const std::string& uri, const char* what);

    /**
     * @brief Applies `ResolveExternalUriEXT` to every external URI a parsed file declares
     * (`GLTF-032` / `GLTF-198`).
     *
     * Buffers are read by `cgltf_load_buffers` itself, which resolves them the same way CNA would
     * and offers no hook to veto one, so containment has to be decided **before** that call --
     * hence a sweep over the parsed-but-not-yet-loaded file rather than a check at each read site.
     * Images are checked here too so a traversal is refused up front rather than at the moment a
     * texture happens to be needed.
     *
     * `data:` URIs and absent URIs (a GLB's own `BIN` chunk, a bufferView-backed image) carry no
     * path and are skipped.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. Call after `cgltf_parse_file` and **before**
     * `cgltf_load_buffers`.
     *
     * @param data The parsed glTF file.
     * @param gltfDir The directory holding the `.gltf` file.
     * @throws std::runtime_error naming the offending URI if any of them escapes @p gltfDir.
     */
    void ValidateExternalUriContainmentEXT(const cgltf_data* data,
                                           const std::filesystem::path& gltfDir);

    /**
     * @brief Returns a mesh's default morph target weights (its own "weights" array), zero-filled
     * up to @p targetCount if the mesh's own array is shorter or absent.
     *
     * @param mesh The glTF mesh.
     * @param targetCount The primitive's own morph target count (MeshOut::morphPositionDeltas.size()).
     * @return The default weight vector, exactly @p targetCount entries long.
     */
    /**
     * @brief The morph weights a mesh instance starts at (plan_gltf.md `GLTF-281`).
     *
     * §3.7.2.2 gives the instancing **node** the final say: `node.weights` *overrides*
     * `mesh.weights` rather than merging with it, so a node declaring `[1,0]` for a mesh whose own
     * weights are `[0,1]` starts at `[1,0]` and not at `[1,1]`. `node.weights` was read by nobody
     * before this, so a mesh instanced by several nodes wore every node's expression at once.
     *
     * @param mesh The mesh whose targets are being weighted.
     * @param targetCount Number of morph targets; the result always has this length.
     * @param instancingNode The node instancing @p mesh, or nullptr when the caller has none.
     * @return One weight per target, zero-filled beyond whichever array supplied them.
     */
    std::vector<float> GetMeshDefaultWeights(const cgltf_mesh* mesh, std::size_t targetCount,
                                              const cgltf_node* instancingNode = nullptr);

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
