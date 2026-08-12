// SPDX-License-Identifier: MS-PL
#pragma once

// plan_gltf.md GLTF-008: the L6 rung of the numerical oracle ladder (plan_gltf.md §7.1) -- the
// effect parameters a draw actually binds.
//
// TEST SCOPE ONLY. Nothing here is CNA public API, CNAEXT surface, or part of
// CNA::Internal::GltfImport. It is compiled into CnaTests alone, and no translation unit under
// modules/*/src may call it. See GltfOracleEXT.hpp for the full scope rules.
//
// WHAT THIS LAYER MEASURES, AND WHAT IT DOES NOT
//
// L3 says what the importer understood the file to mean; L5 says which bytes it packed. Neither
// says whether those facts survive the trip to the shader. A material can decode perfectly at L3
// and still render wrong because nothing assigned its factors to an effect -- which is exactly how
// D7 stayed invisible until the audit. L6 closes that gap by capturing, per drawn part, the
// parameter block the renderer would receive.
//
// The capture calls `Effect::FillGpuDrawParams()` -- the *same* virtual call
// `GraphicsDevice::DrawIndexedPrimitives` makes on the real draw path, on the same effect
// instance, after `Model::Draw` has bound the matrices. What it deliberately does not exercise is
// `GraphicsDevice`'s own additions to the block after that call (vertex stream bindings,
// `vertexStart`, `startIndex`): those describe the buffers, not the material, and none of them is
// a plan_gltf.md §21.1 quantity. Everything §21.1 lists as "Assert at L6" is here.
//
// A few §21.1 rows are effect state that has no `GpuDrawParams` field yet -- the alpha mode,
// cutoff and double-sidedness `GLTF-228`/`GLTF-229`/`GLTF-231` added, which
// `docs/gltf-api-change-review.md` §1.3/§1.4 deliberately left *carried* rather than *applied*.
// Those are captured from the effect's own CNAEXT properties and recorded next to the block, so
// the day one of them starts reaching the GPU the capture shows both sides and the difference
// between "carried" and "bound" stays visible instead of being quietly conflated.

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Matrix.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Model;
}

namespace CnaTest::GltfOracle
{
    /** @brief A 4x4 matrix in the flat column-major layout every renderer uploads. */
    using ColumnMajor4x4 = std::array<float, 16>;

    /** @brief A 3x3 matrix in the flat column-major layout `uNormalMatrix` is uploaded in. */
    using ColumnMajor3x3 = std::array<float, 9>;

    /**
     * @brief The effect parameters one `ModelMeshPart` would bind for a draw (oracle layer L6).
     *
     * One record per part actually drawn, in `Model::Draw`'s own mesh-then-part order. Fields are
     * grouped by plan_gltf.md §21.1 row so a failure can name the contract line it broke.
     */
    struct DrawParamsDump
    {
        // --- identity ------------------------------------------------------------------------

        /** @brief The owning mesh's index in `Model::Meshes`. */
        std::size_t meshIndex = 0;
        /** @brief The owning mesh's name. */
        std::string meshName;
        /** @brief The part's index within its mesh. */
        std::size_t partIndex = 0;
        /** @brief The scene-node bone index the mesh is parented to (`ModelMesh::ParentBone`). */
        int parentBoneIndex = 0;
        /** @brief The bound effect's fully-qualified .NET type name, e.g. "…Graphics.PbrEffect". */
        std::string effectTypeName;

        // --- §21.1 "World / View / Projection" ------------------------------------------------

        /** @brief `IEffectMatrices::World` as `Model::Draw` bound it, column-major. */
        ColumnMajor4x4 world{};
        /** @brief `IEffectMatrices::View` as `Model::Draw` bound it, column-major. */
        ColumnMajor4x4 view{};
        /** @brief `IEffectMatrices::Projection` as `Model::Draw` bound it, column-major. */
        ColumnMajor4x4 projection{};
        /** @brief `GpuDrawParams::worldColMajor` -- the world matrix as the renderer receives it. */
        ColumnMajor4x4 worldColMajor{};

        // --- §21.1 "Normal matrix" -------------------------------------------------------------

        /**
         * @brief `uNormalMatrix` as a renderer derives it from @ref worldColMajor.
         *
         * Computed with the identical cofactor/inverse-determinant expression
         * `EasyGLRenderer::BindDrawParams()` uses, so this is the number that would be uploaded,
         * not a second opinion about what it ought to be. `GLTF-267` supplies the second opinion
         * separately by comparing against `transpose(inverse(world3x3))` built from XNA's own
         * `Matrix::Invert`.
         */
        ColumnMajor3x3 normalMatrix{};

        // --- §21.1 "Base colour factor" ---------------------------------------------------------

        /** @brief `GpuDrawParams::diffuseColor` -- RGB base colour with alpha in `[3]`. */
        std::array<float, 4> diffuseColor{};

        // --- §21.1 "Metallic / roughness" and "Emissive" ----------------------------------------

        /** @brief `GpuDrawParams::pbrMetallicFactor`. */
        float metallicFactor = 0.0f;
        /** @brief `GpuDrawParams::pbrRoughnessFactor`. */
        float roughnessFactor = 0.0f;

        // --- §21.1 colour space (plan_gltf.md GLTF-210/GLTF-212) ---------------------------------

        /** @brief `GpuDrawParams::pbrNormalScale` (glTF `normalTexture.scale`). */
        float normalScale = 0.0f;
        /** @brief `GpuDrawParams::pbrOcclusionStrength` (glTF `occlusionTexture.strength`). */
        float occlusionStrength = 0.0f;
        /** @brief `GpuDrawParams::pbrBaseColorTextureIsSrgb`. */
        bool baseColorTextureIsSrgb = false;
        /** @brief `GpuDrawParams::pbrEmissiveTextureIsSrgb`. */
        bool emissiveTextureIsSrgb = false;
        /** @brief `GpuDrawParams::pbrEncodeOutputToSrgb`. */
        bool encodeOutputToSrgb = false;
        /** @brief `GpuDrawParams::emissiveColor`. */
        std::array<float, 3> emissiveColor{};

        // --- §21.1 texture rows: which map is bound, not what it contains ------------------------

        /** @brief True when a base-colour texture is bound to unit 0. */
        bool hasBaseColorMap = false;
        /** @brief True when a normal map is bound (`GpuDrawParams::pbrNormalMap`). */
        bool hasNormalMap = false;
        /** @brief True when a metallic-roughness map is bound. Channel semantics are L3/L7. */
        bool hasMetallicRoughnessMap = false;
        /** @brief True when an occlusion map is bound. The `.r` channel rule is L7. */
        bool hasOcclusionMap = false;
        /** @brief True when an emissive map is bound. */
        bool hasEmissiveMap = false;

        // --- §21.1 "Bone palette" and "Influences per vertex" -------------------------------------

        /** @brief `GpuDrawParams::boneCount` -- 0 for an unskinned draw. */
        int boneCount = 0;
        /** @brief The first @ref boneCount entries of `GpuDrawParams::boneTransforms`. */
        std::vector<ColumnMajor4x4> boneTransforms;
        /** @brief `GpuDrawParams::weightsPerVertex`. */
        int weightsPerVertex = 0;

        // --- §21.1 "Alpha mode / cutoff" ----------------------------------------------------------

        /**
         * @brief `GpuDrawParams::alphaTest` (x=refVal, y=tolerance, z=passWeight, w=failWeight).
         *
         * The channel the alpha mode would have to reach to be *applied*. `{0,0,1,1}` is the
         * never-discard default, which is what a PBR draw still binds today.
         */
        std::array<float, 4> alphaTest{};
        /**
         * @brief The effect's carried `AlphaModeEXT` as a glTF spelling, or empty when it has none.
         *
         * "OPAQUE" / "MASK" / "BLEND". Non-empty only for `PbrEffect`/`SkinnedPbrEffect`.
         */
        std::string alphaMode;
        /** @brief The effect's carried alpha cutoff; meaningful only for `alphaMode == "MASK"`. */
        float alphaCutoff = 0.0f;
        /** @brief The effect's carried double-sidedness. Applying it is `RasterizerState`, i.e. L7. */
        bool doubleSided = false;
        /** @brief True when the bound effect carries alpha state at all (a PBR effect). */
        bool carriesAlphaState = false;

        // --- shader-variant selection and the draw itself -------------------------------------------

        /** @brief `GpuDrawParams::pbr` -- the renderer selects the metallic-roughness shader. */
        bool pbr = false;
        /** @brief `GpuDrawParams::skinned` -- the renderer selects the skinning shader. */
        bool skinned = false;
        /** @brief `GpuDrawParams::lightingEnabled`. */
        bool lightingEnabled = false;
        /**
         * @brief The three directional lights the lit shaders read, direction then diffuse colour.
         *
         * plan_gltf.md `GLTF-376`. XNA's stock effects light with exactly three directional lights,
         * and a **disabled** one is expressed as a zero diffuse colour rather than by a flag — the
         * shader adds all three unconditionally, so zero is what makes the addition a no-op.
         * Capturing them is what lets `GLTF-325`'s import-side approximation be checked against
         * what the GPU is actually handed, rather than only against `LightOut`.
         */
        std::array<std::array<float, 3>, 3> lightDirections{};
        /** @brief The three lights' diffuse colours, zero for a light that is disabled. */
        std::array<std::array<float, 3>, 3> lightDiffuseColors{};
        /** @brief `GpuDrawParams::textureEnabled`. */
        bool textureEnabled = false;
        /** @brief `GpuDrawParams::vertexColorEnabled`. */
        bool vertexColorEnabled = false;
        /** @brief `GpuDrawParams::ambientColor` -- the ambient term the lit shaders add. */
        std::array<float, 3> ambientColor{};
        /** @brief `ModelMeshPart::PrimitiveTypeEXT` as an XNA enum name, e.g. "TriangleList". */
        std::string primitiveType;
        /** @brief `ModelMeshPart::PrimitiveCount`. */
        int primitiveCount = 0;
    };

    /**
     * @brief Captures the L6 parameter block for every drawable part of @p model.
     *
     * Runs the standard XNA draw sequence: `Model::Draw` binds `absoluteBoneTransform * world`,
     * @p view and @p projection onto each mesh's effects, then each part's effect is asked for its
     * `GpuDrawParams` exactly as `GraphicsDevice` would ask. A part with no effect, or with a
     * non-positive primitive count, is skipped -- `ModelMesh::Draw` skips it too.
     *
     * @param model The imported model. Not const: binding matrices mutates its effects, which is
     *              what a real frame does.
     * @param world The world matrix the application would pass to `Model::Draw`.
     * @param view The view matrix.
     * @param projection The projection matrix.
     * @return One record per drawn part, in mesh-then-part order.
     */
    [[nodiscard]] std::vector<DrawParamsDump> CaptureDrawParamsEXT(
        Microsoft::Xna::Framework::Graphics::Model& model,
        const Microsoft::Xna::Framework::Matrix& world,
        const Microsoft::Xna::Framework::Matrix& view,
        const Microsoft::Xna::Framework::Matrix& projection);

    /**
     * @brief Pushes a skinned model's current skin transforms onto its parts' skinned effects.
     *
     * The bone palette is game code's job in XNA -- `AnimationPlayer::GetSkinTransforms()` into
     * `SkinnedEffect::SetBoneTransforms()`, every frame -- so nothing in CNA does it for you and a
     * capture taken without this call legitimately reports `boneCount == 0`. `GLTF-263` calls it
     * first, then asserts the captured palette equals the transforms it pushed.
     *
     * @param model The model whose parts carry `SkinnedPbrEffect`s.
     * @param skinTransforms The palette, in `paletteIndex` order (plan_gltf.md §15.1.2).
     * @return The number of effects the palette was pushed onto.
     */
    std::size_t ApplyBonePaletteEXT(Microsoft::Xna::Framework::Graphics::Model& model,
                                     const std::vector<Microsoft::Xna::Framework::Matrix>& skinTransforms);

    /**
     * @brief `transpose(inverse(m3x3))` of a column-major 4x4's upper-left 3x3.
     *
     * The renderer-side derivation, reproduced exactly: cofactor matrix scaled by `1/det`, with a
     * singular matrix yielding all zeroes rather than infinities.
     *
     * @param worldColMajor A column-major 4x4.
     * @return The normal matrix, column-major 3x3.
     */
    [[nodiscard]] ColumnMajor3x3 NormalMatrixFromWorldEXT(const ColumnMajor4x4& worldColMajor);

    /** @brief Stable JSON serialisation of a `DrawParamsDump`, for diagnostics. */
    [[nodiscard]] std::string ToJson(const DrawParamsDump& dump);
}
