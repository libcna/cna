#pragma once

#include "CNA/Internal/Backends/Metal/MetalMat4.hpp"
#include "CNA/Internal/Backends/Metal/MetalNormalMatrix.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include <cstring>

// plan_metal.md METAL-34-style extraction: this is the connective tissue between GpuDrawParams (the
// backend-agnostic per-draw state EasyGLGraphicsBackend::BindDrawParams() also reads) and the actual
// float arrays memcpy'd into a real MTLBuffer for each shader family -- field-for-field matching
// EasyGLGraphicsBackend::BindDrawParams()'s own real mapping (ground truth, ported not redesigned).
// Every field here is plain C++ (GpuDrawParams, MetalMat4, plain float arrays) with zero Objective-C
// dependency, so unlike the MSL-side struct layout (kMetalShaderSource's own LitTransform/LitUniforms
// etc., which stay in the shader source string and are NOT touched by this extraction) this mapping
// logic can be genuinely unit-tested on any platform without an Apple toolchain -- and arguably most
// needs it: with dozens of individual field assignments per function, a single copy-paste mistake
// (e.g. `light1Diffuse` accidentally reading `params.light2Diffuse`) would silently render wrong
// lighting rather than fail to compile or crash. MetalGraphicsBackend.mm includes this header instead
// of defining these structs/functions inline; logic (including every field mapping and comment) is
// unchanged from the original inline definitions.
namespace CNA::Internal::Backends::Metal
{
    // Plain C++ mirrors of kMetalShaderSource's `LitTransform`/`LitUniforms` -- every logical vec3 is
    // carried as a 4-float (xyz+pad) group and the normal matrix as 3 separate 4-float "columns"
    // rather than a 3x3 or float3-containing type, matching MSL's own std140-like packing rules.
    struct MetalLitTransform { float wvp[16]; float world[16]; float normalCol0[4]; float normalCol1[4]; float normalCol2[4]; };
    struct MetalLitUniforms {
        float diffuseColor[4], ambientColor[4];
        float light0Dir[4], light0Diffuse[4], light0Specular[4];
        float light1Dir[4], light1Diffuse[4], light1Specular[4];
        float light2Dir[4], light2Diffuse[4], light2Specular[4];
        float specularColorPower[4], eyePosition[4], emissiveColor[4], alphaTest[4];
        float fogColorEnabled[4], fogStartEnd[4];
    };

    // Plain C++ mirrors of kMetalShaderSource's `EnvTransform`/`EnvUniforms` -- real XNA
    // EnvironmentMapEffect has no separate ambient uniform (see the .mm's own BindDrawParams
    // finding), so unlike LitUniforms there is no `ambientColor` field here.
    struct MetalEnvTransform { float wvp[16]; float world[16]; float normalCol0[4]; float normalCol1[4]; float normalCol2[4]; };
    struct MetalEnvUniforms {
        float diffuseColor[4], emissiveColor[4];
        float light0Dir[4], light0Diffuse[4];
        float light1Dir[4], light1Diffuse[4];
        float light2Dir[4], light2Diffuse[4];
        float envMapSpecular[4], eyePosition[4], envParams[4], alphaTest[4];
        float fogColorEnabled[4], fogStartEnd[4];
    };

    // Plain C++ mirrors of kMetalShaderSource's `SkinnedTransform`/`SkinnedUniforms` -- unlike
    // LitTransform/EnvTransform/PbrTransform, the skinned shader has no world-normal-matrix step at
    // all (see cna_skin_common's own comment), so SkinnedTransform has no normalCol0/1/2 fields.
    struct MetalSkinnedTransform { float wvp[16]; float world[16]; float skinParams[4]; };
    struct MetalSkinnedUniforms {
        float diffuseColor[4], emissiveColor[4];
        float light0Dir[4], light0Diffuse[4], light0Specular[4];
        float light1Dir[4], light1Diffuse[4], light1Specular[4];
        float light2Dir[4], light2Diffuse[4], light2Specular[4];
        float specularColorPower[4];
        float eyePosition[4];
        float alphaTest[4];
        float fogColorEnabled[4], fogStartEnd[4];
        float vertexColorEnabled[4];
    };

    // Plain C++ mirrors of kMetalShaderSource's `PbrTransform`/`PbrUniforms`.
    struct MetalPbrTransform { float wvp[16]; float world[16]; float normalCol0[4]; float normalCol1[4]; float normalCol2[4]; };
    struct MetalPbrUniforms {
        float diffuseColor[4];
        float ambientColor[4];
        float emissiveColor[4];
        float light0Dir[4], light0Diffuse[4];
        float light1Dir[4], light1Diffuse[4];
        float light2Dir[4], light2Diffuse[4];
        float eyePosition[4];
        float pbrFactors[4];   // x=MetallicFactor, y=RoughnessFactor
        float alphaTest[4];
        float fogColorEnabled[4], fogStartEnd[4];
    };

    // Plain C++ mirror of kMetalShaderSource's `SkinnedPbrTransform` (reuses `MetalPbrUniforms`
    // as-is -- the fragment-side uniforms are identical between skinned and unskinned PBR).
    struct MetalSkinnedPbrTransform { float wvp[16]; float world[16]; float skinParams[4]; };

    // plan_metal.md METAL-38-47: fills LitTransform/LitUniforms from GpuDrawParams, field-for-field
    // matching EasyGLGraphicsBackend::BindDrawParams()'s own real mapping (ground truth, ported not
    // redesigned).
    inline void FillMetalLitUniforms(MetalLitTransform& t, MetalLitUniforms& lu, const MetalMat4& wvp, const CNA::Internal::Backends::GpuDrawParams& params)
    {
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
        lu.specularColorPower[0]=params.specularColor[0]; lu.specularColorPower[1]=params.specularColor[1]; lu.specularColorPower[2]=params.specularColor[2]; lu.specularColorPower[3]=params.specularPower;
        lu.eyePosition[0]=params.eyePositionWorld[0]; lu.eyePosition[1]=params.eyePositionWorld[1]; lu.eyePosition[2]=params.eyePositionWorld[2]; lu.eyePosition[3]=0;
        lu.emissiveColor[0]=params.emissiveColor[0]; lu.emissiveColor[1]=params.emissiveColor[1]; lu.emissiveColor[2]=params.emissiveColor[2]; lu.emissiveColor[3]=0;
        std::memcpy(lu.alphaTest, params.alphaTest, sizeof(lu.alphaTest));
        lu.fogColorEnabled[0]=params.fogColor[0]; lu.fogColorEnabled[1]=params.fogColor[1]; lu.fogColorEnabled[2]=params.fogColor[2]; lu.fogColorEnabled[3]=params.fogEnabled?1.0f:0.0f;
        lu.fogStartEnd[0]=params.fogStart; lu.fogStartEnd[1]=params.fogEnd; lu.fogStartEnd[2]=0; lu.fogStartEnd[3]=0;
    }

    // plan_metal.md METAL-66-68: fills EnvTransform/EnvUniforms, field-for-field matching
    // EasyGLGraphicsBackend::BindDrawParams()'s real EnvironmentMapEffect-specific mapping (the
    // `p.loc_ambient < 0` gated block -- ground truth, ported not redesigned).
    inline void FillMetalEnvUniforms(MetalEnvTransform& t, MetalEnvUniforms& eu, const MetalMat4& wvp, const CNA::Internal::Backends::GpuDrawParams& params)
    {
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
        eu.fogStartEnd[0]=params.fogStart; eu.fogStartEnd[1]=params.fogEnd; eu.fogStartEnd[2]=0; eu.fogStartEnd[3]=0;
    }

    // plan_metal.md METAL-73/74/76-78: fills SkinnedTransform/SkinnedUniforms, field-for-field
    // matching EasyGLGraphicsBackend::BindDrawParams()'s real SkinnedEffect-specific mapping. Note:
    // unlike FillMetalLitUniforms/FillMetalEnvUniforms, this deliberately does NOT call
    // ComputeMetalNormalMatrixCols -- the skinned shader has no world-normal-matrix step at all (see
    // cna_skin_common's own comment), so MetalSkinnedTransform has no normalCol0/1/2 fields to fill.
    inline void FillMetalSkinnedUniforms(MetalSkinnedTransform& t, MetalSkinnedUniforms& su, const MetalMat4& wvp, const CNA::Internal::Backends::GpuDrawParams& params)
    {
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
        su.fogStartEnd[0]=params.fogStart; su.fogStartEnd[1]=params.fogEnd; su.fogStartEnd[2]=0; su.fogStartEnd[3]=0;
        su.vertexColorEnabled[0]=params.vertexColorEnabled?1.0f:0.0f; su.vertexColorEnabled[1]=su.vertexColorEnabled[2]=su.vertexColorEnabled[3]=0;
    }

    // plan_metal.md METAL-81/83-86: fills PbrTransform/PbrUniforms from GpuDrawParams, field-for-field
    // matching EasyGLGraphicsBackend::BindDrawParams()'s real PBR-specific mapping (ground truth).
    inline void FillMetalPbrUniforms(MetalPbrTransform& t, MetalPbrUniforms& pu, const MetalMat4& wvp, const CNA::Internal::Backends::GpuDrawParams& params)
    {
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
        pu.eyePosition[0]=params.eyePositionWorld[0]; pu.eyePosition[1]=params.eyePositionWorld[1]; pu.eyePosition[2]=params.eyePositionWorld[2]; pu.eyePosition[3]=0;
        pu.pbrFactors[0]=params.pbrMetallicFactor; pu.pbrFactors[1]=params.pbrRoughnessFactor; pu.pbrFactors[2]=0; pu.pbrFactors[3]=0;
        std::memcpy(pu.alphaTest, params.alphaTest, sizeof(pu.alphaTest));
        pu.fogColorEnabled[0]=params.fogColor[0]; pu.fogColorEnabled[1]=params.fogColor[1]; pu.fogColorEnabled[2]=params.fogColor[2]; pu.fogColorEnabled[3]=params.fogEnabled?1.0f:0.0f;
        pu.fogStartEnd[0]=params.fogStart; pu.fogStartEnd[1]=params.fogEnd; pu.fogStartEnd[2]=0; pu.fogStartEnd[3]=0;
    }

    // plan_metal.md METAL-82: fills SkinnedPbrTransform/PbrUniforms from GpuDrawParams. The uniform
    // (fragment-side) fields are identical to FillMetalPbrUniforms' -- only the transform struct
    // differs (adds skinParams, matching FillMetalSkinnedUniforms' own t.skinParams handling), so
    // this delegates the uniform fill to FillMetalPbrUniforms via a throwaway MetalPbrTransform and
    // copies just the shared fields across, rather than duplicating every uniform assignment a third
    // time.
    inline void FillMetalSkinnedPbrUniforms(MetalSkinnedPbrTransform& t, MetalPbrUniforms& pu, const MetalMat4& wvp, const CNA::Internal::Backends::GpuDrawParams& params)
    {
        MetalPbrTransform unusedT{};
        FillMetalPbrUniforms(unusedT, pu, wvp, params);
        std::memcpy(t.wvp, wvp.m, sizeof(t.wvp));
        std::memcpy(t.world, params.worldColMajor, sizeof(t.world));
        t.skinParams[0]=(float)params.weightsPerVertex; t.skinParams[1]=t.skinParams[2]=t.skinParams[3]=0;
    }
}
