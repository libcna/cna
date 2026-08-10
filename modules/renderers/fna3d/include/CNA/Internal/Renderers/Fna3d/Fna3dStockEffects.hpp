// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Fna3d/Fna3dApi.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <string>

namespace CNA::Internal::Renderers::Fna3d
{
    using Matrix = Microsoft::Xna::Framework::Matrix;

    /**
     * @brief Which XNA stock effect a draw is being expressed through.
     *
     * FNA3D can only execute Direct3D 9 Effect Framework binaries, so every draw this renderer
     * issues goes through one of the six XNA stock effects vendored under the module's `effects/`
     * directory. Selection is made from `GpuDrawParams`, not from the public `Effect` object,
     * because the shared graphics layer hands renderers the flattened parameter block.
     */
    enum class StockEffectKind
    {
        /** @brief SpriteEffect -- the 2D SpriteBatch shader (a single `MatrixTransform`). */
        Sprite,
        /** @brief BasicEffect -- 32 variants over fog/vertex-colour/texture/lighting. */
        Basic,
        /** @brief AlphaTestEffect -- 8 variants over fog/vertex-colour/compare-function class. */
        AlphaTest,
        /** @brief DualTextureEffect -- 4 variants over fog/vertex-colour. */
        DualTexture,
        /** @brief EnvironmentMapEffect -- 16 variants over fog/fresnel/specular/one-light. */
        EnvironmentMap,
        /** @brief SkinnedEffect -- 18 variants over fog/weights-per-vertex/lighting. */
        Skinned,
        /** @brief Number of stock effects; not an effect itself. */
        Count
    };

    /**
     * @brief XNA's own `ShaderIndex` arithmetic for the stock effects.
     *
     * Each vendored `.fxb` carries exactly one technique with one pass; the shader *variant* is
     * chosen by an `int ShaderIndex` parameter which the effect's `VSIndices`/`PSIndices` arrays
     * then resolve to a concrete vertex/pixel shader pair. These functions reproduce the
     * arithmetic each stock effect's own `OnApply()` performs, so they are the load-bearing part
     * of variant selection and are deliberately pure and free of any FNA3D type -- which is what
     * makes them directly unit-testable.
     */
    namespace StockShaderIndex
    {
        /**
         * @brief BasicEffect's shader index.
         *
         * @param fogEnabled             Whether fog is enabled.
         * @param vertexColorEnabled     Whether per-vertex colour modulates the material.
         * @param textureEnabled         Whether a diffuse texture is sampled.
         * @param lightingEnabled        Whether directional lighting is evaluated.
         * @param preferPerPixelLighting Whether XNA would compile the per-pixel lighting variant.
         * @param oneLight               Whether only DirectionalLight0 is enabled.
         * @return The `ShaderIndex` value in 0..31.
         */
        [[nodiscard]] constexpr int Basic(bool fogEnabled, bool vertexColorEnabled,
                                          bool textureEnabled, bool lightingEnabled,
                                          bool preferPerPixelLighting, bool oneLight)
        {
            int shaderIndex = 0;
            if (!fogEnabled)         shaderIndex += 1;
            if (vertexColorEnabled)  shaderIndex += 2;
            if (textureEnabled)      shaderIndex += 4;
            if (lightingEnabled)
            {
                if (preferPerPixelLighting) shaderIndex += 24;
                else if (oneLight)          shaderIndex += 16;
                else                        shaderIndex += 8;
            }
            return shaderIndex;
        }

        /**
         * @brief AlphaTestEffect's shader index.
         *
         * @param fogEnabled         Whether fog is enabled.
         * @param vertexColorEnabled Whether per-vertex colour modulates the material.
         * @param isEqNe             Whether the alpha compare function is Equal or NotEqual.
         * @return The `ShaderIndex` value in 0..7.
         */
        [[nodiscard]] constexpr int AlphaTest(bool fogEnabled, bool vertexColorEnabled,
                                              bool isEqNe)
        {
            int shaderIndex = 0;
            if (!fogEnabled)        shaderIndex += 1;
            if (vertexColorEnabled) shaderIndex += 2;
            if (isEqNe)             shaderIndex += 4;
            return shaderIndex;
        }

        /**
         * @brief DualTextureEffect's shader index.
         *
         * @param fogEnabled         Whether fog is enabled.
         * @param vertexColorEnabled Whether per-vertex colour modulates the material.
         * @return The `ShaderIndex` value in 0..3.
         */
        [[nodiscard]] constexpr int DualTexture(bool fogEnabled, bool vertexColorEnabled)
        {
            int shaderIndex = 0;
            if (!fogEnabled)        shaderIndex += 1;
            if (vertexColorEnabled) shaderIndex += 2;
            return shaderIndex;
        }

        /**
         * @brief EnvironmentMapEffect's shader index.
         *
         * @param fogEnabled      Whether fog is enabled.
         * @param fresnelEnabled  Whether the env-map blend factor is Fresnel-weighted.
         * @param specularEnabled Whether XNA would compile the specular-enabled variant.
         * @param oneLight        Whether only DirectionalLight0 is enabled.
         * @return The `ShaderIndex` value in 0..15.
         */
        [[nodiscard]] constexpr int EnvironmentMap(bool fogEnabled, bool fresnelEnabled,
                                                   bool specularEnabled, bool oneLight)
        {
            int shaderIndex = 0;
            if (!fogEnabled)     shaderIndex += 1;
            if (fresnelEnabled)  shaderIndex += 2;
            if (specularEnabled) shaderIndex += 4;
            if (oneLight)        shaderIndex += 8;
            return shaderIndex;
        }

        /**
         * @brief SkinnedEffect's shader index.
         *
         * @param fogEnabled             Whether fog is enabled.
         * @param weightsPerVertex       Bone weight/index pairs evaluated per vertex (1, 2 or 4).
         * @param preferPerPixelLighting Whether XNA would compile the per-pixel lighting variant.
         * @param oneLight               Whether only DirectionalLight0 is enabled.
         * @return The `ShaderIndex` value in 0..17.
         */
        [[nodiscard]] constexpr int Skinned(bool fogEnabled, int weightsPerVertex,
                                            bool preferPerPixelLighting, bool oneLight)
        {
            int shaderIndex = 0;
            if (!fogEnabled)             shaderIndex += 1;
            if (weightsPerVertex == 2)   shaderIndex += 2;
            else if (weightsPerVertex == 4) shaderIndex += 4;
            if (preferPerPixelLighting)  shaderIndex += 12;
            else if (oneLight)           shaderIndex += 6;
            return shaderIndex;
        }
    }

    /**
     * @brief One loaded stock effect: the FNA3D handle plus MojoShader's parsed parameter table.
     *
     * Owns nothing at construction; `Load()` parses a vendored `.fxb` and `Destroy()` releases it
     * through the device that created it. Parameter writes go straight into the buffers MojoShader
     * allocated for the effect (`MOJOSHADER_effectParam::value.values`), which is exactly how FNA's
     * managed `EffectParameter` writes them, and are committed to the GPU by `Apply()`.
     */
    class Fna3dStockEffect
    {
    public:
        /** @brief Constructs an unloaded effect. */
        Fna3dStockEffect() = default;

        Fna3dStockEffect(const Fna3dStockEffect&) = delete;
        Fna3dStockEffect& operator=(const Fna3dStockEffect&) = delete;

        /**
         * @brief Parses a compiled Direct3D 9 Effect binary into a usable effect.
         *
         * @param device    Device that will own the effect.
         * @param blob      Effect binary bytes.
         * @param blobBytes Size of @p blob in bytes.
         * @param debugName Name used in the exception message when parsing fails.
         * @throws std::runtime_error if FNA3D/MojoShader could not parse the binary.
         */
        void Load(FNA3D_Device* device, const unsigned char* blob, unsigned int blobBytes,
                  const char* debugName);

        /**
         * @brief Releases the effect through the device that created it.
         * @param device Device the effect was loaded on.
         */
        void Destroy(FNA3D_Device* device);

        /** @brief Whether an effect binary has been parsed successfully. */
        [[nodiscard]] bool IsValid() const { return effect_ != nullptr; }

        /**
         * @brief Looks a parameter up by its effect-declared name.
         * @param name Parameter name, e.g. "WorldViewProj".
         * @return The parameter, or nullptr when this effect declares no such name.
         */
        [[nodiscard]] MOJOSHADER_effectParam* Find(const char* name) const;

        /**
         * @brief Writes @p count floats into a named parameter, ignoring an absent name.
         * @param name   Parameter name.
         * @param values Source floats.
         * @param count  Number of floats to write; clamped to the parameter's own capacity.
         */
        void SetFloats(const char* name, const float* values, int count) const;

        /**
         * @brief Writes a single int into a named parameter, ignoring an absent name.
         * @param name  Parameter name.
         * @param value Value to write.
         */
        void SetInt(const char* name, int value) const;

        /**
         * @brief Writes an RGB triple plus an explicit alpha into a named `float4` parameter.
         * @param name  Parameter name.
         * @param rgb   Three-component colour.
         * @param alpha Fourth component.
         */
        void SetVector4(const char* name, const float* rgb, float alpha) const;

        /**
         * @brief Writes an XNA matrix into a named parameter using FNA's own packing.
         *
         * The stock effects declare `float4x4`/`float4x3`/`float3x3` matrices, and FNA writes each
         * of them column-first (`M11, M21, M31, M41, M12, ...`) with the row stride padded to four
         * floats. This reproduces that layout exactly rather than memcpy'ing CNA's row-major
         * storage, which would transpose every transform.
         *
         * @param name   Parameter name.
         * @param matrix Source matrix in CNA/XNA row-major storage.
         */
        void SetMatrix(const char* name, const Matrix& matrix) const;

        /**
         * @brief Writes an array of already column-major-packed 4x4 matrices as `float4x3`.
         *
         * `GpuDrawParams::boneTransforms` already holds column-major `mat4`s, whose first twelve
         * floats are precisely the `float4x3` layout SkinnedEffect's `Bones` parameter expects, so
         * this copies twelve of every sixteen floats without rearranging anything.
         *
         * @param name         Parameter name.
         * @param columnMajor  Source array of `count` column-major 4x4 matrices (16 floats each).
         * @param count        Number of matrices.
         */
        void SetMatrix4x3Array(const char* name, const float* columnMajor, int count) const;

        /**
         * @brief Selects technique 0 and applies pass 0, committing every parameter write.
         *
         * Every vendored stock effect carries exactly one technique with one pass (measured, see
         * the module's `effects/README.md`), so there is no technique to choose at runtime -- the
         * variant is the `ShaderIndex` parameter.
         *
         * @param device Device to apply the effect on.
         */
        void Apply(FNA3D_Device* device);

        /** @brief The parsed MojoShader effect, or nullptr when not loaded. */
        [[nodiscard]] MOJOSHADER_effect* GetEffectData() const { return effectData_; }

    private:
        FNA3D_Effect* effect_ = nullptr;
        MOJOSHADER_effect* effectData_ = nullptr;
        MOJOSHADER_effectStateChanges stateChanges_{};
    };
}
