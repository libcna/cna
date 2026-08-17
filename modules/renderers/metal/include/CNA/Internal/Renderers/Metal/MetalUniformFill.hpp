// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Metal/MetalMat4.hpp"
#include "CNA/Internal/Renderers/Metal/MetalNormalMatrix.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include <cstring>

// plan_metal.md METAL-34-style extraction: this is the connective tissue between GpuDrawParams (the
// renderer-agnostic per-draw state EasyGLRenderer::BindDrawParams() also reads) and the actual
// float arrays memcpy'd into a real MTLBuffer for each shader family -- field-for-field matching
// EasyGLRenderer::BindDrawParams()'s own real mapping (ground truth, ported not redesigned).
// Every field here is plain C++ (GpuDrawParams, MetalMat4, plain float arrays) with zero Objective-C
// dependency, so unlike the MSL-side struct layout (kMetalShaderSource's own LitTransform/LitUniforms
// etc., which stay in the shader source string and are NOT touched by this extraction) this mapping
// logic can be genuinely unit-tested on any platform without an Apple toolchain -- and arguably most
// needs it: with dozens of individual field assignments per function, a single copy-paste mistake
// (e.g. `light1Diffuse` accidentally reading `params.light2Diffuse`) would silently render wrong
// lighting rather than fail to compile or crash. MetalRenderer.mm includes this header instead
// of defining these structs/functions inline, so field mapping and the accepted lighting-disabled
// normalization share one portable source of truth.
namespace CNA::Internal::Renderers::Metal
{
    // Plain C++ mirrors of kMetalShaderSource's `LitTransform`/`LitUniforms` -- every logical vec3 is
    // carried as a 4-float (xyz+pad) group and the normal matrix as 3 separate 4-float "columns"
    // rather than a 3x3 or float3-containing type, matching MSL's own std140-like packing rules.
    /** @brief CPU mirror of the built-in lit shader's transform constants. */
    struct MetalLitTransform { float wvp[16]; float world[16]; float normalCol0[4]; float normalCol1[4]; float normalCol2[4]; };
    /** @brief CPU mirror of the built-in lit shader's material and lighting constants. */
    struct MetalLitUniforms {
        float diffuseColor[4], ambientColor[4];
        float light0Dir[4], light0Diffuse[4], light0Specular[4];
        float light1Dir[4], light1Diffuse[4], light1Specular[4];
        float light2Dir[4], light2Diffuse[4], light2Specular[4];
        float specularColorPower[4], eyePosition[4], emissiveColor[4], alphaTest[4];
        float fogColorEnabled[4], fogVector[4];
    };

    // Plain C++ mirrors of kMetalShaderSource's `EnvTransform`/`EnvUniforms` -- real XNA
    // EnvironmentMapEffect has no separate ambient uniform (see the .mm's own BindDrawParams
    // finding), so unlike LitUniforms there is no `ambientColor` field here.
    /** @brief CPU mirror of the environment-map shader's transform constants. */
    struct MetalEnvTransform { float wvp[16]; float world[16]; float normalCol0[4]; float normalCol1[4]; float normalCol2[4]; };
    /** @brief CPU mirror of the environment-map shader's material and lighting constants. */
    struct MetalEnvUniforms {
        float diffuseColor[4], emissiveColor[4];
        float light0Dir[4], light0Diffuse[4];
        float light1Dir[4], light1Diffuse[4];
        float light2Dir[4], light2Diffuse[4];
        float envMapSpecular[4], eyePosition[4], envParams[4], alphaTest[4];
        float fogColorEnabled[4], fogVector[4];
    };

    // Plain C++ mirrors of kMetalShaderSource's `SkinnedTransform`/`SkinnedUniforms` -- unlike
    // LitTransform/EnvTransform/PbrTransform, the skinned shader has no world-normal-matrix step at
    // all (see cna_skin_common's own comment), so SkinnedTransform has no normalCol0/1/2 fields.
    /** @brief CPU mirror of the skinned shader's transform and skinning constants. */
    struct MetalSkinnedTransform { float wvp[16]; float world[16]; float skinParams[4]; };
    /** @brief CPU mirror of the skinned shader's material and lighting constants. */
    struct MetalSkinnedUniforms {
        float diffuseColor[4], emissiveColor[4];
        float light0Dir[4], light0Diffuse[4], light0Specular[4];
        float light1Dir[4], light1Diffuse[4], light1Specular[4];
        float light2Dir[4], light2Diffuse[4], light2Specular[4];
        float specularColorPower[4];
        float eyePosition[4];
        float alphaTest[4];
        float fogColorEnabled[4], fogVector[4];
        float vertexColorEnabled[4];
    };

    // Plain C++ mirrors of kMetalShaderSource's `PbrTransform`/`PbrUniforms`.
    /** @brief CPU mirror of the PBR shader's transform constants. */
    struct MetalPbrTransform { float wvp[16]; float world[16]; float normalCol0[4]; float normalCol1[4]; float normalCol2[4]; };
    /** @brief CPU mirror of the PBR shader's material and lighting constants. */
    struct MetalPbrUniforms {
        float diffuseColor[4];
        float ambientColor[4];
        float emissiveColor[4];
        float light0Dir[4], light0Diffuse[4];
        float light1Dir[4], light1Diffuse[4];
        float light2Dir[4], light2Diffuse[4];
        float eyePosition[4];
        float pbrFactors[4];   // x=MetallicFactor, y=RoughnessFactor, z=NormalScale, w=OcclusionStrength
        float alphaTest[4];
        float fogColorEnabled[4], fogVector[4];
        float srgbFlags[4];    // x=base decode, y=emissive decode, z=output encode
        float dielectricFresnel[4]; // xyz=dielectric F0, w=dielectric F90
        float textureTransformRows[10][4]; // two affine rows per PBR texture slot
    };

    // Plain C++ mirror of kMetalShaderSource's `SkinnedPbrTransform` (reuses `MetalPbrUniforms`
    // as-is -- the fragment-side uniforms are identical between skinned and unskinned PBR).
    /** @brief CPU mirror of the skinned-PBR shader's transform and skinning constants. */
    struct MetalSkinnedPbrTransform {
        float wvp[16];
        float world[16];
        float normalCol0[4], normalCol1[4], normalCol2[4];
        float skinParams[4];
    };

    // plan_metal.md METAL-38-47: fills LitTransform/LitUniforms from GpuDrawParams, field-for-field
    // matching EasyGLRenderer::BindDrawParams()'s own real mapping (ground truth, ported not
    // redesigned).
    /**
     * @brief Fills the lit shader's CPU-side constant structures from normalized draw state.
     *
     * @param transform Receives transform and normal-matrix constants.
     * @param uniforms Receives material, lighting, alpha-test, and fog constants.
     * @param wvp Precomputed world-view-projection matrix.
     * @param params Normalized renderer draw parameters.
     */
    inline void FillMetalLitUniforms(MetalLitTransform& transform,
                                     MetalLitUniforms& uniforms,
                                     const MetalMat4& wvp,
                                     const CNA::Internal::Renderers::GpuDrawParams& params)
    {
        auto& t = transform;
        auto& lu = uniforms;
        std::memcpy(t.wvp, wvp.m, sizeof(t.wvp));
        std::memcpy(t.world, params.worldColMajor, sizeof(t.world));
        ComputeMetalNormalMatrixCols(params.worldColMajor, t.normalCol0, t.normalCol1, t.normalCol2);

        std::memcpy(lu.diffuseColor, params.diffuseColor, sizeof(lu.diffuseColor));
        lu.ambientColor[0]=params.ambientColor[0]; lu.ambientColor[1]=params.ambientColor[1]; lu.ambientColor[2]=params.ambientColor[2]; lu.ambientColor[3]=0;
        lu.light0Dir[0]=params.light0Dir[0]; lu.light0Dir[1]=params.light0Dir[1]; lu.light0Dir[2]=params.light0Dir[2]; lu.light0Dir[3]=0;
        lu.light0Diffuse[0]=params.light0Diffuse[0]; lu.light0Diffuse[1]=params.light0Diffuse[1]; lu.light0Diffuse[2]=params.light0Diffuse[2]; lu.light0Diffuse[3]=0;
        lu.light0Specular[0]=params.light0Specular[0]; lu.light0Specular[1]=params.light0Specular[1]; lu.light0Specular[2]=params.light0Specular[2]; lu.light0Specular[3]=0;
        lu.light1Dir[0]=params.light1Dir[0]; lu.light1Dir[1]=params.light1Dir[1]; lu.light1Dir[2]=params.light1Dir[2]; lu.light1Dir[3]=0;
        lu.light1Diffuse[0]=params.light1Diffuse[0]; lu.light1Diffuse[1]=params.light1Diffuse[1]; lu.light1Diffuse[2]=params.light1Diffuse[2]; lu.light1Diffuse[3]=0;
        lu.light1Specular[0]=params.light1Specular[0]; lu.light1Specular[1]=params.light1Specular[1]; lu.light1Specular[2]=params.light1Specular[2]; lu.light1Specular[3]=0;
        lu.light2Dir[0]=params.light2Dir[0]; lu.light2Dir[1]=params.light2Dir[1]; lu.light2Dir[2]=params.light2Dir[2]; lu.light2Dir[3]=0;
        lu.light2Diffuse[0]=params.light2Diffuse[0]; lu.light2Diffuse[1]=params.light2Diffuse[1]; lu.light2Diffuse[2]=params.light2Diffuse[2]; lu.light2Diffuse[3]=0;
        lu.light2Specular[0]=params.light2Specular[0]; lu.light2Specular[1]=params.light2Specular[1]; lu.light2Specular[2]=params.light2Specular[2]; lu.light2Specular[3]=0;
        if (!params.lightingEnabled)
        {
            lu.ambientColor[0]=lu.ambientColor[1]=lu.ambientColor[2]=1.0f;
            lu.light0Dir[0]=lu.light1Dir[0]=lu.light2Dir[0]=0.0f;
            lu.light0Dir[1]=lu.light1Dir[1]=lu.light2Dir[1]=-1.0f;
            lu.light0Dir[2]=lu.light1Dir[2]=lu.light2Dir[2]=0.0f;
            for (int component=0; component<3; ++component)
            {
                lu.light0Diffuse[component]=lu.light1Diffuse[component]=lu.light2Diffuse[component]=0.0f;
                lu.light0Specular[component]=lu.light1Specular[component]=lu.light2Specular[component]=0.0f;
            }
        }
        lu.specularColorPower[0]=params.specularColor[0]; lu.specularColorPower[1]=params.specularColor[1]; lu.specularColorPower[2]=params.specularColor[2]; lu.specularColorPower[3]=params.specularPower;
        lu.eyePosition[0]=params.eyePositionWorld[0]; lu.eyePosition[1]=params.eyePositionWorld[1]; lu.eyePosition[2]=params.eyePositionWorld[2]; lu.eyePosition[3]=0;
        lu.emissiveColor[0]=params.emissiveColor[0]; lu.emissiveColor[1]=params.emissiveColor[1]; lu.emissiveColor[2]=params.emissiveColor[2]; lu.emissiveColor[3]=0;
        std::memcpy(lu.alphaTest, params.alphaTest, sizeof(lu.alphaTest));
        lu.fogColorEnabled[0]=params.fogColor[0]; lu.fogColorEnabled[1]=params.fogColor[1]; lu.fogColorEnabled[2]=params.fogColor[2]; lu.fogColorEnabled[3]=params.fogEnabled?1.0f:0.0f;
        std::memcpy(lu.fogVector, params.fogVector, sizeof(lu.fogVector));
    }

    // plan_metal.md METAL-66-68: fills EnvTransform/EnvUniforms, field-for-field matching
    // EasyGLRenderer::BindDrawParams()'s real EnvironmentMapEffect-specific mapping (the
    // `p.loc_ambient < 0` gated block -- ground truth, ported not redesigned).
    /**
     * @brief Fills the environment-map shader's CPU-side constants from normalized draw state.
     *
     * @param transform Receives transform and normal-matrix constants.
     * @param uniforms Receives material, lighting, environment-map, alpha-test, and fog constants.
     * @param wvp Precomputed world-view-projection matrix.
     * @param params Normalized renderer draw parameters.
     */
    inline void FillMetalEnvUniforms(MetalEnvTransform& transform,
                                     MetalEnvUniforms& uniforms,
                                     const MetalMat4& wvp,
                                     const CNA::Internal::Renderers::GpuDrawParams& params)
    {
        auto& t = transform;
        auto& eu = uniforms;
        std::memcpy(t.wvp, wvp.m, sizeof(t.wvp));
        std::memcpy(t.world, params.worldColMajor, sizeof(t.world));
        ComputeMetalNormalMatrixCols(params.worldColMajor, t.normalCol0, t.normalCol1, t.normalCol2);

        std::memcpy(eu.diffuseColor, params.diffuseColor, sizeof(eu.diffuseColor));
        eu.emissiveColor[0]=params.emissiveColor[0]; eu.emissiveColor[1]=params.emissiveColor[1]; eu.emissiveColor[2]=params.emissiveColor[2]; eu.emissiveColor[3]=0;
        eu.light0Dir[0]=params.light0Dir[0]; eu.light0Dir[1]=params.light0Dir[1]; eu.light0Dir[2]=params.light0Dir[2]; eu.light0Dir[3]=0;
        eu.light0Diffuse[0]=params.light0Diffuse[0]; eu.light0Diffuse[1]=params.light0Diffuse[1]; eu.light0Diffuse[2]=params.light0Diffuse[2]; eu.light0Diffuse[3]=0;
        eu.light1Dir[0]=params.light1Dir[0]; eu.light1Dir[1]=params.light1Dir[1]; eu.light1Dir[2]=params.light1Dir[2]; eu.light1Dir[3]=0;
        eu.light1Diffuse[0]=params.light1Diffuse[0]; eu.light1Diffuse[1]=params.light1Diffuse[1]; eu.light1Diffuse[2]=params.light1Diffuse[2]; eu.light1Diffuse[3]=0;
        eu.light2Dir[0]=params.light2Dir[0]; eu.light2Dir[1]=params.light2Dir[1]; eu.light2Dir[2]=params.light2Dir[2]; eu.light2Dir[3]=0;
        eu.light2Diffuse[0]=params.light2Diffuse[0]; eu.light2Diffuse[1]=params.light2Diffuse[1]; eu.light2Diffuse[2]=params.light2Diffuse[2]; eu.light2Diffuse[3]=0;
        eu.envMapSpecular[0]=params.envMapSpecular[0]; eu.envMapSpecular[1]=params.envMapSpecular[1]; eu.envMapSpecular[2]=params.envMapSpecular[2]; eu.envMapSpecular[3]=0;
        eu.eyePosition[0]=params.eyePositionWorld[0]; eu.eyePosition[1]=params.eyePositionWorld[1]; eu.eyePosition[2]=params.eyePositionWorld[2]; eu.eyePosition[3]=0;
        eu.envParams[0]=params.envMapAmount; eu.envParams[1]=params.fresnelEnabled?1.0f:0.0f; eu.envParams[2]=params.fresnelFactor; eu.envParams[3]=0;
        std::memcpy(eu.alphaTest, params.alphaTest, sizeof(eu.alphaTest));
        eu.fogColorEnabled[0]=params.fogColor[0]; eu.fogColorEnabled[1]=params.fogColor[1]; eu.fogColorEnabled[2]=params.fogColor[2]; eu.fogColorEnabled[3]=params.fogEnabled?1.0f:0.0f;
        std::memcpy(eu.fogVector, params.fogVector, sizeof(eu.fogVector));
    }

    // plan_metal.md METAL-73/74/76-78: fills SkinnedTransform/SkinnedUniforms, field-for-field
    // matching EasyGLRenderer::BindDrawParams()'s real SkinnedEffect-specific mapping. Note:
    // unlike FillMetalLitUniforms/FillMetalEnvUniforms, this deliberately does NOT call
    // ComputeMetalNormalMatrixCols -- the skinned shader has no world-normal-matrix step at all (see
    // cna_skin_common's own comment), so MetalSkinnedTransform has no normalCol0/1/2 fields to fill.
    /**
     * @brief Fills the skinned shader's CPU-side constants from normalized draw state.
     *
     * @param transform Receives transform and skinning constants.
     * @param uniforms Receives material, lighting, alpha-test, fog, and vertex-color constants.
     * @param wvp Precomputed world-view-projection matrix.
     * @param params Normalized renderer draw parameters.
     */
    inline void FillMetalSkinnedUniforms(MetalSkinnedTransform& transform,
                                         MetalSkinnedUniforms& uniforms,
                                         const MetalMat4& wvp,
                                         const CNA::Internal::Renderers::GpuDrawParams& params)
    {
        auto& t = transform;
        auto& su = uniforms;
        std::memcpy(t.wvp, wvp.m, sizeof(t.wvp));
        std::memcpy(t.world, params.worldColMajor, sizeof(t.world));
        t.skinParams[0]=(float)params.weightsPerVertex; t.skinParams[1]=t.skinParams[2]=t.skinParams[3]=0;

        std::memcpy(su.diffuseColor, params.diffuseColor, sizeof(su.diffuseColor));
        su.emissiveColor[0]=params.emissiveColor[0]; su.emissiveColor[1]=params.emissiveColor[1]; su.emissiveColor[2]=params.emissiveColor[2]; su.emissiveColor[3]=0;
        su.light0Dir[0]=params.light0Dir[0]; su.light0Dir[1]=params.light0Dir[1]; su.light0Dir[2]=params.light0Dir[2]; su.light0Dir[3]=0;
        su.light0Diffuse[0]=params.light0Diffuse[0]; su.light0Diffuse[1]=params.light0Diffuse[1]; su.light0Diffuse[2]=params.light0Diffuse[2]; su.light0Diffuse[3]=0;
        su.light0Specular[0]=params.light0Specular[0]; su.light0Specular[1]=params.light0Specular[1]; su.light0Specular[2]=params.light0Specular[2]; su.light0Specular[3]=0;
        su.light1Dir[0]=params.light1Dir[0]; su.light1Dir[1]=params.light1Dir[1]; su.light1Dir[2]=params.light1Dir[2]; su.light1Dir[3]=0;
        su.light1Diffuse[0]=params.light1Diffuse[0]; su.light1Diffuse[1]=params.light1Diffuse[1]; su.light1Diffuse[2]=params.light1Diffuse[2]; su.light1Diffuse[3]=0;
        su.light1Specular[0]=params.light1Specular[0]; su.light1Specular[1]=params.light1Specular[1]; su.light1Specular[2]=params.light1Specular[2]; su.light1Specular[3]=0;
        su.light2Dir[0]=params.light2Dir[0]; su.light2Dir[1]=params.light2Dir[1]; su.light2Dir[2]=params.light2Dir[2]; su.light2Dir[3]=0;
        su.light2Diffuse[0]=params.light2Diffuse[0]; su.light2Diffuse[1]=params.light2Diffuse[1]; su.light2Diffuse[2]=params.light2Diffuse[2]; su.light2Diffuse[3]=0;
        su.light2Specular[0]=params.light2Specular[0]; su.light2Specular[1]=params.light2Specular[1]; su.light2Specular[2]=params.light2Specular[2]; su.light2Specular[3]=0;
        su.specularColorPower[0]=params.specularColor[0]; su.specularColorPower[1]=params.specularColor[1]; su.specularColorPower[2]=params.specularColor[2]; su.specularColorPower[3]=params.specularPower;
        su.eyePosition[0]=params.eyePositionWorld[0]; su.eyePosition[1]=params.eyePositionWorld[1]; su.eyePosition[2]=params.eyePositionWorld[2]; su.eyePosition[3]=0;
        std::memcpy(su.alphaTest, params.alphaTest, sizeof(su.alphaTest));
        su.fogColorEnabled[0]=params.fogColor[0]; su.fogColorEnabled[1]=params.fogColor[1]; su.fogColorEnabled[2]=params.fogColor[2]; su.fogColorEnabled[3]=params.fogEnabled?1.0f:0.0f;
        std::memcpy(su.fogVector, params.fogVector, sizeof(su.fogVector));
        su.vertexColorEnabled[0]=params.vertexColorEnabled?1.0f:0.0f; su.vertexColorEnabled[1]=su.vertexColorEnabled[2]=su.vertexColorEnabled[3]=0;
    }

    // plan_metal.md METAL-81/83-86: fills PbrTransform/PbrUniforms from GpuDrawParams, field-for-field
    // matching EasyGLRenderer::BindDrawParams()'s real PBR-specific mapping (ground truth).
    /**
     * @brief Fills the PBR shader's CPU-side constants from normalized draw state.
     *
     * @param transform Receives transform and normal-matrix constants.
     * @param uniforms Receives PBR material, lighting, alpha-test, and fog constants.
     * @param wvp Precomputed world-view-projection matrix.
     * @param params Normalized renderer draw parameters.
     */
    inline void FillMetalPbrUniforms(MetalPbrTransform& transform,
                                     MetalPbrUniforms& uniforms,
                                     const MetalMat4& wvp,
                                     const CNA::Internal::Renderers::GpuDrawParams& params)
    {
        auto& t = transform;
        auto& pu = uniforms;
        std::memcpy(t.wvp, wvp.m, sizeof(t.wvp));
        std::memcpy(t.world, params.worldColMajor, sizeof(t.world));
        ComputeMetalNormalMatrixCols(params.worldColMajor, t.normalCol0, t.normalCol1, t.normalCol2);

        std::memcpy(pu.diffuseColor, params.diffuseColor, sizeof(pu.diffuseColor));
        pu.ambientColor[0]=params.ambientColor[0]; pu.ambientColor[1]=params.ambientColor[1]; pu.ambientColor[2]=params.ambientColor[2]; pu.ambientColor[3]=0;
        pu.emissiveColor[0]=params.emissiveColor[0]; pu.emissiveColor[1]=params.emissiveColor[1]; pu.emissiveColor[2]=params.emissiveColor[2]; pu.emissiveColor[3]=0;
        pu.light0Dir[0]=params.light0Dir[0]; pu.light0Dir[1]=params.light0Dir[1]; pu.light0Dir[2]=params.light0Dir[2]; pu.light0Dir[3]=0;
        pu.light0Diffuse[0]=params.light0Diffuse[0]; pu.light0Diffuse[1]=params.light0Diffuse[1]; pu.light0Diffuse[2]=params.light0Diffuse[2]; pu.light0Diffuse[3]=0;
        pu.light1Dir[0]=params.light1Dir[0]; pu.light1Dir[1]=params.light1Dir[1]; pu.light1Dir[2]=params.light1Dir[2]; pu.light1Dir[3]=0;
        pu.light1Diffuse[0]=params.light1Diffuse[0]; pu.light1Diffuse[1]=params.light1Diffuse[1]; pu.light1Diffuse[2]=params.light1Diffuse[2]; pu.light1Diffuse[3]=0;
        pu.light2Dir[0]=params.light2Dir[0]; pu.light2Dir[1]=params.light2Dir[1]; pu.light2Dir[2]=params.light2Dir[2]; pu.light2Dir[3]=0;
        pu.light2Diffuse[0]=params.light2Diffuse[0]; pu.light2Diffuse[1]=params.light2Diffuse[1]; pu.light2Diffuse[2]=params.light2Diffuse[2]; pu.light2Diffuse[3]=0;
        if (!params.lightingEnabled)
        {
            pu.ambientColor[0]=pu.ambientColor[1]=pu.ambientColor[2]=1.0f;
            pu.light0Dir[0]=pu.light1Dir[0]=pu.light2Dir[0]=0.0f;
            pu.light0Dir[1]=pu.light1Dir[1]=pu.light2Dir[1]=-1.0f;
            pu.light0Dir[2]=pu.light1Dir[2]=pu.light2Dir[2]=0.0f;
            for (int component=0; component<3; ++component)
                pu.light0Diffuse[component]=pu.light1Diffuse[component]=pu.light2Diffuse[component]=0.0f;
        }
        pu.eyePosition[0]=params.eyePositionWorld[0]; pu.eyePosition[1]=params.eyePositionWorld[1]; pu.eyePosition[2]=params.eyePositionWorld[2]; pu.eyePosition[3]=0;
        pu.pbrFactors[0]=params.pbrMetallicFactor; pu.pbrFactors[1]=params.pbrRoughnessFactor;
        pu.pbrFactors[2]=params.pbrNormalScale; pu.pbrFactors[3]=params.pbrOcclusionStrength;
        std::memcpy(pu.alphaTest, params.alphaTest, sizeof(pu.alphaTest));
        pu.fogColorEnabled[0]=params.fogColor[0]; pu.fogColorEnabled[1]=params.fogColor[1]; pu.fogColorEnabled[2]=params.fogColor[2]; pu.fogColorEnabled[3]=params.fogEnabled?1.0f:0.0f;
        std::memcpy(pu.fogVector, params.fogVector, sizeof(pu.fogVector));
        pu.srgbFlags[0]=params.pbrBaseColorTextureIsSrgb?1.0f:0.0f;
        pu.srgbFlags[1]=params.pbrEmissiveTextureIsSrgb?1.0f:0.0f;
        pu.srgbFlags[2]=params.pbrEncodeOutputToSrgb?1.0f:0.0f;
        pu.srgbFlags[3]=0.0f;
        pu.dielectricFresnel[0]=params.pbrDielectricF0[0];
        pu.dielectricFresnel[1]=params.pbrDielectricF0[1];
        pu.dielectricFresnel[2]=params.pbrDielectricF0[2];
        pu.dielectricFresnel[3]=params.pbrDielectricF90;
        std::memcpy(pu.textureTransformRows, params.pbrTextureTransformRows,
                    sizeof(pu.textureTransformRows));
    }

    // plan_metal.md METAL-82: fills SkinnedPbrTransform/PbrUniforms from GpuDrawParams. The uniform
    // (fragment-side) fields are identical to FillMetalPbrUniforms' -- only the transform struct
    // differs (adds skinParams, matching FillMetalSkinnedUniforms' own t.skinParams handling), so
    // this delegates the uniform fill to FillMetalPbrUniforms via a throwaway MetalPbrTransform and
    // copies just the shared fields across, rather than duplicating every uniform assignment a third
    // time.
    /**
     * @brief Fills the skinned-PBR shader's CPU-side constants from normalized draw state.
     *
     * @param transform Receives transform and skinning constants.
     * @param uniforms Receives PBR material, lighting, alpha-test, and fog constants.
     * @param wvp Precomputed world-view-projection matrix.
     * @param params Normalized renderer draw parameters.
     */
    inline void FillMetalSkinnedPbrUniforms(MetalSkinnedPbrTransform& transform,
                                            MetalPbrUniforms& uniforms,
                                            const MetalMat4& wvp,
                                            const CNA::Internal::Renderers::GpuDrawParams& params)
    {
        auto& t = transform;
        auto& pu = uniforms;
        MetalPbrTransform unusedT{};
        FillMetalPbrUniforms(unusedT, pu, wvp, params);
        std::memcpy(t.wvp, wvp.m, sizeof(t.wvp));
        std::memcpy(t.world, params.worldColMajor, sizeof(t.world));
        std::memcpy(t.normalCol0, unusedT.normalCol0, sizeof(t.normalCol0));
        std::memcpy(t.normalCol1, unusedT.normalCol1, sizeof(t.normalCol1));
        std::memcpy(t.normalCol2, unusedT.normalCol2, sizeof(t.normalCol2));
        t.skinParams[0]=(float)params.weightsPerVertex; t.skinParams[1]=t.skinParams[2]=t.skinParams[3]=0;
    }
}
