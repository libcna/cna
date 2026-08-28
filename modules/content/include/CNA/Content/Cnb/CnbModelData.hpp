// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"

namespace CNA::Content::Cnb
{
    /**
     * @brief The neutral, fully decoded description of a compiled `.cnb` `Model`
     *        (plans/plan_cnb.md `CNBF-070`).
     *
     * This is the seam between the three pieces of the Model pipeline, and it exists so that none
     * of them has to know about the others' representations:
     *
     * ```text
     *   .cnj + its .bin sidecars  --(compiler front end)-->  CnbModelData
     *   CnbModelData              --(CnbModelCodec)------->  .cnb bytes
     *   .cnb bytes                --(CnbModelCodec)------->  CnbModelData
     *   CnbModelData              --(ContentManager)------>  Graphics::Model
     * ```
     *
     * Everything here is plain data: no pointers into a source file, no GPU objects, no
     * `GraphicsDevice`. That is what lets the whole encode/decode half be unit-tested with no
     * display, no renderer and no device at all.
     *
     * External assets -- textures, and an effect named by asset path -- are held as *logical asset
     * names*, not embedded bytes. A texture shared by a hundred models stays one shared asset that
     * `ContentManager` loads once; embedding a copy per model would defeat its cache and bloat
     * every file (plans/plan_cnb.md decision `D8`).
     */

    /** @brief One node of a compiled model's scene graph. */
    struct CnbModelBone
    {
        /** @brief The bone's name, as `Model::Bones[...]` will expose it. */
        std::string name;

        /** @brief Index of this bone's parent, or -1 for the root. */
        std::int32_t parent = -1;

        /** @brief The bone-local transform, in XNA row-major field order `M11`..`M44`. */
        std::array<float, 16> transform{{1.0f, 0.0f, 0.0f, 0.0f,
                                          0.0f, 1.0f, 0.0f, 0.0f,
                                          0.0f, 0.0f, 1.0f, 0.0f,
                                          0.0f, 0.0f, 0.0f, 1.0f}};
    };

    /** @brief Which effect a compiled mesh part is drawn with. */
    enum class CnbEffectKind : std::uint32_t
    {
        /** @brief `Microsoft::Xna::Framework::Graphics::BasicEffect`. */
        BasicEffect = 0,
        /** @brief `Microsoft::Xna::Framework::Graphics::SkinnedEffect`. */
        SkinnedEffect = 1,
        /** @brief `Microsoft::Xna::Framework::Graphics::DualTextureEffect`. */
        DualTextureEffect = 2,
        /** @brief `Microsoft::Xna::Framework::Graphics::PbrEffect`. */
        PbrEffect = 3,
        /** @brief `Microsoft::Xna::Framework::Graphics::SkinnedPbrEffect`. */
        SkinnedPbrEffect = 4,
        /** @brief An `Effect` asset named by CnbModelPart::externalEffect. */
        External = 5,
    };

    /** @brief Highest CnbEffectKind enumerator this build defines. */
    inline constexpr std::uint32_t CnbMaxEffectKind = 5u;

    /** @brief One texture slot's `KHR_texture_transform`-shaped UV transform. */
    struct CnbTextureTransform
    {
        /** @brief Translation applied after scale and rotation. */
        float offsetX = 0.0f;
        /** @brief Translation applied after scale and rotation. */
        float offsetY = 0.0f;
        /** @brief Per-axis scale, applied first. */
        float scaleX = 1.0f;
        /** @brief Per-axis scale, applied first. */
        float scaleY = 1.0f;
        /** @brief Counter-clockwise rotation in radians, applied after scale. */
        float rotation = 0.0f;
    };

    /** @brief One texture slot's sampler state. */
    struct CnbSamplerState
    {
        /** @brief `Microsoft::Xna::Framework::Graphics::TextureFilter`, as its numeric value. */
        std::uint32_t filter = 0u;
        /** @brief `Microsoft::Xna::Framework::Graphics::TextureAddressMode` for U. */
        std::uint32_t addressU = 0u;
        /** @brief `Microsoft::Xna::Framework::Graphics::TextureAddressMode` for V. */
        std::uint32_t addressV = 0u;
        /**
         * @brief Whether the source asset declared this sampler, rather than these being defaults.
         *
         * The values are identical either way, so this records *why* -- which is what a source
         * that distinguishes "the author chose repeat" from "the author said nothing" needs.
         */
        bool declared = false;
    };

    /** @brief Number of texture slots a compiled material carries state for. */
    inline constexpr std::size_t CnbTextureSlotCount = 7u;

    /**
     * @brief A compiled mesh part's complete material state.
     *
     * Two different orderings meet here, and they are not the same list:
     *
     * - The eight texture *name* fields below are CNA's own effect slots, which include
     *   `DualTextureEffect`'s second layer -- a slot glTF has no counterpart for.
     * - @ref textureCoordinateSets, @ref textureTransforms and @ref samplers are seven-element
     *   arrays in the importer's own `TextureSlotEXT` order: base colour, normal,
     *   metallic-roughness, occlusion, emissive, specular, specular colour.
     */
    struct CnbMaterial
    {
        /** @brief Logical asset name of the base-colour texture, or empty. */
        std::string baseColorTexture;
        /** @brief Logical asset name of the second (`DualTextureEffect`) texture, or empty. */
        std::string texture2;
        /** @brief Logical asset name of the normal map, or empty. */
        std::string normalMap;
        /** @brief Logical asset name of the metallic-roughness map, or empty. */
        std::string metallicRoughnessMap;
        /** @brief Logical asset name of the emissive map, or empty. */
        std::string emissiveMap;
        /** @brief Logical asset name of the occlusion map, or empty. */
        std::string occlusionMap;
        /** @brief Logical asset name of the specular map, or empty. */
        std::string specularMap;
        /** @brief Logical asset name of the specular-colour map, or empty. */
        std::string specularColorMap;

        /** @brief `baseColorFactor`, RGBA. */
        std::array<float, 4> baseColorFactor{{1.0f, 1.0f, 1.0f, 1.0f}};
        /** @brief `emissiveFactor`, RGB. */
        std::array<float, 3> emissiveFactor{{0.0f, 0.0f, 0.0f}};
        /** @brief `KHR_materials_specular.specularColorFactor`, linear RGB. */
        std::array<float, 3> specularColorFactor{{1.0f, 1.0f, 1.0f}};

        /** @brief `pbrMetallicRoughness.metallicFactor`. */
        float metallicFactor = 1.0f;
        /** @brief `pbrMetallicRoughness.roughnessFactor`. */
        float roughnessFactor = 1.0f;
        /** @brief `KHR_materials_ior.ior`. */
        float ior = 1.5f;
        /** @brief `KHR_materials_specular.specularFactor`. */
        float specularFactor = 1.0f;
        /** @brief `normalTexture.scale`. */
        float normalScale = 1.0f;
        /** @brief `occlusionTexture.strength`. */
        float occlusionStrength = 1.0f;
        /** @brief `alphaCutoff`. */
        float alphaCutoff = 0.5f;

        /** @brief `Microsoft::Xna::Framework::Graphics::AlphaModeEXT`, as its numeric value. */
        std::uint32_t alphaMode = 0u;
        /** @brief `doubleSided`. */
        bool doubleSided = false;

        /** @brief Texture-coordinate attribute index (0 or 1) sampled by each slot. */
        std::array<std::uint8_t, CnbTextureSlotCount> textureCoordinateSets{};
        /** @brief One UV transform per slot. */
        std::array<CnbTextureTransform, CnbTextureSlotCount> textureTransforms{};
        /** @brief One sampler state per slot. */
        std::array<CnbSamplerState, CnbTextureSlotCount> samplers{};
    };

    /** @brief One morph target's per-vertex deltas. Each vector is either empty or `3 * vertexCount` long. */
    struct CnbMorphTarget
    {
        /** @brief Position deltas, XYZ per vertex. */
        std::vector<float> positionDeltas;
        /** @brief Normal deltas, XYZ per vertex. */
        std::vector<float> normalDeltas;
        /** @brief Tangent deltas, XYZ per vertex. */
        std::vector<float> tangentDeltas;
    };

    /** @brief One keyframe of a morph weight animation track. */
    struct CnbMorphWeightKey
    {
        /** @brief Time of this key, in seconds. */
        double timeSeconds = 0.0;
        /** @brief One weight per morph target. */
        std::vector<float> weights;
        /** @brief Cubic-spline in-tangents, or empty. */
        std::vector<float> inTangent;
        /** @brief Cubic-spline out-tangents, or empty. */
        std::vector<float> outTangent;
    };

    /** @brief A compiled mesh part's morph-target data. */
    struct CnbMorphData
    {
        /** @brief Vertex count each non-empty delta stream covers. */
        std::uint32_t vertexCount = 0u;
        /** @brief Whether the blend must recompute flat normals from the morphed positions. */
        bool recomputeFlatNormals = false;
        /** @brief One entry per morph target. */
        std::vector<CnbMorphTarget> targets;
        /** @brief Default blend weights, one per target. */
        std::vector<float> weights;
        /** @brief Whether the weight track uses step interpolation. */
        bool weightTrackStepInterpolation = false;
        /** @brief Whether the weight track is cubic-spline interpolated. */
        bool weightTrackCubicSpline = false;
        /** @brief The weight animation track's keys, or empty when there is no track. */
        std::vector<CnbMorphWeightKey> weightTrackKeys;
    };

    /** @brief One compiled renderable part: geometry, material and optional morph data. */
    struct CnbModelPart
    {
        /** @brief The part's name, for diagnostics and bone naming on a hierarchy-less model. */
        std::string name;

        /** @brief Bytes per vertex in @ref vertexBytes. */
        std::uint32_t vertexStride = 0u;
        /** @brief Number of vertices; `vertexStride * vertexCount` must equal `vertexBytes.size()`. */
        std::uint32_t vertexCount = 0u;
        /** @brief Number of indices; `indexElementSize * indexCount` must equal `indexBytes.size()`. */
        std::uint32_t indexCount = 0u;
        /**
         * @brief Bytes per index: 2 or 4.
         *
         * Declared rather than inferred. The `.cnj` pipeline derives it from the vertex count
         * (32-bit above 65535, matching XNA's stock processor), which means a truncated sidecar
         * silently decodes as a shorter mesh; storing it removes that guess.
         */
        std::uint32_t indexElementSize = 2u;
        /**
         * @brief `CNA::Internal::GltfImport::PrimitiveTopology`, as its numeric value.
         *
         * Defaults to 4 (`Triangles`), which is also glTF's own default `mode`.
         */
        std::uint32_t primitiveTopology = 4u;
        /** @brief Number of primitives the index buffer describes, for the chosen topology. */
        std::uint32_t primitiveCount = 0u;

        /** @brief Which effect this part draws with. */
        CnbEffectKind effectKind = CnbEffectKind::BasicEffect;
        /** @brief Logical asset name of the `Effect`, when @ref effectKind is `External`. */
        std::string externalEffect;

        /** @brief Whether the effect samples the part's per-vertex colour. */
        bool vertexColorEnabled = false;
        /** @brief Whether the material is `KHR_materials_unlit`. */
        bool unlit = false;

        /** @brief The part's material state. */
        CnbMaterial material;

        /** @brief Raw interleaved vertex bytes, in the layout @ref vertexStride implies. */
        std::vector<std::uint8_t> vertexBytes;
        /** @brief Raw index bytes, `indexElementSize` bytes per index, little-endian. */
        std::vector<std::uint8_t> indexBytes;

        /** @brief Morph-target data, when the part has any. */
        std::optional<CnbMorphData> morph;
    };

    /** @brief One `ModelMesh`: a named group of parts sharing a parent bone. */
    struct CnbModelMesh
    {
        /** @brief The mesh's name. */
        std::string name;
        /** @brief Index into CnbModelData::bones, or -1 when the model carries no hierarchy. */
        std::int32_t parentBone = -1;
        /** @brief Indices into CnbModelData::parts, in draw order. */
        std::vector<std::uint32_t> partIndices;
    };

    /** @brief A compiled skinning skeleton: the flat arrays `SkinningData` consumes. */
    struct CnbModelSkeleton
    {
        /** @brief Parent index per joint, parent-before-child. */
        std::vector<std::int32_t> hierarchy;
        /** @brief Joint-local bind pose per joint, XNA row-major. */
        std::vector<std::array<float, 16>> bindPose;
        /** @brief Inverse global bind pose per joint, XNA row-major. */
        std::vector<std::array<float, 16>> inverseBindPose;
        /**
         * @brief Per-joint scene-ancestry prefix, or empty when the source did not carry one.
         *
         * The `.skeleton.bin` sidecar signalled this block's presence by whether any bytes were
         * left over, which made "deliberately absent" and "file truncated" the same observation.
         * The compiled form states it, so the two cases are distinguishable.
         */
        std::vector<std::array<float, 16>> rootPrefix;
    };

    /** @brief A named animation clip embedded in a compiled model. */
    struct CnbModelAnimation
    {
        /** @brief The clip's name, as `SkinningData::AnimationClips` will key it. */
        std::string name;
        /** @brief The clip itself. */
        Microsoft::Xna::Framework::Graphics::AnimationClipEXT clip;
    };

    /** @brief One `KHR_lights_punctual` light, already reduced to XNA's directional form. */
    struct CnbModelLight
    {
        /** @brief World-space direction the light travels in. */
        std::array<float, 3> direction{{0.0f, 0.0f, -1.0f}};
        /** @brief Diffuse colour, clamped to `[0,1]` per channel. */
        std::array<float, 3> diffuseColor{{1.0f, 1.0f, 1.0f}};
    };

    /** @brief Everything a compiled `.cnb` `Model` holds. */
    struct CnbModelData
    {
        /**
         * @brief The scene graph, parent-before-child, entry 0 the root.
         *
         * Empty for a model compiled from a `cnjVersion` 1 document, which carried no hierarchy;
         * the runtime then reproduces that document's own "one child bone per mesh" shape.
         */
        std::vector<CnbModelBone> bones;

        /** @brief Every renderable part, in the source document's own order. */
        std::vector<CnbModelPart> parts;

        /** @brief Part groupings, in the source document's own order. */
        std::vector<CnbModelMesh> meshes;

        /** @brief The skinning skeleton, when the model has one. */
        std::optional<CnbModelSkeleton> skeleton;

        /** @brief Animation clips, embedded rather than referenced. */
        std::vector<CnbModelAnimation> animations;

        /** @brief Punctual lights applied to every part's effect. */
        std::vector<CnbModelLight> lights;

        /**
         * @brief Whether this model's materials were authored under glTF's lighting conventions.
         *
         * A model imported from glTF expects the importer's own lighting rig to be applied to
         * every effect -- including the "no light was declared, so light it by default" fallback
         * that rig implements. A hand-authored model expects XNA's own defaults instead, where
         * `BasicEffect` starts unlit. The two are visibly different (the same colours come out
         * dimmer under the glTF policy), so which one applies is a property of the content and
         * must travel with it rather than being guessed from the presence of lights.
         */
        bool appliesGltfLightingPolicy = false;

        /**
         * @brief Whether the source document carried a real scene-node hierarchy.
         *
         * Equivalent to `bones.size() > 1`, but stated rather than inferred, because the runtime
         * behaviour it selects (attach meshes to their named bone, versus give every mesh its own
         * child of the root) is a real semantic fork.
         */
        bool hasBoneHierarchy = false;
    };
}
