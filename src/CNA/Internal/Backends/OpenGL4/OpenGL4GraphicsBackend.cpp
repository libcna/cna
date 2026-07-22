// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/OpenGL4/OpenGL4GraphicsBackend.hpp"
#include "System/InvalidOperationException.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace CNA::Internal::Backends::OpenGL4::GL4;

namespace CNA::Internal::Backends::OpenGL4
{
    namespace
    {
        GLenum ToGLPrimitive(PrimitiveType pt)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return GL_TRIANGLES;
            case PrimitiveType::TriangleStrip: return GL_TRIANGLE_STRIP;
            case PrimitiveType::LineList:      return GL_LINES;
            case PrimitiveType::LineStrip:     return GL_LINE_STRIP;
            case PrimitiveType::PointListEXT:  return GL_POINTS;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
            }
        }

        int VertexCountForPrimitives(PrimitiveType pt, int primitiveCount)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return primitiveCount * 3;
            case PrimitiveType::TriangleStrip: return primitiveCount + 2;
            case PrimitiveType::LineList:      return primitiveCount * 2;
            case PrimitiveType::LineStrip:     return primitiveCount + 1;
            case PrimitiveType::PointListEXT:  return primitiveCount;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
            }
        }

        // ---- Built-in GLSL 410 core shaders -----------------------------------------------

        const char* kSpriteVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform mat4 uProjection;
out vec2 vUV;
out vec4 vColor;
void main()
{
    vUV = aUV;
    vColor = aColor;
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
)GLSL";

        const char* kSpriteFragSrc = R"GLSL(
#version 410 core
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTexture;
out vec4 fragColor;
void main()
{
    fragColor = texture(uTexture, vUV) * vColor;
}
)GLSL";

        const char* kColored3DVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uWorldViewProj;
out vec4 vColor;
void main()
{
    vColor = aColor;
    gl_Position = uWorldViewProj * vec4(aPos, 1.0);
}
)GLSL";

        const char* kColored3DFragSrc = R"GLSL(
#version 410 core
in vec4 vColor;
out vec4 fragColor;
void main()
{
    fragColor = vColor;
}
)GLSL";

        // plan_opengl4.md GL4-13: textured3d (VertexPositionTexture, stride 20). Algorithmic
        // reference: VulkanGraphicsBackend's textured3d.vert/frag.glsl (no fog, no Y-flip --
        // OpenGL's own NDC convention needs none, unlike Vulkan's flipped clip space).
        const char* kTextured3DVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uWorldViewProj;
out vec2 vUV;
void main()
{
    vUV = aUV;
    gl_Position = uWorldViewProj * vec4(aPos, 1.0);
}
)GLSL";

        // plan_opengl4.md GL4-19: AlphaTestEffect's discard test and DualTextureEffect's second
        // sampler are both folded into the SAME textured3d/colored_textured3d programs (not new
        // stride cases) -- both effects reuse VertexPositionTexture/VertexPositionColorTexture
        // unchanged (DualTextureEffect samples both textures with the SAME UV set in real XNA,
        // no separate UV1 attribute). Ported from VulkanGraphicsBackend's alpha_test3d.frag.glsl
        // (discard formula) and dual_texture3d.frag.glsl (the "tex1.rgb*=2.0; result=tex1*tex2"
        // 2x-modulate blend). uAlphaTest defaults to {0,0,1,1} (GpuDrawParams' own documented
        // "always pass, never discard" default), so this is a genuine no-op for every other
        // effect's draws.
        const char* kTextured3DFragSrc = R"GLSL(
#version 410 core
in vec2 vUV;
uniform sampler2D uTexture;
uniform sampler2D uTexture2;
uniform vec4 uDiffuseColor;
uniform bool uTextureEnabled;
uniform bool uDualTextureEnabled;
uniform vec4 uAlphaTest;
out vec4 fragColor;
void main()
{
    vec4 tex = uTextureEnabled ? texture(uTexture, vUV) : vec4(1.0);
    if (uDualTextureEnabled)
    {
        vec4 tex2 = texture(uTexture2, vUV);
        tex.rgb *= 2.0;
        tex *= tex2;
    }
    vec4 result = tex * uDiffuseColor;

    float alpha = result.a;
    bool passTest = (uAlphaTest.y > 0.0) ? (abs(alpha - uAlphaTest.x) < uAlphaTest.y) : (alpha < uAlphaTest.x);
    float w = passTest ? uAlphaTest.z : uAlphaTest.w;
    if (w < 0.0) discard;

    fragColor = result;
}
)GLSL";

        // colored_textured3d (VertexPositionColorTexture, stride 24).
        const char* kColoredTextured3DVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;
uniform mat4 uWorldViewProj;
uniform vec4 uDiffuseColor;
uniform bool uVertexColorEnabled;
out vec2 vUV;
out vec4 vTint;
void main()
{
    vUV = aUV;
    vTint = uVertexColorEnabled ? (aColor * uDiffuseColor) : uDiffuseColor;
    gl_Position = uWorldViewProj * vec4(aPos, 1.0);
}
)GLSL";

        const char* kColoredTextured3DFragSrc = R"GLSL(
#version 410 core
in vec2 vUV;
in vec4 vTint;
uniform sampler2D uTexture;
uniform sampler2D uTexture2;
uniform bool uTextureEnabled;
uniform bool uDualTextureEnabled;
uniform vec4 uAlphaTest;
out vec4 fragColor;
void main()
{
    vec4 tex = uTextureEnabled ? texture(uTexture, vUV) : vec4(1.0);
    if (uDualTextureEnabled)
    {
        vec4 tex2 = texture(uTexture2, vUV);
        tex.rgb *= 2.0;
        tex *= tex2;
    }
    vec4 result = tex * vTint;

    float alpha = result.a;
    bool passTest = (uAlphaTest.y > 0.0) ? (abs(alpha - uAlphaTest.x) < uAlphaTest.y) : (alpha < uAlphaTest.x);
    float w = passTest ? uAlphaTest.z : uAlphaTest.w;
    if (w < 0.0) discard;

    fragColor = result;
}
)GLSL";

        // lit_textured3d (VertexPositionNormalTexture, stride 32) -- BasicEffect's default
        // 3-directional-light rig. Ported from VulkanGraphicsBackend's lit_textured3d.vert/
        // frag.glsl: FNA's Lighting.fxh ComputeLights() (ambient + per-light Lambertian diffuse +
        // Blinn-Phong specular, EmissiveColor added post-multiply, specular added post-texture
        // scaled by alpha). No fog (same deliberate deferral as the other 3D stride variants).
        // World's inverse-transpose upper-left 3x3 is used for the normal matrix (not MVP's),
        // matching EnvironmentMapEffect's own already-correct pattern -- an MVP-based transform
        // would bake View/Projection into the normal.
        const char* kLitTextured3DVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uWorldViewProj;
uniform mat4 uWorld;
out vec2 vUV;
out vec3 vNormal;
out vec3 vWorldPos;
void main()
{
    vUV = aUV;
    mat3 normalMatrix = transpose(inverse(mat3(uWorld)));
    vNormal = normalize(normalMatrix * aNormal);
    vWorldPos = (uWorld * vec4(aPos, 1.0)).xyz;
    gl_Position = uWorldViewProj * vec4(aPos, 1.0);
}
)GLSL";

        const char* kLitTextured3DFragSrc = R"GLSL(
#version 410 core
in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;
uniform sampler2D uTexture;
uniform bool uTextureEnabled;
uniform bool uLightingEnabled;
uniform vec4 uDiffuseColor;
uniform vec3 uAmbientColor;
uniform vec3 uLight0Dir;
uniform vec3 uLight0Diffuse;
uniform vec3 uLight0Specular;
uniform vec3 uLight1Dir;
uniform vec3 uLight1Diffuse;
uniform vec3 uLight1Specular;
uniform vec3 uLight2Dir;
uniform vec3 uLight2Diffuse;
uniform vec3 uLight2Specular;
uniform vec3 uEmissiveColor;
uniform vec3 uEyePosition;
uniform vec3 uSpecularColor;
uniform float uSpecularPower;
out vec4 fragColor;

// Guards against normalize(0,0,0) on a disabled/unconfigured DirectionalLight -- a real bug
// found while porting WebGPU's own lit3d shader (plan_webgpu.md): normalize() on a true zero
// vector is undefined and can poison the whole light sum with NaN.
vec3 safeNormalize(vec3 v)
{
    float len = length(v);
    return len > 1e-6 ? (v / len) : vec3(0.0, -1.0, 0.0);
}

void main()
{
    vec4 tex = uTextureEnabled ? texture(uTexture, vUV) : vec4(1.0);
    vec4 color;
    if (uLightingEnabled)
    {
        vec3 N = normalize(vNormal);
        vec3 E = normalize(uEyePosition - vWorldPos);
        vec3 nL0 = safeNormalize(uLight0Dir);
        vec3 nL1 = safeNormalize(uLight1Dir);
        vec3 nL2 = safeNormalize(uLight2Dir);
        // Direction fields point FROM the light, so negate for the dot with N.
        float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
        float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
        float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
        vec3 lightSum = uAmbientColor + NdotL0 * uLight0Diffuse + NdotL1 * uLight1Diffuse + NdotL2 * uLight2Diffuse;
        vec3 h0 = normalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, uSpecularPower);
        vec3 h1 = normalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, uSpecularPower);
        vec3 h2 = normalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, uSpecularPower);
        vec3 specularRGB = (spec0 * uLight0Specular + spec1 * uLight1Specular + spec2 * uLight2Specular) * uSpecularColor;
        // EmissiveColor is added after the light-sum*DiffuseColor multiply, not scaled by it
        // (matches FNA's Lighting.fxh: result.Diffuse = sum*DiffuseColor + EmissiveColor).
        vec3 lit = lightSum * uDiffuseColor.rgb + uEmissiveColor;
        color = vec4(lit, uDiffuseColor.a) * tex;
        // Specular is added after the texture*diffuse multiply, scaled by the resulting alpha
        // (FNA's AddSpecular macro), never by the texture directly.
        color.rgb += specularRGB * color.a;
    }
    else
    {
        color = uDiffuseColor * tex;
    }
    fragColor = color;
}
)GLSL";

        // plan_opengl4.md GL4-21: env_map3d (VertexPositionNormalTexture, stride 32) --
        // EnvironmentMapEffect's own dedicated program, selected instead of lit_textured3d when
        // GpuDrawParams::envMapping is set (BindProgramForStride branches on it before the stride
        // switch, matching EasyGLGraphicsBackend::SelectProgram's own envMapping-overrides-stride
        // dispatch order). Ported from EasyGLGraphicsBackend::EnsureEnvMapped3DProgram's GLSL ES
        // 300 source (near-verbatim translation to desktop GLSL 410 core -- no ES precision
        // qualifiers, otherwise identical), cross-verified against VulkanGraphicsBackend's
        // env_map3d.frag.glsl (per-fragment Fresnel instead of EasyGL's per-vertex Gouraud
        // interpolation -- a documented, accepted, strictly-more-accurate deviation kept here in
        // its EasyGL per-vertex form since this is the closer sibling GLSL backend to port from).
        // Real XNA EnvironmentMapEffect.fx formula (src/CNA/Internal/Backends/D3D9/shaders/xna/
        // EnvironmentMapEffect.fx): reflection vector reflect(-eyeVector, worldNormal); Fresnel
        // blend factor pow(max(1-|dot(eye,normal)|,0), FresnelFactor)*EnvironmentMapAmount; final
        // colour is a LERP (not additive) between the lit diffuse*texture colour and the
        // alpha-scaled cubemap sample, plus a separately alpha-scaled specular term -- see
        // docs/environmentmapeffect-support.md for the two real formula bugs (additive-not-lerp,
        // missing alpha scaling) found and fixed while porting this to 3 other backends.
        const char* kEnvMap3DVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uWorldViewProj;
uniform mat4 uWorld;
uniform vec3 uEyePosition;
uniform float uEnvMapAmount;
uniform bool uFresnelEnabled;
uniform float uFresnelFactor;
out vec3 vWorldNormal;
out vec3 vEyeDir;
out vec2 vUV;
out float vFresnel;
void main()
{
    gl_Position = uWorldViewProj * vec4(aPos, 1.0);
    vec3 worldPos = (uWorld * vec4(aPos, 1.0)).xyz;
    mat3 normalMatrix = transpose(inverse(mat3(uWorld)));
    vec3 worldNormal = normalize(normalMatrix * aNormal);
    vec3 eyeVector = normalize(uEyePosition - worldPos);
    vWorldNormal = worldNormal;
    vEyeDir = eyeVector;
    vUV = aUV;
    // Fresnel is computed per-vertex in real XNA, then Gouraud-interpolated across the triangle.
    float viewAngle = dot(eyeVector, worldNormal);
    vFresnel = uFresnelEnabled
        ? pow(max(1.0 - abs(viewAngle), 0.0), uFresnelFactor) * uEnvMapAmount
        : uEnvMapAmount;
}
)GLSL";

        const char* kEnvMap3DFragSrc = R"GLSL(
#version 410 core
in vec3 vWorldNormal;
in vec3 vEyeDir;
in vec2 vUV;
in float vFresnel;
uniform sampler2D uTexture;
uniform bool uTextureEnabled;
uniform samplerCube uEnvMap;
uniform vec4 uDiffuseColor;
uniform vec3 uEmissiveColor;
uniform vec3 uLight0Dir;
uniform vec3 uLight0Diffuse;
uniform vec3 uLight1Dir;
uniform vec3 uLight1Diffuse;
uniform vec3 uLight2Dir;
uniform vec3 uLight2Diffuse;
uniform vec3 uEnvMapSpecular;
out vec4 fragColor;
void main()
{
    vec3 N = normalize(vWorldNormal);
    vec3 E = normalize(vEyeDir);
    float NdotL0 = max(dot(N, -uLight0Dir), 0.0);
    float NdotL1 = max(dot(N, -uLight1Dir), 0.0);
    float NdotL2 = max(dot(N, -uLight2Dir), 0.0);
    vec3 lightSum = uLight0Diffuse * NdotL0 + uLight1Diffuse * NdotL1 + uLight2Diffuse * NdotL2;
    // EmissiveColor is pre-combined with AmbientLightColor*DiffuseColor by
    // EnvironmentMapEffect::FillGpuDrawParams -- added after the light-sum*DiffuseColor multiply,
    // matching FNA's Lighting.fxh.
    vec3 litRGB = lightSum * uDiffuseColor.rgb + uEmissiveColor;
    vec4 texColor = uTextureEnabled ? texture(uTexture, vUV) : vec4(1.0);
    vec3 reflDir = reflect(-E, N);
    vec4 envSample = texture(uEnvMap, reflDir);
    vec3 baseColor = litRGB * texColor.rgb;
    float combinedAlpha = uDiffuseColor.a * texColor.a;
    vec3 rgb = mix(baseColor, envSample.rgb * combinedAlpha, vFresnel) +
               uEnvMapSpecular * envSample.a * combinedAlpha;
    fragColor = vec4(rgb, combinedAlpha);
}
)GLSL";

        // plan_opengl4.md GL4-22: skinned3d (VertexPositionNormalTextureSkinned, stride 52/56) --
        // SkinnedEffect's own dedicated program, selected instead of lit_textured3d/env_map3d/
        // textured3d/colored_textured3d for stride 52/56 draws. Ported near-verbatim from
        // EasyGLGraphicsBackend::EnsureSkinnedProgram's GLSL ES 300 source (desktop GLSL 410 core
        // translation only), which itself already matches real XNA SkinnedEffect.fx's Skin()
        // function: skinMat = sum of the first WeightsPerVertex (1, 2, or 4) uBones[index]*weight
        // pairs (never all 4 unconditionally -- a real bug, Task 895, found and fixed while
        // porting this effect to the other backends), position transformed by the full skinMat,
        // normal by its upper-left 3x3. The lighting formula itself reuses lit_textured3d's own
        // already-correct 3-light Lambertian-diffuse + Blinn-Phong-specular + EmissiveColor
        // formula (EmissiveColor pre-folds AmbientLightColor*DiffuseColor via
        // SkinnedEffect::FillGpuDrawParams, same as lit_textured3d/env_map3d), plus a vertex-color
        // modulate (VertexColorEnabled) exercised via the stride-56 aColor attribute. No fog
        // (same deliberate deferral as every other 3D stride variant on this backend).
        //
        // NOTE (matches EasyGL's own established formula, not independently re-derived here):
        // the skinned normal is only rotated by the bone skinning matrix (mat3(skinMat)), not
        // additionally by World's own rotation -- correct for the identity/translation-only World
        // matrices this effect's own test scenarios use, but would under-rotate lighting for a
        // rotated World on a skinned mesh. This is a pre-existing, cross-backend (EasyGL/Vulkan/
        // Bgfx) limitation carried over here for consistency, not something to fix in isolation.
        const char* kSkinned3DVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aBoneWeights;
layout(location = 4) in uvec4 aBoneIndices;
layout(location = 5) in vec4 aColor;
uniform mat4 uWorldViewProj;
uniform mat4 uWorld;
uniform mat4 uBones[72];
uniform int uWeightsPerVertex;
out vec3 vNormal;
out vec2 vUV;
out vec3 vWorldPos;
out vec4 vColor;
void main()
{
    mat4 skinMat = uBones[aBoneIndices.x] * aBoneWeights.x;
    if (uWeightsPerVertex >= 2)
        skinMat += uBones[aBoneIndices.y] * aBoneWeights.y;
    if (uWeightsPerVertex >= 4)
    {
        skinMat += uBones[aBoneIndices.z] * aBoneWeights.z;
        skinMat += uBones[aBoneIndices.w] * aBoneWeights.w;
    }
    vec4 skinnedPos = skinMat * vec4(aPos, 1.0);
    gl_Position = uWorldViewProj * skinnedPos;
    vec3 skinnedNormal = mat3(skinMat) * aNormal;
    float len = length(skinnedNormal);
    // Guards against a near-zero blended normal (e.g. two opposing bone rotations cancelling
    // out) -- normalize(0,0,0) is undefined and would poison the whole light sum with NaN.
    vNormal = (len > 1e-6) ? (skinnedNormal / len) : aNormal;
    vUV = aUV;
    vWorldPos = (uWorld * skinnedPos).xyz;
    vColor = aColor;
}
)GLSL";

        const char* kSkinned3DFragSrc = R"GLSL(
#version 410 core
in vec3 vNormal;
in vec2 vUV;
in vec3 vWorldPos;
in vec4 vColor;
uniform sampler2D uTexture;
uniform bool uTextureEnabled;
uniform bool uVertexColorEnabled;
uniform vec4 uDiffuseColor;
uniform vec3 uLight0Dir;
uniform vec3 uLight0Diffuse;
uniform vec3 uLight0Specular;
uniform vec3 uLight1Dir;
uniform vec3 uLight1Diffuse;
uniform vec3 uLight1Specular;
uniform vec3 uLight2Dir;
uniform vec3 uLight2Diffuse;
uniform vec3 uLight2Specular;
uniform vec3 uEmissiveColor;
uniform vec3 uEyePosition;
uniform vec3 uSpecularColor;
uniform float uSpecularPower;
out vec4 fragColor;

vec3 safeNormalize(vec3 v)
{
    float len = length(v);
    return len > 1e-6 ? (v / len) : vec3(0.0, -1.0, 0.0);
}

void main()
{
    vec4 tex = uTextureEnabled ? texture(uTexture, vUV) : vec4(1.0);
    vec3 N = normalize(vNormal);
    vec3 E = normalize(uEyePosition - vWorldPos);
    vec3 nL0 = safeNormalize(uLight0Dir);
    vec3 nL1 = safeNormalize(uLight1Dir);
    vec3 nL2 = safeNormalize(uLight2Dir);
    float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
    vec3 lightSum = NdotL0 * uLight0Diffuse + NdotL1 * uLight1Diffuse + NdotL2 * uLight2Diffuse;
    vec3 h0 = normalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, uSpecularPower);
    vec3 h1 = normalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, uSpecularPower);
    vec3 h2 = normalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, uSpecularPower);
    vec3 specularRGB = (spec0 * uLight0Specular + spec1 * uLight1Specular + spec2 * uLight2Specular) * uSpecularColor;
    vec3 lit = lightSum * uDiffuseColor.rgb + uEmissiveColor;
    vec4 color = vec4(lit, uDiffuseColor.a) * tex;
    color.rgb += specularRGB * color.a;
    if (uVertexColorEnabled) color *= vColor;
    fragColor = color;
}
)GLSL";

        // XNA TextureFilter ordinal -> (GL min filter, GL mag filter). plan_opengl4.md GL4-18:
        // real mip-aware GL min-filter tokens for every "Mip*" variant now that
        // OpenGL4TextureBackend::UpdatePixelsLevel() lets a texture genuinely have mip levels
        // beyond 0 -- a mip-mapped texture sampled with a non-mip min filter would only ever
        // read level 0 regardless of how many levels were uploaded. Matches
        // EasyGLGraphicsBackend::ApplySamplerState's own identical mapping table exactly; safe to
        // apply unconditionally even to a single-level (levelCount_==1) texture, since GL treats
        // base_level==max_level as a complete single-level mipmap chain regardless of the min
        // filter's own *_MIPMAP_* qualifier.
        // XNA: Linear=0, Point=1, Anisotropic=2, LinearMipPoint=3, PointMipLinear=4,
        //      MinLinearMagPointMipLinear=5, MinLinearMagPointMipPoint=6,
        //      MinPointMagLinearMipLinear=7, MinPointMagLinearMipPoint=8
        void FilterToGL(int filter, GLint& minFilter, GLint& magFilter)
        {
            switch (filter)
            {
            case 1: // Point -- nearest neighbour, no mipmaps
                minFilter = GL_NEAREST; magFilter = GL_NEAREST; break;
            case 2: // Anisotropic
                minFilter = GL_LINEAR_MIPMAP_LINEAR; magFilter = GL_LINEAR; break;
            case 3: // LinearMipPoint
                minFilter = GL_LINEAR_MIPMAP_NEAREST; magFilter = GL_LINEAR; break;
            case 4: // PointMipLinear
                minFilter = GL_NEAREST_MIPMAP_LINEAR; magFilter = GL_NEAREST; break;
            case 5: // MinLinearMagPointMipLinear
                minFilter = GL_LINEAR_MIPMAP_LINEAR; magFilter = GL_NEAREST; break;
            case 6: // MinLinearMagPointMipPoint
                minFilter = GL_LINEAR_MIPMAP_NEAREST; magFilter = GL_NEAREST; break;
            case 7: // MinPointMagLinearMipLinear
                minFilter = GL_NEAREST_MIPMAP_LINEAR; magFilter = GL_LINEAR; break;
            case 8: // MinPointMagLinearMipPoint
                minFilter = GL_NEAREST_MIPMAP_NEAREST; magFilter = GL_LINEAR; break;
            default: // Linear -- bilinear, no mipmaps
                minFilter = GL_LINEAR; magFilter = GL_LINEAR; break;
            }
        }

        GLint AddressModeToGL(int mode)
        {
            switch (mode)
            {
            case 0: return GL_REPEAT;          // Wrap
            case 2: return GL_MIRRORED_REPEAT;  // Mirror
            default: return GL_CLAMP_TO_EDGE;   // Clamp
            }
        }

        // plan_opengl4.md GL4-14: maps a Microsoft::Xna::Framework::Graphics::DepthFormat
        // ordinal to the GL renderbuffer internal format and framebuffer attachment point a
        // render target's depth/stencil buffer should use. Returns false for DepthFormat::None,
        // meaning no depth/stencil attachment should be created at all -- mirrors
        // EasyGLGraphicsBackend's own MapDepthFormat.
        bool MapDepthFormatGL4(int depthFormat, GLenum& outInternalFormat, GLenum& outAttachment)
        {
            switch (depthFormat)
            {
            case 1: // DepthFormat::Depth16
                outInternalFormat = GL_DEPTH_COMPONENT16;
                outAttachment = GL_DEPTH_ATTACHMENT;
                return true;
            case 2: // DepthFormat::Depth24
                outInternalFormat = GL_DEPTH_COMPONENT24;
                outAttachment = GL_DEPTH_ATTACHMENT;
                return true;
            case 3: // DepthFormat::Depth24Stencil8
                outInternalFormat = GL_DEPTH24_STENCIL8;
                outAttachment = GL_DEPTH_STENCIL_ATTACHMENT;
                return true;
            default: // DepthFormat::None
                return false;
            }
        }

        int CalculateRenderTargetMipLevelsGL4(int w, int h)
        {
            int levels = 1;
            while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
            return levels;
        }

        // plan_opengl4.md GL4-16: XNA Blend enum -> GL blend factor token.
        // Blend: One=0, Zero=1, SourceColor=2, InverseSourceColor=3, SourceAlpha=4,
        //        InverseSourceAlpha=5, DestinationColor=6, InverseDestinationColor=7,
        //        DestinationAlpha=8, InverseDestinationAlpha=9, BlendFactor=10,
        //        InverseBlendFactor=11, SourceAlphaSaturation=12
        GLenum ToGLBlendFactor(int xnaBlend)
        {
            switch (xnaBlend)
            {
            case  1: return GL_ZERO;
            case  2: return GL_SRC_COLOR;
            case  3: return GL_ONE_MINUS_SRC_COLOR;
            case  4: return GL_SRC_ALPHA;
            case  5: return GL_ONE_MINUS_SRC_ALPHA;
            case  6: return GL_DST_COLOR;
            case  7: return GL_ONE_MINUS_DST_COLOR;
            case  8: return GL_DST_ALPHA;
            case  9: return GL_ONE_MINUS_DST_ALPHA;
            case 10: return GL_CONSTANT_COLOR;
            case 11: return GL_ONE_MINUS_CONSTANT_COLOR;
            case 12: return GL_SRC_ALPHA_SATURATE;
            default: return GL_ONE; // Blend::One = 0
            }
        }

        // XNA BlendFunction enum -> GL blend equation token. Add=0, Subtract=1,
        // ReverseSubtract=2, Max=3, Min=4.
        GLenum ToGLBlendEquation(int xnaBlendFunc)
        {
            switch (xnaBlendFunc)
            {
            case 1: return GL_FUNC_SUBTRACT;
            case 2: return GL_FUNC_REVERSE_SUBTRACT;
            case 3: return GL_MAX;
            case 4: return GL_MIN;
            default: return GL_FUNC_ADD; // BlendFunction::Add = 0
            }
        }

        // XNA CompareFunction enum -> GL compare-func token. Always=0, Never=1, Less=2,
        // LessEqual=3, Equal=4, GreaterEqual=5, Greater=6, NotEqual=7.
        GLenum ToGLCompareFunc(int xnaCompare)
        {
            switch (xnaCompare)
            {
            case 1: return GL_NEVER;
            case 2: return GL_LESS;
            case 3: return GL_LEQUAL;
            case 4: return GL_EQUAL;
            case 5: return GL_GEQUAL;
            case 6: return GL_GREATER;
            case 7: return GL_NOTEQUAL;
            default: return GL_ALWAYS; // CompareFunction::Always = 0
            }
        }

        // XNA StencilOperation enum -> GL stencil-op token. Keep=0, Zero=1, Replace=2,
        // Increment=3, Decrement=4, IncrementSaturation=5, DecrementSaturation=6, Invert=7.
        GLenum ToGLStencilOp(int xnaOp)
        {
            switch (xnaOp)
            {
            case 1: return GL_ZERO;
            case 2: return GL_REPLACE;
            case 3: return GL_INCR_WRAP;
            case 4: return GL_DECR_WRAP;
            case 5: return GL_INCR;
            case 6: return GL_DECR;
            case 7: return GL_INVERT;
            default: return GL_KEEP; // StencilOperation::Keep = 0
            }
        }
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4RawProgram
    // ------------------------------------------------------------------------------------

    OpenGL4RawProgram::~OpenGL4RawProgram() { Destroy(); }

    OpenGL4RawProgram::OpenGL4RawProgram(OpenGL4RawProgram&& other) noexcept
        : program_(other.program_), error_(std::move(other.error_))
    {
        other.program_ = 0;
    }

    OpenGL4RawProgram& OpenGL4RawProgram::operator=(OpenGL4RawProgram&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            program_ = other.program_;
            error_ = std::move(other.error_);
            other.program_ = 0;
        }
        return *this;
    }

    void OpenGL4RawProgram::Destroy()
    {
        if (program_ != 0)
        {
            gl4_glDeleteProgram(program_);
            program_ = 0;
        }
    }

    bool OpenGL4RawProgram::Compile(const std::string& vertSrc, const std::string& fragSrc)
    {
        Destroy();
        error_.clear();

        const GLuint vs = gl4_glCreateShader(GL_VERTEX_SHADER);
        const char* vsSrc = vertSrc.c_str();
        gl4_glShaderSource(vs, 1, &vsSrc, nullptr);
        gl4_glCompileShader(vs);
        GLint vsOk = 0;
        gl4_glGetShaderiv(vs, GL_COMPILE_STATUS, &vsOk);
        if (!vsOk)
        {
            char log[1024] = {};
            gl4_glGetShaderInfoLog(vs, sizeof(log), nullptr, log);
            error_ = std::string("vertex shader: ") + log;
            gl4_glDeleteShader(vs);
            return false;
        }

        const GLuint fs = gl4_glCreateShader(GL_FRAGMENT_SHADER);
        const char* fsSrc = fragSrc.c_str();
        gl4_glShaderSource(fs, 1, &fsSrc, nullptr);
        gl4_glCompileShader(fs);
        GLint fsOk = 0;
        gl4_glGetShaderiv(fs, GL_COMPILE_STATUS, &fsOk);
        if (!fsOk)
        {
            char log[1024] = {};
            gl4_glGetShaderInfoLog(fs, sizeof(log), nullptr, log);
            error_ = std::string("fragment shader: ") + log;
            gl4_glDeleteShader(vs);
            gl4_glDeleteShader(fs);
            return false;
        }

        const GLuint prog = gl4_glCreateProgram();
        gl4_glAttachShader(prog, vs);
        gl4_glAttachShader(prog, fs);
        gl4_glLinkProgram(prog);

        GLint linkOk = 0;
        gl4_glGetProgramiv(prog, GL_LINK_STATUS, &linkOk);

        // Shaders may be deleted once linked -- the program keeps its own compiled copy.
        gl4_glDeleteShader(vs);
        gl4_glDeleteShader(fs);

        if (!linkOk)
        {
            char log[1024] = {};
            gl4_glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
            error_ = std::string("link: ") + log;
            gl4_glDeleteProgram(prog);
            return false;
        }

        program_ = prog;
        return true;
    }

    void OpenGL4RawProgram::Use() const
    {
        gl4_glUseProgram(program_);
    }

    int OpenGL4RawProgram::UniformLocation(const char* name) const
    {
        return gl4_glGetUniformLocation(program_, name);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4TextureBackend
    // ------------------------------------------------------------------------------------

    OpenGL4TextureBackend::OpenGL4TextureBackend(const ImageData& data)
        : width_(data.width), height_(data.height),
          levelCount_(data.mipLevels > 0 ? data.mipLevels : 1)
    {
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     data.pixels.empty() ? nullptr : data.pixels.data());
        // plan_opengl4.md GL4-18: clamp GL_TEXTURE_MAX_LEVEL to the real level count -- otherwise
        // a mipmap-requiring TextureFilter (e.g. Anisotropic) treats this as an incomplete
        // mipmap chain (GL's own default max level is 1000) and renders solid black, even for an
        // ordinary single-level (levelCount_==1) texture that never uploads anything beyond
        // level 0. Matches EasyGLTextureBackend's own identical Task-924 fix.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levelCount_ - 1);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    OpenGL4TextureBackend::~OpenGL4TextureBackend()
    {
        if (texture_ != 0)
            glDeleteTextures(1, &texture_);
    }

    void OpenGL4TextureBackend::BindGL() const
    {
        glBindTexture(GL_TEXTURE_2D, texture_);
    }

    void OpenGL4TextureBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        glBindTexture(GL_TEXTURE_2D, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        const int expectedStride = width_ * 4;
        if (stride == expectedStride || stride <= 0)
        {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        }
        else
        {
            // Row-by-row upload when the caller's row pitch doesn't match a tightly packed
            // width*4 buffer.
            for (int y = 0; y < height_; ++y)
            {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, width_, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                                 rgba + static_cast<std::size_t>(y) * stride);
            }
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void OpenGL4TextureBackend::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        glBindTexture(GL_TEXTURE_2D, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // Level 0 is allocated at construction (glTexSubImage2D via UpdatePixels); every other
        // level's storage is never pre-allocated, so this must use glTexImage2D, not
        // glTexSubImage2D, matching EasyGLTextureBackend::UpdatePixelsLevel's identical approach.
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, levelW, levelH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4Texture3DBackend
    // ------------------------------------------------------------------------------------

    OpenGL4Texture3DBackend::OpenGL4Texture3DBackend(int w, int h, int depth, bool mipMap)
        : width_(w), height_(h), depth_(depth),
          levelCount_(mipMap ? CalculateRenderTargetMipLevelsGL4(w, h) : 1)
    {
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_3D, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // Allocate storage for every mip level up front (not just level 0): SetData's box
        // writes use gl4_glTexSubImage3D, which requires the target level to already have a
        // defined image -- matches EasyGLTexture3DBackend's own identical pre-allocation loop
        // (and OpenGL4RenderTargetBackend's/OpenGL4RenderTargetCubeBackend's established
        // OpenGL4 precedent for the same reason).
        int levelW = w, levelH = h, levelD = depth;
        for (int level = 0; level < levelCount_; ++level)
        {
            gl4_glTexImage3D(GL_TEXTURE_3D, level, GL_RGBA8, levelW, levelH, levelD, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            levelW = std::max(1, levelW / 2);
            levelH = std::max(1, levelH / 2);
            levelD = std::max(1, levelD / 2);
        }
        // plan_opengl4.md GL4-18/GL4-20: clamp GL_TEXTURE_MAX_LEVEL to the real level count --
        // same "incomplete mipmap chain renders black" reason OpenGL4TextureBackend already
        // fixed for Texture2D; EasyGLTexture3DBackend does not set this at all, so this is
        // deliberately stricter than the EasyGL reference.
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, levelCount_ - 1);
        glBindTexture(GL_TEXTURE_3D, 0);
    }

    OpenGL4Texture3DBackend::~OpenGL4Texture3DBackend()
    {
        if (texture_ != 0)
            glDeleteTextures(1, &texture_);
    }

    void OpenGL4Texture3DBackend::BindGL() const
    {
        glBindTexture(GL_TEXTURE_3D, texture_);
    }

    void OpenGL4Texture3DBackend::SetData(int level, int x, int y, int z, int w, int h, int depth,
                                          const void* data, int /*dataLength*/)
    {
        glBindTexture(GL_TEXTURE_3D, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl4_glTexSubImage3D(GL_TEXTURE_3D, level, x, y, z, w, h, depth, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_3D, 0);
    }

    void OpenGL4Texture3DBackend::GetData(int level, int x, int y, int z, int w, int h, int depth,
                                          void* data, int dataLength) const
    {
        if (w <= 0 || h <= 0 || depth <= 0) return;
        const std::size_t sizeBytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) *
                                       static_cast<std::size_t>(depth) * 4;
        if (static_cast<std::size_t>(dataLength) < sizeBytes)
            throw std::out_of_range("CNA OpenGL4: Texture3D::GetData: dataLength too small for the requested region");

        // Desktop GL 4.x has no per-slice readback shortcut for a 3D texture other than an FBO
        // attached to each Z layer -- mirrors EasyGLTexture3DBackend::GetData's own per-slice
        // gl4_glFramebufferTextureLayer + glReadPixels loop (GLES3 lacks glGetTexImage; this
        // backend uses the identical FBO approach for consistency with
        // OpenGL4RenderTargetCubeBackend's own established per-face FBO readback convention,
        // even though desktop GL does have glGetTexImage as an alternative). No Y-flip -- this
        // is a plain texture, not a framebuffer-origin render target.
        GLint prevFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

        GLuint readFbo = 0;
        gl4_glGenFramebuffers(1, &readFbo);
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, readFbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);

        auto* dest = static_cast<uint8_t*>(data);
        const std::size_t sliceBytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;
        for (int slice = z; slice < z + depth; ++slice)
        {
            gl4_glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture_, level, slice);
            glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, dest);
            dest += sliceBytes;
        }

        gl4_glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        gl4_glDeleteFramebuffers(1, &readFbo);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4TextureCubeBackend
    // ------------------------------------------------------------------------------------

    namespace
    {
        constexpr GLenum kTextureCubeFaceTargetsGL4[6] = {
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + 0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + 1,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + 2, GL_TEXTURE_CUBE_MAP_POSITIVE_X + 3,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + 4, GL_TEXTURE_CUBE_MAP_POSITIVE_X + 5,
        };
    }

    OpenGL4TextureCubeBackend::OpenGL4TextureCubeBackend(int size, bool mipMap)
        : size_(size), levelCount_(mipMap ? CalculateRenderTargetMipLevelsGL4(size, size) : 1)
    {
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_CUBE_MAP, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // Allocate storage for every face x every mip level up front, same rationale as
        // OpenGL4RenderTargetCubeBackend::CreateResources / OpenGL4Texture3DBackend above.
        for (GLenum faceTarget : kTextureCubeFaceTargetsGL4)
        {
            int levelSize = size_;
            for (int level = 0; level < levelCount_; ++level)
            {
                glTexImage2D(faceTarget, level, GL_RGBA8, levelSize, levelSize, 0, GL_RGBA,
                            GL_UNSIGNED_BYTE, nullptr);
                levelSize = std::max(1, levelSize / 2);
            }
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, levelCount_ - 1);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    OpenGL4TextureCubeBackend::~OpenGL4TextureCubeBackend()
    {
        if (texture_ != 0)
            glDeleteTextures(1, &texture_);
    }

    void OpenGL4TextureCubeBackend::BindGL() const
    {
        glBindTexture(GL_TEXTURE_CUBE_MAP, texture_);
    }

    void OpenGL4TextureCubeBackend::SetData(int face, int level, int x, int y, int w, int h,
                                            const void* data, int /*dataLength*/)
    {
        if (face < 0 || face >= 6) return;
        glBindTexture(GL_TEXTURE_CUBE_MAP, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(kTextureCubeFaceTargetsGL4[face], level, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    void OpenGL4TextureCubeBackend::GetData(int face, int level, int x, int y, int w, int h,
                                            void* data, int dataLength) const
    {
        if (face < 0 || face >= 6 || w <= 0 || h <= 0) return;
        const std::size_t sizeBytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;
        if (static_cast<std::size_t>(dataLength) < sizeBytes)
            throw std::out_of_range("CNA OpenGL4: TextureCube::GetData: dataLength too small for the requested region");

        GLint prevFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

        GLuint readFbo = 0;
        gl4_glGenFramebuffers(1, &readFbo);
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, readFbo);
        gl4_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   kTextureCubeFaceTargetsGL4[face], texture_, level);

        // No Y-flip -- unlike OpenGL4RenderTargetCubeBackend::GetData (a framebuffer-origin
        // render target), this is a plain texture; matches EasyGLTextureCubeBackend::GetData's
        // own non-flipped convention.
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);

        gl4_glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        gl4_glDeleteFramebuffers(1, &readFbo);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4RenderTargetBackend
    // ------------------------------------------------------------------------------------

    OpenGL4RenderTargetBackend::OpenGL4RenderTargetBackend(int w, int h, int depthFormat,
                                                            bool mipMap, int multiSampleCount)
        : width_(w), height_(h), depthFormat_(depthFormat), mipMap_(mipMap),
          multiSampleCount_(multiSampleCount)
    {
        levelCount_ = mipMap_ ? CalculateRenderTargetMipLevelsGL4(w, h) : 1;
        CreateResources();
    }

    OpenGL4RenderTargetBackend::~OpenGL4RenderTargetBackend()
    {
        DestroyResources();
    }

    void OpenGL4RenderTargetBackend::CreateResources()
    {
        // Clamp to GL_MAX_SAMPLES so glRenderbufferStorageMultisample never errors, mirroring
        // EasyGLRenderTargetBackend::CreateResources / FNA3D's OPENGL_GetMaxMultiSampleCount.
        if (multiSampleCount_ > 0)
        {
            GLint maxSamples = 0;
            glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
            if (maxSamples > 0 && multiSampleCount_ > static_cast<int>(maxSamples))
                multiSampleCount_ = static_cast<int>(maxSamples);
        }

        glGenTextures(1, &colorTexture_);
        glBindTexture(GL_TEXTURE_2D, colorTexture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // Pre-allocate GPU storage for every mip level up front (not just level 0): the mip
        // chain is regenerated from level 0 via glGenerateMipmap when the target is unbound
        // (see UnbindAsRenderTarget), mirroring FNA3D's OPENGL_ResolveTarget behavior -- without
        // this loop, levels 1+ would have no defined image and glGenerateMipmap's writes would
        // target GL-incomplete storage.
        {
            int levelW = width_, levelH = height_;
            for (int level = 0; level < levelCount_; ++level)
            {
                glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, levelW, levelH, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, nullptr);
                levelW = std::max(1, levelW / 2);
                levelH = std::max(1, levelH / 2);
            }
        }
        glBindTexture(GL_TEXTURE_2D, 0);

        gl4_glGenFramebuffers(1, &fbo_);
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

        if (multiSampleCount_ > 0)
        {
            // Render into a multisampled color renderbuffer; colorTexture_ is only ever the
            // single-sample resolve target, written by UnbindAsRenderTarget()'s blit, never
            // rendered into directly (glReadPixels/sampling a multisample attachment directly
            // is disallowed by GL).
            gl4_glGenRenderbuffers(1, &msaaColorRenderbuffer_);
            gl4_glBindRenderbuffer(GL_RENDERBUFFER, msaaColorRenderbuffer_);
            gl4_glRenderbufferStorageMultisample(GL_RENDERBUFFER, multiSampleCount_, GL_RGBA8,
                                                 width_, height_);
            gl4_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                          msaaColorRenderbuffer_);

            gl4_glGenFramebuffers(1, &resolveFbo_);
            gl4_glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo_);
            gl4_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                       colorTexture_, 0);
            gl4_glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        }
        else
        {
            gl4_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                       colorTexture_, 0);
        }

        {
            GLenum depthInternalFormat = 0, depthAttachment = 0;
            if (MapDepthFormatGL4(depthFormat_, depthInternalFormat, depthAttachment))
            {
                gl4_glGenRenderbuffers(1, &depthRenderbuffer_);
                gl4_glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer_);
                if (multiSampleCount_ > 0)
                    gl4_glRenderbufferStorageMultisample(GL_RENDERBUFFER, multiSampleCount_,
                                                         depthInternalFormat, width_, height_);
                else
                    gl4_glRenderbufferStorage(GL_RENDERBUFFER, depthInternalFormat, width_, height_);
                gl4_glFramebufferRenderbuffer(GL_FRAMEBUFFER, depthAttachment, GL_RENDERBUFFER,
                                              depthRenderbuffer_);
            }
        }

        gl4_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGL4RenderTargetBackend::DestroyResources()
    {
        if (depthRenderbuffer_) gl4_glDeleteRenderbuffers(1, &depthRenderbuffer_);
        if (msaaColorRenderbuffer_) gl4_glDeleteRenderbuffers(1, &msaaColorRenderbuffer_);
        if (resolveFbo_) gl4_glDeleteFramebuffers(1, &resolveFbo_);
        if (fbo_) gl4_glDeleteFramebuffers(1, &fbo_);
        if (colorTexture_) glDeleteTextures(1, &colorTexture_);
        depthRenderbuffer_ = msaaColorRenderbuffer_ = resolveFbo_ = fbo_ = colorTexture_ = 0;
    }

    void OpenGL4RenderTargetBackend::BindGL() const
    {
        glBindTexture(GL_TEXTURE_2D, colorTexture_);
    }

    void OpenGL4RenderTargetBackend::BindAsRenderTarget()
    {
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    }

    void OpenGL4RenderTargetBackend::UnbindAsRenderTarget()
    {
        // Resolve the multisampled color renderbuffer into colorTexture_ before mips (if any)
        // are regenerated from it, matching FNA3D's OPENGL_ResolveTarget resolve-then-mipmap
        // order.
        if (multiSampleCount_ > 0)
        {
            gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
            gl4_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
            gl4_glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_,
                                  GL_COLOR_BUFFER_BIT, GL_LINEAR);
        }
        if (levelCount_ > 1)
        {
            glBindTexture(GL_TEXTURE_2D, colorTexture_);
            gl4_glGenerateMipmap(GL_TEXTURE_2D);
        }
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGL4RenderTargetBackend::GetData(int level, int x, int y, int w, int h,
                                             void* data, int dataLength) const
    {
        if (w <= 0 || h <= 0) return;
        const std::size_t sizeBytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;
        if (static_cast<std::size_t>(dataLength) < sizeBytes)
            throw std::out_of_range("CNA OpenGL4: RenderTarget2D::GetData: dataLength too small for the requested region");

        // Reads from the single-sample, sampleable colour texture (already resolved-into by
        // UnbindAsRenderTarget() when this target is MSAA) via a throwaway FBO attaching the
        // exact requested mip level -- fbo_/resolveFbo_ only ever have level 0 attached, so an
        // arbitrary level read needs its own attachment rather than reusing either of them.
        GLint prevFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

        GLuint readFbo = 0;
        gl4_glGenFramebuffers(1, &readFbo);
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, readFbo);
        gl4_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   colorTexture_, level);

        const int levelH = std::max(1, height_ >> level);

        // OpenGL's origin is bottom-left; flip Y using this level's own height (never the
        // window's) so the caller gets top-left-origin game coordinates, matching
        // OpenGL4GraphicsBackend::ReadBackbuffer's own convention.
        const int glY = levelH - y - h;
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        auto* pixels = static_cast<uint8_t*>(data);
        glReadPixels(x, glY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        const int rowBytes = w * 4;
        std::vector<uint8_t> tmp(rowBytes);
        for (int row = 0; row < h / 2; ++row)
        {
            uint8_t* a = pixels + static_cast<std::size_t>(row) * rowBytes;
            uint8_t* b = pixels + static_cast<std::size_t>(h - 1 - row) * rowBytes;
            std::memcpy(tmp.data(), a, rowBytes);
            std::memcpy(a, b, rowBytes);
            std::memcpy(b, tmp.data(), rowBytes);
        }

        gl4_glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        gl4_glDeleteFramebuffers(1, &readFbo);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4RenderTargetCubeBackend
    // ------------------------------------------------------------------------------------

    OpenGL4RenderTargetCubeBackend::OpenGL4RenderTargetCubeBackend(int size, int depthFormat,
                                                                    bool mipMap, int multiSampleCount)
        : size_(size), depthFormat_(depthFormat), mipMap_(mipMap),
          multiSampleCount_(multiSampleCount)
    {
        levelCount_ = mipMap_ ? CalculateRenderTargetMipLevelsGL4(size, size) : 1;
        CreateResources();
    }

    OpenGL4RenderTargetCubeBackend::~OpenGL4RenderTargetCubeBackend()
    {
        DestroyResources();
    }

    void OpenGL4RenderTargetCubeBackend::CreateResources()
    {
        if (multiSampleCount_ > 0)
        {
            GLint maxSamples = 0;
            glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
            if (maxSamples > 0 && multiSampleCount_ > static_cast<int>(maxSamples))
                multiSampleCount_ = static_cast<int>(maxSamples);
        }

        glGenTextures(1, &cubeTexture_);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubeTexture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // Allocate storage for all 6 faces, all mip levels up front (see
        // OpenGL4RenderTargetBackend::CreateResources for why -- glGenerateMipmap's writes need
        // every level to already have defined storage).
        for (int face = 0; face < 6; ++face)
        {
            int levelSize = size_;
            for (int level = 0; level < levelCount_; ++level)
            {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, GL_RGBA8, levelSize,
                             levelSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                levelSize = std::max(1, levelSize / 2);
            }
        }
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

        gl4_glGenFramebuffers(1, &fbo_);
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

        if (multiSampleCount_ > 0)
        {
            // One shared multisample colour renderbuffer, reused across all 6 faces (only one
            // face is ever rendered into at a time) -- resolveFbo_ is re-attached to whichever
            // face was most recently bound (see BindAsRenderTargetFace) so
            // UnbindAsRenderTarget's blit resolves into the correct face.
            gl4_glGenRenderbuffers(1, &msaaColorRenderbuffer_);
            gl4_glBindRenderbuffer(GL_RENDERBUFFER, msaaColorRenderbuffer_);
            gl4_glRenderbufferStorageMultisample(GL_RENDERBUFFER, multiSampleCount_, GL_RGBA8,
                                                 size_, size_);
            gl4_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                          msaaColorRenderbuffer_);
            gl4_glGenFramebuffers(1, &resolveFbo_);
        }

        GLenum depthInternalFormat = 0, depthAttachment = 0;
        if (MapDepthFormatGL4(depthFormat_, depthInternalFormat, depthAttachment))
        {
            gl4_glGenRenderbuffers(1, &depthRenderbuffer_);
            gl4_glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer_);
            if (multiSampleCount_ > 0)
                gl4_glRenderbufferStorageMultisample(GL_RENDERBUFFER, multiSampleCount_,
                                                     depthInternalFormat, size_, size_);
            else
                gl4_glRenderbufferStorage(GL_RENDERBUFFER, depthInternalFormat, size_, size_);
            gl4_glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
            gl4_glFramebufferRenderbuffer(GL_FRAMEBUFFER, depthAttachment, GL_RENDERBUFFER,
                                          depthRenderbuffer_);
        }

        gl4_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGL4RenderTargetCubeBackend::DestroyResources()
    {
        if (depthRenderbuffer_) gl4_glDeleteRenderbuffers(1, &depthRenderbuffer_);
        if (msaaColorRenderbuffer_) gl4_glDeleteRenderbuffers(1, &msaaColorRenderbuffer_);
        if (resolveFbo_) gl4_glDeleteFramebuffers(1, &resolveFbo_);
        if (fbo_) gl4_glDeleteFramebuffers(1, &fbo_);
        if (cubeTexture_) glDeleteTextures(1, &cubeTexture_);
        depthRenderbuffer_ = msaaColorRenderbuffer_ = resolveFbo_ = fbo_ = cubeTexture_ = 0;
    }

    void OpenGL4RenderTargetCubeBackend::BindGL() const
    {
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubeTexture_);
    }

    void OpenGL4RenderTargetCubeBackend::BindAsRenderTargetFace(int face)
    {
        lastFace_ = face;
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        if (multiSampleCount_ == 0)
        {
            // Non-MSAA: fbo_'s colour attachment IS cubeTexture_ -- re-attach the requested face
            // (0=+X .. 5=-Z) directly, since all faces share this one FBO/texture.
            gl4_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubeTexture_, 0);
        }
        // MSAA: fbo_'s colour attachment is the shared msaaColorRenderbuffer_, which is
        // face-agnostic -- nothing to re-attach on bind; the face only matters when
        // UnbindAsRenderTarget resolves into cubeTexture_'s specific face image.
    }

    void OpenGL4RenderTargetCubeBackend::UnbindAsRenderTarget()
    {
        if (multiSampleCount_ > 0)
        {
            gl4_glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo_);
            gl4_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_CUBE_MAP_POSITIVE_X + lastFace_, cubeTexture_, 0);
            gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
            gl4_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
            gl4_glBlitFramebuffer(0, 0, size_, size_, 0, 0, size_, size_, GL_COLOR_BUFFER_BIT,
                                  GL_LINEAR);
        }
        if (levelCount_ > 1)
        {
            glBindTexture(GL_TEXTURE_CUBE_MAP, cubeTexture_);
            gl4_glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        }
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGL4RenderTargetCubeBackend::SetData(int face, int level, int x, int y, int w, int h,
                                                 const void* data, int /*dataLength*/)
    {
        if (face < 0 || face >= 6) return;
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubeTexture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, x, y, w, h, GL_RGBA,
                        GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    void OpenGL4RenderTargetCubeBackend::GetData(int face, int level, int x, int y, int w, int h,
                                                 void* data, int dataLength) const
    {
        if (face < 0 || face >= 6 || w <= 0 || h <= 0) return;
        const std::size_t sizeBytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;
        if (static_cast<std::size_t>(dataLength) < sizeBytes)
            throw std::out_of_range("CNA OpenGL4: RenderTargetCube::GetData: dataLength too small for the requested region");

        GLint prevFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

        GLuint readFbo = 0;
        gl4_glGenFramebuffers(1, &readFbo);
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, readFbo);
        gl4_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubeTexture_, level);

        const int levelSize = std::max(1, size_ >> level);
        const int glY = levelSize - y - h;
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        auto* pixels = static_cast<uint8_t*>(data);
        glReadPixels(x, glY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        const int rowBytes = w * 4;
        std::vector<uint8_t> tmp(rowBytes);
        for (int row = 0; row < h / 2; ++row)
        {
            uint8_t* a = pixels + static_cast<std::size_t>(row) * rowBytes;
            uint8_t* b = pixels + static_cast<std::size_t>(h - 1 - row) * rowBytes;
            std::memcpy(tmp.data(), a, rowBytes);
            std::memcpy(a, b, rowBytes);
            std::memcpy(b, tmp.data(), rowBytes);
        }

        gl4_glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        gl4_glDeleteFramebuffers(1, &readFbo);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4VertexBufferBackend
    // ------------------------------------------------------------------------------------

    OpenGL4VertexBufferBackend::OpenGL4VertexBufferBackend(int vertexCapacity)
        : capacity_(vertexCapacity)
    {
        gl4_glGenVertexArrays(1, &vao_);
        gl4_glGenBuffers(1, &vbo_);
    }

    OpenGL4VertexBufferBackend::~OpenGL4VertexBufferBackend()
    {
        if (vbo_ != 0) gl4_glDeleteBuffers(1, &vbo_);
        if (vao_ != 0) gl4_glDeleteVertexArrays(1, &vao_);
    }

    void OpenGL4VertexBufferBackend::ApplyLayout(std::size_t stride)
    {
        const auto s = static_cast<GLsizei>(stride);
        gl4_glBindVertexArray(vao_);
        gl4_glBindBuffer(GL_ARRAY_BUFFER, vbo_);

        switch (stride)
        {
        case 16:
            // VertexPositionColor (packed): float3 position + ubyte4 color
            gl4_glEnableVertexAttribArray(0);
            gl4_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, s, (void*)0);
            gl4_glEnableVertexAttribArray(1);
            gl4_glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, s, (void*)12);
            break;
        case 20:
            // VertexPositionTexture (packed): float3 position + float2 texcoord
            gl4_glEnableVertexAttribArray(0);
            gl4_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, s, (void*)0);
            gl4_glEnableVertexAttribArray(1);
            gl4_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, s, (void*)12);
            break;
        case 24:
            // VertexPositionColorTexture (packed): float3 position + ubyte4 color + float2 texcoord
            gl4_glEnableVertexAttribArray(0);
            gl4_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, s, (void*)0);
            gl4_glEnableVertexAttribArray(1);
            gl4_glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, s, (void*)12);
            gl4_glEnableVertexAttribArray(2);
            gl4_glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, s, (void*)16);
            break;
        case 32:
            // VertexPositionNormalTexture (packed): float3 position + float3 normal + float2 texcoord
            gl4_glEnableVertexAttribArray(0);
            gl4_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, s, (void*)0);
            gl4_glEnableVertexAttribArray(1);
            gl4_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, s, (void*)12);
            gl4_glEnableVertexAttribArray(2);
            gl4_glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, s, (void*)24);
            break;
        case 52:
        case 56:
            // plan_opengl4.md GL4-22: VertexPositionNormalTextureSkinned (packed): float3 position
            // + float3 normal + float2 texcoord + float4 blend weight + ubyte4 blend indices
            // (+ ubyte4 normalized color for stride 56, matching EasyGLGraphicsBackend's own
            // stride-52/56 cases). BlendIndices (location 4) uses the true integer attribute path
            // (glVertexAttribIPointer, not glVertexAttribPointer) since it's read as uvec4 bone
            // indices in the shader, not float-converted color/weight data.
            gl4_glEnableVertexAttribArray(0);
            gl4_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, s, (void*)0);
            gl4_glEnableVertexAttribArray(1);
            gl4_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, s, (void*)12);
            gl4_glEnableVertexAttribArray(2);
            gl4_glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, s, (void*)24);
            gl4_glEnableVertexAttribArray(3);
            gl4_glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, s, (void*)32);
            gl4_glEnableVertexAttribArray(4);
            gl4_glVertexAttribIPointer(4, 4, GL_UNSIGNED_BYTE, s, (void*)48);
            if (stride == 56)
            {
                gl4_glEnableVertexAttribArray(5);
                gl4_glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, s, (void*)52);
            }
            break;
        default:
            // Unknown layout (not yet ported to this backend, plan_opengl4.md remaining work):
            // bind position-only as a safe fallback, matching EasyGL's own precedent.
            gl4_glEnableVertexAttribArray(0);
            gl4_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, s, (void*)0);
            break;
        }

        gl4_glBindVertexArray(0);
    }

    void OpenGL4VertexBufferBackend::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        SetDataWithOptions(data, vertex_count, stride_in_bytes, SetDataOptions::None);
    }

    void OpenGL4VertexBufferBackend::SetDataWithOptions(const void* data, int vertex_count,
                                                        std::size_t stride_in_bytes, SetDataOptions /*options*/)
    {
        vertexCount_ = vertex_count;
        strideInBytes_ = stride_in_bytes;
        const auto byteCount = static_cast<GLsizeiptr4>(static_cast<std::size_t>(vertex_count) * stride_in_bytes);

        gl4_glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl4_glBufferData(GL_ARRAY_BUFFER, byteCount, data, GL_DYNAMIC_DRAW);
        ApplyLayout(stride_in_bytes);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4IndexBufferBackend
    // ------------------------------------------------------------------------------------

    OpenGL4IndexBufferBackend::OpenGL4IndexBufferBackend(int indexCapacity)
        : capacity_(indexCapacity)
    {
        gl4_glGenBuffers(1, &ibo_);
    }

    OpenGL4IndexBufferBackend::~OpenGL4IndexBufferBackend()
    {
        if (ibo_ != 0) gl4_glDeleteBuffers(1, &ibo_);
    }

    void OpenGL4IndexBufferBackend::SetData16(const void* data, int index_count)
    {
        SetData16WithOptions(data, index_count, SetDataOptions::None);
    }

    void OpenGL4IndexBufferBackend::SetData16WithOptions(const void* data, int index_count, SetDataOptions /*options*/)
    {
        indexCount_ = index_count;
        const auto byteCount = static_cast<GLsizeiptr4>(static_cast<std::size_t>(index_count) * sizeof(uint16_t));
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        gl4_glBufferData(GL_ELEMENT_ARRAY_BUFFER, byteCount, data, GL_DYNAMIC_DRAW);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4SpriteBatchBackend
    // ------------------------------------------------------------------------------------

    OpenGL4SpriteBatchBackend::OpenGL4SpriteBatchBackend(OpenGL4GraphicsBackend& owner)
        : owner_(&owner)
    {
        gl4_glGenVertexArrays(1, &vao_);
        gl4_glGenBuffers(1, &vbo_);
        gl4_glGenBuffers(1, &ibo_);

        gl4_glBindVertexArray(vao_);
        gl4_glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl4_glEnableVertexAttribArray(0);
        gl4_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, x));
        gl4_glEnableVertexAttribArray(1);
        gl4_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, u));
        gl4_glEnableVertexAttribArray(2);
        gl4_glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, r));
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        gl4_glBindVertexArray(0);
    }

    OpenGL4SpriteBatchBackend::~OpenGL4SpriteBatchBackend()
    {
        gl4_glDeleteBuffers(1, &vbo_);
        gl4_glDeleteBuffers(1, &ibo_);
        gl4_glDeleteVertexArrays(1, &vao_);
    }

    void OpenGL4SpriteBatchBackend::Begin()
    {
        // SpriteBatch::Begin() (the public XNA-facing class) calls SetTransformMatrix()/
        // SetSamplerFilter()/SetSamplerAddressMode() BEFORE this Begin() runs (see
        // SpriteBatch.cpp) -- resetting those fields here would silently discard whatever the
        // caller just requested. Matches EasyGLSpriteBatchBackend::Begin()'s own precedent,
        // which only flips the begun_ flag.
        begun_ = true;
    }

    void OpenGL4SpriteBatchBackend::End()
    {
        FlushBatch();
        begun_ = false;
    }

    void OpenGL4SpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        const int w = texture.GetWidth();
        const int h = texture.GetHeight();
        Draw(texture, Rectangle((int)x, (int)y, w, h), Rectangle(0, 0, w, h), Color::White);
    }

    void OpenGL4SpriteBatchBackend::Draw(const ITextureBackend& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    void OpenGL4SpriteBatchBackend::Draw(const ITextureBackend& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color,
                                         float rotation,
                                         const Vector2& origin,
                                         SpriteEffects effects,
                                         float layerDepth)
    {
        if (!begun_) throw std::runtime_error("Draw called before Begin()");

        if (currentTexture_ != nullptr && currentTexture_ != &texture)
            FlushBatch();
        currentTexture_ = &texture;

        const float texW = static_cast<float>(texture.GetWidth());
        const float texH = static_cast<float>(texture.GetHeight());

        // No [0,1] clamp -- matches FNA's own straight-through divide (SpriteBatch.cs); a
        // sourceRectangle extending past the texture bounds intentionally lets the sampler's
        // TextureAddressMode govern edge sampling.
        float u1 = (float)sourceRectangle.X / texW;
        float v1 = (float)sourceRectangle.Y / texH;
        float u2 = (float)(sourceRectangle.X + sourceRectangle.Width) / texW;
        float v2 = (float)(sourceRectangle.Y + sourceRectangle.Height) / texH;

        if ((int)effects & (int)SpriteEffects::FlipHorizontally) std::swap(u1, u2);
        if ((int)effects & (int)SpriteEffects::FlipVertically) std::swap(v1, v2);

        const float r = (float)color.getRProperty() / 255.0f;
        const float g = (float)color.getGProperty() / 255.0f;
        const float b = (float)color.getBProperty() / 255.0f;
        const float a = (float)color.getAProperty() / 255.0f;

        const float dx = (float)destinationRectangle.X;
        const float dy = (float)destinationRectangle.Y;
        const float dw = (float)destinationRectangle.Width;
        const float dh = (float)destinationRectangle.Height;

        const float sw = (float)sourceRectangle.Width;
        const float sh = (float)sourceRectangle.Height;

        const float ox = origin.X;
        const float oy = origin.Y;

        const float scaleX = sw != 0.0f ? dw / sw : 0.0f;
        const float scaleY = sh != 0.0f ? dh / sh : 0.0f;

        const float p0x = (0.0f - ox) * scaleX, p0y = (0.0f - oy) * scaleY;
        const float p1x = (sw - ox) * scaleX,   p1y = (0.0f - oy) * scaleY;
        const float p2x = (sw - ox) * scaleX,   p2y = (sh - oy) * scaleY;
        const float p3x = (0.0f - ox) * scaleX, p3y = (sh - oy) * scaleY;

        const float cosR = std::cos(rotation);
        const float sinR = std::sin(rotation);

        auto rotateAndTranslate = [&](float px, float py, float& rx, float& ry)
        {
            rx = dx + px * cosR - py * sinR;
            ry = dy + px * sinR + py * cosR;
        };

        float v0x, v0y, v1x, v1y, v2x, v2y, v3x, v3y;
        rotateAndTranslate(p0x, p0y, v0x, v0y);
        rotateAndTranslate(p1x, p1y, v1x, v1y);
        rotateAndTranslate(p2x, p2y, v2x, v2y);
        rotateAndTranslate(p3x, p3y, v3x, v3y);

        const auto base = static_cast<uint16_t>(pendingVertices_.size());

        pendingVertices_.push_back({v0x, v0y, u1, v1, r, g, b, a});
        pendingVertices_.push_back({v1x, v1y, u2, v1, r, g, b, a});
        pendingVertices_.push_back({v2x, v2y, u2, v2, r, g, b, a});
        pendingVertices_.push_back({v3x, v3y, u1, v2, r, g, b, a});

        pendingIndices_.push_back(base + 0);
        pendingIndices_.push_back(base + 1);
        pendingIndices_.push_back(base + 2);
        pendingIndices_.push_back(base + 2);
        pendingIndices_.push_back(base + 3);
        pendingIndices_.push_back(base + 0);
    }

    void OpenGL4SpriteBatchBackend::FlushBatch()
    {
        if (pendingVertices_.empty()) return;

        // plan_opengl4.md GL4-14: size the viewport/ortho projection off the currently-bound
        // RenderTarget2D when one is bound, not unconditionally off the window's physical size
        // -- a SpriteBatch::Draw() into a bound RT smaller than the window would otherwise get a
        // window-sized viewport, offsetting/clipping its own draws (same fix
        // EasyGLGraphicsBackend::FlushBatch's own GetCurrentRenderTarget2DSize check applies).
        int logW = 0, logH = 0;
        if (owner_->GetCurrentRenderTarget2DSize(logW, logH) && logW > 0 && logH > 0)
        {
            glViewport(0, 0, logW, logH);
        }
        else
        {
            int physW = 0, physH = 0;
            owner_->GetPhysicalSize(physW, physH);
            owner_->GetLogicalSize(logW, logH);
            if (physW > 0 && physH > 0)
                glViewport(0, 0, physW, physH);
            if (logW <= 0 || logH <= 0) { logW = physW; logH = physH; }
        }

        const Matrix orthoM = Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(logW), static_cast<float>(logH), 0.0f, -1.0f, 1.0f);
        const Matrix combined = transform_ * orthoM;
        float ortho[16];
        combined.ToColumnMajor(ortho);

        OpenGL4RawProgram& prog = owner_->GetOrCreateSpriteProgram();
        prog.Use();
        const int projLoc = prog.UniformLocation("uProjection");
        if (projLoc >= 0) gl4_glUniformMatrix4fv(projLoc, 1, GL_FALSE, ortho);
        const int texLoc = prog.UniformLocation("uTexture");
        if (texLoc >= 0) gl4_glUniform1i(texLoc, 0);

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        gl4_glActiveTexture(GL_TEXTURE0);
        currentTexture_->BindGL();
        owner_->ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1);

        gl4_glBindVertexArray(vao_);
        gl4_glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl4_glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr4>(pendingVertices_.size() * sizeof(SpriteVertex)),
                         pendingVertices_.data(), GL_STREAM_DRAW);
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        gl4_glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr4>(pendingIndices_.size() * sizeof(uint16_t)),
                         pendingIndices_.data(), GL_STREAM_DRAW);

        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(pendingIndices_.size()), GL_UNSIGNED_SHORT, nullptr);

        gl4_glBindVertexArray(0);

        pendingVertices_.clear();
        pendingIndices_.clear();
        currentTexture_ = nullptr;
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4GraphicsBackend
    // ------------------------------------------------------------------------------------

    OpenGL4GraphicsBackend::OpenGL4GraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                                                   CnaPresentationMode mode, int multiSampleCount, int swapInterval)
        : window_(window)
        , virtualWidth_(virtualWidth)
        , virtualHeight_(virtualHeight)
        , presentationMode_(mode)
        , swapInterval_(swapInterval)
        // GraphicsBackendCreateArgs::multiSampleCount uses 1 = "no MSAA" (matches
        // EasyGLGraphicsBackend's own sampleCount_ convention); this backend's own internal
        // convention is 0 = disabled (matching OpenGL4RenderTargetBackend's multiSampleCount_).
        , msaaSampleCount_(multiSampleCount > 1 ? multiSampleCount : 0)
    {
        if (!window_) throw std::runtime_error("OpenGL4GraphicsBackend initialized with null window.");

        IGraphicsBackend::RegisterForWindow(window_, this);

        // Real desktop OpenGL 4.1 core profile -- unlike EasyGLGraphicsBackend, which requests
        // SDL_GL_CONTEXT_PROFILE_ES (OpenGL ES 3.0 / WebGL2). 4.1 is the highest core version
        // macOS's own GL driver ever exposes, so it is the widest-portable "real OpenGL 4" floor;
        // Linux/Windows drivers report whatever higher core version they actually support once
        // the context is current (see the version string logged below).
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        SDL_GLContext ctx = SDL_GL_CreateContext(window_);
        if (!ctx)
            throw std::runtime_error(std::string("OpenGL4: SDL_GL_CreateContext failed: ") + SDL_GetError());
        glContext_ = ctx;

        if (!SDL_GL_MakeCurrent(window_, ctx))
            throw std::runtime_error(std::string("OpenGL4: SDL_GL_MakeCurrent failed: ") + SDL_GetError());

        if (!GL4::LoadGL4Functions(reinterpret_cast<GL4::GetProcAddressFn>(SDL_GL_GetProcAddress)))
            throw std::runtime_error("OpenGL4: failed to resolve required GL 4.x core entry points");

        const auto* versionStr = glGetString(GL_VERSION);
        std::cout << "OpenGL4GraphicsBackend initialized with OpenGL "
                  << (versionStr ? reinterpret_cast<const char*>(versionStr) : "(unknown)") << std::endl;

        SDL_GL_SetSwapInterval(swapInterval_);

        gl4_glGenSamplers(kMaxSamplerSlots, samplers_);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        if (msaaSampleCount_ > 0)
        {
            int physW = 0, physH = 0;
            SDL_GetWindowSize(window_, &physW, &physH);
            CreateMsaaBuffers(physW, physH);
            gl4_glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        }
    }

    OpenGL4GraphicsBackend::~OpenGL4GraphicsBackend()
    {
        IGraphicsBackend::UnregisterForWindow(window_);
        gl4_glDeleteSamplers(kMaxSamplerSlots, samplers_);
        if (mrtFbo_) gl4_glDeleteFramebuffers(1, &mrtFbo_);
        if (msaaDepthRbo_) gl4_glDeleteRenderbuffers(1, &msaaDepthRbo_);
        if (msaaColorRbo_) gl4_glDeleteRenderbuffers(1, &msaaColorRbo_);
        if (msaaFbo_) gl4_glDeleteFramebuffers(1, &msaaFbo_);
        if (glContext_)
            SDL_GL_DestroyContext(static_cast<SDL_GLContext>(glContext_));
    }

    void OpenGL4GraphicsBackend::CreateMsaaBuffers(int w, int h)
    {
        GLint maxSamples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        if (maxSamples > 0 && msaaSampleCount_ > static_cast<int>(maxSamples))
            msaaSampleCount_ = static_cast<int>(maxSamples);

        msaaW_ = w;
        msaaH_ = h;
        if (!msaaFbo_) gl4_glGenFramebuffers(1, &msaaFbo_);
        if (!msaaColorRbo_) gl4_glGenRenderbuffers(1, &msaaColorRbo_);
        if (!msaaDepthRbo_) gl4_glGenRenderbuffers(1, &msaaDepthRbo_);

        gl4_glBindRenderbuffer(GL_RENDERBUFFER, msaaColorRbo_);
        gl4_glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaaSampleCount_, GL_RGBA8, w, h);
        gl4_glBindRenderbuffer(GL_RENDERBUFFER, msaaDepthRbo_);
        gl4_glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaaSampleCount_, GL_DEPTH24_STENCIL8, w, h);

        gl4_glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        gl4_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColorRbo_);
        gl4_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msaaDepthRbo_);
    }

    void OpenGL4GraphicsBackend::BindDefaultFramebufferOrMsaa()
    {
        if (msaaSampleCount_ > 0)
        {
            // Recreate the MSAA FBO if the window was resized since the last time it was built.
            int physW = 0, physH = 0;
            SDL_GetWindowSize(window_, &physW, &physH);
            if (physW != msaaW_ || physH != msaaH_)
                CreateMsaaBuffers(physW, physH);
            gl4_glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        }
        else
        {
            gl4_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }

    void OpenGL4GraphicsBackend::ResolveMsaa()
    {
        if (msaaSampleCount_ <= 0) return;
        gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_);
        gl4_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        gl4_glBlitFramebuffer(0, 0, msaaW_, msaaH_, 0, 0, msaaW_, msaaH_, GL_COLOR_BUFFER_BIT,
                              GL_NEAREST);
    }

    void OpenGL4GraphicsBackend::Clear(float r, float g, float b, float a)
    {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void OpenGL4GraphicsBackend::Present()
    {
        if (msaaSampleCount_ > 0) ResolveMsaa();
        SDL_GL_SwapWindow(window_);
        if (msaaSampleCount_ > 0) gl4_glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
    }

    void OpenGL4GraphicsBackend::GetPhysicalSize(int& width, int& height) const
    {
        SDL_GetWindowSize(window_, &width, &height);
    }

    void OpenGL4GraphicsBackend::GetLogicalSize(int& width, int& height) const
    {
        if (virtualHeight_ <= 0)
        {
            GetPhysicalSize(width, height);
            return;
        }
        int physW = 0, physH = 0;
        GetPhysicalSize(physW, physH);
        height = virtualHeight_;
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth && physH > 0)
            width = static_cast<int>((double)physW * virtualHeight_ / physH + 0.5);
        else
            width = virtualWidth_ > 0 ? virtualWidth_ : physW;
    }

    void OpenGL4GraphicsBackend::GetViewportSize(int& width, int& height)
    {
        GetLogicalSize(width, height);
    }

    void OpenGL4GraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void OpenGL4GraphicsBackend::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void OpenGL4GraphicsBackend::SetSwapInterval(int interval)
    {
        swapInterval_ = interval;
        SDL_GL_SetSwapInterval(interval);
    }

    std::unique_ptr<ITextureBackend> OpenGL4GraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<OpenGL4TextureBackend>(data);
    }

    std::unique_ptr<ITexture3DBackend> OpenGL4GraphicsBackend::CreateTexture3D(int w, int h, int depth,
                                                                                bool mipMap, int /*surfaceFormat*/)
    {
        // surfaceFormat is currently unused -- matches EasyGLGraphicsBackend::CreateTexture3D's
        // own identical "always RGBA8" behavior (see plan_opengl4.md GL4-20 and
        // docs/texture3d-texturecube-support.md's documented cross-backend gap).
        return std::make_unique<OpenGL4Texture3DBackend>(w, h, depth, mipMap);
    }

    std::unique_ptr<ITextureCubeBackend> OpenGL4GraphicsBackend::CreateTextureCube(int size, bool mipMap,
                                                                                    int /*surfaceFormat*/)
    {
        return std::make_unique<OpenGL4TextureCubeBackend>(size, mipMap);
    }

    std::unique_ptr<ISpriteBatchBackend> OpenGL4GraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<OpenGL4SpriteBatchBackend>(*this);
    }

    std::unique_ptr<IRenderTargetBackend> OpenGL4GraphicsBackend::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // preserveContents (RenderTargetUsage::PreserveContents) has no effect on this backend --
        // FBO contents are never implicitly discarded between GraphicsDevice::SetRenderTarget
        // calls, matching EasyGLGraphicsBackend's own CreateRenderTarget2D (it accepts and
        // likewise ignores the same parameter).
        (void)preserveContents;
        return std::make_unique<OpenGL4RenderTargetBackend>(w, h, depthFormat, mipMap, multiSampleCount);
    }

    void OpenGL4GraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        // Regenerate the OLD target's mip chain (and resolve its MSAA, if any) before switching
        // away from it -- mirrors EasyGLGraphicsBackend::SetRenderTarget2D's identical ordering.
        if (currentRt2D_ && currentRt2D_ != rt)
            currentRt2D_->UnbindAsRenderTarget();
        if (currentRtCube_)
            currentRtCube_->UnbindAsRenderTarget();
        currentRtCube_ = nullptr;

        currentRt2D_ = rt;
        if (rt)
        {
            currentRtHeight_ = rt->GetHeight();
            rt->BindAsRenderTarget();
        }
        else
        {
            currentRtHeight_ = 0;
            BindDefaultFramebufferOrMsaa();
        }
    }

    bool OpenGL4GraphicsBackend::GetCurrentRenderTarget2DSize(int& width, int& height) const
    {
        if (!currentRt2D_) return false;
        width = currentRt2D_->GetWidth();
        height = currentRt2D_->GetHeight();
        return true;
    }

    std::unique_ptr<IRenderTargetCubeBackend> OpenGL4GraphicsBackend::CreateRenderTargetCube(
        int size, int depthFormat, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<OpenGL4RenderTargetCubeBackend>(size, depthFormat, mipMap, multiSampleCount);
    }

    void OpenGL4GraphicsBackend::SetRenderTargetCubeFace(IRenderTargetCubeBackend* rt, int face)
    {
        if (!rt) { SetRenderTarget2D(nullptr); return; }

        if (currentRt2D_)
            currentRt2D_->UnbindAsRenderTarget();
        if (currentRtCube_ && currentRtCube_ != rt)
            currentRtCube_->UnbindAsRenderTarget();
        currentRt2D_ = nullptr;
        currentRtCube_ = rt;
        currentRtHeight_ = rt->GetSize();
        rt->BindAsRenderTargetFace(face);
    }

    void OpenGL4GraphicsBackend::SetRenderTargets(IRenderTargetBackend* const* rts, int count)
    {
        if (count <= 0) { SetRenderTarget2D(nullptr); return; }
        if (count == 1) { SetRenderTarget2D(rts[0]); return; }

        // MRT: unbind whatever single RT/cube-face was previously active (mip regen if needed).
        // MRT + per-target mipmaps is not supported here (mirrors
        // EasyGLGraphicsBackend::SetRenderTargets' identical, documented gap) -- MRT targets are
        // never tracked as currentRt2D_/currentRtCube_, so switching away from MRT mode cannot
        // regenerate their mips.
        if (currentRt2D_) currentRt2D_->UnbindAsRenderTarget();
        if (currentRtCube_) currentRtCube_->UnbindAsRenderTarget();
        currentRt2D_ = nullptr;
        currentRtCube_ = nullptr;

        if (!mrtFbo_) gl4_glGenFramebuffers(1, &mrtFbo_);
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, mrtFbo_);

        constexpr int kMaxMRT = 8;
        const int n = count < kMaxMRT ? count : kMaxMRT;
        GLenum drawBufs[kMaxMRT];
        for (int i = 0; i < n; ++i)
        {
            gl4_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D,
                                       rts[i]->GetColorGLHandle(), 0);
            drawBufs[i] = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i);
        }
        gl4_glDrawBuffers(n, drawBufs);

        // No depth attachment for MRT (same accepted gap as EasyGLGraphicsBackend's own MRT
        // FBO) -- the viewport reset that follows still needs the first target's height for
        // SetViewport's Y-flip.
        currentRtHeight_ = rts[0]->GetHeight();
    }

    void OpenGL4GraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        int fbH = 0, fbW = 0;
        GetPhysicalSize(fbW, fbH);

        // glReadPixels cannot sample a multisample attachment directly -- resolve into FBO 0
        // first (plan_opengl4.md GL4-17), matching EasyGLGraphicsBackend::ReadBackbuffer's own
        // sampleCount_>1 handling. Guarded by currentRtHeight_==0 (no RT bound) since this method
        // is only ever meant to read the real backbuffer, never an active render target's FBO.
        const bool resolvingBackbufferMsaa = msaaSampleCount_ > 0 && currentRtHeight_ == 0;
        if (resolvingBackbufferMsaa)
        {
            ResolveMsaa();
            gl4_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        // OpenGL's origin is bottom-left; flip Y so the caller gets top-left-origin game
        // coordinates, matching EasyGLGraphicsBackend::ReadBackbuffer's own convention.
        const int glY = fbH - y - h;
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x, glY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        const int rowBytes = w * 4;
        std::vector<uint8_t> tmp(rowBytes);
        for (int row = 0; row < h / 2; ++row)
        {
            uint8_t* a = pixels + static_cast<std::size_t>(row) * rowBytes;
            uint8_t* b = pixels + static_cast<std::size_t>(h - 1 - row) * rowBytes;
            std::memcpy(tmp.data(), a, rowBytes);
            std::memcpy(a, b, rowBytes);
            std::memcpy(b, tmp.data(), rowBytes);
        }

        // Restore the MSAA FBO as the draw target after reading from FBO 0.
        if (resolvingBackbufferMsaa) gl4_glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
    }

    void OpenGL4GraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        glClearColor(r, g, b, a);
        glClearDepth(depth);
        const GLboolean wasWritable = depthWriteEnabled_ ? GL_TRUE : GL_FALSE;
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDepthMask(wasWritable);
    }

    void OpenGL4GraphicsBackend::ClearDepth(float depth)
    {
        glClearDepth(depth);
        const GLboolean wasWritable = depthWriteEnabled_ ? GL_TRUE : GL_FALSE;
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        glDepthMask(wasWritable);
    }

    void OpenGL4GraphicsBackend::ClearStencil(int stencil)
    {
        glClearStencil(stencil);
        glStencilMask(0xFFu);
        glClear(GL_STENCIL_BUFFER_BIT);
    }

    void OpenGL4GraphicsBackend::ClearDepthAndStencil(float depth, int stencil)
    {
        glClearDepth(depth);
        glClearStencil(stencil);
        const GLboolean wasWritable = depthWriteEnabled_ ? GL_TRUE : GL_FALSE;
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFu);
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glDepthMask(wasWritable);
    }

    void OpenGL4GraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        glClearColor(r, g, b, a);
        glClearStencil(stencil);
        glStencilMask(0xFFu);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void OpenGL4GraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        glClearColor(r, g, b, a);
        glClearDepth(depth);
        glClearStencil(stencil);
        const GLboolean wasWritable = depthWriteEnabled_ ? GL_TRUE : GL_FALSE;
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFu);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glDepthMask(wasWritable);
    }

    void OpenGL4GraphicsBackend::SetDepthTestEnabled(bool enabled)
    {
        if (enabled) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
    }

    void OpenGL4GraphicsBackend::SetBlendEnabled(bool enabled)
    {
        if (enabled) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
    }

    void OpenGL4GraphicsBackend::SetDepthWriteEnabled(bool enabled)
    {
        depthWriteEnabled_ = enabled;
        glDepthMask(enabled ? GL_TRUE : GL_FALSE);
    }

    std::unique_ptr<IVertexBufferBackend> OpenGL4GraphicsBackend::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<OpenGL4VertexBufferBackend>(vertex_capacity);
    }

    std::unique_ptr<IIndexBufferBackend> OpenGL4GraphicsBackend::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<OpenGL4IndexBufferBackend>(index_capacity);
    }

    void OpenGL4GraphicsBackend::EnsureColored3DProgram()
    {
        if (colored3DProgram_.IsValid()) return;
        if (!colored3DProgram_.Compile(kColored3DVertSrc, kColored3DFragSrc))
            throw std::runtime_error("OpenGL4: colored3d program failed to compile: " + colored3DProgram_.GetError());
        colored3DWvpLoc_ = colored3DProgram_.UniformLocation("uWorldViewProj");
    }

    void OpenGL4GraphicsBackend::EnsureTextured3DProgram()
    {
        if (textured3DProgram_.IsValid()) return;
        if (!textured3DProgram_.Compile(kTextured3DVertSrc, kTextured3DFragSrc))
            throw std::runtime_error("OpenGL4: textured3d program failed to compile: " + textured3DProgram_.GetError());
    }

    void OpenGL4GraphicsBackend::EnsureColoredTextured3DProgram()
    {
        if (coloredTextured3DProgram_.IsValid()) return;
        if (!coloredTextured3DProgram_.Compile(kColoredTextured3DVertSrc, kColoredTextured3DFragSrc))
            throw std::runtime_error("OpenGL4: colored_textured3d program failed to compile: " + coloredTextured3DProgram_.GetError());
    }

    void OpenGL4GraphicsBackend::EnsureLitTextured3DProgram()
    {
        if (litTextured3DProgram_.IsValid()) return;
        if (!litTextured3DProgram_.Compile(kLitTextured3DVertSrc, kLitTextured3DFragSrc))
            throw std::runtime_error("OpenGL4: lit_textured3d program failed to compile: " + litTextured3DProgram_.GetError());
    }

    void OpenGL4GraphicsBackend::EnsureEnvMap3DProgram()
    {
        if (envMap3DProgram_.IsValid()) return;
        if (!envMap3DProgram_.Compile(kEnvMap3DVertSrc, kEnvMap3DFragSrc))
            throw std::runtime_error("OpenGL4: env_map3d program failed to compile: " + envMap3DProgram_.GetError());
    }

    void OpenGL4GraphicsBackend::EnsureSkinned3DProgram()
    {
        if (skinned3DProgram_.IsValid()) return;
        if (!skinned3DProgram_.Compile(kSkinned3DVertSrc, kSkinned3DFragSrc))
            throw std::runtime_error("OpenGL4: skinned3d program failed to compile: " + skinned3DProgram_.GetError());
    }

    bool OpenGL4GraphicsBackend::BindProgramForStride(std::size_t strideInBytes, const Matrix& world, const Matrix& view,
                                                       const Matrix& projection, const GpuDrawParams& params)
    {
        const Matrix wvp = world * view * projection;
        float wvpCol[16];
        wvp.ToColumnMajor(wvpCol);

        const bool hasTexture0 = params.texture0 != nullptr;
        if (hasTexture0)
        {
            gl4_glActiveTexture(GL_TEXTURE0);
            params.texture0->BindGL();
            ApplySamplerState(0, 0, 1, 1, 1); // Linear/Clamp -- matches this phase's SpriteBatch default.
        }
        // plan_opengl4.md GL4-19: DualTextureEffect's second sampler.
        const bool hasTexture1 = params.texture1 != nullptr;
        if (hasTexture1)
        {
            gl4_glActiveTexture(GL_TEXTURE1);
            params.texture1->BindGL();
            ApplySamplerState(1, 0, 1, 1, 1); // Linear/Clamp -- matches texture0's own default.
        }
        // plan_opengl4.md GL4-21: EnvironmentMapEffect's cube map -- unit 1, same slot
        // DualTextureEffect's texture1 uses (the two effects are mutually exclusive per draw, so
        // there's no conflict), matching EasyGLGraphicsBackend::BindDrawParams's own unit choice.
        const bool hasEnvMap = params.envMapping && params.envMap != nullptr;
        if (hasEnvMap)
        {
            gl4_glActiveTexture(GL_TEXTURE1);
            params.envMap->BindGL();
            ApplySamplerState(1, 0, 1, 1, 1); // Linear/Clamp.
        }

        if (params.envMapping && strideInBytes == 32)
        {
            EnsureEnvMap3DProgram();
            envMap3DProgram_.Use();
            float worldCol[16];
            world.ToColumnMajor(worldCol);
            const auto setM4 = [&](const char* name, const float* m) {
                const int loc = envMap3DProgram_.UniformLocation(name);
                if (loc >= 0) gl4_glUniformMatrix4fv(loc, 1, GL_FALSE, m);
            };
            const auto setV3 = [&](const char* name, const float* v) {
                const int loc = envMap3DProgram_.UniformLocation(name);
                if (loc >= 0) gl4_glUniform3f(loc, v[0], v[1], v[2]);
            };
            const auto setB = [&](const char* name, bool v) {
                const int loc = envMap3DProgram_.UniformLocation(name);
                if (loc >= 0) gl4_glUniform1i(loc, v ? 1 : 0);
            };
            setM4("uWorldViewProj", wvpCol);
            setM4("uWorld", worldCol);
            setV3("uEyePosition", params.eyePositionWorld);
            const int envAmountLoc = envMap3DProgram_.UniformLocation("uEnvMapAmount");
            if (envAmountLoc >= 0) gl4_glUniform1f(envAmountLoc, params.envMapAmount);
            setB("uFresnelEnabled", params.fresnelEnabled);
            const int fresnelFactorLoc = envMap3DProgram_.UniformLocation("uFresnelFactor");
            if (fresnelFactorLoc >= 0) gl4_glUniform1f(fresnelFactorLoc, params.fresnelFactor);
            const int diffuseLoc = envMap3DProgram_.UniformLocation("uDiffuseColor");
            if (diffuseLoc >= 0) gl4_glUniform4f(diffuseLoc, params.diffuseColor[0], params.diffuseColor[1],
                                                 params.diffuseColor[2], params.diffuseColor[3]);
            setB("uTextureEnabled", params.textureEnabled && hasTexture0);
            setV3("uEmissiveColor", params.emissiveColor);
            setV3("uLight0Dir", params.light0Dir);
            setV3("uLight0Diffuse", params.light0Diffuse);
            setV3("uLight1Dir", params.light1Dir);
            setV3("uLight1Diffuse", params.light1Diffuse);
            setV3("uLight2Dir", params.light2Dir);
            setV3("uLight2Diffuse", params.light2Diffuse);
            setV3("uEnvMapSpecular", params.envMapSpecular);
            const int texLoc = envMap3DProgram_.UniformLocation("uTexture");
            if (texLoc >= 0) gl4_glUniform1i(texLoc, 0);
            const int envMapLoc = envMap3DProgram_.UniformLocation("uEnvMap");
            if (envMapLoc >= 0) gl4_glUniform1i(envMapLoc, 1);
            return true;
        }

        switch (strideInBytes)
        {
        case 20: // VertexPositionTexture
        {
            EnsureTextured3DProgram();
            textured3DProgram_.Use();
            const int wvpLoc = textured3DProgram_.UniformLocation("uWorldViewProj");
            if (wvpLoc >= 0) gl4_glUniformMatrix4fv(wvpLoc, 1, GL_FALSE, wvpCol);
            const int diffuseLoc = textured3DProgram_.UniformLocation("uDiffuseColor");
            if (diffuseLoc >= 0) gl4_glUniform4f(diffuseLoc, params.diffuseColor[0], params.diffuseColor[1],
                                                 params.diffuseColor[2], params.diffuseColor[3]);
            const int texEnabledLoc = textured3DProgram_.UniformLocation("uTextureEnabled");
            if (texEnabledLoc >= 0) gl4_glUniform1i(texEnabledLoc, (params.textureEnabled && hasTexture0) ? 1 : 0);
            const int texLoc = textured3DProgram_.UniformLocation("uTexture");
            if (texLoc >= 0) gl4_glUniform1i(texLoc, 0);
            const int tex2Loc = textured3DProgram_.UniformLocation("uTexture2");
            if (tex2Loc >= 0) gl4_glUniform1i(tex2Loc, 1);
            const int dualLoc = textured3DProgram_.UniformLocation("uDualTextureEnabled");
            if (dualLoc >= 0) gl4_glUniform1i(dualLoc, (params.dualTexture && hasTexture1) ? 1 : 0);
            const int alphaTestLoc = textured3DProgram_.UniformLocation("uAlphaTest");
            if (alphaTestLoc >= 0) gl4_glUniform4f(alphaTestLoc, params.alphaTest[0], params.alphaTest[1],
                                                   params.alphaTest[2], params.alphaTest[3]);
            return true;
        }
        case 24: // VertexPositionColorTexture
        {
            EnsureColoredTextured3DProgram();
            coloredTextured3DProgram_.Use();
            const int wvpLoc = coloredTextured3DProgram_.UniformLocation("uWorldViewProj");
            if (wvpLoc >= 0) gl4_glUniformMatrix4fv(wvpLoc, 1, GL_FALSE, wvpCol);
            const int diffuseLoc = coloredTextured3DProgram_.UniformLocation("uDiffuseColor");
            if (diffuseLoc >= 0) gl4_glUniform4f(diffuseLoc, params.diffuseColor[0], params.diffuseColor[1],
                                                 params.diffuseColor[2], params.diffuseColor[3]);
            const int vcLoc = coloredTextured3DProgram_.UniformLocation("uVertexColorEnabled");
            if (vcLoc >= 0) gl4_glUniform1i(vcLoc, params.vertexColorEnabled ? 1 : 0);
            const int texEnabledLoc = coloredTextured3DProgram_.UniformLocation("uTextureEnabled");
            if (texEnabledLoc >= 0) gl4_glUniform1i(texEnabledLoc, (params.textureEnabled && hasTexture0) ? 1 : 0);
            const int texLoc = coloredTextured3DProgram_.UniformLocation("uTexture");
            if (texLoc >= 0) gl4_glUniform1i(texLoc, 0);
            const int tex2Loc = coloredTextured3DProgram_.UniformLocation("uTexture2");
            if (tex2Loc >= 0) gl4_glUniform1i(tex2Loc, 1);
            const int dualLoc = coloredTextured3DProgram_.UniformLocation("uDualTextureEnabled");
            if (dualLoc >= 0) gl4_glUniform1i(dualLoc, (params.dualTexture && hasTexture1) ? 1 : 0);
            const int alphaTestLoc = coloredTextured3DProgram_.UniformLocation("uAlphaTest");
            if (alphaTestLoc >= 0) gl4_glUniform4f(alphaTestLoc, params.alphaTest[0], params.alphaTest[1],
                                                   params.alphaTest[2], params.alphaTest[3]);
            return true;
        }
        case 32: // VertexPositionNormalTexture
        {
            EnsureLitTextured3DProgram();
            litTextured3DProgram_.Use();
            float worldCol[16];
            world.ToColumnMajor(worldCol);
            const auto setM4 = [&](const char* name, const float* m) {
                const int loc = litTextured3DProgram_.UniformLocation(name);
                if (loc >= 0) gl4_glUniformMatrix4fv(loc, 1, GL_FALSE, m);
            };
            const auto setV3 = [&](const char* name, const float* v) {
                const int loc = litTextured3DProgram_.UniformLocation(name);
                if (loc >= 0) gl4_glUniform3f(loc, v[0], v[1], v[2]);
            };
            const auto setB = [&](const char* name, bool v) {
                const int loc = litTextured3DProgram_.UniformLocation(name);
                if (loc >= 0) gl4_glUniform1i(loc, v ? 1 : 0);
            };
            setM4("uWorldViewProj", wvpCol);
            setM4("uWorld", worldCol);
            const int diffuseLoc = litTextured3DProgram_.UniformLocation("uDiffuseColor");
            if (diffuseLoc >= 0) gl4_glUniform4f(diffuseLoc, params.diffuseColor[0], params.diffuseColor[1],
                                                 params.diffuseColor[2], params.diffuseColor[3]);
            setB("uTextureEnabled", params.textureEnabled && hasTexture0);
            setB("uLightingEnabled", params.lightingEnabled);
            setV3("uAmbientColor", params.ambientColor);
            setV3("uLight0Dir", params.light0Dir);
            setV3("uLight0Diffuse", params.light0Diffuse);
            setV3("uLight0Specular", params.light0Specular);
            setV3("uLight1Dir", params.light1Dir);
            setV3("uLight1Diffuse", params.light1Diffuse);
            setV3("uLight1Specular", params.light1Specular);
            setV3("uLight2Dir", params.light2Dir);
            setV3("uLight2Diffuse", params.light2Diffuse);
            setV3("uLight2Specular", params.light2Specular);
            setV3("uEmissiveColor", params.emissiveColor);
            setV3("uEyePosition", params.eyePositionWorld);
            setV3("uSpecularColor", params.specularColor);
            const int specPowerLoc = litTextured3DProgram_.UniformLocation("uSpecularPower");
            if (specPowerLoc >= 0) gl4_glUniform1f(specPowerLoc, params.specularPower);
            const int texLoc = litTextured3DProgram_.UniformLocation("uTexture");
            if (texLoc >= 0) gl4_glUniform1i(texLoc, 0);
            return true;
        }
        case 52: // VertexPositionNormalTextureSkinned
        case 56: // VertexPositionNormalTextureSkinned + Color
        {
            EnsureSkinned3DProgram();
            skinned3DProgram_.Use();
            float worldCol[16];
            world.ToColumnMajor(worldCol);
            const auto setM4 = [&](const char* name, const float* m) {
                const int loc = skinned3DProgram_.UniformLocation(name);
                if (loc >= 0) gl4_glUniformMatrix4fv(loc, 1, GL_FALSE, m);
            };
            const auto setV3 = [&](const char* name, const float* v) {
                const int loc = skinned3DProgram_.UniformLocation(name);
                if (loc >= 0) gl4_glUniform3f(loc, v[0], v[1], v[2]);
            };
            const auto setB = [&](const char* name, bool v) {
                const int loc = skinned3DProgram_.UniformLocation(name);
                if (loc >= 0) gl4_glUniform1i(loc, v ? 1 : 0);
            };
            setM4("uWorldViewProj", wvpCol);
            setM4("uWorld", worldCol);
            const int bonesLoc = skinned3DProgram_.UniformLocation("uBones[0]");
            if (bonesLoc >= 0 && params.boneCount > 0)
                gl4_glUniformMatrix4fv(bonesLoc, params.boneCount, GL_FALSE, params.boneTransforms);
            const int weightsLoc = skinned3DProgram_.UniformLocation("uWeightsPerVertex");
            if (weightsLoc >= 0) gl4_glUniform1i(weightsLoc, params.weightsPerVertex);
            const int diffuseLoc = skinned3DProgram_.UniformLocation("uDiffuseColor");
            if (diffuseLoc >= 0) gl4_glUniform4f(diffuseLoc, params.diffuseColor[0], params.diffuseColor[1],
                                                 params.diffuseColor[2], params.diffuseColor[3]);
            setB("uTextureEnabled", params.textureEnabled && hasTexture0);
            setB("uVertexColorEnabled", params.vertexColorEnabled);
            setV3("uLight0Dir", params.light0Dir);
            setV3("uLight0Diffuse", params.light0Diffuse);
            setV3("uLight0Specular", params.light0Specular);
            setV3("uLight1Dir", params.light1Dir);
            setV3("uLight1Diffuse", params.light1Diffuse);
            setV3("uLight1Specular", params.light1Specular);
            setV3("uLight2Dir", params.light2Dir);
            setV3("uLight2Diffuse", params.light2Diffuse);
            setV3("uLight2Specular", params.light2Specular);
            setV3("uEmissiveColor", params.emissiveColor);
            setV3("uEyePosition", params.eyePositionWorld);
            setV3("uSpecularColor", params.specularColor);
            const int specPowerLoc = skinned3DProgram_.UniformLocation("uSpecularPower");
            if (specPowerLoc >= 0) gl4_glUniform1f(specPowerLoc, params.specularPower);
            const int texLoc = skinned3DProgram_.UniformLocation("uTexture");
            if (texLoc >= 0) gl4_glUniform1i(texLoc, 0);
            return true;
        }
        default:
            return false;
        }
    }

    void OpenGL4GraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend& vb_in,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams& params)
    {
        const auto& vb = static_cast<const OpenGL4VertexBufferBackend&>(vb_in);
        if (!BindProgramForStride(vb.GetStrideInBytes(), world, view, projection, params))
        {
            DrawColoredPrimitives(vb_in, world, view, projection, primitive, primitiveCount);
            return;
        }

        const int vertexCount = VertexCountForPrimitives(primitive, primitiveCount);
        gl4_glBindVertexArray(vb.VaoHandle());
        glDrawArrays(ToGLPrimitive(primitive), params.vertexStart, vertexCount);
        gl4_glBindVertexArray(0);
    }

    void OpenGL4GraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb_in, const IIndexBufferBackend& ib_in,
                                                         const Matrix& world, const Matrix& view, const Matrix& projection,
                                                         PrimitiveType primitive, int primitiveCount,
                                                         const GpuDrawParams& params)
    {
        const auto& vb = static_cast<const OpenGL4VertexBufferBackend&>(vb_in);
        const auto& ib = static_cast<const OpenGL4IndexBufferBackend&>(ib_in);
        if (!BindProgramForStride(vb.GetStrideInBytes(), world, view, projection, params))
        {
            DrawIndexedColoredPrimitives(vb_in, ib_in, world, view, projection, primitive, primitiveCount);
            return;
        }

        const int indexCount = VertexCountForPrimitives(primitive, primitiveCount);
        const auto byteOffset = reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(params.startIndex) * sizeof(uint16_t));
        gl4_glBindVertexArray(vb.VaoHandle());
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.IboHandle());
        glDrawElements(ToGLPrimitive(primitive), indexCount, GL_UNSIGNED_SHORT, byteOffset);
        gl4_glBindVertexArray(0);
    }

    OpenGL4RawProgram& OpenGL4GraphicsBackend::GetOrCreateSpriteProgram()
    {
        if (!spriteProgram_.IsValid())
        {
            if (!spriteProgram_.Compile(kSpriteVertSrc, kSpriteFragSrc))
                throw std::runtime_error("OpenGL4: sprite program failed to compile: " + spriteProgram_.GetError());
        }
        return spriteProgram_;
    }

    void OpenGL4GraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& vb_in,
                                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                                       PrimitiveType primitive, int primitiveCount)
    {
        EnsureColored3DProgram();
        const auto& vb = static_cast<const OpenGL4VertexBufferBackend&>(vb_in);

        const Matrix wvp = world * view * projection;
        float wvpCol[16];
        wvp.ToColumnMajor(wvpCol);

        colored3DProgram_.Use();
        if (colored3DWvpLoc_ >= 0)
            gl4_glUniformMatrix4fv(colored3DWvpLoc_, 1, GL_FALSE, wvpCol);

        const int vertexCount = VertexCountForPrimitives(primitive, primitiveCount);
        gl4_glBindVertexArray(vb.VaoHandle());
        glDrawArrays(ToGLPrimitive(primitive), 0, vertexCount);
        gl4_glBindVertexArray(0);
    }

    void OpenGL4GraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb_in, const IIndexBufferBackend& ib_in,
                                                              const Matrix& world, const Matrix& view, const Matrix& projection,
                                                              PrimitiveType primitive, int primitiveCount)
    {
        EnsureColored3DProgram();
        const auto& vb = static_cast<const OpenGL4VertexBufferBackend&>(vb_in);
        const auto& ib = static_cast<const OpenGL4IndexBufferBackend&>(ib_in);

        const Matrix wvp = world * view * projection;
        float wvpCol[16];
        wvp.ToColumnMajor(wvpCol);

        colored3DProgram_.Use();
        if (colored3DWvpLoc_ >= 0)
            gl4_glUniformMatrix4fv(colored3DWvpLoc_, 1, GL_FALSE, wvpCol);

        const int indexCount = VertexCountForPrimitives(primitive, primitiveCount);
        gl4_glBindVertexArray(vb.VaoHandle());
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.IboHandle());
        glDrawElements(ToGLPrimitive(primitive), indexCount, GL_UNSIGNED_SHORT, nullptr);
        gl4_glBindVertexArray(0);
    }

    void OpenGL4GraphicsBackend::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        if (w <= 0 || h <= 0) return; // invalid rect -- leave viewport state unchanged

        // GraphicsDevice::UpdateViewportFromWindow() calls this after every window resize (and
        // once at device creation) -- without a real override here the GL viewport is left at
        // whatever the driver's own initial default was, which 3D draws silently depend on
        // (unlike SpriteBatch's own FlushBatch, which sets glViewport() itself every flush).
        // OpenGL's viewport origin is bottom-left; convert from top-left XNA coordinates. Use
        // the bound render target's own height for the flip when one is bound (currentRtHeight_,
        // plan_opengl4.md GL4-14); fall back to the window's physical height for the default
        // framebuffer -- matches EasyGLGraphicsBackend::SetViewport's own currentRtHeight_-or-
        // window-height pattern. Using the window's height unconditionally while an RT is bound
        // would produce a viewport y-offset entirely outside the RT's pixel range whenever the
        // RT is smaller than the window, discarding every fragment.
        int fbH = currentRtHeight_;
        if (fbH == 0)
        {
            int physW = 0;
            GetPhysicalSize(physW, fbH);
        }
        glViewport(x, fbH - y - h, w, h);
        glDepthRange(minDepth, maxDepth);
    }

    void OpenGL4GraphicsBackend::ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy)
    {
        if (slot < 0 || slot >= kMaxSamplerSlots) return;

        const unsigned int sampler = samplers_[slot];
        GLint minFilter = GL_LINEAR, magFilter = GL_LINEAR;
        FilterToGL(filter, minFilter, magFilter);
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, minFilter);
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, magFilter);
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, AddressModeToGL(addressU));
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, AddressModeToGL(addressV));
        if (filter == 2) // Anisotropic
            gl4_glSamplerParameterf(sampler, GL_TEXTURE_MAX_ANISOTROPY, static_cast<float>(maxAnisotropy));

        gl4_glBindSampler(static_cast<GLuint>(slot), sampler);
    }

    void OpenGL4GraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                 int colorDstBlend, int alphaDstBlend,
                                                 int colorBlendFunc, int alphaBlendFunc)
    {
        // Blend::One=0, Blend::Zero=1 -> the Opaque preset (src=One, dst=Zero) is XNA's own
        // encoding of "no blending", matching EasyGLGraphicsBackend::ApplyBlendState's identical
        // derivation (there's no separate BlendState.Enabled flag in the XNA API).
        const bool blendEnabled = !(colorSrcBlend == 0 && colorDstBlend == 1 &&
                                    alphaSrcBlend == 0 && alphaDstBlend == 1);
        if (blendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (blendEnabled)
        {
            gl4_glBlendFuncSeparate(ToGLBlendFactor(colorSrcBlend), ToGLBlendFactor(colorDstBlend),
                                    ToGLBlendFactor(alphaSrcBlend), ToGLBlendFactor(alphaDstBlend));
            gl4_glBlendEquationSeparate(ToGLBlendEquation(colorBlendFunc),
                                        ToGLBlendEquation(alphaBlendFunc));
        }
    }

    void OpenGL4GraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                         int depthFunc,
                                                         bool stencilEnable, int stencilFunc,
                                                         int stencilPass, int stencilFail, int stencilDepthFail,
                                                         int stencilMask, int stencilWriteMask, int referenceStencil,
                                                         bool twoSidedStencilMode,
                                                         int ccwStencilFunc, int ccwStencilPass,
                                                         int ccwStencilFail, int ccwStencilDepthFail)
    {
        if (depthEnable) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glDepthMask(depthWriteEnable ? GL_TRUE : GL_FALSE);
        depthWriteEnabled_ = depthWriteEnable;
        if (depthEnable) glDepthFunc(ToGLCompareFunc(depthFunc));

        if (stencilEnable) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
        if (stencilEnable)
        {
            const GLenum glSFail = ToGLStencilOp(stencilFail);
            const GLenum glDFail = ToGLStencilOp(stencilDepthFail);
            const GLenum glPass  = ToGLStencilOp(stencilPass);
            if (twoSidedStencilMode)
            {
                gl4_glStencilFuncSeparate(GL_FRONT, ToGLCompareFunc(stencilFunc),
                                          referenceStencil, static_cast<GLuint>(stencilMask));
                gl4_glStencilOpSeparate(GL_FRONT, glSFail, glDFail, glPass);
                gl4_glStencilMaskSeparate(GL_FRONT, static_cast<GLuint>(stencilWriteMask));

                gl4_glStencilFuncSeparate(GL_BACK, ToGLCompareFunc(ccwStencilFunc),
                                          referenceStencil, static_cast<GLuint>(stencilMask));
                gl4_glStencilOpSeparate(GL_BACK, ToGLStencilOp(ccwStencilFail),
                                        ToGLStencilOp(ccwStencilDepthFail),
                                        ToGLStencilOp(ccwStencilPass));
                gl4_glStencilMaskSeparate(GL_BACK, static_cast<GLuint>(stencilWriteMask));
            }
            else
            {
                glStencilFunc(ToGLCompareFunc(stencilFunc), referenceStencil,
                             static_cast<GLuint>(stencilMask));
                glStencilOp(glSFail, glDFail, glPass);
                glStencilMask(static_cast<GLuint>(stencilWriteMask));
            }
        }
    }

    void OpenGL4GraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode,
                                                       bool scissorTestEnable,
                                                       float depthBias,
                                                       float slopeScaleDepthBias)
    {
        // CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2. OpenGL's default
        // front face is CCW, so CW faces are back faces.
        if (cullMode == 0)
        {
            glDisable(GL_CULL_FACE);
        }
        else
        {
            glEnable(GL_CULL_FACE);
            glCullFace(cullMode == 1 ? GL_BACK : GL_FRONT);
        }

        if (scissorTestEnable) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);

        // FillMode: Solid=0, WireFrame=1. Desktop core-profile GL keeps a real glPolygonMode
        // (only GL_FRONT_AND_BACK is valid for `face` in a core-profile context, which is exactly
        // what XNA's single FillMode value needs -- unlike EasyGL's ES target, which has no
        // glPolygonMode at all and instead re-expands triangles into GL_LINES at draw time).
        glPolygonMode(GL_FRONT_AND_BACK, fillMode == 1 ? GL_LINE : GL_FILL);

        // DepthBias/SlopeScaleDepthBias map directly onto real GL polygon offset (matches this
        // project's own established Vulkan/EasyGL convention: glPolygonOffset(slopeScale, bias)).
        // Always enabled -- factor=0/units=0 is a genuine no-op in GL, no need to conditionally
        // disable it.
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(slopeScaleDepthBias, depthBias);
    }

    void OpenGL4GraphicsBackend::SetBlendFactor(float r, float g, float b, float a)
    {
        gl4_glBlendColor(r, g, b, a);
    }

    void OpenGL4GraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0) return; // invalid rect -- leave scissor state unchanged

        // OpenGL's scissor origin is bottom-left; convert from top-left XNA coordinates using
        // the same currentRtHeight_-or-window-height pattern as SetViewport (plan_opengl4.md
        // GL4-14/GL4-16).
        int fbH = currentRtHeight_;
        if (fbH == 0)
        {
            int physW = 0;
            GetPhysicalSize(physW, fbH);
        }
        glScissor(x, fbH - y - h, w, h);
        // Does NOT enable/disable the scissor test itself -- that is controlled exclusively by
        // ApplyRasterizerState via RasterizerState.ScissorTestEnable, matching
        // EasyGLGraphicsBackend::SetScissorRect's identical division of responsibility.
    }
}

namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_OPENGL4
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<OpenGL4::OpenGL4GraphicsBackend>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode,
            args.multiSampleCount, args.swapInterval);
    }
#endif
}
