// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Renderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "System/InvalidOperationException.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace CNA::Internal::Renderers::OpenGL4::GL4;

namespace CNA::Internal::Renderers::OpenGL4
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

        // plan_opengl4.md GL4-25: coloredParams3d (VertexPositionColor, stride 16) -- a SEPARATE
        // program from kColored3DProgram_ above (which stays exactly as-is, used only by the
        // GpuDrawParams-free DrawColoredPrimitives/DrawIndexedColoredPrimitives fast path that
        // GraphicsDevice::DrawUserPrimitives(VertexPositionColor*, ...) and the generic
        // unrecognized-stride fallback both go through). This new program is a real, dedicated
        // stride-16 case in BindProgramForStride, closing a parity gap with
        // EasyGLRenderer::EnsureColored3DProgram (DiffuseColor/VertexColorEnabled/
        // AlphaTest/fog were previously silently unavailable to any stride-16 draw issued via a
        // real Effect.Apply(), unlike every other stride, since BindProgramForStride had no
        // stride-16 case at all and always fell back to the params-free path above). Ported
        // formula from EasyGLRenderer::EnsureColored3DProgram (uDiffuseColor multiply
        // gated by uVertexColorEnabled, alpha-test discard, fog).
        const char* kColoredParams3DVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uWorldViewProj;
uniform vec4 uFogVector;
out vec4 vColor;
out float vFogFactor;
void main()
{
    gl_Position = uWorldViewProj * vec4(aPos, 1.0);
    vColor = aColor;
    // REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a
    // true VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes
    // the third column of World*View (CPU-side, GpuDrawParams::fogVector). vFogFactor is the
    // inverse "keep" (mix(uFogColor, colour, vFogFactor)), so 1 - saturate(dot(pos, uFogVector)).
    // uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the
    // fogStart==fogEnd degenerate case (=> keep 0, fully fogged) -- all handled CPU-side,
    // matching FNA and EasyGLRenderer's own head-of-tree programs exactly.
    vFogFactor = 1.0 - clamp(dot(vec4(aPos, 1.0), uFogVector), 0.0, 1.0);
}
)GLSL";

        const char* kColoredParams3DFragSrc = R"GLSL(
#version 410 core
in vec4 vColor;
in float vFogFactor;
uniform vec4 uDiffuseColor;
uniform bool uVertexColorEnabled;
uniform vec4 uAlphaTest;
uniform vec3 uFogColor;
uniform vec3 uSrgb;
out vec4 fragColor;

vec3 cnaSrgbToLinear(vec3 c)
{
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

vec3 cnaLinearToSrgb(vec3 c)
{
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}
void main()
{
    vec4 vc = uVertexColorEnabled ? vColor : vec4(1.0);
    fragColor = vc * uDiffuseColor;

    float alpha = fragColor.a;
    bool passTest = (uAlphaTest.y > 0.0) ? (abs(alpha - uAlphaTest.x) < uAlphaTest.y) : (alpha < uAlphaTest.x);
    float w = passTest ? uAlphaTest.z : uAlphaTest.w;
    if (w < 0.0) discard;

    fragColor.rgb = mix(uFogColor, fragColor.rgb, vFogFactor);
}
)GLSL";

        // plan_opengl4.md GL4-13: textured3d (VertexPositionTexture, stride 20). Algorithmic
        // reference: VulkanRenderer's textured3d.vert/frag.glsl (no Y-flip -- OpenGL's own
        // NDC convention needs none, unlike Vulkan's flipped clip space). plan_opengl4.md GL4-25
        // added real fog (REMED-GFX-010 fog-vector form, matching EasyGLRenderer's own
        // EnsureTextured3DProgram -- see kColoredParams3DVertSrc's own comment for the full
        // derivation, not re-explained per shader).
        const char* kTextured3DVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uWorldViewProj;
uniform vec4 uFogVector;
out vec2 vUV;
out float vFogFactor;
void main()
{
    vUV = aUV;
    gl_Position = uWorldViewProj * vec4(aPos, 1.0);
    vFogFactor = 1.0 - clamp(dot(vec4(aPos, 1.0), uFogVector), 0.0, 1.0);
}
)GLSL";

        // plan_opengl4.md GL4-19: AlphaTestEffect's discard test and DualTextureEffect's second
        // sampler are both folded into the SAME textured3d/colored_textured3d programs (not new
        // stride cases) -- both effects reuse VertexPositionTexture/VertexPositionColorTexture
        // unchanged (DualTextureEffect samples both textures with the SAME UV set in real XNA,
        // no separate UV1 attribute). Ported from VulkanRenderer's alpha_test3d.frag.glsl
        // (discard formula) and dual_texture3d.frag.glsl (the "tex1.rgb*=2.0; result=tex1*tex2"
        // 2x-modulate blend). uAlphaTest defaults to {0,0,1,1} (GpuDrawParams' own documented
        // "always pass, never discard" default), so this is a genuine no-op for every other
        // effect's draws.
        const char* kTextured3DFragSrc = R"GLSL(
#version 410 core
in vec2 vUV;
in float vFogFactor;
uniform sampler2D uTexture;
uniform sampler2D uTexture2;
uniform vec4 uDiffuseColor;
uniform bool uTextureEnabled;
uniform bool uDualTextureEnabled;
uniform vec4 uAlphaTest;
uniform vec3 uFogColor;
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

    result.rgb = mix(uFogColor, result.rgb, vFogFactor);
    fragColor = result;
}
)GLSL";

        // colored_textured3d (VertexPositionColorTexture, stride 24). plan_opengl4.md GL4-25
        // added real fog (see kColoredParams3DVertSrc's own comment for the formula derivation).
        const char* kColoredTextured3DVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;
uniform mat4 uWorldViewProj;
uniform vec4 uDiffuseColor;
uniform bool uVertexColorEnabled;
uniform vec4 uFogVector;
out vec2 vUV;
out vec4 vTint;
out float vFogFactor;
void main()
{
    vUV = aUV;
    vTint = uVertexColorEnabled ? (aColor * uDiffuseColor) : uDiffuseColor;
    gl_Position = uWorldViewProj * vec4(aPos, 1.0);
    vFogFactor = 1.0 - clamp(dot(vec4(aPos, 1.0), uFogVector), 0.0, 1.0);
}
)GLSL";

        const char* kColoredTextured3DFragSrc = R"GLSL(
#version 410 core
in vec2 vUV;
in vec4 vTint;
in float vFogFactor;
uniform sampler2D uTexture;
uniform sampler2D uTexture2;
uniform bool uTextureEnabled;
uniform bool uDualTextureEnabled;
uniform vec4 uAlphaTest;
uniform vec3 uFogColor;
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

    result.rgb = mix(uFogColor, result.rgb, vFogFactor);
    fragColor = result;
}
)GLSL";

        // lit_textured3d (VertexPositionNormalTexture, stride 32) -- BasicEffect's default
        // 3-directional-light rig. Ported from VulkanRenderer's lit_textured3d.vert/
        // frag.glsl: FNA's Lighting.fxh ComputeLights() (ambient + per-light Lambertian diffuse +
        // Blinn-Phong specular, EmissiveColor added post-multiply, specular added post-texture
        // scaled by alpha). plan_opengl4.md GL4-25 added real fog (see kColoredParams3DVertSrc's
        // own comment for the formula derivation). World's inverse-transpose upper-left 3x3 is
        // used for the normal matrix (not MVP's), matching EnvironmentMapEffect's own
        // already-correct pattern -- an MVP-based transform would bake View/Projection into the
        // normal.
        const char* kLitTextured3DVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uWorldViewProj;
uniform mat4 uWorld;
uniform vec4 uFogVector;
out vec2 vUV;
out vec3 vNormal;
out vec3 vWorldPos;
out float vFogFactor;
void main()
{
    vUV = aUV;
    mat3 normalMatrix = transpose(inverse(mat3(uWorld)));
    vNormal = normalize(normalMatrix * aNormal);
    vWorldPos = (uWorld * vec4(aPos, 1.0)).xyz;
    gl_Position = uWorldViewProj * vec4(aPos, 1.0);
    vFogFactor = 1.0 - clamp(dot(vec4(aPos, 1.0), uFogVector), 0.0, 1.0);
}
)GLSL";

        const char* kLitTextured3DFragSrc = R"GLSL(
#version 410 core
in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;
in float vFogFactor;
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
uniform vec3 uFogColor;
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
    color.rgb = mix(uFogColor, color.rgb, vFogFactor);
    fragColor = color;
}
)GLSL";

        // plan_opengl4.md GL4-29: lit_textured3d's own per-vertex-lit sibling -- real XNA's
        // BasicEffect defaults PreferPerPixelLighting=false (per-vertex/Gouraud-shaded lighting),
        // the opposite of what kLitTextured3DVertSrc/FragSrc above render unconditionally.
        // Identical Blinn-Phong math to kLitTextured3DFragSrc (same formula, same inputs), just
        // computed once per vertex and Gouraud-interpolated via vLitRGB/vSpecularRGB instead of
        // being re-evaluated per fragment. Selected by BindProgramForStride instead of
        // litTextured3DProgram_ when params.lightingEnabled && !params.preferPerPixelLighting
        // (XNA's own default) -- only meaningfully distinct while lighting is actually on, so this
        // variant always computes lighting unconditionally (no uLightingEnabled branch needed,
        // unlike kLitTextured3DFragSrc, since it's never bound with lighting off). Ported from
        // EasyGLRenderer::EnsureLit3DVertexLitProgram's GLSL ES 300 source (desktop GLSL 410
        // core translation only).
        const char* kLitTextured3DVertexLitVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uWorldViewProj;
uniform mat4 uWorld;
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
uniform vec4 uFogVector;
out vec2 vUV;
out float vFogFactor;
out vec3 vLitRGB;
out vec3 vSpecularRGB;

vec3 safeNormalize(vec3 v)
{
    float len = length(v);
    return len > 1e-6 ? (v / len) : vec3(0.0, -1.0, 0.0);
}

void main()
{
    vUV = aUV;
    mat3 normalMatrix = transpose(inverse(mat3(uWorld)));
    vec3 N = normalize(normalMatrix * aNormal);
    vec3 worldPos = (uWorld * vec4(aPos, 1.0)).xyz;
    vec3 E = normalize(uEyePosition - worldPos);
    gl_Position = uWorldViewProj * vec4(aPos, 1.0);
    vec3 nL0 = safeNormalize(uLight0Dir);
    vec3 nL1 = safeNormalize(uLight1Dir);
    vec3 nL2 = safeNormalize(uLight2Dir);
    float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
    vec3 lightSum = uAmbientColor + NdotL0 * uLight0Diffuse + NdotL1 * uLight1Diffuse + NdotL2 * uLight2Diffuse;
    vLitRGB = lightSum * uDiffuseColor.rgb + uEmissiveColor;
    vec3 h0 = normalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, uSpecularPower);
    vec3 h1 = normalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, uSpecularPower);
    vec3 h2 = normalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, uSpecularPower);
    vSpecularRGB = (spec0 * uLight0Specular + spec1 * uLight1Specular + spec2 * uLight2Specular) * uSpecularColor;
    vFogFactor = 1.0 - clamp(dot(vec4(aPos, 1.0), uFogVector), 0.0, 1.0);
}
)GLSL";

        const char* kLitTextured3DVertexLitFragSrc = R"GLSL(
#version 410 core
in vec2 vUV;
in float vFogFactor;
in vec3 vLitRGB;
in vec3 vSpecularRGB;
uniform sampler2D uTexture;
uniform bool uTextureEnabled;
uniform vec4 uDiffuseColor;
uniform vec3 uFogColor;
out vec4 fragColor;
void main()
{
    vec4 tex = uTextureEnabled ? texture(uTexture, vUV) : vec4(1.0);
    vec4 color = vec4(vLitRGB, uDiffuseColor.a) * tex;
    color.rgb += vSpecularRGB * color.a;
    color.rgb = mix(uFogColor, color.rgb, vFogFactor);
    fragColor = color;
}
)GLSL";

        // plan_opengl4.md GL4-21: env_map3d (VertexPositionNormalTexture, stride 32) --
        // EnvironmentMapEffect's own dedicated program, selected instead of lit_textured3d when
        // GpuDrawParams::envMapping is set (BindProgramForStride branches on it before the stride
        // switch, matching EasyGLRenderer::SelectProgram's own envMapping-overrides-stride
        // dispatch order). Ported from EasyGLRenderer::EnsureEnvMapped3DProgram's GLSL ES
        // 300 source (near-verbatim translation to desktop GLSL 410 core -- no ES precision
        // qualifiers, otherwise identical), cross-verified against VulkanRenderer's
        // env_map3d.frag.glsl (per-fragment Fresnel instead of EasyGL's per-vertex Gouraud
        // interpolation -- a documented, accepted, strictly-more-accurate deviation kept here in
        // its EasyGL per-vertex form since this is the closer sibling GLSL renderer to port from).
        // Real XNA EnvironmentMapEffect.fx formula (src/CNA/Internal/Renderers/DirectX9/shaders/xna/
        // EnvironmentMapEffect.fx): reflection vector reflect(-eyeVector, worldNormal); Fresnel
        // blend factor pow(max(1-|dot(eye,normal)|,0), FresnelFactor)*EnvironmentMapAmount; final
        // colour is a LERP (not additive) between the lit diffuse*texture colour and the
        // alpha-scaled cubemap sample, plus a separately alpha-scaled specular term -- see
        // docs/environmentmapeffect-support.md for the two real formula bugs (additive-not-lerp,
        // missing alpha scaling) found and fixed while porting this to 3 other renderers.
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
uniform vec4 uFogVector;
out vec3 vWorldNormal;
out vec3 vEyeDir;
out vec2 vUV;
out float vFresnel;
out float vFogFactor;
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
    // REMED-GFX-010: see kColoredParams3DVertSrc's own comment for the fog-vector formula.
    vFogFactor = 1.0 - clamp(dot(vec4(aPos, 1.0), uFogVector), 0.0, 1.0);
}
)GLSL";

        const char* kEnvMap3DFragSrc = R"GLSL(
#version 410 core
in vec3 vWorldNormal;
in vec3 vEyeDir;
in vec2 vUV;
in float vFresnel;
in float vFogFactor;
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
uniform vec3 uFogColor;
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
    rgb = mix(uFogColor, rgb, vFogFactor);
    fragColor = vec4(rgb, combinedAlpha);
}
)GLSL";

        // plan_opengl4.md GL4-22: skinned3d (VertexPositionNormalTextureSkinned, stride 52/56) --
        // SkinnedEffect's own dedicated program, selected instead of lit_textured3d/env_map3d/
        // textured3d/colored_textured3d for stride 52/56 draws. Ported near-verbatim from
        // EasyGLRenderer::EnsureSkinnedProgram's GLSL ES 300 source (desktop GLSL 410 core
        // translation only), which itself already matches real XNA SkinnedEffect.fx's Skin()
        // function: skinMat = sum of the first WeightsPerVertex (1, 2, or 4) uBones[index]*weight
        // pairs (never all 4 unconditionally -- a real bug, Task 895, found and fixed while
        // porting this effect to the other renderers), position transformed by the full skinMat,
        // normal by its upper-left 3x3. The lighting formula itself reuses lit_textured3d's own
        // already-correct 3-light Lambertian-diffuse + Blinn-Phong-specular + EmissiveColor
        // formula (EmissiveColor pre-folds AmbientLightColor*DiffuseColor via
        // SkinnedEffect::FillGpuDrawParams, same as lit_textured3d/env_map3d), plus a vertex-color
        // modulate (VertexColorEnabled) exercised via the stride-56 aColor attribute. Real fog
        // support landed later, GL4-25 (see kColoredParams3DVertSrc's own comment for the formula).
        //
        // NOTE (matches EasyGL's own established formula, not independently re-derived here):
        // the skinned normal is only rotated by the bone skinning matrix (mat3(skinMat)), not
        // additionally by World's own rotation -- correct for the identity/translation-only World
        // matrices this effect's own test scenarios use, but would under-rotate lighting for a
        // rotated World on a skinned mesh. This is a pre-existing, cross-renderer (EasyGL/Vulkan/
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
uniform vec4 uFogVector;
out vec3 vNormal;
out vec2 vUV;
out vec3 vWorldPos;
out vec4 vColor;
out float vFogFactor;
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
    // REMED-GFX-010: see kColoredParams3DVertSrc's own comment. Skinned: dot the POST-skin
    // position, since FNA's Skin() mutates vin.Position before ComputeFogFactor runs.
    vFogFactor = 1.0 - clamp(dot(skinnedPos, uFogVector), 0.0, 1.0);
}
)GLSL";

        const char* kSkinned3DFragSrc = R"GLSL(
#version 410 core
in vec3 vNormal;
in vec2 vUV;
in vec3 vWorldPos;
in vec4 vColor;
in float vFogFactor;
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
uniform vec3 uFogColor;
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
    color.rgb = mix(uFogColor, color.rgb, vFogFactor);
    fragColor = color;
}
)GLSL";

        // plan_opengl4.md GL4-29: skinned3d's own per-vertex-lit sibling, mirroring
        // kLitTextured3DVertexLitVertSrc/FragSrc's technique exactly -- real XNA's SkinnedEffect
        // also defaults PreferPerPixelLighting=false, same as BasicEffect. The skinning itself is
        // unchanged from kSkinned3DVertSrc above; only WHERE lighting is evaluated moves (once per
        // vertex, Gouraud-interpolated, instead of once per fragment). No separate uAmbientColor
        // uniform here, matching kSkinned3DFragSrc's own shape: SkinnedEffect::FillGpuDrawParams
        // already pre-folds ambient into uEmissiveColor. Ported from
        // EasyGLRenderer::EnsureSkinnedVertexLitProgram's GLSL ES 300 source (desktop GLSL
        // 410 core translation only).
        const char* kSkinned3DVertexLitVertSrc = R"GLSL(
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
uniform vec4 uFogVector;
out vec2 vUV;
out vec4 vColor;
out float vFogFactor;
out vec3 vLitRGB;
out vec3 vSpecularRGB;

vec3 safeNormalize(vec3 v)
{
    float len = length(v);
    return len > 1e-6 ? (v / len) : vec3(0.0, -1.0, 0.0);
}

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
    vec3 N = (len > 1e-6) ? (skinnedNormal / len) : aNormal;
    vUV = aUV;
    vColor = aColor;
    vec3 worldPos = (uWorld * skinnedPos).xyz;
    vec3 E = normalize(uEyePosition - worldPos);
    vec3 nL0 = safeNormalize(uLight0Dir);
    vec3 nL1 = safeNormalize(uLight1Dir);
    vec3 nL2 = safeNormalize(uLight2Dir);
    float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
    vec3 lightSum = NdotL0 * uLight0Diffuse + NdotL1 * uLight1Diffuse + NdotL2 * uLight2Diffuse;
    vLitRGB = lightSum * uDiffuseColor.rgb + uEmissiveColor;
    vec3 h0 = normalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, uSpecularPower);
    vec3 h1 = normalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, uSpecularPower);
    vec3 h2 = normalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, uSpecularPower);
    vSpecularRGB = (spec0 * uLight0Specular + spec1 * uLight1Specular + spec2 * uLight2Specular) * uSpecularColor;
    vFogFactor = 1.0 - clamp(dot(skinnedPos, uFogVector), 0.0, 1.0);
}
)GLSL";

        const char* kSkinned3DVertexLitFragSrc = R"GLSL(
#version 410 core
in vec2 vUV;
in vec4 vColor;
in float vFogFactor;
in vec3 vLitRGB;
in vec3 vSpecularRGB;
uniform sampler2D uTexture;
uniform bool uTextureEnabled;
uniform bool uVertexColorEnabled;
uniform vec4 uDiffuseColor;
uniform vec3 uFogColor;
out vec4 fragColor;
void main()
{
    vec4 tex = uTextureEnabled ? texture(uTexture, vUV) : vec4(1.0);
    vec4 color = vec4(vLitRGB, uDiffuseColor.a) * tex;
    color.rgb += vSpecularRGB * color.a;
    if (uVertexColorEnabled) color *= vColor;
    color.rgb = mix(uFogColor, color.rgb, vFogFactor);
    fragColor = color;
}
)GLSL";

        // plan_opengl4.md GL4-23: pbr3d (VertexPositionNormalTangentTexture, stride 48) and
        // pbr_skinned3d (stride 68, PBR + bone skinning combined) -- PbrEffect/SkinnedPbrEffect's
        // own dedicated programs. Ported near-verbatim from EasyGLRenderer's
        // EnsurePbrProgram()/EnsurePbrSkinnedProgram() GLSL ES 300 source, which is itself the
        // real glTF 2.0 spec's own reference metallic-roughness BRDF (GGX normal distribution,
        // Smith-Schlick-GGX visibility, Schlick Fresnel) -- cross-verified against
        // VulkanRenderer's pbr3d.frag.glsl and BgfxRenderer's fs_pbr3d.sc, both
        // byte-for-byte identical in their PbrLight() math, before writing any OpenGL4 code.
        // 5 texture units: 0=base colour, 1=normal map (tangent-space RGB), 2=metallic-roughness
        // (glTF packing: G=roughness, B=metallic), 3=emissive, 4=occlusion (R channel). All 5 are
        // sampled unconditionally every fragment (unlike DualTextureEffect/EnvironmentMapEffect's
        // uniform-gated optional samplers) -- BindProgramForStride always binds a real texture to
        // every unit, falling back to defaultWhiteTexture_/defaultFlatNormalTexture_ when the
        // corresponding GpuDrawParams::pbr*Map pointer is null.
        const char* kPbr3DVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUV;
uniform mat4 uWorldViewProj;
uniform mat4 uWorld;
uniform vec4 uFogVector;
out vec3 vNormal;
out vec3 vTangent;
out float vBitangentSign;
out vec2 vUV;
out vec3 vWorldPos;
out float vFogFactor;
float cnaDirectionHandedness(mat3 m)
{
    return dot(m[0], cross(m[1], m[2])) < 0.0 ? -1.0 : 1.0;
}
void main()
{
    gl_Position = uWorldViewProj * vec4(aPos, 1.0);
    // In-shader inverse-transpose normal matrix, matching this renderer's own established
    // lit_textured3d/env_map3d convention (rather than EasyGL's CPU-precomputed uNormalMatrix
    // uniform, which would need a new C++-side cofactor helper this renderer doesn't otherwise
    // have) -- numerically equivalent for any World matrix, uniform-scale or not.
    mat3 normalMatrix = transpose(inverse(mat3(uWorld)));
    vNormal = normalMatrix * aNormal;
    vTangent = mat3(uWorld) * aTangent.xyz;
    vBitangentSign = aTangent.w * cnaDirectionHandedness(mat3(uWorld));
    vUV = aUV;
    vWorldPos = (uWorld * vec4(aPos, 1.0)).xyz;
    // REMED-GFX-010: see kColoredParams3DVertSrc's own comment for the fog-vector formula.
    vFogFactor = 1.0 - clamp(dot(vec4(aPos, 1.0), uFogVector), 0.0, 1.0);
}
)GLSL";

        const char* kPbrSkinned3DVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUV;
layout(location = 4) in vec4 aBoneWeights;
layout(location = 5) in uvec4 aBoneIndices;
uniform mat4 uWorldViewProj;
uniform mat4 uWorld;
uniform mat4 uBones[72];
uniform int uWeightsPerVertex;
uniform vec4 uFogVector;
out vec3 vNormal;
out vec3 vTangent;
out float vBitangentSign;
out vec2 vUV;
out vec3 vWorldPos;
out float vFogFactor;
vec3 cnaSkinNormal(mat3 m, vec3 n)
{
    vec3 c0 = m[0], c1 = m[1], c2 = m[2];
    vec3 co0 = cross(c1, c2), co1 = cross(c2, c0), co2 = cross(c0, c1);
    float det = dot(c0, co0);
    vec3 transformed = mat3(co0, co1, co2) * n;
    return abs(det) > 1e-6 ? transformed * sign(det) : m * n;
}
float cnaDirectionHandedness(mat3 m)
{
    return dot(m[0], cross(m[1], m[2])) < 0.0 ? -1.0 : 1.0;
}
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
    vec3 skinnedNormal = cnaSkinNormal(mat3(skinMat), aNormal);
    vec3 skinnedTangent = mat3(skinMat) * aTangent.xyz;
    mat3 normalMatrix = transpose(inverse(mat3(uWorld)));
    vNormal = normalMatrix * skinnedNormal;
    vTangent = mat3(uWorld) * skinnedTangent;
    vBitangentSign = aTangent.w * cnaDirectionHandedness(mat3(uWorld))
                                * cnaDirectionHandedness(mat3(skinMat));
    vUV = aUV;
    vWorldPos = (uWorld * skinnedPos).xyz;
    vFogFactor = 1.0 - clamp(dot(skinnedPos, uFogVector), 0.0, 1.0);
}
)GLSL";

        const char* kPbr3DFragSrc = R"GLSL(
#version 410 core
in vec3 vNormal;
in vec3 vTangent;
in float vBitangentSign;
in vec2 vUV;
in vec3 vWorldPos;
in float vFogFactor;
uniform sampler2D uTexture;
uniform sampler2D uNormalMap;
uniform sampler2D uMetallicRoughnessMap;
uniform sampler2D uEmissiveMap;
uniform sampler2D uOcclusionMap;
uniform vec4 uDiffuseColor;
uniform vec3 uAmbientColor;
uniform vec3 uEmissiveColor;
uniform float uMetallicFactor;
uniform float uRoughnessFactor;
uniform float uNormalScale;
uniform float uOcclusionStrength;
uniform vec3 uLight0Dir;
uniform vec3 uLight0Diffuse;
uniform vec3 uLight1Dir;
uniform vec3 uLight1Diffuse;
uniform vec3 uLight2Dir;
uniform vec3 uLight2Diffuse;
uniform vec3 uEyePosition;
uniform vec4 uAlphaTest;
uniform vec3 uFogColor;
out vec4 fragColor;

vec3 PbrLight(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 albedo, vec3 F0, float roughness, float metallic)
{
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float a2 = pow(roughness, 4.0);
    float dTerm = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    float D = a2 / (3.14159265 * dTerm * dTerm + 1e-7);

    float k = (roughness + 1.0);
    k = k * k / 8.0;
    float G = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));

    vec3 F = F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);

    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    vec3 diffuseColor = albedo * (1.0 - metallic);
    vec3 kd = vec3(1.0) - F;
    return (kd * diffuseColor / 3.14159265 + specular) * lightColor * NdotL;
}

void main()
{
    vec4 baseColorTex = texture(uTexture, vUV);
    vec3 baseColor = mix(baseColorTex.rgb, cnaSrgbToLinear(baseColorTex.rgb), uSrgb.x);
    vec3 albedo = baseColor * uDiffuseColor.rgb;
    float alpha = baseColorTex.a * uDiffuseColor.a;
    bool passesAlphaTest = (uAlphaTest.y > 0.0)
        ? (abs(alpha - uAlphaTest.x) < uAlphaTest.y)
        : (alpha < uAlphaTest.x);
    if ((passesAlphaTest ? uAlphaTest.z : uAlphaTest.w) < 0.0) discard;

    vec3 N = normalize(vNormal);
    vec3 T = normalize(vTangent - N * dot(N, vTangent));
    vec3 B = cross(N, T) * vBitangentSign;
    mat3 TBN = mat3(T, B, N);
    vec3 sampledNormal = texture(uNormalMap, vUV).rgb * 2.0 - 1.0;
    sampledNormal.xy *= uNormalScale;
    vec3 finalNormal = normalize(TBN * sampledNormal);

    vec4 mr = texture(uMetallicRoughnessMap, vUV);
    float roughness = clamp(mr.g * uRoughnessFactor, 0.045, 1.0);
    float metallic = clamp(mr.b * uMetallicFactor, 0.0, 1.0);

    vec3 V = normalize(uEyePosition - vWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);
    Lo += PbrLight(finalNormal, V, normalize(-uLight0Dir), uLight0Diffuse, albedo, F0, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-uLight1Dir), uLight1Diffuse, albedo, F0, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-uLight2Dir), uLight2Diffuse, albedo, F0, roughness, metallic);

    float occlusionSample = texture(uOcclusionMap, vUV).r;
    float occlusion = 1.0 + uOcclusionStrength * (occlusionSample - 1.0);
    vec3 ambient = uAmbientColor * albedo * occlusion;
    vec3 emissiveSample = texture(uEmissiveMap, vUV).rgb;
    emissiveSample = mix(emissiveSample, cnaSrgbToLinear(emissiveSample), uSrgb.y);
    vec3 emissive = uEmissiveColor * emissiveSample;

    vec3 fogLinear = mix(uFogColor, cnaSrgbToLinear(uFogColor), uSrgb.z);
    vec3 rgb = mix(fogLinear, ambient + Lo + emissive, vFogFactor);
    rgb = mix(rgb, cnaLinearToSrgb(rgb), uSrgb.z);
    fragColor = vec4(rgb, alpha);
}
)GLSL";

        // XNA TextureFilter ordinal -> (GL min filter, GL mag filter). plan_opengl4.md GL4-18:
        // real mip-aware GL min-filter tokens for every "Mip*" variant now that
        // OpenGL4TextureRenderer::UpdatePixelsLevel() lets a texture genuinely have mip levels
        // beyond 0 -- a mip-mapped texture sampled with a non-mip min filter would only ever
        // read level 0 regardless of how many levels were uploaded. Matches
        // EasyGLRenderer::ApplySamplerState's own identical mapping table exactly; safe to
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
        // EasyGLRenderer's own MapDepthFormat.
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
    // OpenGL4TextureRenderer
    // ------------------------------------------------------------------------------------

    OpenGL4TextureRenderer::OpenGL4TextureRenderer(const ImageData& data)
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
        // level 0. Matches EasyGLTextureRenderer's own identical Task-924 fix.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levelCount_ - 1);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    OpenGL4TextureRenderer::~OpenGL4TextureRenderer()
    {
        if (texture_ != 0)
            glDeleteTextures(1, &texture_);
    }

    void OpenGL4TextureRenderer::BindGL(int /*unit*/) const
    {
        glBindTexture(GL_TEXTURE_2D, texture_);
    }

    void OpenGL4TextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
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

    void OpenGL4TextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        glBindTexture(GL_TEXTURE_2D, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // Level 0 is allocated at construction (glTexSubImage2D via UpdatePixels); every other
        // level's storage is never pre-allocated, so this must use glTexImage2D, not
        // glTexSubImage2D, matching EasyGLTextureRenderer::UpdatePixelsLevel's identical approach.
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, levelW, levelH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4Texture3DRenderer
    // ------------------------------------------------------------------------------------

    OpenGL4Texture3DRenderer::OpenGL4Texture3DRenderer(int w, int h, int depth, bool mipMap)
        : width_(w), height_(h), depth_(depth),
          levelCount_(mipMap ? CalculateRenderTargetMipLevelsGL4(w, h) : 1)
    {
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_3D, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // Allocate storage for every mip level up front (not just level 0): SetData's box
        // writes use gl4_glTexSubImage3D, which requires the target level to already have a
        // defined image -- matches EasyGLTexture3DRenderer's own identical pre-allocation loop
        // (and OpenGL4RenderTargetRenderer's/OpenGL4RenderTargetCubeRenderer's established
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
        // same "incomplete mipmap chain renders black" reason OpenGL4TextureRenderer already
        // fixed for Texture2D; EasyGLTexture3DRenderer does not set this at all, so this is
        // deliberately stricter than the EasyGL reference.
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, levelCount_ - 1);
        glBindTexture(GL_TEXTURE_3D, 0);
    }

    OpenGL4Texture3DRenderer::~OpenGL4Texture3DRenderer()
    {
        if (texture_ != 0)
            glDeleteTextures(1, &texture_);
    }

    void OpenGL4Texture3DRenderer::BindGL(int /*unit*/) const
    {
        glBindTexture(GL_TEXTURE_3D, texture_);
    }

    bool OpenGL4Texture3DRenderer::SetData(int level, int x, int y, int z, int w, int h, int depth,
                                          const void* data, int /*dataLength*/)
    {
        glBindTexture(GL_TEXTURE_3D, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl4_glTexSubImage3D(GL_TEXTURE_3D, level, x, y, z, w, h, depth, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_3D, 0);
        return true;
    }

    bool OpenGL4Texture3DRenderer::GetData(int level, int x, int y, int z, int w, int h, int depth,
                                          void* data, int dataLength) const
    {
        if (w <= 0 || h <= 0 || depth <= 0) return false;
        const std::size_t sizeBytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) *
                                       static_cast<std::size_t>(depth) * 4;
        if (static_cast<std::size_t>(dataLength) < sizeBytes)
            throw std::out_of_range("CNA OpenGL4: Texture3D::GetData: dataLength too small for the requested region");

        // Desktop GL 4.x has no per-slice readback shortcut for a 3D texture other than an FBO
        // attached to each Z layer -- mirrors EasyGLTexture3DRenderer::GetData's own per-slice
        // gl4_glFramebufferTextureLayer + glReadPixels loop (GLES3 lacks glGetTexImage; this
        // renderer uses the identical FBO approach for consistency with
        // OpenGL4RenderTargetCubeRenderer's own established per-face FBO readback convention,
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
        return true;
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4TextureCubeRenderer
    // ------------------------------------------------------------------------------------

    namespace
    {
        constexpr GLenum kTextureCubeFaceTargetsGL4[6] = {
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + 0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + 1,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + 2, GL_TEXTURE_CUBE_MAP_POSITIVE_X + 3,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + 4, GL_TEXTURE_CUBE_MAP_POSITIVE_X + 5,
        };
    }

    OpenGL4TextureCubeRenderer::OpenGL4TextureCubeRenderer(int size, bool mipMap)
        : size_(size), levelCount_(mipMap ? CalculateRenderTargetMipLevelsGL4(size, size) : 1)
    {
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_CUBE_MAP, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // Allocate storage for every face x every mip level up front, same rationale as
        // OpenGL4RenderTargetCubeRenderer::CreateResources / OpenGL4Texture3DRenderer above.
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

    OpenGL4TextureCubeRenderer::~OpenGL4TextureCubeRenderer()
    {
        if (texture_ != 0)
            glDeleteTextures(1, &texture_);
    }

    void OpenGL4TextureCubeRenderer::BindGL(int /*unit*/) const
    {
        glBindTexture(GL_TEXTURE_CUBE_MAP, texture_);
    }

    bool OpenGL4TextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                            const void* data, int /*dataLength*/)
    {
        if (face < 0 || face >= 6) return false;
        glBindTexture(GL_TEXTURE_CUBE_MAP, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(kTextureCubeFaceTargetsGL4[face], level, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        return true;
    }

    bool OpenGL4TextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                            void* data, int dataLength) const
    {
        if (face < 0 || face >= 6 || w <= 0 || h <= 0) return false;
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

        // No Y-flip -- unlike OpenGL4RenderTargetCubeRenderer::GetData (a framebuffer-origin
        // render target), this is a plain texture; matches EasyGLTextureCubeRenderer::GetData's
        // own non-flipped convention.
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);

        gl4_glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        gl4_glDeleteFramebuffers(1, &readFbo);
        return true;
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4OcclusionQueryRenderer
    // ------------------------------------------------------------------------------------

    OpenGL4OcclusionQueryRenderer::OpenGL4OcclusionQueryRenderer()
    {
        gl4_glGenQueries(1, &query_);
    }

    OpenGL4OcclusionQueryRenderer::~OpenGL4OcclusionQueryRenderer()
    {
        if (query_ != 0)
            gl4_glDeleteQueries(1, &query_);
    }

    void OpenGL4OcclusionQueryRenderer::Begin()
    {
        if (query_ == 0) return;
        resultCached_ = false;
        gl4_glBeginQuery(GL_SAMPLES_PASSED, query_);
    }

    void OpenGL4OcclusionQueryRenderer::End()
    {
        if (query_ == 0) return;
        gl4_glEndQuery(GL_SAMPLES_PASSED);
    }

    bool OpenGL4OcclusionQueryRenderer::IsComplete() const
    {
        if (query_ == 0) return false;
        if (resultCached_) return true;
        GLuint available = 0;
        gl4_glGetQueryObjectuiv(query_, GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available) return false;
        GLuint result = 0;
        gl4_glGetQueryObjectuiv(query_, GL_QUERY_RESULT, &result);
        cachedResult_ = static_cast<int>(result);
        resultCached_ = true;
        return true;
    }

    int OpenGL4OcclusionQueryRenderer::PixelCount() const
    {
        if (!IsComplete()) return 0;
        return cachedResult_;
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4RenderTargetRenderer
    // ------------------------------------------------------------------------------------

    OpenGL4RenderTargetRenderer::OpenGL4RenderTargetRenderer(int w, int h, int depthFormat,
                                                            bool mipMap, int multiSampleCount)
        : width_(w), height_(h), depthFormat_(depthFormat), mipMap_(mipMap),
          multiSampleCount_(multiSampleCount)
    {
        levelCount_ = mipMap_ ? CalculateRenderTargetMipLevelsGL4(w, h) : 1;
        CreateResources();
    }

    OpenGL4RenderTargetRenderer::~OpenGL4RenderTargetRenderer()
    {
        DestroyResources();
    }

    void OpenGL4RenderTargetRenderer::CreateResources()
    {
        // Clamp to GL_MAX_SAMPLES so glRenderbufferStorageMultisample never errors, mirroring
        // EasyGLRenderTargetRenderer::CreateResources / FNA3D's OPENGL_GetMaxMultiSampleCount.
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

    void OpenGL4RenderTargetRenderer::DestroyResources()
    {
        if (depthRenderbuffer_) gl4_glDeleteRenderbuffers(1, &depthRenderbuffer_);
        if (msaaColorRenderbuffer_) gl4_glDeleteRenderbuffers(1, &msaaColorRenderbuffer_);
        if (resolveFbo_) gl4_glDeleteFramebuffers(1, &resolveFbo_);
        if (fbo_) gl4_glDeleteFramebuffers(1, &fbo_);
        if (colorTexture_) glDeleteTextures(1, &colorTexture_);
        depthRenderbuffer_ = msaaColorRenderbuffer_ = resolveFbo_ = fbo_ = colorTexture_ = 0;
    }

    void OpenGL4RenderTargetRenderer::BindGL(int /*unit*/) const
    {
        glBindTexture(GL_TEXTURE_2D, colorTexture_);
    }

    void OpenGL4RenderTargetRenderer::BindAsRenderTarget()
    {
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    }

    void OpenGL4RenderTargetRenderer::UnbindAsRenderTarget()
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

    bool OpenGL4RenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                             void* data, int dataLength) const
    {
        if (w <= 0 || h <= 0) return false;
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
        // OpenGL4Renderer::ReadBackbuffer's own convention.
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
        return true;
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4RenderTargetCubeRenderer
    // ------------------------------------------------------------------------------------

    OpenGL4RenderTargetCubeRenderer::OpenGL4RenderTargetCubeRenderer(int size, int depthFormat,
                                                                    bool mipMap, int multiSampleCount)
        : size_(size), depthFormat_(depthFormat), mipMap_(mipMap),
          multiSampleCount_(multiSampleCount)
    {
        levelCount_ = mipMap_ ? CalculateRenderTargetMipLevelsGL4(size, size) : 1;
        CreateResources();
    }

    OpenGL4RenderTargetCubeRenderer::~OpenGL4RenderTargetCubeRenderer()
    {
        DestroyResources();
    }

    void OpenGL4RenderTargetCubeRenderer::CreateResources()
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
        // OpenGL4RenderTargetRenderer::CreateResources for why -- glGenerateMipmap's writes need
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

    void OpenGL4RenderTargetCubeRenderer::DestroyResources()
    {
        if (depthRenderbuffer_) gl4_glDeleteRenderbuffers(1, &depthRenderbuffer_);
        if (msaaColorRenderbuffer_) gl4_glDeleteRenderbuffers(1, &msaaColorRenderbuffer_);
        if (resolveFbo_) gl4_glDeleteFramebuffers(1, &resolveFbo_);
        if (fbo_) gl4_glDeleteFramebuffers(1, &fbo_);
        if (cubeTexture_) glDeleteTextures(1, &cubeTexture_);
        depthRenderbuffer_ = msaaColorRenderbuffer_ = resolveFbo_ = fbo_ = cubeTexture_ = 0;
    }

    void OpenGL4RenderTargetCubeRenderer::BindGL(int /*unit*/) const
    {
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubeTexture_);
    }

    void OpenGL4RenderTargetCubeRenderer::BindAsRenderTargetFace(int face)
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

    void OpenGL4RenderTargetCubeRenderer::UnbindAsRenderTarget()
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

    bool OpenGL4RenderTargetCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                                 const void* data, int /*dataLength*/)
    {
        if (face < 0 || face >= 6) return false;
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubeTexture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, x, y, w, h, GL_RGBA,
                        GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        return true;
    }

    bool OpenGL4RenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                                 void* data, int dataLength) const
    {
        if (face < 0 || face >= 6 || w <= 0 || h <= 0) return false;
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
        return true;
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4VertexBufferRenderer
    // ------------------------------------------------------------------------------------

    OpenGL4VertexBufferRenderer::OpenGL4VertexBufferRenderer(int vertexCapacity)
        : capacity_(vertexCapacity)
    {
        gl4_glGenVertexArrays(1, &vao_);
        gl4_glGenBuffers(1, &vbo_);
    }

    OpenGL4VertexBufferRenderer::~OpenGL4VertexBufferRenderer()
    {
        if (vbo_ != 0) gl4_glDeleteBuffers(1, &vbo_);
        if (vao_ != 0) gl4_glDeleteVertexArrays(1, &vao_);
    }

    namespace
    {
        struct VertexAttribFormat
        {
            int componentCount;
            GLenum type;
            bool normalized;
            bool isInteger;
        };

        // plan_opengl4.md GL4-33: maps XNA's VertexElementFormat to the GL attribute shape needed
        // to bind it -- component count, GL scalar type, whether values are normalized to
        // [0,1]/[-1,1], and whether the attribute must be read as a true integer
        // (glVertexAttribIPointer) rather than converted to float (glVertexAttribPointer). Ported
        // from EasyGLRenderer's own DescribeVertexElementFormat (Task 1080) verbatim.
        VertexAttribFormat DescribeVertexElementFormat(VertexElementFormat format)
        {
            switch (format)
            {
            case VertexElementFormat::Single:           return { 1, GL_FLOAT,         false, false };
            case VertexElementFormat::Vector2:          return { 2, GL_FLOAT,         false, false };
            case VertexElementFormat::Vector3:          return { 3, GL_FLOAT,         false, false };
            case VertexElementFormat::Vector4:          return { 4, GL_FLOAT,         false, false };
            case VertexElementFormat::Color:            return { 4, GL_UNSIGNED_BYTE, true,  false };
            case VertexElementFormat::Byte4:            return { 4, GL_UNSIGNED_BYTE, false, true  };
            case VertexElementFormat::Short2:           return { 2, GL_SHORT,         false, false };
            case VertexElementFormat::Short4:           return { 4, GL_SHORT,         false, false };
            case VertexElementFormat::NormalizedShort2: return { 2, GL_SHORT,         true,  false };
            case VertexElementFormat::NormalizedShort4: return { 4, GL_SHORT,         true,  false };
            case VertexElementFormat::HalfVector2:      return { 2, GL_HALF_FLOAT,    false, false };
            case VertexElementFormat::HalfVector4:      return { 4, GL_HALF_FLOAT,    false, false };
            }
            return { 3, GL_FLOAT, false, false };
        }
    }

    void OpenGL4VertexBufferRenderer::SetVertexDeclaration(const VertexDeclaration& vertexDeclaration)
    {
        declaration_.Remember(vertexDeclaration);
    }

    void OpenGL4VertexBufferRenderer::ApplyLayout(std::size_t stride)
    {
        const auto s = static_cast<GLsizei>(stride);
        gl4_glBindVertexArray(vao_);
        gl4_glBindBuffer(GL_ARRAY_BUFFER, vbo_);

        const std::vector<VertexElement>& declarationElements = declaration_.GetElements();
        if (!declarationElements.empty())
        {
            // plan_opengl4.md GL4-33: generic layout binding driven by the caller's own
            // VertexDeclaration -- attribute location = the element's own index within the
            // declaration's element list, matching EasyGLVertexBufferRenderer::ApplyLayout's own
            // Task 1080 convention. Covers layouts that don't match any of the fixed strides the
            // switch below recognizes (needed by hardware instancing's per-instance buffer).
            for (std::size_t i = 0; i < declarationElements.size(); ++i)
            {
                const VertexElement& element = declarationElements[i];
                const VertexAttribFormat desc =
                    DescribeVertexElementFormat(element.getVertexElementFormatProperty());
                const auto location = static_cast<GLuint>(i);
                const void* offset = reinterpret_cast<void*>(
                    static_cast<std::uintptr_t>(element.getOffsetProperty()));
                gl4_glEnableVertexAttribArray(location);
                if (desc.isInteger)
                    gl4_glVertexAttribIPointer(location, desc.componentCount, desc.type, s, offset);
                else
                    gl4_glVertexAttribPointer(location, desc.componentCount, desc.type,
                                              desc.normalized ? GL_TRUE : GL_FALSE, s, offset);
            }
            gl4_glBindVertexArray(0);
            return;
        }

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
            // (+ ubyte4 normalized color for stride 56, matching EasyGLRenderer's own
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
        case 48:
        case 68:
            // plan_opengl4.md GL4-23: VertexPositionNormalTangentTexture (packed): float3
            // position + float3 normal + float4 tangent (xyz + bitangent-handedness sign in w)
            // + float2 texcoord (+ float4 blend weight + ubyte4 blend indices for the stride-68
            // PBR+skinned combo -- locations 0-3 stay byte-identical to stride 48, matching
            // GL4-22's own stride-52-to-56 "append, don't insert" precedent), matching
            // EasyGLRenderer's own stride-48/68 cases.
            gl4_glEnableVertexAttribArray(0);
            gl4_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, s, (void*)0);
            gl4_glEnableVertexAttribArray(1);
            gl4_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, s, (void*)12);
            gl4_glEnableVertexAttribArray(2);
            gl4_glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, s, (void*)24);
            gl4_glEnableVertexAttribArray(3);
            gl4_glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, s, (void*)40);
            if (stride == 68)
            {
                gl4_glEnableVertexAttribArray(4);
                gl4_glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, s, (void*)48);
                gl4_glEnableVertexAttribArray(5);
                gl4_glVertexAttribIPointer(5, 4, GL_UNSIGNED_BYTE, s, (void*)64);
            }
            break;
        default:
            // Unknown layout (not yet ported to this renderer, plan_opengl4.md remaining work):
            // bind position-only as a safe fallback, matching EasyGL's own precedent.
            gl4_glEnableVertexAttribArray(0);
            gl4_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, s, (void*)0);
            break;
        }

        gl4_glBindVertexArray(0);
    }

    void OpenGL4VertexBufferRenderer::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        SetDataWithOptions(data, vertex_count, stride_in_bytes, SetDataOptions::None);
    }

    void OpenGL4VertexBufferRenderer::SetDataWithOptions(const void* data, int vertex_count,
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
    // OpenGL4IndexBufferRenderer
    // ------------------------------------------------------------------------------------

    OpenGL4IndexBufferRenderer::OpenGL4IndexBufferRenderer(int indexCapacity, bool thirtyTwoBit)
        : capacity_(indexCapacity)
        , thirtyTwoBit_(thirtyTwoBit)
    {
        gl4_glGenBuffers(1, &ibo_);
    }

    OpenGL4IndexBufferRenderer::~OpenGL4IndexBufferRenderer()
    {
        if (ibo_ != 0) gl4_glDeleteBuffers(1, &ibo_);
    }

    void OpenGL4IndexBufferRenderer::SetData16(const void* data, int index_count)
    {
        SetData16WithOptions(data, index_count, SetDataOptions::None);
    }

    void OpenGL4IndexBufferRenderer::SetData16WithOptions(const void* data, int index_count, SetDataOptions /*options*/)
    {
        indexCount_ = index_count;
        const auto byteCount = static_cast<GLsizeiptr4>(static_cast<std::size_t>(index_count) * sizeof(uint16_t));
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        gl4_glBufferData(GL_ELEMENT_ARRAY_BUFFER, byteCount, data, GL_DYNAMIC_DRAW);
    }

    void OpenGL4IndexBufferRenderer::SetData32(const void* data, int index_count)
    {
        SetData32WithOptions(data, index_count, SetDataOptions::None);
    }

    void OpenGL4IndexBufferRenderer::SetData32WithOptions(const void* data, int index_count, SetDataOptions /*options*/)
    {
        indexCount_ = index_count;
        const auto byteCount = static_cast<GLsizeiptr4>(static_cast<std::size_t>(index_count) * sizeof(uint32_t));
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        gl4_glBufferData(GL_ELEMENT_ARRAY_BUFFER, byteCount, data, GL_DYNAMIC_DRAW);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4SpriteBatchRenderer
    // ------------------------------------------------------------------------------------

    OpenGL4SpriteBatchRenderer::OpenGL4SpriteBatchRenderer(OpenGL4Renderer& owner)
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

    OpenGL4SpriteBatchRenderer::~OpenGL4SpriteBatchRenderer()
    {
        gl4_glDeleteBuffers(1, &vbo_);
        gl4_glDeleteBuffers(1, &ibo_);
        gl4_glDeleteVertexArrays(1, &vao_);
    }

    void OpenGL4SpriteBatchRenderer::Begin()
    {
        // SpriteBatch::Begin() (the public XNA-facing class) calls SetTransformMatrix()/
        // SetSamplerFilter()/SetSamplerAddressMode() BEFORE this Begin() runs (see
        // SpriteBatch.cpp) -- resetting those fields here would silently discard whatever the
        // caller just requested. Matches EasyGLSpriteBatchRenderer::Begin()'s own precedent,
        // which only flips the begun_ flag.
        begun_ = true;
    }

    void OpenGL4SpriteBatchRenderer::End()
    {
        FlushBatch();
        begun_ = false;
    }

    void OpenGL4SpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        // plan_opengl4.md GL4-32: flush any already-batched sprites under the PREVIOUS effect
        // (built-in or a different custom one) before switching, mirroring
        // EasyGLSpriteBatchRenderer::SetCustomEffect's own identical guard.
        if (customEffect_ != effect)
        {
            FlushBatch();
            customEffect_ = effect;
        }
    }

    void OpenGL4SpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const int w = texture.GetWidth();
        const int h = texture.GetHeight();
        Draw(texture, Rectangle((int)x, (int)y, w, h), Rectangle(0, 0, w, h), Color::White);
    }

    void OpenGL4SpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    void OpenGL4SpriteBatchRenderer::Draw(const ITextureRenderer& texture,
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

    void OpenGL4SpriteBatchRenderer::FlushBatch()
    {
        if (pendingVertices_.empty()) return;

        // plan_opengl4.md GL4-14: size the viewport/ortho projection off the currently-bound
        // RenderTarget2D when one is bound, not unconditionally off the window's physical size
        // -- a SpriteBatch::Draw() into a bound RT smaller than the window would otherwise get a
        // window-sized viewport, offsetting/clipping its own draws (same fix
        // EasyGLRenderer::FlushBatch's own GetCurrentRenderTarget2DSize check applies).
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

        // plan_opengl4.md GL4-32: bind the SAME compiled program the custom ShaderEffect itself
        // owns (Effect::GetEffectRendererPtr(), overridden by ShaderEffect) instead of the
        // built-in sprite program -- mirrors EasyGLSpriteBatchRenderer::FlushBatch's own
        // customEffect_ dispatch (Task 1077's "bind the same program" fix, applied here from the
        // start). "projection" is this codebase's established custom-2D-effect uniform-name
        // convention (see easygl_shader_effect_test.cpp); the built-in program's own
        // "uProjection"/"uTexture" names are this renderer's private internal naming, unrelated to
        // what a caller-authored shader declares.
        OpenGL4RawProgram* prog = &owner_->GetOrCreateSpriteProgram();
        if (customEffect_)
        {
            auto* renderer = dynamic_cast<OpenGL4EffectRenderer*>(customEffect_->GetEffectRendererPtr());
            if (renderer && renderer->IsValid())
                prog = &renderer->GetProgram();
            customEffect_->Apply();
        }

        prog->Use();
        if (customEffect_)
        {
            const int projLoc = prog->UniformLocation("projection");
            if (projLoc >= 0) gl4_glUniformMatrix4fv(projLoc, 1, GL_FALSE, ortho);
        }
        else
        {
            const int projLoc = prog->UniformLocation("uProjection");
            if (projLoc >= 0) gl4_glUniformMatrix4fv(projLoc, 1, GL_FALSE, ortho);
            const int texLoc = prog->UniformLocation("uTexture");
            if (texLoc >= 0) gl4_glUniform1i(texLoc, 0);
        }

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
    // OpenGL4Renderer
    // ------------------------------------------------------------------------------------

    OpenGL4Renderer::OpenGL4Renderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                                                   CnaPresentationMode mode, int multiSampleCount, int swapInterval)
        : window_(window)
        , virtualWidth_(virtualWidth)
        , virtualHeight_(virtualHeight)
        , presentationMode_(mode)
        , swapInterval_(swapInterval)
        // GraphicsRendererCreateArgs::multiSampleCount uses 1 = "no MSAA" (matches
        // EasyGLRenderer's own sampleCount_ convention); this renderer's own internal
        // convention is 0 = disabled (matching OpenGL4RenderTargetRenderer's multiSampleCount_).
        , msaaSampleCount_(multiSampleCount > 1 ? multiSampleCount : 0)
    {
        if (!window_) throw std::runtime_error("OpenGL4Renderer initialized with null window.");

        IGraphicsRenderer::RegisterForWindow(window_, this);

        // Real desktop OpenGL 4.1 core profile -- unlike EasyGLRenderer, which requests
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
        std::cout << "OpenGL4Renderer initialized with OpenGL "
                  << (versionStr ? reinterpret_cast<const char*>(versionStr) : "(unknown)") << std::endl;

        SDL_GL_SetSwapInterval(swapInterval_);

        // Anisotropic filtering is an extension until GL 4.6
        // (EXT/ARB_texture_filter_anisotropic), so the driver's real ceiling is queried once
        // here: drain any pending error, ask, and treat a raised GL_INVALID_ENUM as "not
        // supported" (maxAnisotropy_ stays 1). SupportsCapability answers from this value and
        // ApplySamplerState only uploads the sampler parameter when the driver accepted the
        // query, so an unsupporting driver never sees the unknown pname at all.
        while (glGetError() != GL_NO_ERROR) {}
        GLfloat maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);
        if (glGetError() == GL_NO_ERROR && maxAniso > 1.0f)
            maxAnisotropy_ = maxAniso;

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

    OpenGL4Renderer::~OpenGL4Renderer()
    {
        IGraphicsRenderer::UnregisterForWindow(window_);
        gl4_glDeleteSamplers(kMaxSamplerSlots, samplers_);
        if (defaultWhiteTexture_) glDeleteTextures(1, &defaultWhiteTexture_);
        if (defaultFlatNormalTexture_) glDeleteTextures(1, &defaultFlatNormalTexture_);
        if (mrtFbo_) gl4_glDeleteFramebuffers(1, &mrtFbo_);
        if (msaaDepthRbo_) gl4_glDeleteRenderbuffers(1, &msaaDepthRbo_);
        if (msaaColorRbo_) gl4_glDeleteRenderbuffers(1, &msaaColorRbo_);
        if (msaaFbo_) gl4_glDeleteFramebuffers(1, &msaaFbo_);
        if (glContext_)
            SDL_GL_DestroyContext(static_cast<SDL_GLContext>(glContext_));
    }

    void OpenGL4Renderer::CreateMsaaBuffers(int w, int h)
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

    void OpenGL4Renderer::BindDefaultFramebufferOrMsaa()
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

    void OpenGL4Renderer::ResolveMsaa()
    {
        if (msaaSampleCount_ <= 0) return;
        gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_);
        gl4_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        gl4_glBlitFramebuffer(0, 0, msaaW_, msaaH_, 0, 0, msaaW_, msaaH_, GL_COLOR_BUFFER_BIT,
                              GL_NEAREST);
    }

    bool OpenGL4Renderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
        // Real vertex/index buffers, 3D draw routes, depth/stencil clears and state (GL4-1..16).
        case CNA::GraphicsCapability::ThreeD: return true;
        // A real 24-bit depth / 8-bit stencil buffer on the window and on FBO render targets.
        case CNA::GraphicsCapability::DepthStencilBuffer: return true;
        // Real backbuffer and render-target MSAA via multisample renderbuffers + blit resolve
        // (GL4-17). The GL 4.x core spec requires GL_MAX_SAMPLES >= 4, so a 4.1-core context can
        // never answer no.
        case CNA::GraphicsCapability::MultiSampleAntiAliasing: return true;
        // Real MRT: up to 8 colour attachments with a real glDrawBuffers call (GL4-15).
        case CNA::GraphicsCapability::MultipleRenderTargets: return true;
        // Device/driver-dependent: an extension until GL 4.6, so answered from the ceiling the
        // running driver actually granted at context creation.
        case CNA::GraphicsCapability::AnisotropicFiltering: return maxAnisotropy_ > 1.0f;
        // Real glPolygonMode(GL_FRONT_AND_BACK, GL_LINE) -- desktop core GL keeps the entry
        // point EasyGL's ES target has to emulate (GL4-16).
        case CNA::GraphicsCapability::WireFrame: return true;
        // Real GL_SAMPLES_PASSED query objects with exact passed-sample counts (GL4-24).
        case CNA::GraphicsCapability::OcclusionQuery: return true;
        // Real caller-supplied GLSL compilation via CreateEffectRenderer (GL4-30/32).
        case CNA::GraphicsCapability::CustomEffects: return true;
        // Real GL_TEXTURE_3D storage with per-slice FBO readback (GL4-20).
        case CNA::GraphicsCapability::Texture3D: return true;
        // REMED-GFX-201: not implemented. ApplyLayout binds ONE GL_ARRAY_BUFFER and reads every
        // attribute out of it at stride offsets; there is no second per-vertex stream to bind,
        // and the Ex draw routes refuse a wider binding set up front.
        case CNA::GraphicsCapability::MultiStreamVertexInput: return false;
        // Real hardware instancing (GL4-33): glDrawElementsInstanced/glVertexAttribDivisor are
        // core in every 4.x context, and DrawInstancedPrimitivesEx drives them unconditionally.
        case CNA::GraphicsCapability::Instancing: return true;
        case CNA::GraphicsCapability::StencilBuffer: return true;
        case CNA::GraphicsCapability::AdditiveBlending: return true;
        }
        // Unreachable for current members; a future member lands here (after the -Wswitch
        // warning above) and is reported unsupported until this renderer explicitly claims it.
        return false;
    }

    void OpenGL4Renderer::Clear(float r, float g, float b, float a)
    {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void OpenGL4Renderer::Present()
    {
        if (msaaSampleCount_ > 0) ResolveMsaa();
        SDL_GL_SwapWindow(window_);
        if (msaaSampleCount_ > 0) gl4_glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
    }

    void OpenGL4Renderer::GetPhysicalSize(int& width, int& height) const
    {
        SDL_GetWindowSize(window_, &width, &height);
    }

    void OpenGL4Renderer::GetLogicalSize(int& width, int& height) const
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

    void OpenGL4Renderer::GetViewportSize(int& width, int& height)
    {
        GetLogicalSize(width, height);
    }

    bool OpenGL4Renderer::TransformWindowToLogical(float windowX, float windowY,
                                                           float& logX, float& logY) const
    {
        if (virtualHeight_ <= 0) return false;
        int physW = 0, physH = 0;
        SDL_GetWindowSize(window_, &physW, &physH);
        if (physH <= 0) return false;
        const float scale = static_cast<float>(virtualHeight_) / static_cast<float>(physH);
        logX = windowX * scale;
        logY = windowY * scale;
        return true;
    }

    bool OpenGL4Renderer::TransformLogicalToWindow(float logX, float logY,
                                                           float& windowX, float& windowY) const
    {
        // Inverse of TransformWindowToLogical: logical = window * (virtualHeight_ / physH), so
        // window = logical * (physH / virtualHeight_). A pure uniform scale with NO offset,
        // exact for this renderer's own default FixedHeightDynamicWidth presentation (the logical
        // viewport always fills the whole physical window, no letterbox bars), matching
        // EasyGLRenderer::TransformLogicalToWindow's own identical formula/rationale.
        if (virtualHeight_ <= 0) return false;
        int physW = 0, physH = 0;
        SDL_GetWindowSize(window_, &physW, &physH);
        if (physH <= 0) return false;
        const float invScale = static_cast<float>(physH) / static_cast<float>(virtualHeight_);
        windowX = logX * invScale;
        windowY = logY * invScale;
        return true;
    }

    void OpenGL4Renderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void OpenGL4Renderer::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void OpenGL4Renderer::SetSwapInterval(int interval)
    {
        swapInterval_ = interval;
        SDL_GL_SetSwapInterval(interval);
    }

    std::unique_ptr<ITextureRenderer> OpenGL4Renderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<OpenGL4TextureRenderer>(data);
    }

    std::unique_ptr<ITexture3DRenderer> OpenGL4Renderer::CreateTexture3D(int w, int h, int depth,
                                                                                bool mipMap, int /*surfaceFormat*/)
    {
        // surfaceFormat is currently unused -- matches EasyGLRenderer::CreateTexture3D's
        // own identical "always RGBA8" behavior (see plan_opengl4.md GL4-20 and
        // docs/texture3d-texturecube-support.md's documented cross-renderer gap).
        return std::make_unique<OpenGL4Texture3DRenderer>(w, h, depth, mipMap);
    }

    std::unique_ptr<ITextureCubeRenderer> OpenGL4Renderer::CreateTextureCube(int size, bool mipMap,
                                                                                    int /*surfaceFormat*/)
    {
        return std::make_unique<OpenGL4TextureCubeRenderer>(size, mipMap);
    }

    std::unique_ptr<IOcclusionQueryRenderer> OpenGL4Renderer::CreateOcclusionQuery()
    {
        return std::make_unique<OpenGL4OcclusionQueryRenderer>();
    }

    std::unique_ptr<IEffectRenderer> OpenGL4Renderer::CreateEffectRenderer(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        auto renderer = std::make_unique<OpenGL4EffectRenderer>();
        renderer->CompileProgram(vertSrc, fragSrc);
        return renderer;
    }

    // --- OpenGL4EffectRenderer (plan_opengl4.md GL4-30) ---

    bool OpenGL4EffectRenderer::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        return program_.Compile(vertSrc, fragSrc);
    }

    void OpenGL4EffectRenderer::Bind()
    {
        if (program_.IsValid())
            program_.Use();
    }

    void OpenGL4EffectRenderer::Unbind()
    {
        // No explicit "unbind program" concept in raw GL -- the next Use() (built-in shader or
        // another effect) simply overrides it, matching OpenGL4RawProgram's own convention (every
        // built-in stride-dispatched shader in BindProgramForStride behaves identically).
    }

    bool OpenGL4EffectRenderer::IsValid() const
    {
        return program_.IsValid();
    }

    std::string OpenGL4EffectRenderer::GetCompileError() const
    {
        return program_.GetError();
    }

    void OpenGL4EffectRenderer::SetUniformFloat(const char* name, float value)
    {
        const int loc = program_.UniformLocation(name);
        if (loc >= 0) gl4_glUniform1f(loc, value);
    }

    void OpenGL4EffectRenderer::SetUniformInt(const char* name, int value)
    {
        const int loc = program_.UniformLocation(name);
        if (loc >= 0) gl4_glUniform1i(loc, value);
    }

    void OpenGL4EffectRenderer::SetUniformVec2(const char* name, float x, float y)
    {
        const int loc = program_.UniformLocation(name);
        if (loc >= 0) gl4_glUniform2f(loc, x, y);
    }

    void OpenGL4EffectRenderer::SetUniformVec3(const char* name, float x, float y, float z)
    {
        const int loc = program_.UniformLocation(name);
        if (loc >= 0) gl4_glUniform3f(loc, x, y, z);
    }

    void OpenGL4EffectRenderer::SetUniformVec4(const char* name, float x, float y, float z, float w)
    {
        const int loc = program_.UniformLocation(name);
        if (loc >= 0) gl4_glUniform4f(loc, x, y, z, w);
    }

    void OpenGL4EffectRenderer::SetUniformMat4(const char* name, const float* matrix)
    {
        const int loc = program_.UniformLocation(name);
        if (loc >= 0) gl4_glUniformMatrix4fv(loc, 1, GL_FALSE, matrix);
    }

    void OpenGL4EffectRenderer::SetUniformFloatArray(const char* name, const float* values, int count)
    {
        const int loc = program_.UniformLocation(name);
        if (loc >= 0) gl4_glUniform1fv(loc, count, values);
    }

    void OpenGL4EffectRenderer::SetUniformVec2Array(const char* name, const float* values, int count)
    {
        const int loc = program_.UniformLocation(name);
        if (loc >= 0) gl4_glUniform2fv(loc, count, values);
    }

    void OpenGL4EffectRenderer::BindTexture(int unit, ITextureRenderer* texture)
    {
        if (!texture) return;
        gl4_glActiveTexture(GL_TEXTURE0 + unit);
        texture->BindGL();
        gl4_glActiveTexture(GL_TEXTURE0);
    }

    void OpenGL4EffectRenderer::BindTextureCube(int unit, ITextureCubeRenderer* texture)
    {
        if (!texture) return;
        gl4_glActiveTexture(GL_TEXTURE0 + unit);
        texture->BindGL();
        gl4_glActiveTexture(GL_TEXTURE0);
    }

    void OpenGL4EffectRenderer::BindTexture3D(int unit, ITexture3DRenderer* texture)
    {
        if (!texture) return;
        gl4_glActiveTexture(GL_TEXTURE0 + unit);
        texture->BindGL();
        gl4_glActiveTexture(GL_TEXTURE0);
    }

    std::unique_ptr<ISpriteBatchRenderer> OpenGL4Renderer::CreateSpriteBatch()
    {
        return std::make_unique<OpenGL4SpriteBatchRenderer>(*this);
    }

    std::unique_ptr<IRenderTargetRenderer> OpenGL4Renderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // preserveContents (RenderTargetUsage::PreserveContents) has no effect on this renderer --
        // FBO contents are never implicitly discarded between GraphicsDevice::SetRenderTarget
        // calls, matching EasyGLRenderer's own CreateRenderTarget2D (it accepts and
        // likewise ignores the same parameter).
        (void)preserveContents;
        return std::make_unique<OpenGL4RenderTargetRenderer>(w, h, depthFormat, mipMap, multiSampleCount);
    }

    void OpenGL4Renderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        // Regenerate the OLD target's mip chain (and resolve its MSAA, if any) before switching
        // away from it -- mirrors EasyGLRenderer::SetRenderTarget2D's identical ordering.
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

    bool OpenGL4Renderer::GetCurrentRenderTarget2DSize(int& width, int& height) const
    {
        if (!currentRt2D_) return false;
        width = currentRt2D_->GetWidth();
        height = currentRt2D_->GetHeight();
        return true;
    }

    std::unique_ptr<IRenderTargetCubeRenderer> OpenGL4Renderer::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // REMED-GFX-136: consumed by being deliberately unused, matching
        // EasyGLRenderer::CreateRenderTargetCube's own reasoning -- the FBO's colour
        // attachment IS the cube texture and binding an FBO never touches its contents, so a
        // single-sample face is preserved by construction. The only thing that clears one is the
        // explicit glClear GraphicsDevice issues (and only issues) for a DiscardContents target.
        (void) preserveContents;
        return std::make_unique<OpenGL4RenderTargetCubeRenderer>(size, depthFormat, mipMap, multiSampleCount);
    }

    void OpenGL4Renderer::SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face)
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

    void OpenGL4Renderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (count <= 0) { SetRenderTarget2D(nullptr); return; }
        if (!renderTargets)
            throw std::invalid_argument(
                "OpenGL4 SetRenderTargets: nonzero count requires a binding array.");
        if (count == 1)
        {
            // Every descriptor kind is explicitly consumed: a cube-face binding routes to the
            // real cube-face setter, never flattened to a RenderTarget2D or to face +X.
            if (renderTargets[0].IsRenderTargetCubeFace())
                SetRenderTargetCubeFace(renderTargets[0].GetRenderTargetCube(),
                                        renderTargets[0].GetCubeFace());
            else
                SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
            return;
        }

        // MRT: unbind whatever single RT/cube-face was previously active (mip regen if needed).
        // MRT + per-target mipmaps is not supported here (mirrors
        // EasyGLRenderer::SetRenderTargets' identical, documented gap) -- MRT targets are
        // never tracked as currentRt2D_/currentRtCube_, so switching away from MRT mode cannot
        // regenerate their mips.
        constexpr int kMaxMRT = 8;
        if (count > kMaxMRT)
            throw std::runtime_error(
                "OpenGL4 SetRenderTargets: requested " + std::to_string(count)
                + " targets, but this renderer binds at most " + std::to_string(kMaxMRT) + ".");

        std::array<IRenderTargetRenderer*, kMaxMRT> targets{};
        for (int i = 0; i < count; ++i)
        {
            if (renderTargets[i].IsRenderTargetCubeFace())
                throw std::runtime_error(
                    "OpenGL4 SetRenderTargets: cube faces in a multi-target set are not "
                    "implemented by this CNA renderer.");
            targets[static_cast<std::size_t>(i)] = renderTargets[i].GetRenderTarget2D();
            if (!targets[static_cast<std::size_t>(i)])
                throw std::runtime_error(
                    "OpenGL4 SetRenderTargets: binding " + std::to_string(i)
                    + " does not carry a RenderTarget2D.");
        }

        if (currentRt2D_) currentRt2D_->UnbindAsRenderTarget();
        if (currentRtCube_) currentRtCube_->UnbindAsRenderTarget();
        currentRt2D_ = nullptr;
        currentRtCube_ = nullptr;

        if (!mrtFbo_) gl4_glGenFramebuffers(1, &mrtFbo_);
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, mrtFbo_);

        GLenum drawBufs[kMaxMRT];
        for (int i = 0; i < count; ++i)
        {
            gl4_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D,
                                       targets[static_cast<std::size_t>(i)]->GetColorGLHandle(), 0);
            drawBufs[i] = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i);
        }
        gl4_glDrawBuffers(count, drawBufs);

        // No depth attachment for MRT (same accepted gap as EasyGLRenderer's own MRT
        // FBO) -- the viewport reset that follows still needs the first target's height for
        // SetViewport's Y-flip.
        currentRtHeight_ = targets[0]->GetHeight();
    }

    void OpenGL4Renderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        int fbH = 0, fbW = 0;
        GetPhysicalSize(fbW, fbH);

        // glReadPixels cannot sample a multisample attachment directly -- resolve into FBO 0
        // first (plan_opengl4.md GL4-17), matching EasyGLRenderer::ReadBackbuffer's own
        // sampleCount_>1 handling. Guarded by currentRtHeight_==0 (no RT bound) since this method
        // is only ever meant to read the real backbuffer, never an active render target's FBO.
        const bool resolvingBackbufferMsaa = msaaSampleCount_ > 0 && currentRtHeight_ == 0;
        if (resolvingBackbufferMsaa)
        {
            ResolveMsaa();
            gl4_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        // OpenGL's origin is bottom-left; flip Y so the caller gets top-left-origin game
        // coordinates, matching EasyGLRenderer::ReadBackbuffer's own convention.
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

    void OpenGL4Renderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        glClearColor(r, g, b, a);
        glClearDepth(depth);
        const GLboolean wasWritable = depthWriteEnabled_ ? GL_TRUE : GL_FALSE;
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDepthMask(wasWritable);
    }

    void OpenGL4Renderer::ClearDepth(float depth)
    {
        glClearDepth(depth);
        const GLboolean wasWritable = depthWriteEnabled_ ? GL_TRUE : GL_FALSE;
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        glDepthMask(wasWritable);
    }

    void OpenGL4Renderer::ClearStencil(int stencil)
    {
        glClearStencil(stencil);
        glStencilMask(0xFFu);
        glClear(GL_STENCIL_BUFFER_BIT);
    }

    void OpenGL4Renderer::ClearDepthAndStencil(float depth, int stencil)
    {
        glClearDepth(depth);
        glClearStencil(stencil);
        const GLboolean wasWritable = depthWriteEnabled_ ? GL_TRUE : GL_FALSE;
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFu);
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glDepthMask(wasWritable);
    }

    void OpenGL4Renderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        glClearColor(r, g, b, a);
        glClearStencil(stencil);
        glStencilMask(0xFFu);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void OpenGL4Renderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
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

    void OpenGL4Renderer::SetDepthTestEnabled(bool enabled)
    {
        if (enabled) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
    }

    void OpenGL4Renderer::SetBlendEnabled(bool enabled)
    {
        if (enabled) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
    }

    void OpenGL4Renderer::SetDepthWriteEnabled(bool enabled)
    {
        depthWriteEnabled_ = enabled;
        glDepthMask(enabled ? GL_TRUE : GL_FALSE);
    }

    std::unique_ptr<IVertexBufferRenderer> OpenGL4Renderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<OpenGL4VertexBufferRenderer>(vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> OpenGL4Renderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<OpenGL4IndexBufferRenderer>(index_capacity, /*thirtyTwoBit=*/false);
    }

    std::unique_ptr<IIndexBufferRenderer> OpenGL4Renderer::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<OpenGL4IndexBufferRenderer>(index_capacity, /*thirtyTwoBit=*/true);
    }

    void OpenGL4Renderer::EnsureColored3DProgram()
    {
        if (colored3DProgram_.IsValid()) return;
        if (!colored3DProgram_.Compile(kColored3DVertSrc, kColored3DFragSrc))
            throw std::runtime_error("OpenGL4: colored3d program failed to compile: " + colored3DProgram_.GetError());
        colored3DWvpLoc_ = colored3DProgram_.UniformLocation("uWorldViewProj");
    }

    void OpenGL4Renderer::EnsureColoredParams3DProgram()
    {
        if (coloredParams3DProgram_.IsValid()) return;
        if (!coloredParams3DProgram_.Compile(kColoredParams3DVertSrc, kColoredParams3DFragSrc))
            throw std::runtime_error("OpenGL4: coloredParams3d program failed to compile: " + coloredParams3DProgram_.GetError());
    }

    void OpenGL4Renderer::EnsureTextured3DProgram()
    {
        if (textured3DProgram_.IsValid()) return;
        if (!textured3DProgram_.Compile(kTextured3DVertSrc, kTextured3DFragSrc))
            throw std::runtime_error("OpenGL4: textured3d program failed to compile: " + textured3DProgram_.GetError());
    }

    void OpenGL4Renderer::EnsureColoredTextured3DProgram()
    {
        if (coloredTextured3DProgram_.IsValid()) return;
        if (!coloredTextured3DProgram_.Compile(kColoredTextured3DVertSrc, kColoredTextured3DFragSrc))
            throw std::runtime_error("OpenGL4: colored_textured3d program failed to compile: " + coloredTextured3DProgram_.GetError());
    }

    void OpenGL4Renderer::EnsureLitTextured3DProgram()
    {
        if (litTextured3DProgram_.IsValid()) return;
        if (!litTextured3DProgram_.Compile(kLitTextured3DVertSrc, kLitTextured3DFragSrc))
            throw std::runtime_error("OpenGL4: lit_textured3d program failed to compile: " + litTextured3DProgram_.GetError());
    }

    void OpenGL4Renderer::EnsureLitTextured3DVertexLitProgram()
    {
        if (litTextured3DVertexLitProgram_.IsValid()) return;
        if (!litTextured3DVertexLitProgram_.Compile(kLitTextured3DVertexLitVertSrc, kLitTextured3DVertexLitFragSrc))
            throw std::runtime_error("OpenGL4: lit_textured3d (vertex-lit) program failed to compile: " +
                                      litTextured3DVertexLitProgram_.GetError());
    }

    void OpenGL4Renderer::EnsureEnvMap3DProgram()
    {
        if (envMap3DProgram_.IsValid()) return;
        if (!envMap3DProgram_.Compile(kEnvMap3DVertSrc, kEnvMap3DFragSrc))
            throw std::runtime_error("OpenGL4: env_map3d program failed to compile: " + envMap3DProgram_.GetError());
    }

    void OpenGL4Renderer::EnsureSkinned3DProgram()
    {
        if (skinned3DProgram_.IsValid()) return;
        if (!skinned3DProgram_.Compile(kSkinned3DVertSrc, kSkinned3DFragSrc))
            throw std::runtime_error("OpenGL4: skinned3d program failed to compile: " + skinned3DProgram_.GetError());
    }

    void OpenGL4Renderer::EnsureSkinned3DVertexLitProgram()
    {
        if (skinned3DVertexLitProgram_.IsValid()) return;
        if (!skinned3DVertexLitProgram_.Compile(kSkinned3DVertexLitVertSrc, kSkinned3DVertexLitFragSrc))
            throw std::runtime_error("OpenGL4: skinned3d (vertex-lit) program failed to compile: " +
                                      skinned3DVertexLitProgram_.GetError());
    }

    void OpenGL4Renderer::EnsurePbr3DProgram()
    {
        if (pbr3DProgram_.IsValid()) return;
        if (!pbr3DProgram_.Compile(kPbr3DVertSrc, kPbr3DFragSrc))
            throw std::runtime_error("OpenGL4: pbr3d program failed to compile: " + pbr3DProgram_.GetError());
    }

    void OpenGL4Renderer::EnsurePbrSkinned3DProgram()
    {
        if (pbrSkinned3DProgram_.IsValid()) return;
        if (!pbrSkinned3DProgram_.Compile(kPbrSkinned3DVertSrc, kPbr3DFragSrc))
            throw std::runtime_error("OpenGL4: pbr_skinned3d program failed to compile: " + pbrSkinned3DProgram_.GetError());
    }

    void OpenGL4Renderer::EnsureDefaultWhiteTexture()
    {
        if (defaultWhiteTexture_ != 0) return;
        const uint8_t white[4] = {255, 255, 255, 255};
        glGenTextures(1, &defaultWhiteTexture_);
        glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void OpenGL4Renderer::EnsureDefaultFlatNormalTexture()
    {
        if (defaultFlatNormalTexture_ != 0) return;
        const uint8_t flatNormal[4] = {128, 128, 255, 255};
        glGenTextures(1, &defaultFlatNormalTexture_);
        glBindTexture(GL_TEXTURE_2D, defaultFlatNormalTexture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, flatNormal);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    bool OpenGL4Renderer::BindProgramForStride(std::size_t strideInBytes, const Matrix& world, const Matrix& view,
                                                       const Matrix& projection, const GpuDrawParams& params)
    {
        const Matrix wvp = world * view * projection;
        float wvpCol[16];
        wvp.ToColumnMajor(wvpCol);

        // plan_opengl4.md GL4-25: uploads the 4 fog uniforms (a no-op via the `loc>=0` guard on
        // any program whose shader source doesn't declare them, so this is safe to call
        // unconditionally for every stride case below).
        const auto setFog = [&](OpenGL4RawProgram& prog) {
            const int fogVectorLoc = prog.UniformLocation("uFogVector");
            if (fogVectorLoc >= 0)
                gl4_glUniform4f(fogVectorLoc, params.fogVector[0], params.fogVector[1],
                                params.fogVector[2], params.fogVector[3]);
            const int fogColorLoc = prog.UniformLocation("uFogColor");
            if (fogColorLoc >= 0) gl4_glUniform3f(fogColorLoc, params.fogColor[0], params.fogColor[1], params.fogColor[2]);
        };

        // plan_opengl4.md GL4-23: lazily create PbrEffect's fallback textures BEFORE any real
        // per-draw texture gets bound below -- EnsureDefaultWhiteTexture()/
        // EnsureDefaultFlatNormalTexture() do their own glBindTexture(GL_TEXTURE_2D, ...) on
        // whatever unit is currently active, then unbind (GL_TEXTURE_2D -> 0) when done. Calling
        // them AFTER texture0 was already bound to unit 0 (a real bug found while testing this
        // task) would clobber/unbind that real base-colour texture on the one draw call where
        // these fallbacks are first created -- every later PBR draw call is unaffected, since
        // both Ensure* functions early-return once created.
        if (params.pbr)
        {
            EnsureDefaultWhiteTexture();
            EnsureDefaultFlatNormalTexture();
        }

        // plan_opengl4.md GL4-26: no ApplySamplerState() call is needed here for any texture
        // unit -- GraphicsDevice::applySamplerStatesToRenderer() already calls
        // renderer_->ApplySamplerState(slot, ...) for ALL 16 sampler slots, reading each slot's
        // REAL GraphicsDevice.SamplerStates[slot] value, immediately before every
        // DrawPrimitivesEx/DrawIndexedPrimitivesEx call reaches this function (every call site in
        // GraphicsDevice.cpp pairs the two calls back to back). A real, now-fixed bug: this
        // function used to call ApplySamplerState(slot, 0, 1, 1, 1) (Linear + hardcoded Clamp)
        // for every bound unit AFTER that real state was already applied, silently overwriting
        // it -- always Clamp, and ignoring the real per-slot SamplerState entirely, on every
        // single direct 3D draw. Real XNA's own SamplerState default is Linear+Wrap (not Clamp),
        // so the old hardcoded value was not just non-dynamic but the wrong default too. Matches
        // EasyGLRenderer::BindDrawParams's own established convention of never touching
        // sampler state itself during a 3D draw dispatch, relying solely on the same upstream
        // GraphicsDevice call.
        const bool hasTexture0 = params.texture0 != nullptr;
        if (hasTexture0)
        {
            gl4_glActiveTexture(GL_TEXTURE0);
            params.texture0->BindGL();
        }
        // plan_opengl4.md GL4-19: DualTextureEffect's second sampler.
        const bool hasTexture1 = params.texture1 != nullptr;
        if (hasTexture1)
        {
            gl4_glActiveTexture(GL_TEXTURE1);
            params.texture1->BindGL();
        }
        // plan_opengl4.md GL4-21: EnvironmentMapEffect's cube map -- unit 1, same slot
        // DualTextureEffect's texture1 uses (the two effects are mutually exclusive per draw, so
        // there's no conflict), matching EasyGLRenderer::BindDrawParams's own unit choice.
        const bool hasEnvMap = params.envMapping && params.envMap != nullptr;
        if (hasEnvMap)
        {
            gl4_glActiveTexture(GL_TEXTURE1);
            params.envMap->BindGL();
        }

        // plan_opengl4.md GL4-23: PbrEffect's 4 extra texture units (1=normal, 2=metallic-
        // roughness, 3=emissive, 4=occlusion). Unlike texture1/envMap above, the PBR fragment
        // shader samples all 5 units unconditionally (no uniform-gated branch), so every unit
        // always gets a real bound texture -- defaultFlatNormalTexture_/defaultWhiteTexture_ when
        // the corresponding GpuDrawParams::pbr*Map pointer is null, matching
        // EasyGLRenderer::BindDrawParams's own fallback convention.
        if (params.pbr)
        {
            gl4_glActiveTexture(GL_TEXTURE1);
            if (params.pbrNormalMap) params.pbrNormalMap->BindGL();
            else glBindTexture(GL_TEXTURE_2D, defaultFlatNormalTexture_);

            gl4_glActiveTexture(GL_TEXTURE2);
            if (params.pbrMetallicRoughnessMap) params.pbrMetallicRoughnessMap->BindGL();
            else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture_);

            gl4_glActiveTexture(GL_TEXTURE3);
            if (params.pbrEmissiveMap) params.pbrEmissiveMap->BindGL();
            else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture_);

            gl4_glActiveTexture(GL_TEXTURE4);
            if (params.pbrOcclusionMap) params.pbrOcclusionMap->BindGL();
            else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture_);
        }

        if (params.pbr && (strideInBytes == 48 || strideInBytes == 68))
        {
            OpenGL4RawProgram& prog = (strideInBytes == 68) ? pbrSkinned3DProgram_ : pbr3DProgram_;
            if (strideInBytes == 68) EnsurePbrSkinned3DProgram(); else EnsurePbr3DProgram();
            prog.Use();
            float worldCol[16];
            world.ToColumnMajor(worldCol);
            const auto setM4 = [&](const char* name, const float* m) {
                const int loc = prog.UniformLocation(name);
                if (loc >= 0) gl4_glUniformMatrix4fv(loc, 1, GL_FALSE, m);
            };
            const auto setV3 = [&](const char* name, const float* v) {
                const int loc = prog.UniformLocation(name);
                if (loc >= 0) gl4_glUniform3f(loc, v[0], v[1], v[2]);
            };
            setM4("uWorldViewProj", wvpCol);
            setM4("uWorld", worldCol);
            if (strideInBytes == 68)
            {
                const int bonesLoc = prog.UniformLocation("uBones[0]");
                if (bonesLoc >= 0 && params.boneCount > 0)
                    gl4_glUniformMatrix4fv(bonesLoc, params.boneCount, GL_FALSE, params.boneTransforms);
                const int weightsLoc = prog.UniformLocation("uWeightsPerVertex");
                if (weightsLoc >= 0) gl4_glUniform1i(weightsLoc, params.weightsPerVertex);
            }
            const int diffuseLoc = prog.UniformLocation("uDiffuseColor");
            if (diffuseLoc >= 0) gl4_glUniform4f(diffuseLoc, params.diffuseColor[0], params.diffuseColor[1],
                                                 params.diffuseColor[2], params.diffuseColor[3]);
            setV3("uAmbientColor", params.ambientColor);
            setV3("uEmissiveColor", params.emissiveColor);
            const int metallicLoc = prog.UniformLocation("uMetallicFactor");
            if (metallicLoc >= 0) gl4_glUniform1f(metallicLoc, params.pbrMetallicFactor);
            const int roughnessLoc = prog.UniformLocation("uRoughnessFactor");
            if (roughnessLoc >= 0) gl4_glUniform1f(roughnessLoc, params.pbrRoughnessFactor);
            const int normalScaleLoc = prog.UniformLocation("uNormalScale");
            if (normalScaleLoc >= 0) gl4_glUniform1f(normalScaleLoc, params.pbrNormalScale);
            const int occlusionStrengthLoc = prog.UniformLocation("uOcclusionStrength");
            if (occlusionStrengthLoc >= 0) gl4_glUniform1f(occlusionStrengthLoc, params.pbrOcclusionStrength);
            const int srgbLoc = prog.UniformLocation("uSrgb");
            if (srgbLoc >= 0)
                gl4_glUniform3f(srgbLoc,
                                params.pbrBaseColorTextureIsSrgb ? 1.0f : 0.0f,
                                params.pbrEmissiveTextureIsSrgb ? 1.0f : 0.0f,
                                params.pbrEncodeOutputToSrgb ? 1.0f : 0.0f);
            const int alphaTestLoc = prog.UniformLocation("uAlphaTest");
            if (alphaTestLoc >= 0)
                gl4_glUniform4f(alphaTestLoc, params.alphaTest[0], params.alphaTest[1],
                                params.alphaTest[2], params.alphaTest[3]);
            setV3("uLight0Dir", params.light0Dir);
            setV3("uLight0Diffuse", params.light0Diffuse);
            setV3("uLight1Dir", params.light1Dir);
            setV3("uLight1Diffuse", params.light1Diffuse);
            setV3("uLight2Dir", params.light2Dir);
            setV3("uLight2Diffuse", params.light2Diffuse);
            setV3("uEyePosition", params.eyePositionWorld);
            const int texLoc = prog.UniformLocation("uTexture");
            if (texLoc >= 0) gl4_glUniform1i(texLoc, 0);
            const int normalMapLoc = prog.UniformLocation("uNormalMap");
            if (normalMapLoc >= 0) gl4_glUniform1i(normalMapLoc, 1);
            const int mrLoc = prog.UniformLocation("uMetallicRoughnessMap");
            if (mrLoc >= 0) gl4_glUniform1i(mrLoc, 2);
            const int emissiveMapLoc = prog.UniformLocation("uEmissiveMap");
            if (emissiveMapLoc >= 0) gl4_glUniform1i(emissiveMapLoc, 3);
            const int occlusionMapLoc = prog.UniformLocation("uOcclusionMap");
            if (occlusionMapLoc >= 0) gl4_glUniform1i(occlusionMapLoc, 4);
            setFog(prog);
            return true;
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
            setFog(envMap3DProgram_);
            return true;
        }

        switch (strideInBytes)
        {
        case 16: // VertexPositionColor -- plan_opengl4.md GL4-25: real GpuDrawParams-aware case
        {
            EnsureColoredParams3DProgram();
            coloredParams3DProgram_.Use();
            const int wvpLoc = coloredParams3DProgram_.UniformLocation("uWorldViewProj");
            if (wvpLoc >= 0) gl4_glUniformMatrix4fv(wvpLoc, 1, GL_FALSE, wvpCol);
            const int diffuseLoc = coloredParams3DProgram_.UniformLocation("uDiffuseColor");
            if (diffuseLoc >= 0) gl4_glUniform4f(diffuseLoc, params.diffuseColor[0], params.diffuseColor[1],
                                                 params.diffuseColor[2], params.diffuseColor[3]);
            const int vcLoc = coloredParams3DProgram_.UniformLocation("uVertexColorEnabled");
            if (vcLoc >= 0) gl4_glUniform1i(vcLoc, params.vertexColorEnabled ? 1 : 0);
            const int alphaTestLoc = coloredParams3DProgram_.UniformLocation("uAlphaTest");
            if (alphaTestLoc >= 0) gl4_glUniform4f(alphaTestLoc, params.alphaTest[0], params.alphaTest[1],
                                                   params.alphaTest[2], params.alphaTest[3]);
            setFog(coloredParams3DProgram_);
            return true;
        }
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
            setFog(textured3DProgram_);
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
            setFog(coloredTextured3DProgram_);
            return true;
        }
        case 32: // VertexPositionNormalTexture
        {
            // plan_opengl4.md GL4-29: real XNA's BasicEffect defaults PreferPerPixelLighting=false
            // (per-vertex/Gouraud-shaded lighting) -- only meaningfully distinct while lighting is
            // actually on, matching EasyGLRenderer::SelectProgram's own identical gate.
            const bool vertexLit = params.lightingEnabled && !params.preferPerPixelLighting;
            if (vertexLit) EnsureLitTextured3DVertexLitProgram(); else EnsureLitTextured3DProgram();
            OpenGL4RawProgram& prog = vertexLit ? litTextured3DVertexLitProgram_ : litTextured3DProgram_;
            prog.Use();
            float worldCol[16];
            world.ToColumnMajor(worldCol);
            const auto setM4 = [&](const char* name, const float* m) {
                const int loc = prog.UniformLocation(name);
                if (loc >= 0) gl4_glUniformMatrix4fv(loc, 1, GL_FALSE, m);
            };
            const auto setV3 = [&](const char* name, const float* v) {
                const int loc = prog.UniformLocation(name);
                if (loc >= 0) gl4_glUniform3f(loc, v[0], v[1], v[2]);
            };
            const auto setB = [&](const char* name, bool v) {
                const int loc = prog.UniformLocation(name);
                if (loc >= 0) gl4_glUniform1i(loc, v ? 1 : 0);
            };
            setM4("uWorldViewProj", wvpCol);
            setM4("uWorld", worldCol);
            const int diffuseLoc = prog.UniformLocation("uDiffuseColor");
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
            const int specPowerLoc = prog.UniformLocation("uSpecularPower");
            if (specPowerLoc >= 0) gl4_glUniform1f(specPowerLoc, params.specularPower);
            const int texLoc = prog.UniformLocation("uTexture");
            if (texLoc >= 0) gl4_glUniform1i(texLoc, 0);
            setFog(prog);
            return true;
        }
        case 52: // VertexPositionNormalTextureSkinned
        case 56: // VertexPositionNormalTextureSkinned + Color
        {
            // plan_opengl4.md GL4-29: real XNA's SkinnedEffect also defaults
            // PreferPerPixelLighting=false, same gate as the stride-32 case above.
            const bool vertexLit = params.lightingEnabled && !params.preferPerPixelLighting;
            if (vertexLit) EnsureSkinned3DVertexLitProgram(); else EnsureSkinned3DProgram();
            OpenGL4RawProgram& prog = vertexLit ? skinned3DVertexLitProgram_ : skinned3DProgram_;
            prog.Use();
            float worldCol[16];
            world.ToColumnMajor(worldCol);
            const auto setM4 = [&](const char* name, const float* m) {
                const int loc = prog.UniformLocation(name);
                if (loc >= 0) gl4_glUniformMatrix4fv(loc, 1, GL_FALSE, m);
            };
            const auto setV3 = [&](const char* name, const float* v) {
                const int loc = prog.UniformLocation(name);
                if (loc >= 0) gl4_glUniform3f(loc, v[0], v[1], v[2]);
            };
            const auto setB = [&](const char* name, bool v) {
                const int loc = prog.UniformLocation(name);
                if (loc >= 0) gl4_glUniform1i(loc, v ? 1 : 0);
            };
            setM4("uWorldViewProj", wvpCol);
            setM4("uWorld", worldCol);
            const int bonesLoc = prog.UniformLocation("uBones[0]");
            if (bonesLoc >= 0 && params.boneCount > 0)
                gl4_glUniformMatrix4fv(bonesLoc, params.boneCount, GL_FALSE, params.boneTransforms);
            const int weightsLoc = prog.UniformLocation("uWeightsPerVertex");
            if (weightsLoc >= 0) gl4_glUniform1i(weightsLoc, params.weightsPerVertex);
            const int diffuseLoc = prog.UniformLocation("uDiffuseColor");
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
            const int specPowerLoc = prog.UniformLocation("uSpecularPower");
            if (specPowerLoc >= 0) gl4_glUniform1f(specPowerLoc, params.specularPower);
            const int texLoc = prog.UniformLocation("uTexture");
            if (texLoc >= 0) gl4_glUniform1i(texLoc, 0);
            setFog(prog);
            return true;
        }
        default:
            return false;
        }
    }

    namespace
    {
        // plan_opengl4.md GL4-30: binds a ShaderEffect's own compiled program (bypassing the
        // built-in stride-dispatched shaders) and its World/View/Projection uniforms, matching
        // the exact uniform names every original XNA sample's own .fx source already declares.
        // Mirrors EasyGLRenderer's own BindCustomEffectMatrices helper exactly.
        void BindCustomEffectMatrices(IEffectRenderer& renderer,
                                      const Matrix& world, const Matrix& view, const Matrix& projection)
        {
            renderer.Bind();
            float worldCol[16], viewCol[16], projCol[16];
            world.ToColumnMajor(worldCol);
            view.ToColumnMajor(viewCol);
            projection.ToColumnMajor(projCol);
            renderer.SetUniformMat4("World", worldCol);
            renderer.SetUniformMat4("View", viewCol);
            renderer.SetUniformMat4("Projection", projCol);
        }
    }

    void OpenGL4Renderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb_in,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams& params)
    {
        // REMED-GFX-201: one per-vertex stream only on this renderer; refuse a wider binding
        // set before any program or VAO state is touched, never render from a stream subset.
        if (HasMultipleVertexStreams(params) || HasMultipleInstanceStreams(params))
            throw System::NotSupportedException(
                "OpenGL4: multiple per-vertex or per-instance vertex streams are not supported "
                "by this renderer (GraphicsCapability::MultiStreamVertexInput is false).");
        // REMED-GFX-DECL-GUARD: before a program is selected and before any draw is issued.
        // A custom ShaderEffect binds its attributes generically from the declaration itself
        // (GL4-33) and is deliberately not guarded, matching EasyGL's own gating.
        if (params.customEffectRenderer == nullptr)
            RequireFaithfulDeclarationEXT(vb_in, "ordinary-nonindexed");
        const auto& vb = static_cast<const OpenGL4VertexBufferRenderer&>(vb_in);

        if (params.customEffectRenderer)
        {
            BindCustomEffectMatrices(*params.customEffectRenderer, world, view, projection);
            const int vertexCount = VertexCountForPrimitives(primitive, primitiveCount);
            gl4_glBindVertexArray(vb.VaoHandle());
            glDrawArrays(ToGLPrimitive(primitive), params.vertexStart, vertexCount);
            gl4_glBindVertexArray(0);
            return;
        }

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

    void OpenGL4Renderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb_in, const IIndexBufferRenderer& ib_in,
                                                         const Matrix& world, const Matrix& view, const Matrix& projection,
                                                         PrimitiveType primitive, int primitiveCount,
                                                         const GpuDrawParams& params)
    {
        // REMED-GFX-201: one per-vertex stream only on this renderer; refuse a wider binding
        // set before any program or VAO state is touched, never render from a stream subset.
        if (HasMultipleVertexStreams(params) || HasMultipleInstanceStreams(params))
            throw System::NotSupportedException(
                "OpenGL4: multiple per-vertex or per-instance vertex streams are not supported "
                "by this renderer (GraphicsCapability::MultiStreamVertexInput is false).");
        // REMED-GFX-DECL-GUARD: see DrawPrimitivesEx above.
        if (params.customEffectRenderer == nullptr)
            RequireFaithfulDeclarationEXT(vb_in, "ordinary-indexed");
        const auto& vb = static_cast<const OpenGL4VertexBufferRenderer&>(vb_in);
        const auto& ib = static_cast<const OpenGL4IndexBufferRenderer&>(ib_in);

        // plan_opengl4.md GL4-31: real 32-bit index buffer support -- honors
        // IIndexBufferRenderer::IsThirtyTwoBit() instead of hardcoding GL_UNSIGNED_SHORT, matching
        // every other established renderer's own idxType-selection convention.
        const GLenum idxType = ib.IsThirtyTwoBit() ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
        const std::size_t idxSize = ib.IsThirtyTwoBit() ? sizeof(uint32_t) : sizeof(uint16_t);

        if (params.customEffectRenderer)
        {
            BindCustomEffectMatrices(*params.customEffectRenderer, world, view, projection);
            const int indexCount = VertexCountForPrimitives(primitive, primitiveCount);
            const auto byteOffsetCustom = reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(params.startIndex) * idxSize);
            gl4_glBindVertexArray(vb.VaoHandle());
            gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.IboHandle());
            gl4_glDrawElementsBaseVertex(ToGLPrimitive(primitive), indexCount, idxType,
                                         byteOffsetCustom, params.baseVertex);
            gl4_glBindVertexArray(0);
            return;
        }

        if (!BindProgramForStride(vb.GetStrideInBytes(), world, view, projection, params))
        {
            DrawIndexedColoredPrimitives(vb_in, ib_in, world, view, projection, primitive, primitiveCount);
            return;
        }

        const int indexCount = VertexCountForPrimitives(primitive, primitiveCount);
        const auto byteOffset = reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(params.startIndex) * idxSize);
        gl4_glBindVertexArray(vb.VaoHandle());
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.IboHandle());
        // plan_opengl4.md GL4-27: real GpuDrawParams::baseVertex support -- glDrawElementsBaseVertex
        // adds params.baseVertex to every fetched index before it indexes into the currently bound
        // vertex buffer (maps directly to FNA's own D3D9/OpenGL baseVertex parameter), letting
        // multiple sub-meshes share one large vertex buffer with per-draw index-space-relative
        // indices, matching every effect's own DrawIndexedPrimitivesEx(..., baseVertex, ...)
        // contract. params.baseVertex defaults to 0, so this is a genuine no-op for every existing
        // draw that never set it.
        gl4_glDrawElementsBaseVertex(ToGLPrimitive(primitive), indexCount, idxType,
                                     byteOffset, params.baseVertex);
        gl4_glBindVertexArray(0);
    }

    void OpenGL4Renderer::DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb_in, const IIndexBufferRenderer& ib_in,
                                                           const Matrix& world, const Matrix& view, const Matrix& projection,
                                                           PrimitiveType primitive, int primitiveCount, int instanceCount,
                                                           const GpuDrawParams& params)
    {
        const auto& vb = static_cast<const OpenGL4VertexBufferRenderer&>(vb_in);
        const auto& ib = static_cast<const OpenGL4IndexBufferRenderer&>(ib_in);

        // REMED-GFX-201/202: this renderer binds exactly one per-vertex stream plus at most one
        // per-instance stream. GraphicsDevice already rejects wider shapes for a renderer that
        // reports no MultiStreamVertexInput; this defensive check keeps the refusal in place even
        // if a caller reaches the renderer directly, rather than rendering from a stream subset.
        if (HasMultipleVertexStreams(params) || HasMultipleInstanceStreams(params))
            throw System::NotSupportedException(
                "OpenGL4: multiple per-vertex or per-instance vertex streams are not supported "
                "by this renderer (GraphicsCapability::MultiStreamVertexInput is false).");

        const int indexCount = VertexCountForPrimitives(primitive, primitiveCount);
        const GLenum idxType = ib.IsThirtyTwoBit() ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;

        // REMED-GFX-DECL-GUARD: the non-custom-effect instanced path below dispatches by stride.
        if (params.customEffectRenderer == nullptr)
            RequireFaithfulDeclarationEXT(vb_in, "instanced");

        if (params.customEffectRenderer)
        {
            // plan_opengl4.md GL4-33: hardware instancing with a custom ShaderEffect. The
            // per-vertex mesh buffer's own attributes are already bound (via ApplyLayout, at
            // SetData time) into vb's own VAO; bind the *second*, per-instance buffer's own
            // attributes into that same VAO here, continuing at locations right after the mesh
            // buffer's own. REMED-GFX-202: the per-instance stream arrives as the
            // GpuVertexStreamBinding whose instanceFrequency > 0 -- there is no second
            // representation of "the instance buffer". REMED-GFX-213: the attribute divisor IS
            // the public InstanceFrequency (GL advances the attribute once per `divisor`
            // instances, the same rule D3D11's InstanceDataStepRate defines). REMED-GFX-211: the
            // stream's own VertexOffset (in vertex elements) offsets every attribute pointer by
            // that many of the stream's OWN records.
            BindCustomEffectMatrices(*params.customEffectRenderer, world, view, projection);

            gl4_glBindVertexArray(vb.VaoHandle());

            const auto& meshDecl = vb.GetDeclarationElements();
            const auto baseLocation = static_cast<GLuint>(meshDecl.size());
            const GpuVertexStreamBinding* instanceStream = FirstInstanceStream(params);
            if (instanceStream)
            {
                const auto& instVb =
                    static_cast<const OpenGL4VertexBufferRenderer&>(*instanceStream->buffer);
                const auto& instDecl = instVb.GetDeclarationElements();
                const auto instStride = static_cast<GLsizei>(instVb.GetStrideInBytes());
                const auto instBase = static_cast<std::uintptr_t>(instanceStream->vertexOffset)
                                      * static_cast<std::uintptr_t>(instVb.GetStrideInBytes());
                const auto divisor =
                    static_cast<GLuint>(instanceStream->instanceFrequency > 0
                                            ? instanceStream->instanceFrequency : 1);

                gl4_glBindBuffer(GL_ARRAY_BUFFER, instVb.VboHandle());
                for (std::size_t i = 0; i < instDecl.size(); ++i)
                {
                    const VertexElement& element = instDecl[i];
                    const VertexAttribFormat desc =
                        DescribeVertexElementFormat(element.getVertexElementFormatProperty());
                    const auto location = baseLocation + static_cast<GLuint>(i);
                    const void* offset = reinterpret_cast<void*>(
                        instBase + static_cast<std::uintptr_t>(element.getOffsetProperty()));
                    gl4_glEnableVertexAttribArray(location);
                    if (desc.isInteger)
                        gl4_glVertexAttribIPointer(location, desc.componentCount, desc.type, instStride, offset);
                    else
                        gl4_glVertexAttribPointer(location, desc.componentCount, desc.type,
                                                  desc.normalized ? GL_TRUE : GL_FALSE, instStride, offset);
                    gl4_glVertexAttribDivisor(location, divisor);
                }
            }

            gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.IboHandle());
            gl4_glDrawElementsInstanced(ToGLPrimitive(primitive), indexCount, idxType, nullptr, instanceCount);

            if (instanceStream)
            {
                const auto& instVb =
                    static_cast<const OpenGL4VertexBufferRenderer&>(*instanceStream->buffer);
                const auto& instDecl = instVb.GetDeclarationElements();
                for (std::size_t i = 0; i < instDecl.size(); ++i)
                {
                    const auto location = baseLocation + static_cast<GLuint>(i);
                    gl4_glVertexAttribDivisor(location, 0);
                    gl4_glDisableVertexAttribArray(location);
                }
            }

            gl4_glBindVertexArray(0);
            return;
        }

        if (!BindProgramForStride(vb.GetStrideInBytes(), world, view, projection, params))
        {
            // plan_opengl4.md GL4-33: unrecognized stride, no custom effect -- fall back to the
            // params-free colored3d program, mirroring DrawIndexedColoredPrimitives's own
            // fallback shape (matches EasyGLRenderer::SelectProgram's own `default:`
            // colored-program case, which always succeeds for any stride rather than failing).
            EnsureColored3DProgram();
            const Matrix wvp = world * view * projection;
            float wvpCol[16];
            wvp.ToColumnMajor(wvpCol);
            colored3DProgram_.Use();
            if (colored3DWvpLoc_ >= 0) gl4_glUniformMatrix4fv(colored3DWvpLoc_, 1, GL_FALSE, wvpCol);
            gl4_glBindVertexArray(vb.VaoHandle());
            gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.IboHandle());
            gl4_glDrawElementsInstanced(ToGLPrimitive(primitive), indexCount, idxType, nullptr, instanceCount);
            gl4_glBindVertexArray(0);
            return;
        }

        gl4_glBindVertexArray(vb.VaoHandle());
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.IboHandle());
        gl4_glDrawElementsInstanced(ToGLPrimitive(primitive), indexCount, idxType, nullptr, instanceCount);
        gl4_glBindVertexArray(0);
    }

    OpenGL4RawProgram& OpenGL4Renderer::GetOrCreateSpriteProgram()
    {
        if (!spriteProgram_.IsValid())
        {
            if (!spriteProgram_.Compile(kSpriteVertSrc, kSpriteFragSrc))
                throw std::runtime_error("OpenGL4: sprite program failed to compile: " + spriteProgram_.GetError());
        }
        return spriteProgram_;
    }

    void OpenGL4Renderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb_in,
                                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                                       PrimitiveType primitive, int primitiveCount)
    {
        // REMED-GFX-DECL-GUARD: this route reads the fixed stride-16 position+colour layout.
        RequireFaithfulDeclarationEXT(vb_in, "colored-nonindexed");
        EnsureColored3DProgram();
        const auto& vb = static_cast<const OpenGL4VertexBufferRenderer&>(vb_in);

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

    void OpenGL4Renderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb_in, const IIndexBufferRenderer& ib_in,
                                                              const Matrix& world, const Matrix& view, const Matrix& projection,
                                                              PrimitiveType primitive, int primitiveCount)
    {
        // REMED-GFX-DECL-GUARD: see DrawColoredPrimitives above.
        RequireFaithfulDeclarationEXT(vb_in, "colored-indexed");
        EnsureColored3DProgram();
        const auto& vb = static_cast<const OpenGL4VertexBufferRenderer&>(vb_in);
        const auto& ib = static_cast<const OpenGL4IndexBufferRenderer&>(ib_in);

        const Matrix wvp = world * view * projection;
        float wvpCol[16];
        wvp.ToColumnMajor(wvpCol);

        colored3DProgram_.Use();
        if (colored3DWvpLoc_ >= 0)
            gl4_glUniformMatrix4fv(colored3DWvpLoc_, 1, GL_FALSE, wvpCol);

        const int indexCount = VertexCountForPrimitives(primitive, primitiveCount);
        gl4_glBindVertexArray(vb.VaoHandle());
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.IboHandle());
        // plan_opengl4.md GL4-31: real 32-bit index buffer support.
        const GLenum idxType = ib.IsThirtyTwoBit() ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
        glDrawElements(ToGLPrimitive(primitive), indexCount, idxType, nullptr);
        gl4_glBindVertexArray(0);
    }

    void OpenGL4Renderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        if (w <= 0 || h <= 0) return; // invalid rect -- leave viewport state unchanged

        // GraphicsDevice::UpdateViewportFromWindow() calls this after every window resize (and
        // once at device creation) -- without a real override here the GL viewport is left at
        // whatever the driver's own initial default was, which 3D draws silently depend on
        // (unlike SpriteBatch's own FlushBatch, which sets glViewport() itself every flush).
        // OpenGL's viewport origin is bottom-left; convert from top-left XNA coordinates. Use
        // the bound render target's own height for the flip when one is bound (currentRtHeight_,
        // plan_opengl4.md GL4-14); fall back to the window's physical height for the default
        // framebuffer -- matches EasyGLRenderer::SetViewport's own currentRtHeight_-or-
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

    void OpenGL4Renderer::ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy)
    {
        if (slot < 0 || slot >= kMaxSamplerSlots) return;

        const unsigned int sampler = samplers_[slot];
        GLint minFilter = GL_LINEAR, magFilter = GL_LINEAR;
        FilterToGL(filter, minFilter, magFilter);
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, minFilter);
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, magFilter);
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, AddressModeToGL(addressU));
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, AddressModeToGL(addressV));
        if (filter == 2 && maxAnisotropy_ > 1.0f) // Anisotropic, and the driver has the extension
            gl4_glSamplerParameterf(sampler, GL_TEXTURE_MAX_ANISOTROPY,
                                    std::min(static_cast<float>(maxAnisotropy), maxAnisotropy_));

        gl4_glBindSampler(static_cast<GLuint>(slot), sampler);
    }

    void OpenGL4Renderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                 int colorDstBlend, int alphaDstBlend,
                                                 int colorBlendFunc, int alphaBlendFunc,
                                                 const BlendWriteState& writeState)
    {
        // Blend::One=0, Blend::Zero=1 -> the Opaque preset (src=One, dst=Zero) is XNA's own
        // encoding of "no blending", matching EasyGLRenderer::ApplyBlendState's identical
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
        // REMED-GFX-077: per-MRT-slot ColorWriteChannels via the GL 3.0+ core indexed mask --
        // always available on this renderer's 4.1-core-minimum context, so no capability split is
        // needed (unlike EasyGL's ES profile). The XNA bit layout (bit0=R,1=G,2=B,3=A) maps
        // directly.
        for (int i = 0; i < 4; ++i)
        {
            const int cwc = writeState.colorWriteChannels[i];
            gl4_glColorMaski(static_cast<GLuint>(i),
                             ColorWriteHasRed(cwc)   ? GL_TRUE : GL_FALSE,
                             ColorWriteHasGreen(cwc) ? GL_TRUE : GL_FALSE,
                             ColorWriteHasBlue(cwc)  ? GL_TRUE : GL_FALSE,
                             ColorWriteHasAlpha(cwc) ? GL_TRUE : GL_FALSE);
        }
        // BlendState.MultiSampleMask: expressible via glSampleMaski + GL_SAMPLE_MASK, but left at
        // the all-ones default here -- the same documented capability gap
        // EasyGLRenderer::ApplyBlendState records; the value reaches the renderer and only
        // the (rare) non-default path is unimplemented, never silently dropped.
    }

    void OpenGL4Renderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
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

    void OpenGL4Renderer::ApplyRasterizerState(int cullMode, int fillMode,
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

    void OpenGL4Renderer::SetBlendFactor(float r, float g, float b, float a)
    {
        gl4_glBlendColor(r, g, b, a);
    }

    void OpenGL4Renderer::SetScissorRect(int x, int y, int w, int h)
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
        // EasyGLRenderer::SetScissorRect's identical division of responsibility.
    }
}

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_OPENGL4
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<OpenGL4::OpenGL4Renderer>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode,
            args.multiSampleCount, args.swapInterval);
    }
#endif
}
