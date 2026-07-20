#include "CNA/Internal/Backends/Metal/MetalGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Metal/MetalPipelineKey.hpp"
#include "CNA/Internal/Backends/Metal/MetalNormalMatrix.hpp"
#include "CNA/Internal/Backends/Metal/MetalPrimitiveVertexCount.hpp"
#include "CNA/Internal/Backends/Metal/MetalLogicalViewport.hpp"
#include "CNA/Internal/Backends/Metal/MetalMat4.hpp"

#ifdef __APPLE__
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Backends::Metal
{
// plan_metal.md METAL-131/122/125: forward-declared at this (non-anonymous-namespace) scope so
// MetalTextureCube/MetalTexture3D's own GetData() overrides, defined inside the anonymous
// namespace below, can call it -- its real definition lives later in this file, also at this same
// scope (outside the anonymous namespace), not inside it, so the forward declaration must live out
// here too or it would silently become a different, unrelated, unlinkable declaration. Needs only
// ordinary Metal types, not MetalGraphicsBackend::Impl, so unlike nativeTextureFor()/
// resolveActiveAttachments() this one has no incomplete-type ordering constraint of its own.
static void blitTextureToClientBuffer(id<MTLDevice> device, id<MTLCommandQueue> queue, id<MTLTexture> src, NSUInteger slice, int level, int x, int y, int z, int w, int h, int depth, void* data, int dataLength);

namespace
{
    static const char* kMetalShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct U3D { float4x4 wvp; };
// plan_metal.md METAL-35/36/37/51-63: DiffuseColor/VertexColorEnabled/AlphaTest/DualTexture
// material uniforms, shared by every unlit-textured fragment variant below. alphaTest defaults
// to {0,0,1,1} (CNA's documented "always pass" convention -- tolerance=0 forces the `a<refVal`
// branch, which is always false since alpha is never negative, so failWeight is selected but
// `1.0 < 0.0` is false and nothing discards) so folding this check into every fragment shader
// unconditionally is provably a no-op for draws that never touch AlphaTestEffect, exactly
// mirroring EasyGLGraphicsBackend::EnsureDualTextured3DProgram()'s own fsrc, which does the same.
struct UMaterialParams { float4 diffuseColor; float4 alphaTest; float4 flags; }; // flags.x = vertexColorEnabled (0/1)
struct V3Out { float4 position [[position]]; float4 color; float2 uv; };
struct V3ColorIn { float3 position [[attribute(0)]]; float4 color [[attribute(1)]]; };
struct V3TexIn { float3 position [[attribute(0)]]; float2 uv [[attribute(1)]]; };
struct V3ColorTexIn { float3 position [[attribute(0)]]; float4 color [[attribute(1)]]; float2 uv [[attribute(2)]]; };
struct V3NormalTexIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float2 uv [[attribute(2)]]; };
vertex V3Out cna_v3d_color(V3ColorIn in [[stage_in]], constant U3D& u [[buffer(1)]]) {
    V3Out o; o.position=u.wvp*float4(in.position,1.0); o.color=in.color; o.uv=float2(0.0); return o;
}
vertex V3Out cna_v3d_tex(V3TexIn in [[stage_in]], constant U3D& u [[buffer(1)]]) {
    V3Out o; o.position=u.wvp*float4(in.position,1.0); o.color=float4(1.0); o.uv=in.uv; return o;
}
vertex V3Out cna_v3d_colortex(V3ColorTexIn in [[stage_in]], constant U3D& u [[buffer(1)]]) {
    V3Out o; o.position=u.wvp*float4(in.position,1.0); o.color=in.color; o.uv=in.uv; return o;
}
// Returns discard-tested output alpha via `outA`; callers that don't need a second sample (the
// non-textured colored path) just pass the already-known alpha straight through.
inline bool cna_alpha_test_fails(float a, float4 at) {
    bool pass = (at.y > 0.0) ? (abs(a - at.x) < at.y) : (a < at.x);
    float w = pass ? at.z : at.w;
    return w < 0.0;
}
fragment float4 cna_f3d_color(V3Out in [[stage_in]], constant UMaterialParams& m [[buffer(2)]]) {
    float4 vcolor = (m.flags.x > 0.5) ? in.color : float4(1.0);
    float4 c = vcolor * m.diffuseColor;
    if (cna_alpha_test_fails(c.a, m.alphaTest)) discard_fragment();
    return c;
}
fragment float4 cna_f3d_texture(V3Out in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]], constant UMaterialParams& m [[buffer(2)]]) {
    float4 vcolor = (m.flags.x > 0.5) ? in.color : float4(1.0);
    float4 c = tex.sample(smp, in.uv) * vcolor * m.diffuseColor;
    if (cna_alpha_test_fails(c.a, m.alphaTest)) discard_fragment();
    return c;
}
// DualTextureEffect (plan_metal.md METAL-58/59): ported from FNA's real DualTextureEffect.fx
// PSDualTexture -- `color.rgb *= 2; color *= overlay * diffuse;` (a lightmap-style RGB-doubling
// factor on the FIRST texture only, alpha untouched) -- already found, fixed, and pixel-verified
// on EasyGL/Vulkan/Bgfx (docs/dualtextureeffect-support.md Task 383). CNA's cross-backend
// convention (confirmed against WebGPUGraphicsBackend's shipped dual-texture dispatch) samples
// both textures at the SAME shared UV (stride 20/24), not FNA's real separate TexCoord/TexCoord2
// -- an intentional, already-established simplification this shader matches for consistency
// with every other CNA backend rather than reintroducing a second UV set nothing else here uses.
fragment float4 cna_f3d_dualtex(V3Out in [[stage_in]], texture2d<float> tex0 [[texture(0)]], sampler smp0 [[sampler(0)]], texture2d<float> tex1 [[texture(1)]], sampler smp1 [[sampler(1)]], constant UMaterialParams& m [[buffer(2)]]) {
    float4 vcolor = (m.flags.x > 0.5) ? in.color : float4(1.0);
    float4 base = tex0.sample(smp0, in.uv);
    base.rgb *= 2.0;
    float4 c = base * tex1.sample(smp1, in.uv) * vcolor * m.diffuseColor;
    if (cna_alpha_test_fails(c.a, m.alphaTest)) discard_fragment();
    return c;
}

// BasicEffect per-pixel lighting (plan_metal.md METAL-38/40-47), ported line-for-line from
// EasyGLGraphicsBackend::EnsureLit3DProgram()'s real GLSL (both vertex and fragment stage), the
// same reference every other backend's own lit-textured shader already matches. Every `vec3`
// uniform is carried as a `float4` here (xyz + unused pad) to sidestep MSL `constant`-address-space
// float3 column-padding ambiguity entirely -- deliberately NOT a float3x3 uniform either, for the
// same reason (a 3x3 matrix's columns are *also* individually padded to 16 bytes in `constant`
// address space); the normal matrix crosses the CPU/GPU boundary as 3 separate float4 columns and
// is reassembled into a real float3x3 inside the shader instead.
//
// Deliberately NOT ported this pass: the fog-factor formula below uses raw OBJECT-SPACE vertex Z
// (`in.position.z`), an already-known, already-documented EasyGL simplification (only exactly
// correct when World/View are identity) -- copied here bug-for-bug to match the established
// cross-backend reference exactly, not "improved," per this project's own match-the-reference
// discipline. The per-vertex (Gouraud) lit variant (EnsureLit3DVertexLitProgram(), selected when
// lightingEnabled && !preferPerPixelLighting) is NOT ported this pass -- every draw with lighting
// on always takes the per-pixel path here, the same accepted, already-documented divergence
// GpuDrawParams::preferPerPixelLighting's own doc comment describes for "every backend except D3D9".
struct LitTransform { float4x4 wvp; float4x4 world; float4 normalCol0; float4 normalCol1; float4 normalCol2; };
struct LitUniforms {
    float4 diffuseColor;
    float4 ambientColor;
    float4 light0Dir;
    float4 light0Diffuse;
    float4 light0Specular;
    float4 light1Dir;
    float4 light1Diffuse;
    float4 light1Specular;
    float4 light2Dir;
    float4 light2Diffuse;
    float4 light2Specular;
    float4 specularColorPower; // xyz = SpecularColor, w = SpecularPower
    float4 eyePosition;
    float4 emissiveColor;
    float4 alphaTest;
    float4 fogColorEnabled;    // xyz = FogColor, w = FogEnabled (0/1)
    float4 fogStartEnd;        // x = FogStart, y = FogEnd
};
struct VLitOut { float4 position [[position]]; float3 normal; float2 uv; float3 worldPos; float fogFactor; };
vertex VLitOut cna_v3d_lit(V3NormalTexIn in [[stage_in]], constant LitTransform& t [[buffer(1)]], constant LitUniforms& lu [[buffer(2)]]) {
    VLitOut o;
    o.position = t.wvp * float4(in.position, 1.0);
    float3x3 normalMat = float3x3(t.normalCol0.xyz, t.normalCol1.xyz, t.normalCol2.xyz);
    o.normal = normalMat * in.normal;
    o.uv = in.uv;
    float fogStart = lu.fogStartEnd.x, fogEnd = lu.fogStartEnd.y;
    o.fogFactor = (lu.fogColorEnabled.w > 0.5)
        ? ((abs(fogEnd - fogStart) < 1e-6) ? 0.0 : clamp((in.position.z + fogEnd) / (fogEnd - fogStart), 0.0, 1.0))
        : 1.0;
    o.worldPos = (t.world * float4(in.position, 1.0)).xyz;
    return o;
}
fragment float4 cna_f3d_lit(VLitOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]], constant LitUniforms& lu [[buffer(2)]]) {
    float3 N = normalize(in.normal);
    float3 E = normalize(lu.eyePosition.xyz - in.worldPos);
    float dotL0 = dot(N, -lu.light0Dir.xyz); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -lu.light1Dir.xyz); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -lu.light2Dir.xyz); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
    float3 lightSum = lu.ambientColor.xyz + lu.light0Diffuse.xyz*NdotL0 + lu.light1Diffuse.xyz*NdotL1 + lu.light2Diffuse.xyz*NdotL2;
    float3 litRGB = lightSum * lu.diffuseColor.xyz + lu.emissiveColor.xyz;
    float3 h0 = normalize(E - lu.light0Dir.xyz); float spec0 = pow(max(dot(h0,N),0.0)*zeroL0, lu.specularColorPower.w);
    float3 h1 = normalize(E - lu.light1Dir.xyz); float spec1 = pow(max(dot(h1,N),0.0)*zeroL1, lu.specularColorPower.w);
    float3 h2 = normalize(E - lu.light2Dir.xyz); float spec2 = pow(max(dot(h2,N),0.0)*zeroL2, lu.specularColorPower.w);
    float3 specularRGB = (spec0*lu.light0Specular.xyz + spec1*lu.light1Specular.xyz + spec2*lu.light2Specular.xyz) * lu.specularColorPower.xyz;
    float4 c = tex.sample(smp, in.uv) * float4(litRGB, lu.diffuseColor.w);
    c.rgb += specularRGB * c.a;
    if (cna_alpha_test_fails(c.a, lu.alphaTest)) discard_fragment();
    c.rgb = mix(lu.fogColorEnabled.xyz, c.rgb, in.fogFactor);
    return c;
}

// plan_metal.md METAL-39: real XNA BasicEffect defaults PreferPerPixelLighting=false, which selects
// a per-vertex (Gouraud) lit shader family -- lighting is computed ONCE per vertex and interpolated
// across the triangle, not re-evaluated per fragment. cna_v3d_lit/cna_f3d_lit above are the
// PreferPerPixelLighting=true family; this is its per-vertex-lit sibling, ported line-for-line from
// EasyGLGraphicsBackend::EnsureLit3DVertexLitProgram()'s real GLSL (identical Blinn-Phong math to
// EnsureLit3DProgram() -- only the STAGE it runs in changes). Reuses the SAME LitTransform/
// LitUniforms structs as the per-pixel variant (just consumed differently), so fillLitUniforms()
// needs no changes at all.
struct VLitVertexLitOut { float4 position [[position]]; float2 uv; float fogFactor; float3 litRGB; float3 specularRGB; };
vertex VLitVertexLitOut cna_v3d_lit_vertexlit(V3NormalTexIn in [[stage_in]], constant LitTransform& t [[buffer(1)]], constant LitUniforms& lu [[buffer(2)]]) {
    VLitVertexLitOut o;
    o.position = t.wvp * float4(in.position, 1.0);
    o.uv = in.uv;
    float3x3 normalMat = float3x3(t.normalCol0.xyz, t.normalCol1.xyz, t.normalCol2.xyz);
    float3 N = normalize(normalMat * in.normal);
    float3 worldPos = (t.world * float4(in.position, 1.0)).xyz;
    float3 E = normalize(lu.eyePosition.xyz - worldPos);
    float dotL0 = dot(N, -lu.light0Dir.xyz); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -lu.light1Dir.xyz); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -lu.light2Dir.xyz); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
    float3 lightSum = lu.ambientColor.xyz + lu.light0Diffuse.xyz*NdotL0 + lu.light1Diffuse.xyz*NdotL1 + lu.light2Diffuse.xyz*NdotL2;
    o.litRGB = lightSum * lu.diffuseColor.xyz + lu.emissiveColor.xyz;
    float3 h0 = normalize(E - lu.light0Dir.xyz); float spec0 = pow(max(dot(h0,N),0.0)*zeroL0, lu.specularColorPower.w);
    float3 h1 = normalize(E - lu.light1Dir.xyz); float spec1 = pow(max(dot(h1,N),0.0)*zeroL1, lu.specularColorPower.w);
    float3 h2 = normalize(E - lu.light2Dir.xyz); float spec2 = pow(max(dot(h2,N),0.0)*zeroL2, lu.specularColorPower.w);
    o.specularRGB = (spec0*lu.light0Specular.xyz + spec1*lu.light1Specular.xyz + spec2*lu.light2Specular.xyz) * lu.specularColorPower.xyz;
    float fogStart = lu.fogStartEnd.x, fogEnd = lu.fogStartEnd.y;
    o.fogFactor = (lu.fogColorEnabled.w > 0.5)
        ? ((abs(fogEnd - fogStart) < 1e-6) ? 0.0 : clamp((in.position.z + fogEnd) / (fogEnd - fogStart), 0.0, 1.0))
        : 1.0;
    return o;
}
fragment float4 cna_f3d_lit_vertexlit(VLitVertexLitOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]], constant LitUniforms& lu [[buffer(2)]]) {
    float4 c = tex.sample(smp, in.uv) * float4(in.litRGB, lu.diffuseColor.w);
    c.rgb += in.specularRGB * c.a;
    if (cna_alpha_test_fails(c.a, lu.alphaTest)) discard_fragment();
    c.rgb = mix(lu.fogColorEnabled.xyz, c.rgb, in.fogFactor);
    return c;
}

// EnvironmentMapEffect (plan_metal.md METAL-64/66-68), ported line-for-line from
// EasyGLGraphicsBackend::EnsureEnvMapped3DProgram()'s real GLSL. Real XNA `EnvironmentMapEffect`
// has no separate AmbientLightColor uniform in its own shader at all -- `GpuDrawParams::
// emissiveColor`'s own doc comment already documents this: for EnvironmentMapEffect it carries
// "emissive+ambient combined," pre-baked by the C++ effect layer before reaching any backend, so
// (unlike BasicEffect's lit path) there is deliberately no separate ambientColor field/uniform
// here -- confirmed by reading EnsureEnvMapped3DProgram()'s fsrc, which declares no uAmbientColor
// uniform either. Fresnel is computed per-VERTEX from each vertex's own un-interpolated normal/eye
// vector then Gouraud-interpolated (real XNA EnvironmentMapEffect.fx behavior, not a per-fragment
// recompute from an interpolated normal -- Task 1112, not equivalent once vertices carry different
// normals) -- ported that way here too, not "corrected" to per-fragment.
struct EnvTransform { float4x4 wvp; float4x4 world; float4 normalCol0; float4 normalCol1; float4 normalCol2; };
struct EnvUniforms {
    float4 diffuseColor;
    float4 emissiveColor;      // pre-combined ambient+emissive, xyz+pad
    float4 light0Dir; float4 light0Diffuse;
    float4 light1Dir; float4 light1Diffuse;
    float4 light2Dir; float4 light2Diffuse;
    float4 envMapSpecular;     // xyz+pad
    float4 eyePosition;        // xyz+pad
    float4 envParams;          // x=EnvMapAmount, y=FresnelEnabled(0/1), z=FresnelFactor
    float4 alphaTest;
    float4 fogColorEnabled;
    float4 fogStartEnd;
};
struct VEnvOut { float4 position [[position]]; float3 worldNormal; float3 eyeDir; float2 uv; float fresnel; float fogFactor; };
vertex VEnvOut cna_v3d_envmap(V3NormalTexIn in [[stage_in]], constant EnvTransform& t [[buffer(1)]], constant EnvUniforms& eu [[buffer(2)]]) {
    VEnvOut o;
    o.position = t.wvp * float4(in.position, 1.0);
    float3 worldPos = (t.world * float4(in.position, 1.0)).xyz;
    float3x3 normalMat = float3x3(t.normalCol0.xyz, t.normalCol1.xyz, t.normalCol2.xyz);
    float3 worldNormal = normalize(normalMat * in.normal);
    float3 eyeVector = normalize(eu.eyePosition.xyz - worldPos);
    o.worldNormal = worldNormal;
    o.eyeDir = eyeVector;
    o.uv = in.uv;
    float viewAngle = dot(eyeVector, worldNormal);
    o.fresnel = (eu.envParams.y > 0.5)
        ? pow(max(1.0 - abs(viewAngle), 0.0), eu.envParams.z) * eu.envParams.x
        : eu.envParams.x;
    float fogStart = eu.fogStartEnd.x, fogEnd = eu.fogStartEnd.y;
    o.fogFactor = (eu.fogColorEnabled.w > 0.5)
        ? ((abs(fogEnd - fogStart) < 1e-6) ? 0.0 : clamp((in.position.z + fogEnd) / (fogEnd - fogStart), 0.0, 1.0))
        : 1.0;
    return o;
}
fragment float4 cna_f3d_envmap(VEnvOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]], texturecube<float> envMap [[texture(1)]], sampler envSmp [[sampler(1)]], constant EnvUniforms& eu [[buffer(2)]]) {
    float3 N = normalize(in.worldNormal);
    float3 E = normalize(in.eyeDir);
    float NdotL0 = max(dot(N, -eu.light0Dir.xyz), 0.0);
    float NdotL1 = max(dot(N, -eu.light1Dir.xyz), 0.0);
    float NdotL2 = max(dot(N, -eu.light2Dir.xyz), 0.0);
    float3 lightSum = eu.light0Diffuse.xyz*NdotL0 + eu.light1Diffuse.xyz*NdotL1 + eu.light2Diffuse.xyz*NdotL2;
    float3 litRGB = lightSum * eu.diffuseColor.xyz + eu.emissiveColor.xyz;
    float4 texColor = tex.sample(smp, in.uv);
    float3 reflDir = reflect(-E, N);
    float4 envSample = envMap.sample(envSmp, reflDir);
    float3 baseColor = litRGB * texColor.rgb;
    float combinedAlpha = eu.diffuseColor.w * texColor.a;
    float blendFactor = in.fresnel;
    float3 rgb = mix(baseColor, envSample.rgb*combinedAlpha, blendFactor) + eu.envMapSpecular.xyz*envSample.a*combinedAlpha;
    float4 c = float4(rgb, combinedAlpha);
    if (cna_alpha_test_fails(c.a, eu.alphaTest)) discard_fragment();
    c.rgb = mix(eu.fogColorEnabled.xyz, c.rgb, in.fogFactor);
    return c;
}

// SkinnedEffect (plan_metal.md METAL-72-80), ported line-for-line from
// EasyGLGraphicsBackend::EnsureSkinnedProgram()'s real GLSL. Vertex layout: position(12)+
// normal(12)+uv(8)+boneWeights(16, real float4, not packed/normalized)+boneIndices(4, packed
// UChar4, unnormalized -- read as an integer type in-shader, not auto-converted to float like a
// Normalized format would be) = 52 bytes; +color(4, packed UChar4Normalized) = 56 -- confirmed
// against WebGPUGraphicsBackend::GetOrCreatePipelineSkinned3D's own `hasVertexColor=(stride==56)`.
// Real, load-bearing finding from reading the reference shader closely: unlike BasicEffect's lit
// path, the skinned vertex shader does NOT apply a separate world-space inverse-transpose normal
// matrix at all -- the normal is only ever transformed by `mat3(skinMat)` (the bone blend's own
// upper-left 3x3), then normalized, and used as-is. Ported that way here, not "corrected" to also
// apply a world normal matrix -- CNA's own established skinned behavior, confirmed not assumed.
struct SkinnedTransform { float4x4 wvp; float4x4 world; float4 skinParams; }; // skinParams.x = weightsPerVertex
struct SkinnedUniforms {
    float4 diffuseColor, emissiveColor;
    float4 light0Dir, light0Diffuse, light0Specular;
    float4 light1Dir, light1Diffuse, light1Specular;
    float4 light2Dir, light2Diffuse, light2Specular;
    float4 specularColorPower; // xyz=SpecularColor, w=SpecularPower
    float4 eyePosition;
    float4 alphaTest;
    float4 fogColorEnabled;    // xyz=FogColor, w=FogEnabled
    float4 fogStartEnd;        // x=FogStart, y=FogEnd
    float4 vertexColorEnabled; // x = 0/1
};
struct VSkinnedIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float2 uv [[attribute(2)]]; float4 boneWeights [[attribute(3)]]; uchar4 boneIndices [[attribute(4)]]; };
struct VSkinnedColorIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float2 uv [[attribute(2)]]; float4 boneWeights [[attribute(3)]]; uchar4 boneIndices [[attribute(4)]]; float4 color [[attribute(5)]]; };
struct VSkinnedOut { float4 position [[position]]; float3 normal; float2 uv; float3 worldPos; float fogFactor; float4 color; };
inline VSkinnedOut cna_skin_common(float3 position, float3 normal, float2 uv, float4 boneWeights, uchar4 boneIndices, float4 vcolor,
                                    constant SkinnedTransform& t, constant float4x4* bones,
                                    float fogStart, float fogEnd, float fogEnabled) {
    VSkinnedOut o;
    int weightsPerVertex = int(t.skinParams.x);
    // Task 895: real XNA Skin(vin, boneCount) only sums the first WeightsPerVertex (1, 2, or 4)
    // weight/index pairs.
    float4x4 skinMat = bones[boneIndices.x] * boneWeights.x;
    if (weightsPerVertex >= 2) skinMat += bones[boneIndices.y] * boneWeights.y;
    if (weightsPerVertex >= 4) skinMat += bones[boneIndices.z] * boneWeights.z + bones[boneIndices.w] * boneWeights.w;
    float4 skinnedPos = skinMat * float4(position, 1.0);
    o.position = t.wvp * skinnedPos;
    // Safe-normalize guard (ported, not invented): a vertex blended near-evenly between two bones
    // whose relative rotation is near 180 degrees can make the linearly-blended skinMat's
    // rotational part nearly cancel for a given normal, collapsing its transformed length toward
    // zero; normalize() of a near-zero vector is unstable (can yield NaN). Falls back to the
    // untransformed bind-pose normal for just that vertex.
    float3x3 skinMat3 = float3x3(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    float3 skinnedNormal = skinMat3 * normal;
    float skinnedNormalLen = length(skinnedNormal);
    o.normal = (skinnedNormalLen > 1e-6) ? (skinnedNormal / skinnedNormalLen) : normal;
    o.uv = uv;
    o.worldPos = (t.world * skinnedPos).xyz;
    o.color = vcolor;
    o.fogFactor = (fogEnabled > 0.5)
        ? ((abs(fogEnd - fogStart) < 1e-6) ? 0.0 : clamp((position.z + fogEnd) / (fogEnd - fogStart), 0.0, 1.0))
        : 1.0;
    return o;
}
vertex VSkinnedOut cna_v3d_skinned(VSkinnedIn in [[stage_in]], constant SkinnedTransform& t [[buffer(1)]], constant SkinnedUniforms& su [[buffer(2)]], constant float4x4* bones [[buffer(3)]]) {
    return cna_skin_common(in.position, in.normal, in.uv, in.boneWeights, in.boneIndices, float4(1.0), t, bones, su.fogStartEnd.x, su.fogStartEnd.y, su.fogColorEnabled.w);
}
vertex VSkinnedOut cna_v3d_skinned_color(VSkinnedColorIn in [[stage_in]], constant SkinnedTransform& t [[buffer(1)]], constant SkinnedUniforms& su [[buffer(2)]], constant float4x4* bones [[buffer(3)]]) {
    return cna_skin_common(in.position, in.normal, in.uv, in.boneWeights, in.boneIndices, in.color, t, bones, su.fogStartEnd.x, su.fogStartEnd.y, su.fogColorEnabled.w);
}
fragment float4 cna_f3d_skinned(VSkinnedOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]], constant SkinnedUniforms& su [[buffer(2)]]) {
    float3 N = normalize(in.normal);
    float3 E = normalize(su.eyePosition.xyz - in.worldPos);
    float dotL0 = dot(N, -su.light0Dir.xyz); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -su.light1Dir.xyz); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -su.light2Dir.xyz); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
    float3 lightSum = su.light0Diffuse.xyz*NdotL0 + su.light1Diffuse.xyz*NdotL1 + su.light2Diffuse.xyz*NdotL2;
    float3 litRGB = lightSum * su.diffuseColor.xyz + su.emissiveColor.xyz;
    float3 h0 = normalize(E - su.light0Dir.xyz); float spec0 = pow(max(dot(h0,N),0.0)*zeroL0, su.specularColorPower.w);
    float3 h1 = normalize(E - su.light1Dir.xyz); float spec1 = pow(max(dot(h1,N),0.0)*zeroL1, su.specularColorPower.w);
    float3 h2 = normalize(E - su.light2Dir.xyz); float spec2 = pow(max(dot(h2,N),0.0)*zeroL2, su.specularColorPower.w);
    float3 specularRGB = (spec0*su.light0Specular.xyz + spec1*su.light1Specular.xyz + spec2*su.light2Specular.xyz) * su.specularColorPower.xyz;
    float4 texColor = tex.sample(smp, in.uv);
    float4 vc = (su.vertexColorEnabled.x > 0.5) ? in.color : float4(1.0);
    float4 c = float4(litRGB * texColor.rgb, su.diffuseColor.w * texColor.a * vc.a);
    c.rgb += specularRGB * c.a;
    // Vertex color modulates the whole combined diffuse+specular output, not just diffuse -- after
    // the specular add so VertexColorEnabled=true with a black vertex color genuinely zeroes the
    // pixel (matches EasyGL's own real ordering, not an arbitrary choice).
    c.rgb *= vc.rgb;
    if (cna_alpha_test_fails(c.a, su.alphaTest)) discard_fragment();
    c.rgb = mix(su.fogColorEnabled.xyz, c.rgb, in.fogFactor);
    return c;
}

// plan_metal.md METAL-76: real XNA SkinnedEffect defaults PreferPerPixelLighting=false too, same as
// BasicEffect (METAL-39) -- this is its per-vertex-lit sibling, ported line-for-line from
// EasyGLGraphicsBackend::EnsureSkinnedVertexLitProgram()'s real GLSL (same technique as METAL-39:
// move the Blinn-Phong math from the fragment stage into the vertex stage, Gouraud-interpolate the
// result). The skinning itself (bone blend, degenerate-normal guard) is unchanged from
// cna_skin_common -- only WHERE lighting is evaluated moves. No separate ambient uniform here,
// matching cna_f3d_skinned's own shape (SkinnedEffect pre-folds ambient into emissiveColor at the
// C++ effect layer), so SkinnedUniforms/fillSkinnedUniforms() need no changes.
struct VSkinnedVertexLitOut { float4 position [[position]]; float2 uv; float fogFactor; float3 litRGB; float3 specularRGB; float4 color; };
inline VSkinnedVertexLitOut cna_skin_vertexlit_common(float3 position, float3 normal, float2 uv, float4 boneWeights, uchar4 boneIndices, float4 vcolor,
                                    constant SkinnedTransform& t, constant SkinnedUniforms& su, constant float4x4* bones) {
    VSkinnedVertexLitOut o;
    int weightsPerVertex = int(t.skinParams.x);
    float4x4 skinMat = bones[boneIndices.x] * boneWeights.x;
    if (weightsPerVertex >= 2) skinMat += bones[boneIndices.y] * boneWeights.y;
    if (weightsPerVertex >= 4) skinMat += bones[boneIndices.z] * boneWeights.z + bones[boneIndices.w] * boneWeights.w;
    float4 skinnedPos = skinMat * float4(position, 1.0);
    o.position = t.wvp * skinnedPos;
    float3x3 skinMat3 = float3x3(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    float3 skinnedNormal = skinMat3 * normal;
    float skinnedNormalLen = length(skinnedNormal);
    float3 N = (skinnedNormalLen > 1e-6) ? (skinnedNormal / skinnedNormalLen) : normal;
    o.uv = uv;
    o.color = vcolor;
    float3 worldPos = (t.world * skinnedPos).xyz;
    float3 E = normalize(su.eyePosition.xyz - worldPos);
    float dotL0 = dot(N, -su.light0Dir.xyz); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -su.light1Dir.xyz); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -su.light2Dir.xyz); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
    float3 lightSum = su.light0Diffuse.xyz*NdotL0 + su.light1Diffuse.xyz*NdotL1 + su.light2Diffuse.xyz*NdotL2;
    o.litRGB = lightSum * su.diffuseColor.xyz + su.emissiveColor.xyz;
    float3 h0 = normalize(E - su.light0Dir.xyz); float spec0 = pow(max(dot(h0,N),0.0)*zeroL0, su.specularColorPower.w);
    float3 h1 = normalize(E - su.light1Dir.xyz); float spec1 = pow(max(dot(h1,N),0.0)*zeroL1, su.specularColorPower.w);
    float3 h2 = normalize(E - su.light2Dir.xyz); float spec2 = pow(max(dot(h2,N),0.0)*zeroL2, su.specularColorPower.w);
    o.specularRGB = (spec0*su.light0Specular.xyz + spec1*su.light1Specular.xyz + spec2*su.light2Specular.xyz) * su.specularColorPower.xyz;
    float fogStart = su.fogStartEnd.x, fogEnd = su.fogStartEnd.y;
    o.fogFactor = (su.fogColorEnabled.w > 0.5)
        ? ((abs(fogEnd - fogStart) < 1e-6) ? 0.0 : clamp((position.z + fogEnd) / (fogEnd - fogStart), 0.0, 1.0))
        : 1.0;
    return o;
}
vertex VSkinnedVertexLitOut cna_v3d_skinned_vertexlit(VSkinnedIn in [[stage_in]], constant SkinnedTransform& t [[buffer(1)]], constant SkinnedUniforms& su [[buffer(2)]], constant float4x4* bones [[buffer(3)]]) {
    return cna_skin_vertexlit_common(in.position, in.normal, in.uv, in.boneWeights, in.boneIndices, float4(1.0), t, su, bones);
}
vertex VSkinnedVertexLitOut cna_v3d_skinned_color_vertexlit(VSkinnedColorIn in [[stage_in]], constant SkinnedTransform& t [[buffer(1)]], constant SkinnedUniforms& su [[buffer(2)]], constant float4x4* bones [[buffer(3)]]) {
    return cna_skin_vertexlit_common(in.position, in.normal, in.uv, in.boneWeights, in.boneIndices, in.color, t, su, bones);
}
fragment float4 cna_f3d_skinned_vertexlit(VSkinnedVertexLitOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]], constant SkinnedUniforms& su [[buffer(2)]]) {
    float4 texColor = tex.sample(smp, in.uv);
    float4 vc = (su.vertexColorEnabled.x > 0.5) ? in.color : float4(1.0);
    float4 c = float4(in.litRGB * texColor.rgb, su.diffuseColor.w * texColor.a * vc.a);
    c.rgb += in.specularRGB * c.a;
    c.rgb *= vc.rgb;
    if (cna_alpha_test_fails(c.a, su.alphaTest)) discard_fragment();
    c.rgb = mix(su.fogColorEnabled.xyz, c.rgb, in.fogFactor);
    return c;
}

// NOXNA PBR (plan_metal.md METAL-81/83-86, plan_cnj.md CNB-58), ported line-for-line from
// EasyGLGraphicsBackend::EnsurePbrProgram()'s real GLSL -- the glTF 2.0 spec's own reference
// metallic-roughness BRDF (Appendix B.3.2-B.3.4: GGX/Trowbridge-Reitz D, Smith-Schlick-GGX
// visibility with direct-lighting k=(roughness+1)^2/8, Schlick Fresnel). Tangent transforms as a
// plain direction under mat3(World) (not the inverse-transpose normal matrix the surface normal
// itself uses) -- a documented simplification for non-uniform-scale World transforms shared with
// most real-time engines lacking a full per-tangent inverse-transpose, ported as-is not "improved."
struct PbrTransform { float4x4 wvp; float4x4 world; float4 normalCol0; float4 normalCol1; float4 normalCol2; };
struct PbrUniforms {
    float4 diffuseColor;
    float4 ambientColor;
    float4 emissiveColor;
    float4 light0Dir; float4 light0Diffuse;
    float4 light1Dir; float4 light1Diffuse;
    float4 light2Dir; float4 light2Diffuse;
    float4 eyePosition;
    float4 pbrFactors;      // x=MetallicFactor, y=RoughnessFactor
    float4 alphaTest;
    float4 fogColorEnabled;
    float4 fogStartEnd;
};
struct VPbrIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float4 tangent [[attribute(2)]]; float2 uv [[attribute(3)]]; };
struct VPbrOut { float4 position [[position]]; float3 normal; float3 tangent; float bitangentSign; float2 uv; float fogFactor; float3 worldPos; };
vertex VPbrOut cna_v3d_pbr(VPbrIn in [[stage_in]], constant PbrTransform& t [[buffer(1)]], constant PbrUniforms& pu [[buffer(2)]]) {
    VPbrOut o;
    o.position = t.wvp * float4(in.position, 1.0);
    float3x3 normalMat = float3x3(t.normalCol0.xyz, t.normalCol1.xyz, t.normalCol2.xyz);
    o.normal = normalMat * in.normal;
    float3x3 world3 = float3x3(t.world[0].xyz, t.world[1].xyz, t.world[2].xyz);
    o.tangent = world3 * in.tangent.xyz;
    o.bitangentSign = in.tangent.w;
    o.uv = in.uv;
    o.worldPos = (t.world * float4(in.position, 1.0)).xyz;
    float fogStart = pu.fogStartEnd.x, fogEnd = pu.fogStartEnd.y;
    o.fogFactor = (pu.fogColorEnabled.w > 0.5)
        ? ((abs(fogEnd - fogStart) < 1e-6) ? 0.0 : clamp((in.position.z + fogEnd) / (fogEnd - fogStart), 0.0, 1.0))
        : 1.0;
    return o;
}
inline float3 cna_pbr_light(float3 N, float3 V, float3 L, float3 lightColor, float3 albedo, float3 F0, float roughness, float metallic) {
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float a2 = pow(roughness, 4.0);
    float dTerm = (NdotH*NdotH*(a2-1.0)+1.0);
    float D = a2 / (3.14159265*dTerm*dTerm + 1e-7);
    float k = (roughness+1.0); k = k*k/8.0;
    float G = (NdotV/(NdotV*(1.0-k)+k)) * (NdotL/(NdotL*(1.0-k)+k));
    float3 F = F0 + (float3(1.0)-F0) * pow(clamp(1.0-VdotH, 0.0, 1.0), 5.0);
    float3 specular = (D*G*F) / max(4.0*NdotV*NdotL, 1e-4);
    float3 diffuseColor = albedo * (1.0-metallic);
    float3 kd = float3(1.0) - F;
    return (kd*diffuseColor/3.14159265 + specular) * lightColor * NdotL;
}
fragment float4 cna_f3d_pbr(VPbrOut in [[stage_in]],
    texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]],
    texture2d<float> normalMap [[texture(1)]], sampler normalSmp [[sampler(1)]],
    texture2d<float> mrMap [[texture(2)]], sampler mrSmp [[sampler(2)]],
    texture2d<float> emissiveMap [[texture(3)]], sampler emissiveSmp [[sampler(3)]],
    texture2d<float> occlusionMap [[texture(4)]], sampler occlusionSmp [[sampler(4)]],
    constant PbrUniforms& pu [[buffer(2)]])
{
    float4 baseColorTex = tex.sample(smp, in.uv);
    float3 albedo = baseColorTex.rgb * pu.diffuseColor.rgb;
    float alpha = baseColorTex.a * pu.diffuseColor.a;
    float3 N = normalize(in.normal);
    float3 T = normalize(in.tangent - N*dot(N, in.tangent));
    float3 B = cross(N, T) * in.bitangentSign;
    float3x3 TBN = float3x3(T, B, N);
    float3 sampledNormal = normalMap.sample(normalSmp, in.uv).rgb*2.0 - 1.0;
    float3 finalNormal = normalize(TBN * sampledNormal);
    float4 mr = mrMap.sample(mrSmp, in.uv);
    float roughness = clamp(mr.g * pu.pbrFactors.y, 0.045, 1.0);
    float metallic = clamp(mr.b * pu.pbrFactors.x, 0.0, 1.0);
    float3 V = normalize(pu.eyePosition.xyz - in.worldPos);
    float3 F0 = mix(float3(0.04), albedo, metallic);
    float3 Lo = float3(0.0);
    Lo += cna_pbr_light(finalNormal, V, normalize(-pu.light0Dir.xyz), pu.light0Diffuse.xyz, albedo, F0, roughness, metallic);
    Lo += cna_pbr_light(finalNormal, V, normalize(-pu.light1Dir.xyz), pu.light1Diffuse.xyz, albedo, F0, roughness, metallic);
    Lo += cna_pbr_light(finalNormal, V, normalize(-pu.light2Dir.xyz), pu.light2Diffuse.xyz, albedo, F0, roughness, metallic);
    float occlusion = occlusionMap.sample(occlusionSmp, in.uv).r;
    float3 ambient = pu.ambientColor.xyz * albedo * occlusion;
    float3 emissive = pu.emissiveColor.xyz * emissiveMap.sample(emissiveSmp, in.uv).rgb;
    float4 c = float4(ambient + Lo + emissive, alpha);
    if (cna_alpha_test_fails(c.a, pu.alphaTest)) discard_fragment();
    c.rgb = mix(pu.fogColorEnabled.xyz, c.rgb, in.fogFactor);
    return c;
}

// NOXNA SkinnedPbrEffect (plan_metal.md METAL-82): combines cna_skin_common's real GPU-skinning
// blend (position+normal+tangent all transformed by the same per-vertex weighted bone matrix sum,
// mat3(skinMat) directly -- no separate inverse-transpose normal-matrix step, matching
// SkinnedEffect's own established precedent, not PBR's unskinned inverse-transpose path) with
// cna_f3d_pbr's existing fragment shader unchanged -- both emit/consume the same VPbrOut, so no new
// fragment shader is needed, only a new vertex shader producing that same interpolant struct.
struct SkinnedPbrTransform { float4x4 wvp; float4x4 world; float4 skinParams; }; // skinParams.x = weightsPerVertex
struct VSkinnedPbrIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float4 tangent [[attribute(2)]]; float2 uv [[attribute(3)]]; float4 boneWeights [[attribute(4)]]; uchar4 boneIndices [[attribute(5)]]; };
vertex VPbrOut cna_v3d_skinned_pbr(VSkinnedPbrIn in [[stage_in]], constant SkinnedPbrTransform& t [[buffer(1)]], constant PbrUniforms& pu [[buffer(2)]], constant float4x4* bones [[buffer(3)]]) {
    VPbrOut o;
    int weightsPerVertex = int(t.skinParams.x);
    float4x4 skinMat = bones[in.boneIndices.x] * in.boneWeights.x;
    if (weightsPerVertex >= 2) skinMat += bones[in.boneIndices.y] * in.boneWeights.y;
    if (weightsPerVertex >= 4) skinMat += bones[in.boneIndices.z] * in.boneWeights.z + bones[in.boneIndices.w] * in.boneWeights.w;
    float4 skinnedPos = skinMat * float4(in.position, 1.0);
    o.position = t.wvp * skinnedPos;
    float3x3 skinMat3 = float3x3(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    float3 skinnedNormal = skinMat3 * in.normal;
    float skinnedNormalLen = length(skinnedNormal);
    o.normal = (skinnedNormalLen > 1e-6) ? (skinnedNormal / skinnedNormalLen) : in.normal;
    // Not renormalized here (matches the unskinned cna_v3d_pbr's own o.tangent = world3*tangent.xyz,
    // which is also left unnormalized) -- cna_f3d_pbr's Gram-Schmidt orthogonalization against the
    // interpolated normal already renormalizes it per-pixel regardless.
    o.tangent = skinMat3 * in.tangent.xyz;
    o.bitangentSign = in.tangent.w;
    o.uv = in.uv;
    o.worldPos = (t.world * skinnedPos).xyz;
    float fogStart = pu.fogStartEnd.x, fogEnd = pu.fogStartEnd.y;
    o.fogFactor = (pu.fogColorEnabled.w > 0.5)
        ? ((abs(fogEnd - fogStart) < 1e-6) ? 0.0 : clamp((in.position.z + fogEnd) / (fogEnd - fogStart), 0.0, 1.0))
        : 1.0;
    return o;
}

struct V2In { float2 position; float2 uv; float4 color; };
// plan_metal.md METAL-157/158: was `float2 viewport` (raw physical drawable pixels), completely
// bypassing virtual-resolution/letterbox scaling -- a real, currently-shipping bug. `scale`/
// `offset` fold the logical-to-physical-to-NDC chain into one multiply-add; see
// MetalGraphicsBackend::Impl::computeSpriteTransform() for the derivation, hand-verified to
// degrade to this struct's exact prior formula when no virtual resolution is set.
struct U2D { float2 scale; float2 offset; };
struct V2Out { float4 position [[position]]; float2 uv; float4 color; };
vertex V2Out cna_v2d(uint vid [[vertex_id]], const device V2In* v [[buffer(0)]], constant U2D& u [[buffer(1)]]) {
    V2In i=v[vid]; V2Out o;
    float2 ndc = i.position * u.scale + u.offset;
    o.position=float4(ndc,0.0,1.0); o.uv=i.uv; o.color=i.color; return o;
}
fragment float4 cna_f2d(V2Out in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {
    return tex.sample(smp, in.uv) * in.color;
}
)MSL";

    // plan_metal.md METAL-34-style extraction: this formula's real logic now lives in the plain-C++
    // MetalPrimitiveVertexCount.hpp (no Objective-C, buildable and unit-tested on any platform
    // without an Apple toolchain) -- kept as a thin same-signature wrapper here so the existing call
    // site is unaffected.
    static int primitiveVertexCount(PrimitiveType p, int count)
    {
        return ComputeMetalPrimitiveVertexCount(p, count);
    }

    static MTLPrimitiveType metalPrimitive(PrimitiveType p)
    {
        using PT = PrimitiveType;
        switch (p) {
            case PT::TriangleStrip: return MTLPrimitiveTypeTriangleStrip;
            case PT::LineList: return MTLPrimitiveTypeLine;
            case PT::LineStrip: return MTLPrimitiveTypeLineStrip;
            case PT::PointListEXT: return MTLPrimitiveTypePoint; // plan_metal.md METAL-12: was falling to Triangle
            default: return MTLPrimitiveTypeTriangle;
        }
    }

    // XNA CompareFunction ordinals -> MTLCompareFunction (mirrors EasyGL's ToEasyGLCompareFunc /
    // Vulkan's ToVkCompareOp exactly): Always=0, Never=1, Less=2, LessEqual=3, Equal=4,
    // GreaterEqual=5, Greater=6, NotEqual=7.
    static MTLCompareFunction metalCompareFunction(int cmp)
    {
        switch (cmp) {
            case 1: return MTLCompareFunctionNever;
            case 2: return MTLCompareFunctionLess;
            case 3: return MTLCompareFunctionLessEqual;
            case 4: return MTLCompareFunctionEqual;
            case 5: return MTLCompareFunctionGreaterEqual;
            case 6: return MTLCompareFunctionGreater;
            case 7: return MTLCompareFunctionNotEqual;
            default: return MTLCompareFunctionAlways; // CompareFunction::Always = 0
        }
    }

    // XNA StencilOperation ordinals -> MTLStencilOperation (mirrors EasyGL/Vulkan's
    // ToVkStencilOp exactly): Keep=0, Zero=1, Replace=2, Increment=3, Decrement=4,
    // IncrementSaturation=5, DecrementSaturation=6, Invert=7. XNA's Increment/Decrement wrap
    // (D3DSTENCILOP_INCR/DECR); the *Saturation variants clamp (D3DSTENCILOP_INCRSAT/DECRSAT) --
    // confirmed against Vulkan's already-tested VulkanGraphicsBackend::ToVkStencilOp.
    static MTLStencilOperation metalStencilOp(int op)
    {
        switch (op) {
            case 1: return MTLStencilOperationZero;
            case 2: return MTLStencilOperationReplace;
            case 3: return MTLStencilOperationIncrementWrap;
            case 4: return MTLStencilOperationDecrementWrap;
            case 5: return MTLStencilOperationIncrementClamp;
            case 6: return MTLStencilOperationDecrementClamp;
            case 7: return MTLStencilOperationInvert;
            default: return MTLStencilOperationKeep; // StencilOperation::Keep = 0
        }
    }

    // XNA Blend ordinals -> MTLBlendFactor (mirrors EasyGL's ToEasyGLBlendFactor / Vulkan's
    // ToVkBlendFactor exactly, including their identical no-RGB/Alpha-channel-distinction choice
    // for BlendFactor/InverseBlendFactor -- SourceColor/DestinationColor/BlendFactor as an
    // *Alpha*-slot factor is not a combination real D3D9/XNA content legally produces, and every
    // established CNA backend already made this same simplifying choice, not just this one):
    // One=0, Zero=1, SourceColor=2, InverseSourceColor=3, SourceAlpha=4, InverseSourceAlpha=5,
    // DestinationColor=6, InverseDestinationColor=7, DestinationAlpha=8, InverseDestinationAlpha=9,
    // BlendFactor=10, InverseBlendFactor=11, SourceAlphaSaturation=12.
    static MTLBlendFactor metalBlendFactor(int xnaBlend)
    {
        switch (xnaBlend) {
            case  1: return MTLBlendFactorZero;
            case  2: return MTLBlendFactorSourceColor;
            case  3: return MTLBlendFactorOneMinusSourceColor;
            case  4: return MTLBlendFactorSourceAlpha;
            case  5: return MTLBlendFactorOneMinusSourceAlpha;
            case  6: return MTLBlendFactorDestinationColor;
            case  7: return MTLBlendFactorOneMinusDestinationColor;
            case  8: return MTLBlendFactorDestinationAlpha;
            case  9: return MTLBlendFactorOneMinusDestinationAlpha;
            case 10: return MTLBlendFactorBlendColor;
            case 11: return MTLBlendFactorOneMinusBlendColor;
            case 12: return MTLBlendFactorSourceAlphaSaturated;
            default: return MTLBlendFactorOne; // Blend::One = 0
        }
    }

    // XNA BlendFunction ordinals -> MTLBlendOperation (mirrors EasyGL's ToEasyGLBlendEquation /
    // Vulkan's ToVkBlendOp): Add=0, Subtract=1, ReverseSubtract=2, Max=3, Min=4.
    static MTLBlendOperation metalBlendOp(int xnaBlendFunc)
    {
        switch (xnaBlendFunc) {
            case 1: return MTLBlendOperationSubtract;
            case 2: return MTLBlendOperationReverseSubtract;
            case 3: return MTLBlendOperationMax;
            case 4: return MTLBlendOperationMin;
            default: return MTLBlendOperationAdd; // BlendFunction::Add = 0
        }
    }

    // Microsoft::Xna::Framework::Graphics::TextureFilter ordinals (TextureFilter.hpp):
    // 0 Linear, 1 Point, 2 Anisotropic, 3 LinearMipPoint, 4 PointMipLinear,
    // 5 MinLinearMagPointMipLinear, 6 MinLinearMagPointMipPoint,
    // 7 MinPointMagLinearMipLinear, 8 MinPointMagLinearMipPoint.
    static MTLSamplerMinMagFilter metalMinFilter(int filter)
    {
        switch (filter) {
            case 1: case 4: case 6: case 8: return MTLSamplerMinMagFilterNearest;
            default: return MTLSamplerMinMagFilterLinear;
        }
    }
    static MTLSamplerMinMagFilter metalMagFilter(int filter)
    {
        switch (filter) {
            case 1: case 3: case 4: case 5: return MTLSamplerMinMagFilterNearest;
            default: return MTLSamplerMinMagFilterLinear;
        }
    }
    static MTLSamplerMipFilter metalMipFilter(int filter)
    {
        switch (filter) {
            case 1: case 3: case 6: case 8: return MTLSamplerMipFilterNearest;
            default: return MTLSamplerMipFilterLinear;
        }
    }
    // Microsoft::Xna::Framework::Graphics::TextureAddressMode ordinals: 0 Wrap, 1 Clamp, 2 Mirror.
    static MTLSamplerAddressMode metalAddressMode(int mode)
    {
        switch (mode) {
            case 0: return MTLSamplerAddressModeRepeat;
            case 2: return MTLSamplerAddressModeMirrorRepeat;
            default: return MTLSamplerAddressModeClampToEdge;
        }
    }

    // plan_metal.md METAL-34: the pipeline-cache key/hash types now live in the plain-C++
    // MetalPipelineKey.hpp (no Objective-C, buildable and unit-tested on any platform without an
    // Apple toolchain) -- these aliases let every existing call site in this file keep using the
    // short, unprefixed names unchanged.
    using PipelineKind = MetalPipelineKind;
    using BlendKey = MetalBlendKey;
    using PipelineCacheKey = MetalPipelineCacheKey;
    using PipelineCacheKeyHash = MetalPipelineCacheKeyHash;

    // Builds the MTLVertexDescriptor for one of the 4 fixed byte-strides this backend currently
    // recognizes -- byte-for-byte identical to the 4 descriptors the original constructor built
    // eagerly (vd16/vd20/vd24/vd32), just refactored so the now-lazy pipeline cache can build one
    // on demand instead of every stride having to exist up front.
    static MTLVertexDescriptor* vertexDescriptorForStride(std::size_t stride)
    {
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        switch (stride) {
            case 16:
                vd.attributes[0].format=MTLVertexFormatFloat3; vd.attributes[0].offset=0; vd.attributes[0].bufferIndex=0;
                vd.attributes[1].format=MTLVertexFormatUChar4Normalized; vd.attributes[1].offset=12; vd.attributes[1].bufferIndex=0;
                vd.layouts[0].stride=16;
                return vd;
            case 20:
                vd.attributes[0].format=MTLVertexFormatFloat3; vd.attributes[0].offset=0; vd.attributes[0].bufferIndex=0;
                vd.attributes[1].format=MTLVertexFormatFloat2; vd.attributes[1].offset=12; vd.attributes[1].bufferIndex=0;
                vd.layouts[0].stride=20;
                return vd;
            case 24:
                vd.attributes[0].format=MTLVertexFormatFloat3; vd.attributes[0].offset=0; vd.attributes[0].bufferIndex=0;
                vd.attributes[1].format=MTLVertexFormatUChar4Normalized; vd.attributes[1].offset=12; vd.attributes[1].bufferIndex=0;
                vd.attributes[2].format=MTLVertexFormatFloat2; vd.attributes[2].offset=16; vd.attributes[2].bufferIndex=0;
                vd.layouts[0].stride=24;
                return vd;
            case 32:
                vd.attributes[0].format=MTLVertexFormatFloat3; vd.attributes[0].offset=0; vd.attributes[0].bufferIndex=0;
                vd.attributes[1].format=MTLVertexFormatFloat3; vd.attributes[1].offset=12; vd.attributes[1].bufferIndex=0;
                vd.attributes[2].format=MTLVertexFormatFloat2; vd.attributes[2].offset=24; vd.attributes[2].bufferIndex=0;
                vd.layouts[0].stride=32;
                return vd;
            // plan_metal.md METAL-81: PbrEffect layout -- position(12)+normal(12)+tangent(16, real
            // float4: xyz direction + w bitangent-handedness sign, NOT packed/normalized)+uv(8) = 48.
            case 48:
                vd.attributes[0].format=MTLVertexFormatFloat3; vd.attributes[0].offset=0;  vd.attributes[0].bufferIndex=0;
                vd.attributes[1].format=MTLVertexFormatFloat3; vd.attributes[1].offset=12; vd.attributes[1].bufferIndex=0;
                vd.attributes[2].format=MTLVertexFormatFloat4; vd.attributes[2].offset=24; vd.attributes[2].bufferIndex=0;
                vd.attributes[3].format=MTLVertexFormatFloat2; vd.attributes[3].offset=40; vd.attributes[3].bufferIndex=0;
                vd.layouts[0].stride=48;
                return vd;
            // plan_metal.md METAL-72: SkinnedEffect layout -- position(12)+normal(12)+uv(8)+
            // boneWeights(16, real float4)+boneIndices(4, packed UChar4, UNNORMALIZED -- read as
            // an integer type in-shader, not MTLVertexFormatUChar4Normalized's auto-float-convert)
            // = 52; +color(4, packed UChar4Normalized) = 56.
            case 52:
                vd.attributes[0].format=MTLVertexFormatFloat3; vd.attributes[0].offset=0;  vd.attributes[0].bufferIndex=0;
                vd.attributes[1].format=MTLVertexFormatFloat3; vd.attributes[1].offset=12; vd.attributes[1].bufferIndex=0;
                vd.attributes[2].format=MTLVertexFormatFloat2; vd.attributes[2].offset=24; vd.attributes[2].bufferIndex=0;
                vd.attributes[3].format=MTLVertexFormatFloat4; vd.attributes[3].offset=32; vd.attributes[3].bufferIndex=0;
                vd.attributes[4].format=MTLVertexFormatUChar4; vd.attributes[4].offset=48; vd.attributes[4].bufferIndex=0;
                vd.layouts[0].stride=52;
                return vd;
            case 56:
                vd.attributes[0].format=MTLVertexFormatFloat3; vd.attributes[0].offset=0;  vd.attributes[0].bufferIndex=0;
                vd.attributes[1].format=MTLVertexFormatFloat3; vd.attributes[1].offset=12; vd.attributes[1].bufferIndex=0;
                vd.attributes[2].format=MTLVertexFormatFloat2; vd.attributes[2].offset=24; vd.attributes[2].bufferIndex=0;
                vd.attributes[3].format=MTLVertexFormatFloat4; vd.attributes[3].offset=32; vd.attributes[3].bufferIndex=0;
                vd.attributes[4].format=MTLVertexFormatUChar4; vd.attributes[4].offset=48; vd.attributes[4].bufferIndex=0;
                vd.attributes[5].format=MTLVertexFormatUChar4Normalized; vd.attributes[5].offset=52; vd.attributes[5].bufferIndex=0;
                vd.layouts[0].stride=56;
                return vd;
            // plan_metal.md METAL-82: SkinnedPbrEffect layout -- position(12)+normal(12)+
            // tangent(16, real float4 as in the unskinned PBR case)+uv(8)+boneWeights(16)+
            // boneIndices(4, UNNORMALIZED UChar4 as in the stride-52/56 skinned case) = 68.
            case 68:
                vd.attributes[0].format=MTLVertexFormatFloat3; vd.attributes[0].offset=0;  vd.attributes[0].bufferIndex=0;
                vd.attributes[1].format=MTLVertexFormatFloat3; vd.attributes[1].offset=12; vd.attributes[1].bufferIndex=0;
                vd.attributes[2].format=MTLVertexFormatFloat4; vd.attributes[2].offset=24; vd.attributes[2].bufferIndex=0;
                vd.attributes[3].format=MTLVertexFormatFloat2; vd.attributes[3].offset=40; vd.attributes[3].bufferIndex=0;
                vd.attributes[4].format=MTLVertexFormatFloat4; vd.attributes[4].offset=48; vd.attributes[4].bufferIndex=0;
                vd.attributes[5].format=MTLVertexFormatUChar4; vd.attributes[5].offset=64; vd.attributes[5].bufferIndex=0;
                vd.layouts[0].stride=68;
                return vd;
            default:
                throw std::runtime_error("Metal: unsupported vertex stride until generic VertexDeclaration pipeline cache is implemented (plan_metal.md METAL-27)");
        }
    }

    // plan_metal.md METAL-6/24: real per-BlendState blend factors/operation, replacing the
    // previous hardcoded-into-every-pipeline straight-alpha blend. When !blend.enabled, blending
    // is left off entirely (matches BlendState.Opaque's real observable behavior).
    static id<MTLRenderPipelineState> makePipeline(id<MTLDevice> dev, id<MTLLibrary> lib,
                                                    NSString* vs, NSString* fs,
                                                    MTLVertexDescriptor* vd, const BlendKey& blend)
    {
        MTLRenderPipelineDescriptor* d=[[MTLRenderPipelineDescriptor alloc] init];
        d.vertexFunction=[lib newFunctionWithName:vs]; d.fragmentFunction=[lib newFunctionWithName:fs]; d.vertexDescriptor=vd;
        d.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm; d.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8; d.stencilAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8;
        d.colorAttachments[0].blendingEnabled = blend.enabled ? YES : NO;
        if (blend.enabled) {
            d.colorAttachments[0].sourceRGBBlendFactor=metalBlendFactor(blend.colorSrc);
            d.colorAttachments[0].destinationRGBBlendFactor=metalBlendFactor(blend.colorDst);
            d.colorAttachments[0].rgbBlendOperation=metalBlendOp(blend.colorFunc);
            d.colorAttachments[0].sourceAlphaBlendFactor=metalBlendFactor(blend.alphaSrc);
            d.colorAttachments[0].destinationAlphaBlendFactor=metalBlendFactor(blend.alphaDst);
            d.colorAttachments[0].alphaBlendOperation=metalBlendOp(blend.alphaFunc);
        }
        NSError* err=nil; id<MTLRenderPipelineState> p=[dev newRenderPipelineStateWithDescriptor:d error:&err]; [d.vertexFunction release]; [d.fragmentFunction release]; [d release];
        if(!p) throw std::runtime_error(std::string("Metal pipeline compile failed: ")+([[err localizedDescription] UTF8String]?:"unknown")); return p;
    }

    // Plain C++ mirror of kMetalShaderSource's `struct UMaterialParams { float4 diffuseColor;
    // float4 alphaTest; float4 flags; };` -- three consecutive float4s, 48 bytes, no padding
    // ambiguity either side (unlike a float3-containing struct, which would need manual padding
    // to match MSL's `constant` address-space layout rules).
    struct UMaterialParams { float diffuseColor[4]; float alphaTest[4]; float flags[4]; };

    // plan_metal.md METAL-34-style extraction: this row-major 4x4 matrix helper set's real logic
    // now lives in the plain-C++ MetalMat4.hpp (no Objective-C, buildable and unit-tested on any
    // platform without an Apple toolchain) -- kept as thin same-name aliases/wrappers here so every
    // existing call site in this file is unaffected.
    using Mat4 = MetalMat4;
    static Mat4 multiply(const Mat4& a, const Mat4& b) { return MetalMat4Multiply(a, b); }
    static Mat4 fromXna(const Matrix& x) { return MetalMat4FromXna(x); }
    static Mat4 transpose(const Mat4& x) { return MetalMat4Transpose(x); }

    // Plain C++ mirrors of kMetalShaderSource's `LitTransform`/`LitUniforms` -- see that struct's
    // own comment for why every logical vec3 is carried as a 4-float (xyz+pad) group and the
    // normal matrix as 3 separate 4-float "columns" rather than a 3x3 or float3-containing type.
    struct LitTransform { float wvp[16]; float world[16]; float normalCol0[4]; float normalCol1[4]; float normalCol2[4]; };
    struct LitUniforms {
        float diffuseColor[4], ambientColor[4];
        float light0Dir[4], light0Diffuse[4], light0Specular[4];
        float light1Dir[4], light1Diffuse[4], light1Specular[4];
        float light2Dir[4], light2Diffuse[4], light2Specular[4];
        float specularColorPower[4], eyePosition[4], emissiveColor[4], alphaTest[4];
        float fogColorEnabled[4], fogStartEnd[4];
    };

    // plan_metal.md METAL-34-style extraction: this formula's real logic now lives in the plain-C++
    // MetalNormalMatrix.hpp (no Objective-C, buildable and unit-tested on any platform without an
    // Apple toolchain) -- kept as a thin same-signature wrapper here so all 3 existing call sites
    // in this file are unaffected.
    static void computeNormalMatrixCols(const float* w, float col0[4], float col1[4], float col2[4])
    {
        ComputeMetalNormalMatrixCols(w, col0, col1, col2);
    }

    // Plain C++ mirrors of kMetalShaderSource's `EnvTransform`/`EnvUniforms` -- see that struct's
    // own comment for the real-XNA-EnvironmentMapEffect-has-no-separate-ambient-uniform finding.
    struct EnvTransform { float wvp[16]; float world[16]; float normalCol0[4]; float normalCol1[4]; float normalCol2[4]; };
    struct EnvUniforms {
        float diffuseColor[4], emissiveColor[4];
        float light0Dir[4], light0Diffuse[4];
        float light1Dir[4], light1Diffuse[4];
        float light2Dir[4], light2Diffuse[4];
        float envMapSpecular[4], eyePosition[4], envParams[4], alphaTest[4];
        float fogColorEnabled[4], fogStartEnd[4];
    };

    // Plain C++ mirrors of kMetalShaderSource's `SkinnedTransform`/`SkinnedUniforms` -- see that
    // struct's own comment for the "no separate world-normal-matrix step" finding.
    struct SkinnedTransform { float wvp[16]; float world[16]; float skinParams[4]; };
    struct SkinnedUniforms {
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
    struct PbrTransform { float wvp[16]; float world[16]; float normalCol0[4]; float normalCol1[4]; float normalCol2[4]; };
    struct PbrUniforms {
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

    // Plain C++ mirror of kMetalShaderSource's `SkinnedPbrTransform` (reuses `PbrUniforms` as-is --
    // the fragment-side uniforms are identical between skinned and unskinned PBR).
    struct SkinnedPbrTransform { float wvp[16]; float world[16]; float skinParams[4]; };

    class MetalTexture final : public ITextureBackend
    {
    public:
        MetalTexture(id<MTLDevice> dev, const ImageData& data) : w_(data.width), h_(data.height)
        {
            MTLTextureDescriptor* d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                width:w_ height:h_ mipmapped:(data.mipLevels > 1)];
            d.usage=MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
            texture_=[dev newTextureWithDescriptor:d];
            if (!texture_) throw std::runtime_error("Metal: failed to create texture");
            if (!data.pixels.empty()) {
                MTLRegion r=MTLRegionMake2D(0,0,w_,h_);
                [texture_ replaceRegion:r mipmapLevel:0 withBytes:data.pixels.data() bytesPerRow:w_*4];
            }
        }
        ~MetalTexture() override { [texture_ release]; }
        int GetWidth() const override { return w_; }
        int GetHeight() const override { return h_; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override {
            [texture_ replaceRegion:MTLRegionMake2D(0,0,w_,h_) mipmapLevel:0 withBytes:rgba bytesPerRow:stride];
        }
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int lw, int lh) override {
            [texture_ replaceRegion:MTLRegionMake2D(0,0,lw,lh) mipmapLevel:level withBytes:rgba bytesPerRow:lw*4];
        }
        id<MTLTexture> native() const { return texture_; }
    private:
        int w_, h_; id<MTLTexture> texture_ = nil;
    };

    // plan_metal.md Phase 11 (METAL-120/121): `surfaceFormat` is deliberately ignored and always
    // RGBA8Unorm, matching EasyGLTextureCubeBackend's own established convention exactly (its
    // constructor takes the parameter as `int /*surfaceFormat*/` -- confirmed by reading it, not
    // assumed) -- not a new gap introduced here. `MTLTextureDescriptor
    // textureCubeDescriptorWithPixelFormat:size:mipmapped:` computes the correct mip level count
    // itself; unlike EasyGL's GL-based backend, Metal needs no "pre-allocate every mip level with
    // null data" workaround (glTexSubImage requires a level to already be defined; MTLTexture
    // allocates all `mipmapLevelCount` levels together at creation time).
    class MetalTextureCube final : public ITextureCubeBackend
    {
    public:
        // plan_metal.md METAL-122: `queue` is stored (retained) purely so GetData() below can blit
        // -- found while implementing RenderTargetCube's own GetData() that TextureCube.cpp's real
        // code (not just this interface's own doc comment) *always* calls backend_->GetData()
        // unconditionally, with no CPU-side pixel-shadow shortcut the way Texture2D has; unlike the
        // 2D case, a plain (non-render-target) cube texture's GetData() genuinely reaches the
        // backend for every call, so this was a real, previously-shipping gap, not a deliberate
        // scope match to Texture2D's own precedent.
        MetalTextureCube(id<MTLDevice> dev, id<MTLCommandQueue> queue, int size, bool mipMap) : dev_(dev), queue_(queue)
        {
            [dev_ retain]; [queue_ retain];
            MTLTextureDescriptor* d=[MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm size:(NSUInteger)size mipmapped:mipMap];
            d.usage=MTLTextureUsageShaderRead;
            texture_=[dev newTextureWithDescriptor:d];
            if(!texture_) throw std::runtime_error("Metal: failed to create cube texture");
        }
        ~MetalTextureCube() override { [texture_ release]; [queue_ release]; [dev_ release]; }
        // Face ordinals (0=+X,1=-X,2=+Y,3=-Y,4=+Z,5=-Z) already match Metal's own cube `slice`
        // ordering directly -- same convention documented on IRenderTargetCubeBackend and already
        // relied upon, unchanged, by every other backend (confirmed against
        // EasyGLTextureCubeBackend's own kCubeFaceTargets order).
        void SetData(int face,int level,int x,int y,int w,int h,const void* data,int /*dataLength*/) override
        {
            if(face<0||face>=6) return;
            MTLRegion r=MTLRegionMake2D((NSUInteger)x,(NSUInteger)y,(NSUInteger)w,(NSUInteger)h);
            [texture_ replaceRegion:r mipmapLevel:(NSUInteger)level slice:(NSUInteger)face withBytes:data bytesPerRow:(NSUInteger)(w*4) bytesPerImage:0];
        }
        void GetData(int face,int level,int x,int y,int w,int h,void* data,int dataLength) const override
        {
            if(face<0||face>=6) return;
            blitTextureToClientBuffer(dev_, queue_, texture_, (NSUInteger)face, level, x, y, 0, w, h, 1, data, dataLength);
        }
        id<MTLTexture> native() const { return texture_; }
    private:
        id<MTLDevice> dev_=nil; id<MTLCommandQueue> queue_=nil;
        id<MTLTexture> texture_=nil;
    };

    class MetalTexture3D final : public ITexture3DBackend
    {
    public:
        // plan_metal.md METAL-122 (3D analog): same finding as MetalTextureCube above --
        // Texture3D.cpp's real code always calls backend_->GetData() unconditionally, no CPU-side
        // shadow shortcut, so this was a real, previously-shipping gap for any Texture3D, not just
        // a render-target-only concern.
        MetalTexture3D(id<MTLDevice> dev, id<MTLCommandQueue> queue, int w,int h,int depth,bool mipMap) : dev_(dev), queue_(queue)
        {
            [dev_ retain]; [queue_ retain];
            MTLTextureDescriptor* d=[[MTLTextureDescriptor alloc] init];
            d.textureType=MTLTextureType3D; d.pixelFormat=MTLPixelFormatRGBA8Unorm;
            d.width=(NSUInteger)w; d.height=(NSUInteger)h; d.depth=(NSUInteger)depth;
            NSUInteger levels=1;
            if(mipMap){ int m=std::max({w,h,depth}); while(m>1){ m/=2; ++levels; } }
            d.mipmapLevelCount=levels;
            d.usage=MTLTextureUsageShaderRead;
            texture_=[dev newTextureWithDescriptor:d]; [d release];
            if(!texture_) throw std::runtime_error("Metal: failed to create 3D texture");
        }
        ~MetalTexture3D() override { [texture_ release]; [queue_ release]; [dev_ release]; }
        void SetData(int level,int x,int y,int z,int w,int h,int depth,const void* data,int /*dataLength*/) override
        {
            MTLRegion r=MTLRegionMake3D((NSUInteger)x,(NSUInteger)y,(NSUInteger)z,(NSUInteger)w,(NSUInteger)h,(NSUInteger)depth);
            [texture_ replaceRegion:r mipmapLevel:(NSUInteger)level slice:0 withBytes:data bytesPerRow:(NSUInteger)(w*4) bytesPerImage:(NSUInteger)(w*4*h)];
        }
        // plan_metal.md METAL-125: real per-level readback via the shared blit helper -- slice is
        // always 0 for a 3D texture (there is no per-slice concept the way a cube's 6 faces have;
        // `z`/`depth` address the volume directly within slice 0).
        void GetData(int level,int x,int y,int z,int w,int h,int depth,void* data,int dataLength) const override
        {
            blitTextureToClientBuffer(dev_, queue_, texture_, 0, level, x, y, z, w, h, depth, data, dataLength);
        }
        id<MTLTexture> native() const { return texture_; }
    private:
        id<MTLDevice> dev_=nil; id<MTLCommandQueue> queue_=nil;
        id<MTLTexture> texture_=nil;
    };

    class MetalVertexBuffer final : public IVertexBufferBackend
    {
    public:
        explicit MetalVertexBuffer(id<MTLDevice> dev, int cap) : dev_(dev), capacity_(cap) { [dev_ retain]; }
        ~MetalVertexBuffer() override { [buffer_ release]; [dev_ release]; }
        void SetData(const void* data,int count,std::size_t stride) override {
            count_=count; stride_=stride; const NSUInteger bytes=(NSUInteger)count*stride;
            [buffer_ release]; buffer_=[dev_ newBufferWithBytes:data length:bytes options:MTLResourceStorageModeShared];
            if(!buffer_) throw std::runtime_error("Metal: failed to create vertex buffer");
        }
        int GetVertexCount() const override { return count_; }
        id<MTLBuffer> native() const { return buffer_; }
        std::size_t stride() const { return stride_; }
    private:
        id<MTLDevice> dev_; id<MTLBuffer> buffer_=nil; int capacity_=0,count_=0; std::size_t stride_=0;
    };

    class MetalIndexBuffer final : public IIndexBufferBackend
    {
    public:
        MetalIndexBuffer(id<MTLDevice> dev,bool is32):dev_(dev),is32_(is32){[dev_ retain];}
        ~MetalIndexBuffer() override{[buffer_ release];[dev_ release];}
        void SetData16(const void* d,int n) override { upload(d,n,2,false); }
        void SetData32(const void* d,int n) override { upload(d,n,4,true); }
        int GetIndexCount() const override{return count_;}
        bool IsThirtyTwoBit() const override{return is32_;}
        id<MTLBuffer> native() const{return buffer_;}
    private:
        void upload(const void* d,int n,int sz,bool v32){is32_=v32;count_=n;[buffer_ release];buffer_=[dev_ newBufferWithBytes:d length:n*sz options:MTLResourceStorageModeShared];}
        id<MTLDevice> dev_; id<MTLBuffer> buffer_=nil; bool is32_; int count_=0;
    };
}

// plan_metal.md Phase 10: forward-declared so Impl can hold a non-owning pointer to whichever
// MetalRenderTargetBackend is currently bound (ownership lives in the RenderTarget2D C++ object
// via its own unique_ptr<IRenderTargetBackend>, matching every other backend's convention) --
// MetalRenderTargetBackend itself is defined later, after Impl, since it needs Impl to be a
// complete type. Impl::resolveActiveAttachments() (used by ensureFrame()/clear()) is declared
// inside Impl below but its body is defined out-of-line, after MetalRenderTargetBackend, for the
// same reason -- C++ allows this: a member function's body has complete-class access to sibling
// members regardless of textual declaration/definition order.
class MetalRenderTargetBackend;
// plan_metal.md METAL-109: same forward-declaration reasoning as MetalRenderTargetBackend above,
// for the cube-map analog -- Impl needs a non-owning pointer to whichever RenderTargetCube face is
// currently bound.
class MetalRenderTargetCubeBackend;

// plan_metal.md Phase 10: a previously-rendered-to RenderTarget2D used as an ordinary texture in a
// later draw (post-processing, portals, mirrors, etc.) is a real, common XNA pattern --
// MetalRenderTargetBackend does NOT inherit from MetalTexture (they are separate sibling
// hierarchies that both implement ITextureBackend directly, matching the real interface shape:
// IRenderTargetBackend : ITextureBackend, not IRenderTargetBackend : ITextureBackend, MetalTexture),
// so a bare `dynamic_cast<const MetalTexture*>` on an ITextureBackend* slot would silently fail
// (return null) whenever it actually holds a render target -- every texture-binding call site in
// this file goes through this one helper instead of dynamic_cast<MetalTexture*> directly, so this
// gets fixed everywhere at once rather than risking a missed call site. Forward-declared here
// (needs MetalSpriteBatch, defined before MetalRenderTargetBackend, to be able to call it), defined
// out-of-line after MetalRenderTargetBackend for the same reason as resolveActiveAttachments above.
static id<MTLTexture> nativeTextureFor(const ITextureBackend* t);

// plan_metal.md METAL-109/METAL-64ff: same reasoning as nativeTextureFor() above, for the cube
// case -- EnvironmentMapEffect's envMap slot is an ITextureCubeBackend*, and a RenderTargetCube
// rendered into and then sampled as a reflection source (a very common real-time-reflection XNA
// pattern -- arguably RenderTargetCube's single most common real use) must resolve through here
// too, or the existing bare `dynamic_cast<const MetalTextureCube*>` at the EnvMap32 draw site would
// silently fail for it exactly the way the 2D case did before nativeTextureFor() existed.
static id<MTLTexture> nativeCubeTextureFor(const ITextureCubeBackend* t);

struct MetalGraphicsBackend::Impl
{
    SDL_Window* window=nullptr;
    SDL_MetalView view=nullptr;
    CAMetalLayer* layer=nil;
    id<MTLDevice> device=nil;
    id<MTLCommandQueue> queue=nil;
    id<MTLLibrary> library=nil;
    std::unordered_map<PipelineCacheKey, id<MTLRenderPipelineState>, PipelineCacheKeyHash> pipelineCache;
    id<MTLDepthStencilState> depthState=nil;
    id<MTLSamplerState> sampler=nil;
    std::unordered_map<uint32_t, id<MTLSamplerState>> samplerCache;
    id<MTLSamplerState> samplerSlots[16]={};
    id<MTLCommandBuffer> command=nil;
    id<MTLRenderCommandEncoder> encoder=nil;
    id<CAMetalDrawable> drawable=nil;
    id<MTLTexture> depthTexture=nil;
    int virtualW=0,virtualH=0,presentationMode=0,swapInterval=1;
    bool depthEnabled=true,depthWrite=true,blendEnabled=true,scissorEnabled=false;
    int refStencil=0;
    MTLViewport viewport{0,0,1,1,0,1};
    MTLScissorRect scissor{0,0,1,1};
    MTLCullMode cull=MTLCullModeNone;
    MTLTriangleFillMode fill=MTLTriangleFillModeFill;
    float depthBias=0,slopeBias=0;
    BlendKey currentBlend; // real per-BlendState pipeline selection key, see ApplyBlendState() below
    // plan_metal.md METAL-7/9/10: real DepthStencilState fields, defaults matching
    // DepthStencilState::DepthStencilState()'s own real values exactly (DepthStencilState.cpp).
    int depthFunc=3;               // CompareFunction::LessEqual -- DepthStencilState.Default's own value
    bool stencilEnabled=false;
    int stencilFunc=0, stencilPass=0, stencilFail=0, stencilDepthFail=0; // Always=0 / Keep=0
    int stencilMask=0x7FFFFFFF, stencilWriteMask=0x7FFFFFFF;
    bool twoSidedStencil=false;
    int ccwStencilFunc=0, ccwStencilPass=0, ccwStencilFail=0, ccwStencilDepthFail=0;
    float blendColor[4]={1,1,1,1}; // BlendState.BlendFactor default == Color.White

    // plan_metal.md METAL-136/137: real occlusion queries via MTLVisibilityResultBuffer. This
    // buffer must be attached to the MTLRenderPassDescriptor at render-pass-creation time (Metal
    // has no way to attach it mid-encoder the way setVisibilityResultMode:offset: can be called
    // mid-encoder) -- so it is allocated once here and referenced by every render pass
    // ensureFrame()/clear() create, with each MetalOcclusionQueryBackend instance owning one
    // 8-byte slot (a uint64_t sample-passed count written by the GPU) via a simple incrementing
    // counter. kMaxOcclusionQuerySlots is a practical, generous cap, not an API-imposed one.
    static constexpr int kMaxOcclusionQuerySlots = 1024;
    id<MTLBuffer> visibilityBuffer=nil;
    int nextQuerySlot=0;

    // plan_metal.md Phase 10 (METAL-98/107): non-owning pointer to whichever RenderTarget2D is
    // currently bound; nullptr means "drawing to the backbuffer" (the default). Ownership lives in
    // the RenderTarget2D C++ object's own unique_ptr<IRenderTargetBackend>, matching every other
    // backend's convention -- MetalRenderTargetBackend's own destructor clears this pointer if it
    // is destroyed while still bound, so it never dangles.
    MetalRenderTargetBackend* currentRenderTarget=nullptr;

    // plan_metal.md METAL-109/110: same non-owning-pointer convention as currentRenderTarget above,
    // for whichever face of whichever RenderTargetCube is currently bound (mutually exclusive with
    // currentRenderTarget -- SetRenderTarget2D()/SetRenderTargetCubeFace() each cross-unbind the
    // other before binding, matching EasyGLGraphicsBackend::SetRenderTarget2D/SetRenderTargetCubeFace's
    // own established real contract exactly, not an invented convention).
    MetalRenderTargetCubeBackend* currentRenderTargetCube=nullptr;
    NSUInteger currentRenderTargetCubeFace=0;

    // plan_metal.md METAL-87: fallback textures for PbrEffect's 4 optional maps (normalMap/
    // metallicRoughnessMap/emissiveMap/occlusionMap) when left unbound, mirroring
    // EasyGLGraphicsBackend::EnsureDefaultWhiteTexture()/EnsureDefaultFlatNormalTexture() exactly
    // -- a null map must not sample garbage or crash. White (255,255,255,255) is the correct
    // neutral default for metallicRoughness (mr.g=1,mr.b=1 -> full requested roughness/metallic
    // via the *Factor multiply)/emissive(*1)/occlusion(*1, no darkening); flat normal
    // (128,128,255,255 -> decodes to (0,0,1) in tangent space, i.e. "no perturbation") for the
    // normal map specifically.
    id<MTLTexture> defaultWhiteTexture=nil;
    id<MTLTexture> defaultFlatNormalTexture=nil;

    // Re-applies every piece of encoder-scoped dynamic state this backend tracks. Metal has no
    // persistent-across-encoders state at all (unlike, say, retained GL context state) -- a fresh
    // MTLRenderCommandEncoder starts with undefined cull/fill/bias/stencil-ref/blend-color, so
    // ensureFrame()/clear() must both call this every time they create one. Previously ensureFrame()
    // inlined a partial version of this (missing stencil reference and blend color entirely) and
    // clear() didn't reapply cull/fill/depthBias/stencil-reference at all -- a real, pre-existing
    // inconsistency between the two encoder-creation paths, fixed here by sharing one function.
    void applyTrackedEncoderState()
    {
        // plan_metal.md METAL-5: explicit, not relied-on-by-accident. XNA CullMode's
        // CullClockwiseFace(1)/CullCounterClockwiseFace(2) map to MTLCullModeFront/Back exactly like
        // VulkanGraphicsBackend's own VK_CULL_MODE_FRONT_BIT/BACK_BIT mapping (see its own comment:
        // "Pipeline uses VK_FRONT_FACE_CLOCKWISE, so CW faces are front faces") ONLY if Metal's front
        // face is also clockwise-winding -- Apple's own MTLRenderCommandEncoder docs state clockwise
        // IS the default, so this was already behaviorally correct, but silently depending on an
        // unstated default is exactly the kind of fragile assumption this project's own history has
        // been burned by before (see Vulkan's own front/back stencil swap, discovered the hard way).
        // Setting it explicitly here removes that risk permanently.
        [encoder setFrontFacingWinding:MTLWindingClockwise];
        [encoder setViewport:viewport]; [encoder setCullMode:cull]; [encoder setTriangleFillMode:fill];
        [encoder setDepthBias:depthBias slopeScale:slopeBias clamp:0]; [encoder setDepthStencilState:depthState];
        [encoder setStencilReferenceValue:(uint32_t)refStencil];
        [encoder setBlendColorRed:blendColor[0] green:blendColor[1] blue:blendColor[2] alpha:blendColor[3]];
    }

    // plan_metal.md Phase 10 (METAL-98/100/107): resolves the color+depth textures for whatever's
    // currently active -- either the bound MetalRenderTargetBackend, or (if none) the backbuffer
    // drawable (acquiring a fresh one via nextDrawable if not already held). Declared here,
    // defined out-of-line after MetalRenderTargetBackend's own definition (see the forward-decl's
    // own comment above Impl for why). Every RenderTarget2D always gets a real depth+stencil
    // texture regardless of the requested DepthFormat -- the same simplification tier Vulkan's own
    // CreateRenderTarget2D doc comment already documents and accepts project-wide (not EasyGL/
    // Bgfx's more honest "only allocate what was actually requested" tier) -- because every Metal
    // pipeline in this file already hardcodes depthAttachmentPixelFormat/stencilAttachmentPixelFormat
    // unconditionally, so a render pass with no depth attachment at all would be a real
    // pipeline/render-pass format mismatch, not just a missed optimization.
    //
    // Returns false only for the backbuffer case where nextDrawable legitimately returns nil (e.g.
    // a minimized/occluded window) -- returns a bool rather than throwing itself so each caller can
    // keep its own pre-existing, DELIBERATELY different response to that failure: ensureFrame()
    // already threw on it before this refactor, clear() already silently no-op'd on it before this
    // refactor -- preserved exactly as each caller's own choice below, not unified into one
    // behavior (which would have been a real, unintended regression for whichever caller didn't
    // already do that).
    // plan_metal.md METAL-109/110: sliceOut is always 0 except when a RenderTargetCube face is the
    // active target (MTLRenderPassColorAttachmentDescriptor.slice selects which cube face/array
    // layer of colorOut a render pass actually writes to; 0 is simply ignored/inert for a plain 2D
    // texture, so callers can set it unconditionally with no branching of their own).
    bool resolveActiveAttachments(id<MTLTexture>& colorOut, id<MTLTexture>& depthOut, NSUInteger& sliceOut);

    // Ends whatever encoder/command-buffer is currently active -- shared by endFrame() and
    // MetalRenderTargetBackend's own Bind/UnbindAsRenderTarget() (Metal render passes are
    // fixed-attachment for their whole encoder lifetime, unlike GL's dynamic FBO rebinding, so
    // switching what's being rendered to always means ending the current pass first).
    //
    // plan_metal.md METAL-180 (Phase 18 audit): `presentBackbuffer` MUST be false for every call
    // site except endFrame()/Present() itself. This was a real, previously-shipped bug: the
    // original version always called `presentDrawable:` here whenever `drawable` was non-nil,
    // which fires on *every* mid-frame encoder boundary, not just real end-of-frame -- clear()
    // calling this to start a fresh render pass, or SetRenderTarget2D() switching to/from an
    // offscreen target mid-frame, would each present whatever partial content was in the backbuffer
    // drawable at that moment (visible tearing/flicker), then immediately null `drawable` so the
    // NEXT backbuffer touch that same frame fetched a brand-new drawable via nextDrawable -- meaning
    // a single logical frame with N target switches could call nextDrawable/present up to N+1
    // times instead of exactly once. `command` is still always committed here (a Metal command
    // buffer cannot be resumed once an encoder ends), but the drawable itself now survives across
    // mid-frame boundaries and is presented+released exactly once, by endFrame() alone -- matching
    // GraphicsDevice.Present()'s real XNA contract of presenting once per game-initiated Present()
    // call regardless of how many render-target switches happened in between.
    void endActiveEncoding(bool presentBackbuffer)
    {
        if (encoder) { [encoder endEncoding]; [encoder release]; encoder=nil; }
        if (command) {
            if (presentBackbuffer && drawable) [command presentDrawable:drawable];
            [command commit];
            [command release]; command=nil;
            if (presentBackbuffer) drawable=nil;
        }
    }

    void ensureFrame()
    {
        if (encoder) return;
        id<MTLTexture> colorTex=nil, depthTex=nil; NSUInteger slice=0;
        if (!resolveActiveAttachments(colorTex, depthTex, slice)) throw std::runtime_error("Metal: CAMetalLayer returned no drawable");
        command=[queue commandBuffer]; [command retain];
        MTLRenderPassDescriptor* rp=[MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture=colorTex; rp.colorAttachments[0].slice=slice;
        rp.colorAttachments[0].loadAction=MTLLoadActionLoad; rp.colorAttachments[0].storeAction=MTLStoreActionStore;
        rp.depthAttachment.texture=depthTex; rp.depthAttachment.loadAction=MTLLoadActionLoad; rp.depthAttachment.storeAction=MTLStoreActionStore;
        rp.stencilAttachment.texture=depthTex; rp.stencilAttachment.loadAction=MTLLoadActionLoad; rp.stencilAttachment.storeAction=MTLStoreActionStore;
        rp.visibilityResultBuffer=visibilityBuffer;
        encoder=[command renderCommandEncoderWithDescriptor:rp]; [encoder retain];
        const NSUInteger w=colorTex.width,h=colorTex.height;
        viewport={0,0,(double)w,(double)h,0,1}; scissor={0,0,w,h};
        applyTrackedEncoderState();
    }

    void endFrame()
    {
        if(!command) return;
        endActiveEncoding(true); // real end-of-frame -- the only call site allowed to present.
    }

    void clear(bool color,float r,float g,float b,float a,bool depth,float dv,bool stencil,int sv)
    {
        endActiveEncoding(false); // mid-frame boundary only -- see endActiveEncoding()'s own METAL-180 note.
        id<MTLTexture> colorTex=nil, depthTex=nil; NSUInteger slice=0;
        if (!resolveActiveAttachments(colorTex, depthTex, slice)) return;
        command=[queue commandBuffer]; [command retain];
        MTLRenderPassDescriptor* rp=[MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture=colorTex; rp.colorAttachments[0].slice=slice; rp.colorAttachments[0].loadAction=color?MTLLoadActionClear:MTLLoadActionLoad; rp.colorAttachments[0].storeAction=MTLStoreActionStore; rp.colorAttachments[0].clearColor=MTLClearColorMake(r,g,b,a);
        rp.depthAttachment.texture=depthTex; rp.depthAttachment.loadAction=depth?MTLLoadActionClear:MTLLoadActionLoad; rp.depthAttachment.storeAction=MTLStoreActionStore; rp.depthAttachment.clearDepth=dv;
        rp.stencilAttachment.texture=depthTex; rp.stencilAttachment.loadAction=stencil?MTLLoadActionClear:MTLLoadActionLoad; rp.stencilAttachment.storeAction=MTLStoreActionStore; rp.stencilAttachment.clearStencil=sv;
        rp.visibilityResultBuffer=visibilityBuffer;
        encoder=[command renderCommandEncoderWithDescriptor:rp]; [encoder retain];
        const NSUInteger w=colorTex.width,h=colorTex.height;
        viewport={0,0,(double)w,(double)h,0,1}; scissor={0,0,w,h};
        applyTrackedEncoderState();
    }

    void rebuildDepthState()
    {
        MTLDepthStencilDescriptor* d=[[MTLDepthStencilDescriptor alloc] init];
        d.depthCompareFunction = depthEnabled ? metalCompareFunction(depthFunc) : MTLCompareFunctionAlways;
        d.depthWriteEnabled=depthWrite;
        // plan_metal.md METAL-9/10: real front/back stencil test, replacing the previous
        // reference-value-only plumbing. Front face carries XNA's "normal" stencil fields; back
        // face carries the CounterClockwise fields when TwoSidedStencilMode is set, else mirrors
        // front exactly -- matches FNA's own real behavior (CCW fields are simply ignored when
        // TwoSidedStencilMode=false, not reset to any default) and EasyGLGraphicsBackend's
        // identical fallback-to-front pattern. UNLIKE VulkanGraphicsBackend::FillDepthStencilState
        // (see its own long comment), this front/back assignment is NOT swapped -- Metal has no
        // Vulkan-style NDC Y-flip in this codebase's vertex shaders (Vulkan's own swap was an
        // empirically-found compensation for that Y-flip's winding interaction, root-caused to
        // Vulkan specifically, not a general rule) -- but this has NOT been empirically verified
        // on real Metal hardware and must be treated as unproven until it is (plan_metal.md
        // Testing strategy tier 2/3).
        if (stencilEnabled) {
            MTLStencilDescriptor* front=[[MTLStencilDescriptor alloc] init];
            front.stencilCompareFunction = metalCompareFunction(stencilFunc);
            front.stencilFailureOperation = metalStencilOp(stencilFail);
            front.depthFailureOperation = metalStencilOp(stencilDepthFail);
            front.depthStencilPassOperation = metalStencilOp(stencilPass);
            front.readMask = (uint32_t)stencilMask;
            front.writeMask = (uint32_t)stencilWriteMask;
            d.frontFaceStencil = front;
            if (twoSidedStencil) {
                MTLStencilDescriptor* back=[[MTLStencilDescriptor alloc] init];
                back.stencilCompareFunction = metalCompareFunction(ccwStencilFunc);
                back.stencilFailureOperation = metalStencilOp(ccwStencilFail);
                back.depthFailureOperation = metalStencilOp(ccwStencilDepthFail);
                back.depthStencilPassOperation = metalStencilOp(ccwStencilPass);
                back.readMask = (uint32_t)stencilMask;
                back.writeMask = (uint32_t)stencilWriteMask;
                d.backFaceStencil = back;
                [back release];
            } else {
                d.backFaceStencil = front;
            }
            [front release];
        }
        // else: leave frontFaceStencil/backFaceStencil nil (MTLDepthStencilDescriptor's default),
        // which Metal treats as "stencil test always passes, no writes" -- correct for
        // DepthStencilState.StencilEnable=false.
        id<MTLDepthStencilState> s=[device newDepthStencilStateWithDescriptor:d]; [d release]; [depthState release]; depthState=s;
        if(encoder) [encoder setDepthStencilState:depthState];
    }

    // plan_metal.md METAL-23/29: replaces the 5 eagerly-built named pipeline fields with a
    // lazily-populated cache keyed by (shader/vertex-layout variant, current blend state).
    id<MTLRenderPipelineState> getOrCreatePipeline(PipelineKind kind)
    {
        PipelineCacheKey key{kind, currentBlend};
        auto it = pipelineCache.find(key);
        if (it != pipelineCache.end()) return it->second;
        NSString* vs=nil; NSString* fs=nil; std::size_t stride=0;
        switch (kind) {
            case PipelineKind::Colored16:        vs=@"cna_v3d_color";    fs=@"cna_f3d_color";   stride=16; break;
            case PipelineKind::Textured20:       vs=@"cna_v3d_tex";      fs=@"cna_f3d_texture"; stride=20; break;
            case PipelineKind::ColorTex24:       vs=@"cna_v3d_colortex"; fs=@"cna_f3d_texture"; stride=24; break;
            case PipelineKind::LitTex32:         vs=@"cna_v3d_lit";      fs=@"cna_f3d_lit";     stride=32; break;
            case PipelineKind::LitTex32VertexLit: vs=@"cna_v3d_lit_vertexlit"; fs=@"cna_f3d_lit_vertexlit"; stride=32; break;
            case PipelineKind::DualTex20:        vs=@"cna_v3d_tex";      fs=@"cna_f3d_dualtex"; stride=20; break;
            case PipelineKind::DualTex24Colored: vs=@"cna_v3d_colortex"; fs=@"cna_f3d_dualtex"; stride=24; break;
            case PipelineKind::EnvMap32:         vs=@"cna_v3d_envmap";   fs=@"cna_f3d_envmap";  stride=32; break;
            case PipelineKind::Skinned52:        vs=@"cna_v3d_skinned";       fs=@"cna_f3d_skinned"; stride=52; break;
            case PipelineKind::Skinned56:        vs=@"cna_v3d_skinned_color"; fs=@"cna_f3d_skinned"; stride=56; break;
            case PipelineKind::Skinned52VertexLit: vs=@"cna_v3d_skinned_vertexlit";       fs=@"cna_f3d_skinned_vertexlit"; stride=52; break;
            case PipelineKind::Skinned56VertexLit: vs=@"cna_v3d_skinned_color_vertexlit"; fs=@"cna_f3d_skinned_vertexlit"; stride=56; break;
            case PipelineKind::Pbr48:            vs=@"cna_v3d_pbr";           fs=@"cna_f3d_pbr";     stride=48; break;
            case PipelineKind::SkinnedPbr68:      vs=@"cna_v3d_skinned_pbr";  fs=@"cna_f3d_pbr";      stride=68; break;
            case PipelineKind::Sprite2D:         vs=@"cna_v2d";          fs=@"cna_f2d";          stride=0;  break;
        }
        id<MTLVertexDescriptor> vd = (kind==PipelineKind::Sprite2D) ? nil : vertexDescriptorForStride(stride);
        id<MTLRenderPipelineState> pipe = makePipeline(device, library, vs, fs, vd, currentBlend);
        pipelineCache.emplace(key, pipe);
        return pipe;
    }

    // Builds (or reuses) a cached MTLSamplerState for the given raw XNA TextureFilter/
    // TextureAddressMode/maxAnisotropy combination. Cache is owned by this Impl and released
    // once, in its destructor -- samplerSlots[] below only holds non-owning references into it.
    id<MTLSamplerState> samplerFor(int filter,int addressU,int addressV,int maxAnisotropy)
    {
        const uint32_t aniso=(uint32_t)std::clamp(maxAnisotropy,1,16);
        const uint32_t key=(uint32_t)(filter&0xFF) | ((uint32_t)(addressU&0xFF)<<8) | ((uint32_t)(addressV&0xFF)<<16) | (aniso<<24);
        auto it=samplerCache.find(key);
        if(it!=samplerCache.end()) return it->second;
        MTLSamplerDescriptor* sd=[[MTLSamplerDescriptor alloc] init];
        sd.minFilter=metalMinFilter(filter); sd.magFilter=metalMagFilter(filter); sd.mipFilter=metalMipFilter(filter);
        sd.sAddressMode=metalAddressMode(addressU); sd.tAddressMode=metalAddressMode(addressV);
        if(filter==2) sd.maxAnisotropy=aniso;
        id<MTLSamplerState> s=[device newSamplerStateWithDescriptor:sd]; [sd release];
        samplerCache.emplace(key,s);
        return s;
    }

    // plan_metal.md Phase 15 (METAL-153/155/156/158/159): the shared letterbox/overscan/stretch/
    // native/fixed-height-dynamic-width viewport math, ported near-verbatim from
    // SdlGpuGraphicsBackend::ComputeLogicalViewport() (an already-shipped, already-relied-upon
    // implementation of the exact same CnaPresentationMode contract every backend shares) rather
    // than re-derived from scratch. `width`/`height`/`x`/`y` are the logical canvas's rectangle in
    // physical window pixels; `logicalWidth`/`logicalHeight` are the virtual-resolution size that
    // rectangle represents (equal to the physical size whenever no virtual resolution is set,
    // which is also this struct's all-zero-input-safe degenerate case).
    // plan_metal.md METAL-34-style extraction: the real arithmetic now lives in the plain-C++
    // MetalLogicalViewport.hpp (no Objective-C, buildable and unit-tested on any platform without
    // an Apple toolchain) -- kept as a thin same-name alias plus a wrapper here so all existing call
    // sites in this file are unaffected; this method's only remaining job is asking SDL for the
    // real physical window size, which genuinely does need a live window.
    using LogicalViewport = MetalLogicalViewport;
    LogicalViewport computeLogicalViewport() const
    {
        int pw=0, ph=0; SDL_GetWindowSizeInPixels(window, &pw, &ph);
        LogicalViewport vp = ComputeMetalLogicalViewport(pw, ph, (CnaPresentationMode)presentationMode, virtualW, virtualH);
        return vp;
    }

    // plan_metal.md METAL-157/158: previously `cna_v2d` mapped sprite coordinates directly from
    // raw physical drawable pixels, completely bypassing virtual resolution/letterboxing -- a
    // real, currently-shipping bug whenever the physical window size differs from the requested
    // virtual resolution. Algebraically folding computeLogicalViewport()'s rect + the physical
    // drawable size into one scale+offset pair keeps `cna_v2d` a single multiply-add per vertex;
    // when no virtual resolution is set this reduces exactly (verified by hand, not just by
    // inspection) to the original `px/dw*2-1, 1-py/dh*2` formula -- zero behavior change for every
    // draw that isn't using virtual resolution today.
    struct Sprite2DTransform { float scaleX=1, scaleY=1, offsetX=0, offsetY=0; };
    // plan_metal.md Phase 10: real bug found and fixed while adding RenderTarget2D support -- this
    // used to read `drawable.texture.width/height` unconditionally, but `drawable` is nil whenever
    // a RenderTarget2D is currently bound (see resolveActiveAttachments()'s render-target branch,
    // which never touches `drawable` at all), so a message-to-nil would silently degrade to
    // dw=dh=0 and this function would return the identity-ish default -- SpriteBatch draws into a
    // bound render target would have used the WRONG (backbuffer-shaped) transform, or worse,
    // whatever stale identity default, not the render target's own real dimensions. Declared here,
    // defined out-of-line after MetalRenderTargetBackend (same reason as resolveActiveAttachments:
    // its body now calls currentRenderTarget->colorTexture(), which needs the complete type).
    Sprite2DTransform computeSpriteTransform() const;

    // plan_metal.md METAL-153/154: real window<->logical coordinate transforms, previously
    // entirely unimplemented (base `IGraphicsBackend` default returns false) -- SdlInputBridge
    // depends on this for correct mouse coordinates on any letterboxed/scaled window, per this
    // method's own doc comment on IGraphicsBackend.hpp. Ported from
    // SdlGpuGraphicsBackend::TransformWindowToLogical/TransformLogicalToWindow verbatim (same
    // LogicalViewport shape, same formula) rather than re-derived.
    bool transformWindowToLogical(float windowX, float windowY, float& logX, float& logY) const
    {
        LogicalViewport vp = computeLogicalViewport();
        if (vp.width == 0.0f || vp.height == 0.0f) return false;
        logX = (windowX - vp.x) * vp.logicalWidth / vp.width;
        logY = (windowY - vp.y) * vp.logicalHeight / vp.height;
        return windowX >= vp.x && windowX < vp.x + vp.width &&
               windowY >= vp.y && windowY < vp.y + vp.height;
    }
    bool transformLogicalToWindow(float logX, float logY, float& windowX, float& windowY) const
    {
        LogicalViewport vp = computeLogicalViewport();
        if (vp.logicalWidth == 0.0f || vp.logicalHeight == 0.0f) return false;
        windowX = vp.x + logX * vp.width / vp.logicalWidth;
        windowY = vp.y + logY * vp.height / vp.logicalHeight;
        return true;
    }
};

class MetalSpriteBatch final : public ISpriteBatchBackend
{
public:
    explicit MetalSpriteBatch(MetalGraphicsBackend& b):b_(b){}
    void Begin() override { begun_=true; }
    void End() override { begun_=false; }
    void SetSamplerFilter(int f) override { filter_=f; }
    void SetSamplerAddressMode(int addressU,int addressV) override { addressU_=addressU; addressV_=addressV; }
    // plan_metal.md METAL-182/183: previously entirely unimplemented (base no-op) -- `cna_v2d` had
    // no matrix uniform at all, so `SpriteBatch.Begin(transformMatrix)` had zero effect on Metal.
    // Applied as a 2D point transform (z=0) on the already-screen-space quad corners, matching the
    // same convention this plan's own research found `SOFTWARE`'s `SetTransformMatrix` already
    // uses -- CPU-side, not threaded through the vertex shader, so identity (the default) costs
    // nothing extra and is provably a no-op.
    void SetTransformMatrix(const Matrix& m) override { transform_=m; }
    void Draw(const ITextureBackend& t,float x,float y) override { Rectangle d((int)x,(int)y,t.GetWidth(),t.GetHeight()); Rectangle s(0,0,t.GetWidth(),t.GetHeight()); Draw(t,d,s,Color::White); }
    void Draw(const ITextureBackend& t,const Rectangle& d,const Rectangle& s,const Color& c) override { Draw(t,d,s,c,0,Vector2::Zero,SpriteEffects::None,0); }
    void Draw(const ITextureBackend& t,const Rectangle& d,const Rectangle& s,const Color& c,float rotation,const Vector2& origin,SpriteEffects effects,float) override
    {
        if(!begun_) throw std::runtime_error("Metal SpriteBatch.Draw called outside Begin/End");
        // plan_metal.md Phase 10: nativeTextureFor() (not a bare MetalTexture dynamic_cast) so
        // drawing a previously-rendered-to RenderTarget2D as a sprite works, not just a plain Texture2D.
        id<MTLTexture> nativeTex=nativeTextureFor(&t); if(!nativeTex) throw std::runtime_error("Metal: foreign texture backend");
        auto& p=b_.impl(); p.ensureFrame();
        struct V{float x,y,u,v,r,g,b,a;}; V q[6];
        float x0=(float)d.getXProperty(), y0=(float)d.getYProperty(), x1=x0+d.getWidthProperty(), y1=y0+d.getHeightProperty();
        float u0=(float)s.getXProperty()/t.GetWidth(), v0=(float)s.getYProperty()/t.GetHeight();
        float u1=(float)(s.getXProperty()+s.getWidthProperty())/t.GetWidth(), v1=(float)(s.getYProperty()+s.getHeightProperty())/t.GetHeight();
        if((int)effects & 1) std::swap(u0,u1); if((int)effects & 2) std::swap(v0,v1);
        const float cr=c.getRProperty()/255.f,cg=c.getGProperty()/255.f,cb=c.getBProperty()/255.f,ca=c.getAProperty()/255.f;
        auto xf=[&](float x,float y){ float px=x-(x0+origin.getXProperty()), py=y-(y0+origin.getYProperty()); float cs=std::cos(rotation),sn=std::sin(rotation); return std::array<float,2>{x0+origin.getXProperty()+px*cs-py*sn,y0+origin.getYProperty()+px*sn+py*cs};};
        auto tf=[&](std::array<float,2> q){ float x=q[0],y=q[1]; return std::array<float,2>{x*transform_.M11+y*transform_.M21+transform_.M41, x*transform_.M12+y*transform_.M22+transform_.M42}; };
        auto a=tf(xf(x0,y0)),bb=tf(xf(x1,y0)),cc=tf(xf(x1,y1)),dd=tf(xf(x0,y1));
        V vs[6]={{a[0],a[1],u0,v0,cr,cg,cb,ca},{bb[0],bb[1],u1,v0,cr,cg,cb,ca},{cc[0],cc[1],u1,v1,cr,cg,cb,ca},{a[0],a[1],u0,v0,cr,cg,cb,ca},{cc[0],cc[1],u1,v1,cr,cg,cb,ca},{dd[0],dd[1],u0,v1,cr,cg,cb,ca}};
        // plan_metal.md METAL-157/158: was raw physical-drawable-pixel NDC mapping (`{w,h}`),
        // ignoring virtual resolution/letterboxing entirely -- now the real scale+offset transform.
        auto st=p.computeSpriteTransform();
        struct U{float sx,sy,ox,oy;} u{st.scaleX,st.scaleY,st.offsetX,st.offsetY};
        [p.encoder setRenderPipelineState:p.getOrCreatePipeline(PipelineKind::Sprite2D)]; [p.encoder setVertexBytes:vs length:sizeof(vs) atIndex:0]; [p.encoder setVertexBytes:&u length:sizeof(u) atIndex:1];
        [p.encoder setFragmentTexture:nativeTex atIndex:0]; [p.encoder setFragmentSamplerState:p.samplerFor(filter_,addressU_,addressV_,1) atIndex:0]; [p.encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    }
private: MetalGraphicsBackend& b_; bool begun_=false; int filter_=0; int addressU_=1; int addressV_=1; Matrix transform_=Matrix::getIdentityProperty();
};

// plan_metal.md METAL-136-139: real occlusion queries via a shared MTLVisibilityResultBuffer slot
// per instance. `completed_` is a heap-allocated flag (not a plain bool member) because Objective-C
// completion-handler blocks capture it by reference into GPU-driven, asynchronously-invoked code
// that must outlive this object's own Begin()/End() call stack -- a std::shared_ptr keeps it alive
// exactly as long as either this object or the in-flight block still needs it.
//
// Documented scope limitation, not a hidden bug: Begin() registers its completion handler against
// whichever command buffer is active at that moment. If a Clear() call (which commits+waits
// synchronously, starting a fresh command buffer) happens between Begin() and End(), the
// visibility write ends up split across two command buffers and this simple single-handler
// design will not track completion correctly. Real game code's Begin()/End() pairs tightly
// bracket a small set of draws with no Clear() in between, so this is a real but narrow gap, not
// silently ignored -- flagged here rather than solved with a bigger multi-command-buffer design
// this pass didn't attempt.
class MetalOcclusionQueryBackend final : public IOcclusionQueryBackend
{
public:
    MetalOcclusionQueryBackend(MetalGraphicsBackend::Impl& owner, int slot) : owner_(owner), slot_(slot) {}
    void Begin() override
    {
        owner_.ensureFrame();
        [owner_.encoder setVisibilityResultMode:MTLVisibilityResultModeCounting offset:(NSUInteger)(slot_*8)];
        completed_ = std::make_shared<std::atomic<bool>>(false);
        auto flag = completed_;
        [owner_.command addCompletedHandler:^(id<MTLCommandBuffer> cb) { flag->store(true); }];
    }
    void End() override
    {
        // MTLVisibilityResultModeDisabled's offset argument is ignored by Metal but still
        // required by the method signature; 0 is the conventional value other Apple sample code
        // uses for the disable call.
        if (owner_.encoder) [owner_.encoder setVisibilityResultMode:MTLVisibilityResultModeDisabled offset:0];
    }
    bool IsComplete() const override { return completed_ && completed_->load(); }
    int PixelCount() const override
    {
        if (!IsComplete()) return 0;
        const auto* data = static_cast<const uint64_t*>([owner_.visibilityBuffer contents]);
        return (int)data[slot_];
    }
private:
    MetalGraphicsBackend::Impl& owner_;
    int slot_;
    std::shared_ptr<std::atomic<bool>> completed_;
};

// plan_metal.md METAL-131/122/125: shared blit-to-staging-buffer readback helper, used by
// MetalRenderTargetBackend/MetalRenderTargetCubeBackend/MetalTextureCube/MetalTexture3D's
// GetData() overrides instead of four near-duplicate implementations (matching the task's own
// "sharing one helper" framing). Uses an independent, freshly-created command buffer rather than
// the frame's own in-flight one -- correct because callers are responsible for ensuring the source
// texture's content is actually GPU-complete first (see each render-target GetData() override's
// own "if still the active render target, end its encoding first" guard, mirroring
// ReadBackbuffer()'s identical established reasoning; plain SetData()-populated textures have no
// such concern since nothing ever renders into them) -- Metal command buffers on one queue execute
// in submission order, so a blit submitted after the writes it depends on have already been
// committed is guaranteed to see their result.
//
// depth defaults to 1 (an ordinary 2D-region copy: RenderTarget2D, one RenderTargetCube/
// TextureCube face, or one Texture3D Z-slice at a time is still just a 2D blit region with a fixed
// z origin). destinationBytesPerImage is explicitly 0 whenever depth<=1 -- Apple's own
// documentation for this method states it is only meaningful (and only read) for a genuine
// multi-image copy (depth>1); passing a non-zero-but-otherwise-correct value for a single-image
// copy risks Metal's debug/validation layer flagging it as unused-but-invalid, so 0 is the
// unambiguously safe choice here rather than reusing the same byte count as destinationBytesPerRow.
static void blitTextureToClientBuffer(id<MTLDevice> device, id<MTLCommandQueue> queue, id<MTLTexture> src, NSUInteger slice, int level, int x, int y, int z, int w, int h, int depth, void* data, int dataLength)
{
    if (w<=0 || h<=0 || depth<=0) return;
    const NSUInteger bytesPerRow=(NSUInteger)w*4;
    const NSUInteger bytesPerImage=bytesPerRow*(NSUInteger)h;
    const NSUInteger length=bytesPerImage*(NSUInteger)depth;
    if ((int)length > dataLength) throw std::runtime_error("Metal: GetData buffer too small for the requested region");
    id<MTLBuffer> staging=[device newBufferWithLength:length options:MTLResourceStorageModeShared];
    if(!staging) throw std::runtime_error("Metal: GetData failed to allocate staging buffer");
    // Explicitly retained/released like every other command buffer in this file (p.command's own
    // convention), even though a purely local, synchronously-used-and-discarded command buffer would
    // be safe unretained too (autorelease pools only drain at pool-scope boundaries, never mid-call)
    // -- consistency over relying on that more subtle argument.
    id<MTLCommandBuffer> cmd=[queue commandBuffer]; [cmd retain];
    id<MTLBlitCommandEncoder> blit=[cmd blitCommandEncoder];
    [blit copyFromTexture:src sourceSlice:slice sourceLevel:(NSUInteger)level
              sourceOrigin:MTLOriginMake((NSUInteger)x,(NSUInteger)y,(NSUInteger)z)
                sourceSize:MTLSizeMake((NSUInteger)w,(NSUInteger)h,(NSUInteger)depth)
                  toBuffer:staging destinationOffset:0
    destinationBytesPerRow:bytesPerRow destinationBytesPerImage:(depth>1?bytesPerImage:0)];
    [blit endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted]; // plan_metal.md METAL-133: same intentional correctness-over-throughput stall as ReadBackbuffer().
    std::memcpy(data,[staging contents],length);
    [staging release];
    [cmd release];
}

// plan_metal.md Phase 10 (METAL-99/101/106): RenderTarget2D backend. Deliberately NOT yet
// implementing MSAA/mip (METAL-104, multiSampleCount is silently ignored, matching the interface's
// own established "backends that cannot honor a preference silently clamp" convention, not a hard
// requirement) or preserveContents-driven DontCare-on-rebind (METAL-102, always uses Load --
// correct, just not the optimization, see CreateRenderTarget2D's own note on why this is by design).
//
// Lifetime assumption, shared with MetalOcclusionQueryBackend and SdlGpuRenderTargetBackend's own
// identical owner_-back-pointer pattern (not a risk unique to this class): holds a plain reference
// to MetalGraphicsBackend::Impl, so it assumes the owning GraphicsDevice/backend outlives every
// RenderTarget2D created from it -- the standard XNA/FNA object-lifetime discipline (GraphicsDevice
// is the longest-lived graphics object), not independently enforced by the type system here.
class MetalRenderTargetBackend final : public IRenderTargetBackend
{
public:
    MetalRenderTargetBackend(MetalGraphicsBackend::Impl& owner, int w, int h, bool mipMap) : owner_(owner), w_(w), h_(h), mipMap_(mipMap)
    {
        // plan_metal.md METAL-101: MUST be BGRA8Unorm, matching every pipeline's own hardcoded
        // colorAttachments[0].pixelFormat (makePipeline(), keyed to the backbuffer's own format) --
        // Metal requires a render pipeline's declared color-attachment pixel format to exactly
        // match the render pass's real attachment texture, or drawing into this target with any
        // of this file's existing pipelines would be a real format mismatch (a Metal API
        // validation error, not just a style choice). Zero shader-visible difference from
        // RGBA8Unorm either way -- BGRA/RGBA pixel-format naming is about memory byte order only;
        // texture.sample() in MSL always presents components as .rgba regardless of storage order,
        // matching MetalTexture's own separate RGBA8Unorm choice for plain (non-render-target)
        // textures, which never needs to match a pipeline's color-attachment format at all.
        //
        // plan_metal.md METAL-103: `mipmapped:mipMap` makes this convenience initializer allocate
        // the full mip chain (mipmapLevelCount = floor(log2(max(w,h)))+1) when requested, matching
        // MTLTextureDescriptor's own documented behavior for this factory method.
        MTLTextureDescriptor* cd=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:(NSUInteger)w height:(NSUInteger)h mipmapped:mipMap];
        cd.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
        colorTexture_=[owner_.device newTextureWithDescriptor:cd];
        if(!colorTexture_) throw std::runtime_error("Metal: failed to create RenderTarget2D color texture");
        MTLTextureDescriptor* dd=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:(NSUInteger)w height:(NSUInteger)h mipmapped:NO];
        dd.storageMode=MTLStorageModePrivate; dd.usage=MTLTextureUsageRenderTarget;
        depthTexture_=[owner_.device newTextureWithDescriptor:dd];
        if(!depthTexture_) throw std::runtime_error("Metal: failed to create RenderTarget2D depth texture");
    }
    ~MetalRenderTargetBackend() override
    {
        // If this target is still bound when destroyed (the game forgot to SetRenderTarget2D(null)
        // first, or just let it go out of scope), end the active encoding BEFORE releasing the
        // underlying textures -- otherwise Impl::encoder/command would be left referencing textures
        // about to be freed, a real use-after-free/dangling-attachment risk, not just untidy state.
        // false: destruction mid-frame must never present -- see endActiveEncoding()'s METAL-180 note.
        if (owner_.currentRenderTarget==this) { owner_.endActiveEncoding(false); owner_.currentRenderTarget=nullptr; }
        [depthTexture_ release]; [colorTexture_ release];
    }
    int GetWidth() const override { return w_; }
    int GetHeight() const override { return h_; }
    SDL_Texture* GetNativeTexture() const override { return nullptr; }
    void BindAsRenderTarget() override
    {
        owner_.endActiveEncoding(false); // mid-frame switch, never presents -- see METAL-180 note.
        owner_.currentRenderTarget=this;
    }
    void UnbindAsRenderTarget() override
    {
        if (owner_.currentRenderTarget==this) {
            // plan_metal.md METAL-103: regenerate the full mip chain from level 0's just-rendered
            // content on every unbind when mipMap was requested, unconditionally -- matches
            // EasyGLRenderTargetBackend::UnbindAsRenderTarget()'s own established precedent exactly
            // (itself citing FNA3D's OPENGL_ResolveTarget: "if (target->levelCount > 1) { ...
            // glGenerateMipmap... }"), not gated on whether anything was actually drawn this bind
            // session -- EasyGL's glGenerateMipmap call isn't gated on that either.
            if (mipMap_) {
                if (owner_.encoder) { [owner_.encoder endEncoding]; [owner_.encoder release]; owner_.encoder=nil; }
                // A blit encoder needs a real command buffer to encode into; if nothing was ever
                // drawn/cleared this bind session owner_.command is still nil (ensureFrame() was
                // never called) -- create one just for the blit, mirroring ReadBackbuffer()'s own
                // precedent of using a small standalone command buffer for a one-off GPU operation.
                if (!owner_.command) { owner_.command=[owner_.queue commandBuffer]; [owner_.command retain]; }
                id<MTLBlitCommandEncoder> blit=[owner_.command blitCommandEncoder];
                [blit generateMipmapsForTexture:colorTexture_];
                [blit endEncoding];
            }
            owner_.endActiveEncoding(false); owner_.currentRenderTarget=nullptr;
        }
    }
    // plan_metal.md METAL-131: real readback via the shared blit helper, replacing ITextureBackend's
    // inherited no-op default. If this target is still the currently active render target, its
    // pending command buffer is forced to actually execute first -- same reasoning as
    // ReadBackbuffer()'s own established "end the current encoder before blitting" pattern (a fresh,
    // independent command buffer's blit could otherwise run before the still-uncommitted render pass
    // that wrote this content, reading stale/undefined data instead of what was just rendered).
    void GetData(int level,int x,int y,int w,int h,void* data,int dataLength) const override
    {
        if (owner_.currentRenderTarget==this) owner_.endActiveEncoding(false);
        blitTextureToClientBuffer(owner_.device, owner_.queue, colorTexture_, 0, level, x, y, 0, w, h, 1, data, dataLength);
    }
    id<MTLTexture> colorTexture() const { return colorTexture_; }
    id<MTLTexture> depthTextureNative() const { return depthTexture_; }
private:
    MetalGraphicsBackend::Impl& owner_;
    int w_, h_;
    bool mipMap_;
    id<MTLTexture> colorTexture_=nil;
    id<MTLTexture> depthTexture_=nil;
};

// plan_metal.md METAL-109/110/111: RenderTargetCube backend. A single MTLTextureTypeCube color
// texture (6 slices) plus ONE shared 2D depth texture reused across every face -- matches
// EasyGLRenderTargetCubeBackend's own already-tested precedent exactly (its own comment: "single
// depth renderbuffer regardless of face (since only one face is ever rendered into at a time) --
// matches FNA's RenderTargetCube.cs, which also allocates a single glColorBuffer regardless of
// face"), not a Metal-specific shortcut. Per-face binding uses
// MTLRenderPassColorAttachmentDescriptor.slice (see resolveActiveAttachments() below) rather than
// 6 separate 2D texture views, mirroring MetalTextureCube's own existing SetData(face,...) ->
// replaceRegion:...slice:face convention (face ordinals already match Metal's own cube slice
// order, see MetalTextureCube's own comment on this).
class MetalRenderTargetCubeBackend final : public IRenderTargetCubeBackend
{
public:
    MetalRenderTargetCubeBackend(MetalGraphicsBackend::Impl& owner, int size, bool mipMap) : owner_(owner), size_(size), mipMap_(mipMap)
    {
        MTLTextureDescriptor* cd=[MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm size:(NSUInteger)size mipmapped:mipMap];
        cd.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
        colorTexture_=[owner_.device newTextureWithDescriptor:cd];
        if(!colorTexture_) throw std::runtime_error("Metal: failed to create RenderTargetCube color texture");
        MTLTextureDescriptor* dd=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:(NSUInteger)size height:(NSUInteger)size mipmapped:NO];
        dd.storageMode=MTLStorageModePrivate; dd.usage=MTLTextureUsageRenderTarget;
        depthTexture_=[owner_.device newTextureWithDescriptor:dd];
        if(!depthTexture_) throw std::runtime_error("Metal: failed to create RenderTargetCube depth texture");
    }
    ~MetalRenderTargetCubeBackend() override
    {
        // Same reasoning as MetalRenderTargetBackend's own destructor: end any still-active
        // encoding referencing these textures before releasing them.
        if (owner_.currentRenderTargetCube==this) { owner_.endActiveEncoding(false); owner_.currentRenderTargetCube=nullptr; }
        [depthTexture_ release]; [colorTexture_ release];
    }
    int GetSize() const override { return size_; }
    void BindAsRenderTargetFace(int face) override
    {
        owner_.endActiveEncoding(false); // mid-frame switch, never presents -- see METAL-180 note.
        owner_.currentRenderTargetCube=this;
        owner_.currentRenderTargetCubeFace=(NSUInteger)face;
    }
    void UnbindAsRenderTarget() override
    {
        if (owner_.currentRenderTargetCube==this) {
            // plan_metal.md METAL-103 (cube analog): same unconditional mip-regeneration-on-unbind
            // policy as MetalRenderTargetBackend's own, applied to the whole cube map at once (all
            // 6 faces' mip chains regenerate together in one generateMipmapsForTexture: call --
            // Metal's own documented behavior for a cube texture, matching
            // EasyGLRenderTargetCubeBackend::UnbindAsRenderTarget's own single glGenerateMipmap
            // call over the whole cube map, not a separate call per face).
            if (mipMap_) {
                if (owner_.encoder) { [owner_.encoder endEncoding]; [owner_.encoder release]; owner_.encoder=nil; }
                if (!owner_.command) { owner_.command=[owner_.queue commandBuffer]; [owner_.command retain]; }
                id<MTLBlitCommandEncoder> blit=[owner_.command blitCommandEncoder];
                [blit generateMipmapsForTexture:colorTexture_];
                [blit endEncoding];
            }
            owner_.endActiveEncoding(false);
            owner_.currentRenderTargetCube=nullptr;
        }
    }
    // plan_metal.md METAL-131 (cube analog): same shared-helper readback and same "flush pending
    // encoding first if still active" guard as MetalRenderTargetBackend::GetData() -- checked
    // against currentRenderTargetCube==this regardless of which face, since every face shares the
    // same underlying MTLTexture object and command-buffer ordering concern.
    void GetData(int face,int level,int x,int y,int w,int h,void* data,int dataLength) const override
    {
        if (face<0||face>=6) return;
        if (owner_.currentRenderTargetCube==this) owner_.endActiveEncoding(false);
        blitTextureToClientBuffer(owner_.device, owner_.queue, colorTexture_, (NSUInteger)face, level, x, y, 0, w, h, 1, data, dataLength);
    }
    id<MTLTexture> colorTexture() const { return colorTexture_; }
    id<MTLTexture> depthTextureNative() const { return depthTexture_; }
private:
    MetalGraphicsBackend::Impl& owner_;
    int size_;
    bool mipMap_;
    id<MTLTexture> colorTexture_=nil;
    id<MTLTexture> depthTexture_=nil;
};

bool MetalGraphicsBackend::Impl::resolveActiveAttachments(id<MTLTexture>& colorOut, id<MTLTexture>& depthOut, NSUInteger& sliceOut)
{
    sliceOut = 0;
    if (currentRenderTarget) {
        colorOut = currentRenderTarget->colorTexture();
        depthOut = currentRenderTarget->depthTextureNative();
        return true;
    }
    if (currentRenderTargetCube) {
        colorOut = currentRenderTargetCube->colorTexture();
        depthOut = currentRenderTargetCube->depthTextureNative();
        sliceOut = currentRenderTargetCubeFace;
        return true;
    }
    if (!drawable) drawable=[layer nextDrawable];
    if (!drawable) return false;
    colorOut = drawable.texture;
    const NSUInteger w=drawable.texture.width, h=drawable.texture.height;
    if(!depthTexture || depthTexture.width!=w || depthTexture.height!=h){
        [depthTexture release];
        MTLTextureDescriptor* dd=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:w height:h mipmapped:NO];
        dd.storageMode=MTLStorageModePrivate; dd.usage=MTLTextureUsageRenderTarget;
        depthTexture=[device newTextureWithDescriptor:dd];
    }
    depthOut = depthTexture;
    return true;
}

MetalGraphicsBackend::Impl::Sprite2DTransform MetalGraphicsBackend::Impl::computeSpriteTransform() const
{
    Sprite2DTransform t{};
    if (currentRenderTarget) {
        // An offscreen RenderTarget2D's own pixel space IS the logical space -- no
        // window-relative letterbox scaling applies at all (computeLogicalViewport() queries the
        // real window size via SDL, which has nothing to do with an offscreen texture's own
        // coordinate space); this is a real, deliberate 1:1 mapping, not a shortcut.
        id<MTLTexture> rtColor = currentRenderTarget->colorTexture();
        const float dw=(float)rtColor.width, dh=(float)rtColor.height;
        if (dw<=0 || dh<=0) return t;
        t.scaleX = 2.0f/dw; t.offsetX = -1.0f;
        t.scaleY = -2.0f/dh; t.offsetY = 1.0f;
        return t;
    }
    if (currentRenderTargetCube) {
        // Same 1:1 reasoning as the RenderTarget2D branch above -- sprite drawing into a bound cube
        // face (e.g. rendering a 2D overlay/skybox-adjacent element directly into a reflection
        // probe face) uses that face's own size x size pixel space directly.
        id<MTLTexture> rtColor = currentRenderTargetCube->colorTexture();
        const float dw=(float)rtColor.width, dh=(float)rtColor.height;
        if (dw<=0 || dh<=0) return t;
        t.scaleX = 2.0f/dw; t.offsetX = -1.0f;
        t.scaleY = -2.0f/dh; t.offsetY = 1.0f;
        return t;
    }
    const float dw=(float)drawable.texture.width, dh=(float)drawable.texture.height;
    if (dw<=0 || dh<=0) return t;
    LogicalViewport vp = computeLogicalViewport();
    if (vp.logicalWidth<=0 || vp.logicalHeight<=0) return t;
    t.scaleX = 2.0f*vp.width/(vp.logicalWidth*dw);
    t.offsetX = 2.0f*vp.x/dw - 1.0f;
    t.scaleY = -2.0f*vp.height/(vp.logicalHeight*dh);
    t.offsetY = 1.0f - 2.0f*vp.y/dh;
    return t;
}

static id<MTLTexture> nativeTextureFor(const ITextureBackend* t)
{
    if (!t) return nil;
    if (auto* mt = dynamic_cast<const MetalTexture*>(t)) return mt->native();
    if (auto* rt = dynamic_cast<const MetalRenderTargetBackend*>(t)) return rt->colorTexture();
    return nil;
}

static id<MTLTexture> nativeCubeTextureFor(const ITextureCubeBackend* t)
{
    if (!t) return nil;
    if (auto* mt = dynamic_cast<const MetalTextureCube*>(t)) return mt->native();
    if (auto* rt = dynamic_cast<const MetalRenderTargetCubeBackend*>(t)) return rt->colorTexture();
    return nil;
}

MetalGraphicsBackend::MetalGraphicsBackend(const GraphicsBackendCreateArgs& args):impl_(std::make_unique<Impl>())
{
    auto& p=*impl_; p.window=args.window; p.virtualW=args.virtualWidth; p.virtualH=args.virtualHeight; p.swapInterval=args.swapInterval;
    // plan_metal.md Phase 15: real, previously-invisible bug -- args.presentationMode was never
    // read at all (Impl::presentationMode's own field default, Letterbox=0, silently won this
    // instead), even though GraphicsBackendCreateArgs::presentationMode's own doc comment states
    // its default is FixedHeightDynamicWidth (XNA/Windows-Phone-matching) and SdlGpu/EasyGL's own
    // constructors both already forward it correctly. Had zero observable effect before this
    // phase since nothing consumed `presentationMode` yet; matters now that computeLogicalViewport()
    // does.
    p.presentationMode=(int)args.presentationMode;
    if(!p.window) throw std::runtime_error("Metal backend requires an SDL_Window");
    p.device=MTLCreateSystemDefaultDevice(); if(!p.device) throw std::runtime_error("Metal: MTLCreateSystemDefaultDevice failed"); [p.device retain];
    p.view=SDL_Metal_CreateView(p.window); if(!p.view) throw std::runtime_error(std::string("Metal: SDL_Metal_CreateView failed: ")+SDL_GetError());
    p.layer=(CAMetalLayer*)SDL_Metal_GetLayer(p.view); [p.layer retain]; p.layer.device=p.device; p.layer.pixelFormat=MTLPixelFormatBGRA8Unorm; p.layer.framebufferOnly=NO;
    // plan_metal.md METAL-168: swapInterval was previously stored but never applied -- CAMetalLayer
    // has no direct integer-interval knob (unlike SDL_GL_SetSwapInterval's 0/1/-1 or Vulkan's
    // present-mode choice, both real per-value behavior elsewhere in this codebase), only the
    // boolean displaySyncEnabled. XNA PresentInterval::Immediate(0) -> NO (uncapped); One(1, the
    // default)/Two(2) both -> YES (real, honest vsync) since Metal has no true half-rate present
    // mode to map Two to -- an approximation, not a silent gap, and documented as such rather than
    // pretending Two behaves differently from One.
    p.layer.displaySyncEnabled = (p.swapInterval != 0);
    p.queue=[p.device newCommandQueue];
    NSError* err=nil; NSString* src=[NSString stringWithUTF8String:kMetalShaderSource]; p.library=[p.device newLibraryWithSource:src options:nil error:&err];
    if(!p.library) throw std::runtime_error(std::string("Metal shader compile failed: ")+([[err localizedDescription] UTF8String]?:"unknown"));
    // plan_metal.md METAL-23: pipelines are no longer built eagerly here -- getOrCreatePipeline()
    // lazily builds+caches each (PipelineKind, BlendKey) combination on first use instead.
    MTLSamplerDescriptor* sd=[[MTLSamplerDescriptor alloc]init];sd.minFilter=MTLSamplerMinMagFilterLinear;sd.magFilter=MTLSamplerMinMagFilterLinear;sd.sAddressMode=MTLSamplerAddressModeClampToEdge;sd.tAddressMode=MTLSamplerAddressModeClampToEdge;p.sampler=[p.device newSamplerStateWithDescriptor:sd];[sd release];
    // plan_metal.md METAL-136: visibility-result buffer for real occlusion queries, attached to
    // every render pass from here on (must exist at render-pass-creation time, see its own field
    // comment on Impl).
    p.visibilityBuffer=[p.device newBufferWithLength:(NSUInteger)(MetalGraphicsBackend::Impl::kMaxOcclusionQuerySlots*8) options:MTLResourceStorageModeShared];
    // plan_metal.md METAL-87: PbrEffect's 4 optional-map fallback textures (see Impl's own field
    // comment for why these exact 2 colors).
    {
        MTLTextureDescriptor* td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1 height:1 mipmapped:NO];
        td.usage=MTLTextureUsageShaderRead;
        p.defaultWhiteTexture=[p.device newTextureWithDescriptor:td];
        const uint8_t white[4]={255,255,255,255};
        [p.defaultWhiteTexture replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:white bytesPerRow:4];
        p.defaultFlatNormalTexture=[p.device newTextureWithDescriptor:td];
        const uint8_t flatNormal[4]={128,128,255,255};
        [p.defaultFlatNormalTexture replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:flatNormal bytesPerRow:4];
    }
    p.rebuildDepthState();
}
MetalGraphicsBackend::~MetalGraphicsBackend(){auto&p=*impl_;p.endFrame();for(auto& kv:p.samplerCache)[kv.second release];p.samplerCache.clear();for(auto& kv:p.pipelineCache)[kv.second release];p.pipelineCache.clear();[p.defaultFlatNormalTexture release];[p.defaultWhiteTexture release];[p.visibilityBuffer release];[p.depthTexture release];[p.depthState release];[p.sampler release];[p.library release];[p.queue release];[p.layer release];if(p.view)SDL_Metal_DestroyView(p.view);[p.device release];}
MetalGraphicsBackend::Impl& MetalGraphicsBackend::impl(){return *impl_;} const MetalGraphicsBackend::Impl& MetalGraphicsBackend::impl()const{return *impl_;}
void MetalGraphicsBackend::Clear(float r,float g,float b,float a){impl_->clear(true,r,g,b,a,false,1,false,0);} void MetalGraphicsBackend::Present(){impl_->endFrame();}
void MetalGraphicsBackend::GetViewportSize(int&w,int&h){
    // plan_metal.md METAL-156: now routed through the same computeLogicalViewport() the real
    // window<->logical transforms use, instead of a separate, simpler ad hoc formula. Verified by
    // hand to produce byte-identical results to the old `virtualW>0?virtualW:pw` formula for every
    // mode except FixedHeightDynamicWidth (Letterbox/Overscan/Stretch/NativeBackBuffer all set
    // vp.logicalWidth/Height to the raw virtualW/virtualH before any aspect-ratio math runs) --
    // FixedHeightDynamicWidth now correctly derives logical width from the real surface aspect
    // ratio instead of returning virtualW unconditionally, matching its own documented contract.
    auto vp=impl_->computeLogicalViewport();
    w=(int)std::lround(vp.logicalWidth); h=(int)std::lround(vp.logicalHeight);
}
void MetalGraphicsBackend::SetVirtualResolution(int w,int h){impl_->virtualW=w;impl_->virtualH=h;} void MetalGraphicsBackend::SetPresentationMode(int m){impl_->presentationMode=m;}
void MetalGraphicsBackend::SetSwapInterval(int i){impl_->swapInterval=i;impl_->layer.displaySyncEnabled=(i!=0);} // plan_metal.md METAL-168, same mapping as the constructor.
bool MetalGraphicsBackend::TransformWindowToLogical(float windowX,float windowY,float& logX,float& logY) const{return impl_->transformWindowToLogical(windowX,windowY,logX,logY);}
bool MetalGraphicsBackend::TransformLogicalToWindow(float logX,float logY,float& windowX,float& windowY) const{return impl_->transformLogicalToWindow(logX,logY,windowX,windowY);}
// plan_metal.md METAL-130: real ReadBackbuffer, previously unimplemented (base default throws).
// x,y are top-left in game/physical-drawable coordinates, pixels is top-down RGBA8 -- confirmed
// against EasyGLGraphicsBackend::ReadBackbuffer's own documented contract, but Metal (unlike GL,
// whose framebuffer origin is bottom-left) needs NO row-order Y-flip at all: Metal's own texture
// origin is already top-left, matching D3D/XNA convention directly, the same reason
// MTLRegionMake2D()-based texture uploads elsewhere in this file never need one either.
//
// Documented tradeoff, not a hidden bug: since the drawable's color texture lives in
// MTLStorageModePrivate (GPU-only) memory, the only way to get it CPU-readable is a blit into a
// MTLResourceStorageModeShared staging buffer within the SAME command buffer, which then has to
// be committed and waited on -- and once a command buffer is committed there is no way to resume
// encoding into it for a later, separate Present(). So this function ends the current encoder,
// blits, and commits+presents+waits as one unit (i.e., behaves like an early, forced end-of-frame)
// -- the game's own subsequent Present() call becomes a safe no-op (Impl::endFrame()'s own
// `if(!command) return;` guard) rather than a double-present. Net effect: calling ReadBackbuffer
// mid-Draw() ends that frame slightly earlier than the game intended -- a real, minor, documented
// behavioral quirk, not a memory-safety or correctness bug.
void MetalGraphicsBackend::ReadBackbuffer(int x,int y,int w,int h,uint8_t* pixels)
{
    if (w<=0 || h<=0) return;
    auto& p=*impl_;
    // plan_metal.md Phase 10: real bug found and fixed while adding RenderTarget2D support --
    // ReadBackbuffer (implemented in an earlier pass, before RenderTarget2D existed) assumed
    // p.drawable is always valid right after ensureFrame(); it is not when a RenderTarget2D is
    // currently bound (resolveActiveAttachments()'s render-target branch never touches drawable at
    // all), which would have made `p.drawable.texture` a message-to-nil blit source and, worse,
    // `presentDrawable:` with a nil argument below -- a real Metal API misuse, likely an
    // Objective-C exception, not silent garbage. ReadBackbuffer's own name and contract are
    // specifically about the backbuffer (RenderTarget2D's own readback is GetData(), METAL-131,
    // still open) -- so this throws a clear, honest error instead of the game restoring the
    // backbuffer implicitly or silently reading the wrong surface.
    // plan_metal.md METAL-109: same guard, extended -- a bound RenderTargetCube face is exactly the
    // same hazard as a bound RenderTarget2D (resolveActiveAttachments()'s cube branch never touches
    // drawable either), found while re-reading this function against the newly-added cube branch
    // rather than assuming the original RenderTarget2D-only guard still covered every case.
    if (p.currentRenderTarget || p.currentRenderTargetCube)
        throw std::runtime_error("Metal: ReadBackbuffer requires the backbuffer to be the active render target (call SetRenderTarget2D(null) first)");
    p.ensureFrame();
    id<MTLTexture> src = p.drawable.texture;
    if (p.encoder) { [p.encoder endEncoding]; [p.encoder release]; p.encoder=nil; }
    const NSUInteger bytesPerRow=(NSUInteger)w*4;
    const NSUInteger length=bytesPerRow*(NSUInteger)h;
    id<MTLBuffer> staging=[p.device newBufferWithLength:length options:MTLResourceStorageModeShared];
    if(!staging) throw std::runtime_error("Metal: ReadBackbuffer failed to allocate staging buffer");
    id<MTLBlitCommandEncoder> blit=[p.command blitCommandEncoder];
    [blit copyFromTexture:src sourceSlice:0 sourceLevel:0
              sourceOrigin:MTLOriginMake((NSUInteger)x,(NSUInteger)y,0)
                sourceSize:MTLSizeMake((NSUInteger)w,(NSUInteger)h,1)
                  toBuffer:staging destinationOffset:0
    destinationBytesPerRow:bytesPerRow destinationBytesPerImage:length];
    [blit endEncoding];
    [p.command presentDrawable:p.drawable];
    [p.command commit];
    [p.command waitUntilCompleted];
    std::memcpy(pixels,[staging contents],length);
    [staging release];
    [p.command release]; p.command=nil; p.drawable=nil;
}
SDL_Window* MetalGraphicsBackend::GetWindowInternal()const{return impl_->window;} SDL_Renderer* MetalGraphicsBackend::GetRendererInternal()const{return nullptr;}
std::unique_ptr<ITextureBackend> MetalGraphicsBackend::CreateTexture(const ImageData& d){return std::make_unique<MetalTexture>(impl_->device,d);} std::unique_ptr<ISpriteBatchBackend> MetalGraphicsBackend::CreateSpriteBatch(){return std::make_unique<MetalSpriteBatch>(*this);}
std::unique_ptr<ITextureCubeBackend> MetalGraphicsBackend::CreateTextureCube(int size,bool mipMap,int /*surfaceFormat*/){return std::make_unique<MetalTextureCube>(impl_->device,impl_->queue,size,mipMap);}
std::unique_ptr<IOcclusionQueryBackend> MetalGraphicsBackend::CreateOcclusionQuery()
{
    auto& p=*impl_;
    if(p.nextQuerySlot>=MetalGraphicsBackend::Impl::kMaxOcclusionQuerySlots)
        throw std::runtime_error("Metal: exceeded the maximum number of live OcclusionQuery slots (plan_metal.md METAL-136)");
    return std::make_unique<MetalOcclusionQueryBackend>(p, p.nextQuerySlot++);
}
std::unique_ptr<ITexture3DBackend> MetalGraphicsBackend::CreateTexture3D(int w,int h,int depth,bool mipMap,int /*surfaceFormat*/){return std::make_unique<MetalTexture3D>(impl_->device,impl_->queue,w,h,depth,mipMap);}
// plan_metal.md METAL-102: preserveContents is deliberately NOT threaded into the backend at all --
// GraphicsDevice::SetRenderTarget() already implements RenderTargetUsage.DiscardContents entirely
// at the shared layer (an explicit Clear() call right after binding, see its own real code), and
// does nothing extra for PreserveContents, which ensureFrame()'s existing unconditional
// MTLLoadActionLoad already satisfies correctly. EasyGLRenderTargetBackend::CreateRenderTarget2D
// ignores this same parameter for the identical reason (its own commented-out parameter name) --
// not a Metal-specific gap, matches established cross-backend precedent exactly.
std::unique_ptr<IRenderTargetBackend> MetalGraphicsBackend::CreateRenderTarget2D(int w,int h,int /*depthFormat*/,bool /*preserveContents*/,bool mipMap,int /*multiSampleCount*/)
{
    return std::make_unique<MetalRenderTargetBackend>(*impl_, w, h, mipMap);
}
// plan_metal.md METAL-109: always allocates depth32+stencil8 and ignores multiSampleCount, matching
// CreateRenderTarget2D's own already-documented simplification exactly (METAL-101's own note) --
// not a new, cube-specific gap.
std::unique_ptr<IRenderTargetCubeBackend> MetalGraphicsBackend::CreateRenderTargetCube(int size,int /*depthFormat*/,bool mipMap,int /*multiSampleCount*/)
{
    return std::make_unique<MetalRenderTargetCubeBackend>(*impl_, size, mipMap);
}
// plan_metal.md METAL-109/110/111 (real bug fixed alongside RenderTargetCube's own addition): both
// SetRenderTarget2D() and the new SetRenderTargetCubeFace() below must cross-unbind whichever OTHER
// kind of render target (2D vs. cube) is currently active before binding a new one -- mirroring
// EasyGLGraphicsBackend::SetRenderTarget2D/SetRenderTargetCubeFace's own real, already-tested
// contract exactly (both cross-check `currentRt2D_`/`currentRtCube_` against each other). Without
// this, switching directly from RenderTarget A to RenderTarget B (a normal XNA multi-pass pattern --
// no intervening SetRenderTarget2D(null) call) would never call UnbindAsRenderTarget() on A, so A's
// mip chain (METAL-103) would silently never regenerate. This gap was latent and harmless before
// METAL-103 existed (nothing depended on UnbindAsRenderTarget() actually running) but became a real
// correctness bug the moment mip generation started living there -- fixed here as part of the same
// pass that made it observable, not left for a later phase to rediscover.
void MetalGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
{
    auto& p=*impl_;
    if (p.currentRenderTarget && p.currentRenderTarget != rt) p.currentRenderTarget->UnbindAsRenderTarget();
    if (p.currentRenderTargetCube) p.currentRenderTargetCube->UnbindAsRenderTarget();
    if (rt) static_cast<MetalRenderTargetBackend*>(rt)->BindAsRenderTarget();
}
void MetalGraphicsBackend::SetRenderTargetCubeFace(IRenderTargetCubeBackend* rt,int face)
{
    if (!rt) { SetRenderTarget2D(nullptr); return; }
    auto& p=*impl_;
    if (p.currentRenderTarget) p.currentRenderTarget->UnbindAsRenderTarget();
    if (p.currentRenderTargetCube && p.currentRenderTargetCube != rt) p.currentRenderTargetCube->UnbindAsRenderTarget();
    static_cast<MetalRenderTargetCubeBackend*>(rt)->BindAsRenderTargetFace(face);
}
void MetalGraphicsBackend::ClearColorAndDepth(float r,float g,float b,float a,float d){impl_->clear(true,r,g,b,a,true,d,false,0);} void MetalGraphicsBackend::ClearDepth(float d){impl_->clear(false,0,0,0,0,true,d,false,0);} void MetalGraphicsBackend::ClearStencil(int s){impl_->clear(false,0,0,0,0,false,1,true,s);} void MetalGraphicsBackend::ClearDepthAndStencil(float d,int s){impl_->clear(false,0,0,0,0,true,d,true,s);} void MetalGraphicsBackend::ClearColorAndStencil(float r,float g,float b,float a,int s){impl_->clear(true,r,g,b,a,false,1,true,s);} void MetalGraphicsBackend::ClearColorDepthAndStencil(float r,float g,float b,float a,float d,int s){impl_->clear(true,r,g,b,a,true,d,true,s);}
void MetalGraphicsBackend::SetDepthTestEnabled(bool e){impl_->depthEnabled=e;impl_->rebuildDepthState();} void MetalGraphicsBackend::SetBlendEnabled(bool e){impl_->blendEnabled=e;} void MetalGraphicsBackend::SetDepthWriteEnabled(bool e){impl_->depthWrite=e;impl_->rebuildDepthState();}
void MetalGraphicsBackend::ApplyBlendState(int colorSrcBlend,int alphaSrcBlend,int colorDstBlend,int alphaDstBlend,int colorBlendFunc,int alphaBlendFunc)
{
    // plan_metal.md METAL-6/24: real per-BlendState pipeline selection, replacing the previous
    // complete no-op (every pipeline was hardcoded to a fixed straight-alpha blend regardless of
    // the actual requested BlendState). `enabled` derivation mirrors
    // EasyGLGraphicsBackend::ApplyBlendState's identical Blend::One/Blend::Zero Opaque-preset
    // check exactly (Blend::One=0, Blend::Zero=1).
    auto& p=*impl_;
    p.currentBlend.colorSrc=(uint8_t)colorSrcBlend; p.currentBlend.colorDst=(uint8_t)colorDstBlend;
    p.currentBlend.alphaSrc=(uint8_t)alphaSrcBlend; p.currentBlend.alphaDst=(uint8_t)alphaDstBlend;
    p.currentBlend.colorFunc=(uint8_t)colorBlendFunc; p.currentBlend.alphaFunc=(uint8_t)alphaBlendFunc;
    p.currentBlend.enabled = !(colorSrcBlend==0 && colorDstBlend==1 && alphaSrcBlend==0 && alphaDstBlend==1);
}
void MetalGraphicsBackend::ApplyDepthStencilState(bool depthEnable,bool depthWriteEnable,int depthFunc,
                                                   bool stencilEnable,int stencilFunc,int stencilPass,int stencilFail,int stencilDepthFail,
                                                   int stencilMask,int stencilWriteMask,int referenceStencil,
                                                   bool twoSidedStencilMode,int ccwStencilFunc,int ccwStencilPass,int ccwStencilFail,int ccwStencilDepthFail)
{
    // plan_metal.md METAL-7/9/10: real depthFunc + full front/back stencil-op wiring, replacing
    // the previous depthEnable/depthWrite/referenceStencil-only plumbing (depthFunc and all 8
    // stencil-op/mask/twoSided fields were silently ignored before this).
    auto& p=*impl_;
    p.depthEnabled=depthEnable; p.depthWrite=depthWriteEnable; p.depthFunc=depthFunc;
    p.stencilEnabled=stencilEnable; p.stencilFunc=stencilFunc; p.stencilPass=stencilPass;
    p.stencilFail=stencilFail; p.stencilDepthFail=stencilDepthFail;
    p.stencilMask=stencilMask; p.stencilWriteMask=stencilWriteMask;
    p.twoSidedStencil=twoSidedStencilMode; p.ccwStencilFunc=ccwStencilFunc; p.ccwStencilPass=ccwStencilPass;
    p.ccwStencilFail=ccwStencilFail; p.ccwStencilDepthFail=ccwStencilDepthFail;
    p.refStencil=referenceStencil;
    p.rebuildDepthState();
    if(p.encoder)[p.encoder setStencilReferenceValue:referenceStencil];
}
void MetalGraphicsBackend::ApplyRasterizerState(int c,int f,bool se,float db,float sb){impl_->cull=c==1?MTLCullModeFront:(c==2?MTLCullModeBack:MTLCullModeNone);impl_->fill=f==1?MTLTriangleFillModeLines:MTLTriangleFillModeFill;impl_->scissorEnabled=se;impl_->depthBias=db;impl_->slopeBias=sb;if(impl_->encoder){[impl_->encoder setFrontFacingWinding:MTLWindingClockwise];[impl_->encoder setCullMode:impl_->cull];[impl_->encoder setTriangleFillMode:impl_->fill];[impl_->encoder setDepthBias:db slopeScale:sb clamp:0];}}
void MetalGraphicsBackend::ApplySamplerState(int slot,int filter,int addressU,int addressV,int maxAnisotropy){if(slot<0||slot>=16)return;impl_->samplerSlots[slot]=impl_->samplerFor(filter,addressU,addressV,maxAnisotropy);}
void MetalGraphicsBackend::SetBlendFactor(float r,float g,float b,float a){impl_->blendColor[0]=r;impl_->blendColor[1]=g;impl_->blendColor[2]=b;impl_->blendColor[3]=a;if(impl_->encoder)[impl_->encoder setBlendColorRed:r green:g blue:b alpha:a];}
void MetalGraphicsBackend::SetReferenceStencil(int v){impl_->refStencil=v;if(impl_->encoder)[impl_->encoder setStencilReferenceValue:v];}
void MetalGraphicsBackend::SetScissorRect(int x,int y,int w,int h){impl_->scissor={(NSUInteger)std::max(0,x),(NSUInteger)std::max(0,y),(NSUInteger)std::max(0,w),(NSUInteger)std::max(0,h)};if(impl_->encoder)[impl_->encoder setScissorRect:impl_->scissor];}
void MetalGraphicsBackend::SetViewport(int x,int y,int w,int h,float mn,float mx){impl_->viewport={(double)x,(double)y,(double)w,(double)h,mn,mx};if(impl_->encoder)[impl_->encoder setViewport:impl_->viewport];}
std::unique_ptr<IVertexBufferBackend> MetalGraphicsBackend::CreateVertexBuffer(int c){return std::make_unique<MetalVertexBuffer>(impl_->device,c);} std::unique_ptr<IIndexBufferBackend> MetalGraphicsBackend::CreateIndexBuffer16(int){return std::make_unique<MetalIndexBuffer>(impl_->device,false);} std::unique_ptr<IIndexBufferBackend> MetalGraphicsBackend::CreateIndexBuffer32(int){return std::make_unique<MetalIndexBuffer>(impl_->device,true);}

// plan_metal.md METAL-29/55/61/69/78: dispatch precedence deliberately mirrors
// EasyGLGraphicsBackend::SelectProgram()'s own top-of-function order (pbr+skinned -> pbr ->
// skinned -> envMapping -> dualTexture -> plain stride switch). `pbr && skinned` (SkinnedPbrEffect)
// throws a clear, honest "not implemented" error rather than silently falling through to a
// non-skinned PBR shader -- that combination hasn't landed, and rendering the wrong (unskinned)
// result silently would be worse than an exception.
static PipelineKind selectPipelineKind(std::size_t stride, const GpuDrawParams* params)
{
    const bool textured = params && params->texture0;
    const bool pbr = params && params->pbr;
    const bool skinned = params && params->skinned;
    const bool envMapping = params && params->envMapping;
    const bool dual = params && params->dualTexture;
    if (pbr) {
        if (skinned) {
            if (stride != 68) throw std::runtime_error("Metal: SkinnedPbrEffect requires stride 68 (position+normal+tangent+uv+boneWeights+boneIndices)");
            return PipelineKind::SkinnedPbr68;
        }
        if (stride != 48) throw std::runtime_error("Metal: PbrEffect requires stride 48 (position+normal+tangent+uv)");
        return PipelineKind::Pbr48;
    }
    if (skinned) {
        // plan_metal.md METAL-76: same real XNA precedence as METAL-39's BasicEffect case
        // (matching EasyGLGraphicsBackend::SelectProgram()'s own identical skinned branch) --
        // per-vertex-lit only when lighting is actually on and per-pixel wasn't explicitly
        // requested; with lighting disabled both shaders degenerate identically, so the existing
        // per-pixel pipeline stays selected there too.
        const bool vertexLit = params && params->lightingEnabled && !params->preferPerPixelLighting;
        if (stride == 56) return vertexLit ? PipelineKind::Skinned56VertexLit : PipelineKind::Skinned56;
        if (stride == 52) return vertexLit ? PipelineKind::Skinned52VertexLit : PipelineKind::Skinned52;
        throw std::runtime_error("Metal: SkinnedEffect requires stride 52 or 56");
    }
    if (envMapping) {
        if (stride != 32) throw std::runtime_error("Metal: EnvironmentMapEffect requires VertexPositionNormalTexture (stride 32)");
        return PipelineKind::EnvMap32;
    }
    if (dual) {
        if (!textured) throw std::runtime_error("Metal: DualTextureEffect requires Texture to be set");
        if (stride == 24) return PipelineKind::DualTex24Colored;
        if (stride == 20) return PipelineKind::DualTex20;
        throw std::runtime_error("Metal: DualTextureEffect requires stride 20 or 24");
    }
    if (textured) {
        switch (stride) {
            case 20: return PipelineKind::Textured20;
            case 24: return PipelineKind::ColorTex24;
            case 32:
                // plan_metal.md METAL-39: real XNA BasicEffect precedence (matches
                // EasyGLGraphicsBackend::SelectProgram()'s own stride-32 case exactly) -- with
                // lighting disabled, both shaders degenerate to the identical trivial
                // ambient=(1,1,1) case, so the per-pixel-lit pipeline stays selected there rather
                // than forcing an unnecessary extra PipelineKind/pipeline-cache entry.
                if (params && params->lightingEnabled && !params->preferPerPixelLighting)
                    return PipelineKind::LitTex32VertexLit;
                return PipelineKind::LitTex32;
            default: throw std::runtime_error("Metal: textured 3D requires stride 20, 24, or 32 until generic VertexDeclaration pipeline cache is implemented");
        }
    }
    if (stride != 16) throw std::runtime_error("Metal: colored 3D currently requires VertexPositionColor stride 16");
    return PipelineKind::Colored16;
}

// plan_metal.md METAL-38-47: fills LitTransform/LitUniforms from GpuDrawParams, field-for-field
// matching EasyGLGraphicsBackend::BindDrawParams()'s own real mapping (ground truth, ported not
// redesigned). `params` is never null here -- LitTex32 is only reachable via `textured`, which
// requires a non-null `params` (see selectPipelineKind's own `textured = params && ...` gate).
static void fillLitUniforms(LitTransform& t, LitUniforms& lu, const Mat4& wvp, const GpuDrawParams& params)
{
    std::memcpy(t.wvp, wvp.m, sizeof(t.wvp));
    std::memcpy(t.world, params.worldColMajor, sizeof(t.world));
    computeNormalMatrixCols(params.worldColMajor, t.normalCol0, t.normalCol1, t.normalCol2);

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
// `p.loc_ambient < 0` gated block -- ground truth, ported not redesigned). `params` is never null
// here for the same reason `fillLitUniforms` documents.
static void fillEnvUniforms(EnvTransform& t, EnvUniforms& eu, const Mat4& wvp, const GpuDrawParams& params)
{
    std::memcpy(t.wvp, wvp.m, sizeof(t.wvp));
    std::memcpy(t.world, params.worldColMajor, sizeof(t.world));
    computeNormalMatrixCols(params.worldColMajor, t.normalCol0, t.normalCol1, t.normalCol2);

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
// unlike fillLitUniforms/fillEnvUniforms, this deliberately does NOT call computeNormalMatrixCols
// -- the skinned shader has no world-normal-matrix step at all (see cna_skin_common's own
// comment), so SkinnedTransform has no normalCol0/1/2 fields to fill.
static void fillSkinnedUniforms(SkinnedTransform& t, SkinnedUniforms& su, const Mat4& wvp, const GpuDrawParams& params)
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
static void fillPbrUniforms(PbrTransform& t, PbrUniforms& pu, const Mat4& wvp, const GpuDrawParams& params)
{
    std::memcpy(t.wvp, wvp.m, sizeof(t.wvp));
    std::memcpy(t.world, params.worldColMajor, sizeof(t.world));
    computeNormalMatrixCols(params.worldColMajor, t.normalCol0, t.normalCol1, t.normalCol2);

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
// (fragment-side) fields are identical to fillPbrUniforms' -- only the transform struct differs
// (adds skinParams, matching fillSkinnedUniforms' own t.skinParams handling), so this delegates the
// uniform fill to fillPbrUniforms via a throwaway PbrTransform and copies just the shared fields
// across, rather than duplicating every uniform assignment a third time.
static void fillSkinnedPbrUniforms(SkinnedPbrTransform& t, PbrUniforms& pu, const Mat4& wvp, const GpuDrawParams& params)
{
    PbrTransform unusedT{};
    fillPbrUniforms(unusedT, pu, wvp, params);
    std::memcpy(t.wvp, wvp.m, sizeof(t.wvp));
    std::memcpy(t.world, params.worldColMajor, sizeof(t.world));
    t.skinParams[0]=(float)params.weightsPerVertex; t.skinParams[1]=t.skinParams[2]=t.skinParams[3]=0;
}

static void drawMetal3D(MetalGraphicsBackend::Impl& p,const MetalVertexBuffer& vb,const MetalIndexBuffer* ib,const Matrix&w,const Matrix&v,const Matrix&pr,PrimitiveType pt,int pc,const GpuDrawParams* params)
{
    p.ensureFrame(); Mat4 wvp=transpose(multiply(multiply(fromXna(w),fromXna(v)),fromXna(pr)));
    const bool textured = params && params->texture0;
    const bool dual = params && params->dualTexture;
    const PipelineKind kind = selectPipelineKind(vb.stride(), params);
    id<MTLRenderPipelineState> pipeline = p.getOrCreatePipeline(kind);
    [p.encoder setRenderPipelineState:pipeline]; [p.encoder setVertexBuffer:vb.native() offset:0 atIndex:0];
    [p.encoder setDepthStencilState:p.depthState]; [p.encoder setFrontFacingWinding:MTLWindingClockwise]; [p.encoder setCullMode:p.cull]; [p.encoder setTriangleFillMode:p.fill];

    if (kind == PipelineKind::LitTex32 || kind == PipelineKind::LitTex32VertexLit) {
        // plan_metal.md METAL-38-47/39: real per-pixel (LitTex32) or per-vertex/Gouraud
        // (LitTex32VertexLit, XNA's real PreferPerPixelLighting=false default) lighting/fog/
        // specular/emissive path -- both share the identical LitTransform/LitUniforms uniform
        // layout and texture binding, only the vertex/fragment shader pair (selected via `kind` by
        // getOrCreatePipeline()) differs in which pipeline stage actually does the lighting math.
        LitTransform t{}; LitUniforms lu{};
        fillLitUniforms(t, lu, wvp, *params);
        [p.encoder setVertexBytes:&t length:sizeof(t) atIndex:1];
        [p.encoder setVertexBytes:&lu length:sizeof(lu) atIndex:2];
        [p.encoder setFragmentBytes:&lu length:sizeof(lu) atIndex:2];
        id<MTLTexture> tex0=nativeTextureFor(params->texture0);
        if(tex0)[p.encoder setFragmentTexture:tex0 atIndex:0];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[0]?p.samplerSlots[0]:p.sampler) atIndex:0];
    } else if (kind == PipelineKind::EnvMap32) {
        // plan_metal.md METAL-64/66-68: real cube-map reflection/Fresnel path.
        EnvTransform t{}; EnvUniforms eu{};
        fillEnvUniforms(t, eu, wvp, *params);
        [p.encoder setVertexBytes:&t length:sizeof(t) atIndex:1];
        [p.encoder setVertexBytes:&eu length:sizeof(eu) atIndex:2];
        [p.encoder setFragmentBytes:&eu length:sizeof(eu) atIndex:2];
        id<MTLTexture> tex0=nativeTextureFor(params->texture0);
        if(tex0)[p.encoder setFragmentTexture:tex0 atIndex:0];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[0]?p.samplerSlots[0]:p.sampler) atIndex:0];
        // plan_metal.md: same established null-envMap fallback gap as texture0/texture1 (leaves
        // whatever a prior draw last bound at unit 1) -- not a new class of gap, matches the
        // existing DualTexture-null-Texture2 precedent exactly. Resolved through
        // nativeCubeTextureFor() (METAL-109) rather than a bare dynamic_cast<MetalTextureCube*>, so
        // a RenderTargetCube just rendered into (a real-time-reflection-probe pattern -- arguably
        // RenderTargetCube's single most common real use) can be sampled here too.
        id<MTLTexture> envMapTex=nativeCubeTextureFor(params->envMap);
        if(envMapTex)[p.encoder setFragmentTexture:envMapTex atIndex:1];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[1]?p.samplerSlots[1]:p.sampler) atIndex:1];
    } else if (kind == PipelineKind::Skinned52 || kind == PipelineKind::Skinned56
            || kind == PipelineKind::Skinned52VertexLit || kind == PipelineKind::Skinned56VertexLit) {
        // plan_metal.md METAL-72-80/76: real skinned lit/fog/specular/emissive path, either
        // per-pixel (Skinned52/56) or per-vertex/Gouraud (Skinned52/56VertexLit, XNA's real
        // PreferPerPixelLighting=false default) -- both share the identical SkinnedTransform/
        // SkinnedUniforms uniform layout, bone buffer, and texture binding, only the vertex/
        // fragment shader pair (selected via `kind` by getOrCreatePipeline()) differs.
        SkinnedTransform t{}; SkinnedUniforms su{};
        fillSkinnedUniforms(t, su, wvp, *params);
        [p.encoder setVertexBytes:&t length:sizeof(t) atIndex:1];
        [p.encoder setVertexBytes:&su length:sizeof(su) atIndex:2];
        [p.encoder setFragmentBytes:&su length:sizeof(su) atIndex:2];
        // plan_metal.md METAL-73: 72 bones x 4x4 = 4608 floats (18KB) exceeds setVertexBytes:'s
        // 4KB inline limit -- must be a real MTLBuffer, unlike every other uniform in this file.
        // Reallocated fresh each draw (newBufferWithBytes:), matching MetalVertexBuffer::SetData's
        // own established "always reallocate, never mutate in place" pattern (same
        // command-buffer-resource-lifetime assumption already relied on throughout this file, not
        // a new risk category -- see plan_metal.md Phase 18's own still-open resource-lifetime
        // audit). GpuDrawParams::boneTransforms is already column-major (its own doc comment),
        // matching worldColMajor's convention, so no per-bone transpose is needed before upload.
        id<MTLBuffer> bonesBuf = [p.device newBufferWithBytes:params->boneTransforms length:sizeof(float)*72*16 options:MTLResourceStorageModeShared];
        [p.encoder setVertexBuffer:bonesBuf offset:0 atIndex:3];
        [bonesBuf release];
        id<MTLTexture> tex0=nativeTextureFor(params->texture0);
        if(tex0)[p.encoder setFragmentTexture:tex0 atIndex:0];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[0]?p.samplerSlots[0]:p.sampler) atIndex:0];
    } else if (kind == PipelineKind::Pbr48) {
        // plan_metal.md METAL-81/83-86: real metallic-roughness PBR path.
        PbrTransform t{}; PbrUniforms pu{};
        fillPbrUniforms(t, pu, wvp, *params);
        [p.encoder setVertexBytes:&t length:sizeof(t) atIndex:1];
        [p.encoder setVertexBytes:&pu length:sizeof(pu) atIndex:2];
        [p.encoder setFragmentBytes:&pu length:sizeof(pu) atIndex:2];
        // plan_metal.md METAL-87: a null map falls back to the shared default white / flat-normal
        // 1x1 textures (mirrors EnsureDefaultWhiteTexture()/EnsureDefaultFlatNormalTexture()
        // exactly) -- unlike texture0/texture1's own established "leave whatever was bound before"
        // gap elsewhere in this file, PBR's 4 optional maps are given real, always-correct
        // fallbacks, since the constant-cost 1x1-texture-per-unbound-map approach is cheap and
        // there is no equivalent "correct default" for the 2D case's leave-it-alone convention.
        id<MTLTexture> tex0=nativeTextureFor(params->texture0);
        id<MTLTexture> normalMap=nativeTextureFor(params->pbrNormalMap);
        id<MTLTexture> mrMap=nativeTextureFor(params->pbrMetallicRoughnessMap);
        id<MTLTexture> emissiveMap=nativeTextureFor(params->pbrEmissiveMap);
        id<MTLTexture> occlusionMap=nativeTextureFor(params->pbrOcclusionMap);
        id<MTLSamplerState> smp=(p.samplerSlots[0]?p.samplerSlots[0]:p.sampler);
        [p.encoder setFragmentTexture:(tex0?tex0:p.defaultWhiteTexture) atIndex:0];
        [p.encoder setFragmentSamplerState:smp atIndex:0];
        [p.encoder setFragmentTexture:(normalMap?normalMap:p.defaultFlatNormalTexture) atIndex:1];
        [p.encoder setFragmentSamplerState:smp atIndex:1];
        [p.encoder setFragmentTexture:(mrMap?mrMap:p.defaultWhiteTexture) atIndex:2];
        [p.encoder setFragmentSamplerState:smp atIndex:2];
        [p.encoder setFragmentTexture:(emissiveMap?emissiveMap:p.defaultWhiteTexture) atIndex:3];
        [p.encoder setFragmentSamplerState:smp atIndex:3];
        [p.encoder setFragmentTexture:(occlusionMap?occlusionMap:p.defaultWhiteTexture) atIndex:4];
        [p.encoder setFragmentSamplerState:smp atIndex:4];
    } else if (kind == PipelineKind::SkinnedPbr68) {
        // plan_metal.md METAL-82: real SkinnedPbrEffect path -- same bone-buffer handling as
        // Skinned52/56, same 5-texture PBR-map binding as Pbr48.
        SkinnedPbrTransform t{}; PbrUniforms pu{};
        fillSkinnedPbrUniforms(t, pu, wvp, *params);
        [p.encoder setVertexBytes:&t length:sizeof(t) atIndex:1];
        [p.encoder setVertexBytes:&pu length:sizeof(pu) atIndex:2];
        [p.encoder setFragmentBytes:&pu length:sizeof(pu) atIndex:2];
        id<MTLBuffer> bonesBuf = [p.device newBufferWithBytes:params->boneTransforms length:sizeof(float)*72*16 options:MTLResourceStorageModeShared];
        [p.encoder setVertexBuffer:bonesBuf offset:0 atIndex:3];
        [bonesBuf release];
        id<MTLTexture> tex0=nativeTextureFor(params->texture0);
        id<MTLTexture> normalMap=nativeTextureFor(params->pbrNormalMap);
        id<MTLTexture> mrMap=nativeTextureFor(params->pbrMetallicRoughnessMap);
        id<MTLTexture> emissiveMap=nativeTextureFor(params->pbrEmissiveMap);
        id<MTLTexture> occlusionMap=nativeTextureFor(params->pbrOcclusionMap);
        id<MTLSamplerState> smp=(p.samplerSlots[0]?p.samplerSlots[0]:p.sampler);
        [p.encoder setFragmentTexture:(tex0?tex0:p.defaultWhiteTexture) atIndex:0];
        [p.encoder setFragmentSamplerState:smp atIndex:0];
        [p.encoder setFragmentTexture:(normalMap?normalMap:p.defaultFlatNormalTexture) atIndex:1];
        [p.encoder setFragmentSamplerState:smp atIndex:1];
        [p.encoder setFragmentTexture:(mrMap?mrMap:p.defaultWhiteTexture) atIndex:2];
        [p.encoder setFragmentSamplerState:smp atIndex:2];
        [p.encoder setFragmentTexture:(emissiveMap?emissiveMap:p.defaultWhiteTexture) atIndex:3];
        [p.encoder setFragmentSamplerState:smp atIndex:3];
        [p.encoder setFragmentTexture:(occlusionMap?occlusionMap:p.defaultWhiteTexture) atIndex:4];
        [p.encoder setFragmentSamplerState:smp atIndex:4];
    } else {
        // plan_metal.md METAL-35/36/37/51-63: DiffuseColor/VertexColorEnabled/AlphaTest now
        // actually reach the shader (previously silently ignored for every draw). Defaults below
        // exactly reproduce this function's own prior hardcoded behavior for the non-Ex
        // (params==nullptr) path: diffuseColor=white, alphaTest=always-pass, vertexColorEnabled=true.
        UMaterialParams mp;
        if (params) {
            mp.diffuseColor[0]=params->diffuseColor[0]; mp.diffuseColor[1]=params->diffuseColor[1];
            mp.diffuseColor[2]=params->diffuseColor[2]; mp.diffuseColor[3]=params->diffuseColor[3];
            mp.alphaTest[0]=params->alphaTest[0]; mp.alphaTest[1]=params->alphaTest[1];
            mp.alphaTest[2]=params->alphaTest[2]; mp.alphaTest[3]=params->alphaTest[3];
            mp.flags[0]=params->vertexColorEnabled?1.0f:0.0f; mp.flags[1]=mp.flags[2]=mp.flags[3]=0.0f;
        } else {
            mp.diffuseColor[0]=mp.diffuseColor[1]=mp.diffuseColor[2]=mp.diffuseColor[3]=1.0f;
            mp.alphaTest[0]=0.0f; mp.alphaTest[1]=0.0f; mp.alphaTest[2]=1.0f; mp.alphaTest[3]=1.0f;
            mp.flags[0]=1.0f; mp.flags[1]=mp.flags[2]=mp.flags[3]=0.0f;
        }
        [p.encoder setVertexBytes:&wvp length:sizeof(wvp) atIndex:1];
        [p.encoder setFragmentBytes:&mp length:sizeof(mp) atIndex:2];
        if(textured){
            id<MTLTexture> tex0=nativeTextureFor(params->texture0);
            if(tex0)[p.encoder setFragmentTexture:tex0 atIndex:0];
            [p.encoder setFragmentSamplerState:(p.samplerSlots[0]?p.samplerSlots[0]:p.sampler) atIndex:0];
            if(dual){
                // plan_metal.md: EasyGL/Vulkan/Bgfx fall back to a 1x1 opaque white texture when
                // Texture2 is left null (docs/dualtextureeffect-support.md Task 386/387); Metal does
                // not yet have that fallback mechanism (a real, tracked follow-up gap, not silently
                // dropped), so a null Texture2 here leaves texture unit 1 holding whatever a prior
                // draw last bound there -- matches this same function's own pre-existing texture0
                // null-handling pattern exactly, not a new class of gap introduced by DualTexture.
                id<MTLTexture> tex1=nativeTextureFor(params->texture1);
                if(tex1)[p.encoder setFragmentTexture:tex1 atIndex:1];
                [p.encoder setFragmentSamplerState:(p.samplerSlots[1]?p.samplerSlots[1]:p.sampler) atIndex:1];
            }
        }
    }
    int n=primitiveVertexCount(pt,pc); if(ib)[p.encoder drawIndexedPrimitives:metalPrimitive(pt) indexCount:n indexType:ib->IsThirtyTwoBit()?MTLIndexTypeUInt32:MTLIndexTypeUInt16 indexBuffer:ib->native() indexBufferOffset:0];else[p.encoder drawPrimitives:metalPrimitive(pt) vertexStart:0 vertexCount:n];
}
void MetalGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&v,const Matrix&w,const Matrix&vi,const Matrix&p,PrimitiveType pt,int pc){auto*vb=dynamic_cast<const MetalVertexBuffer*>(&v);if(!vb)throw std::runtime_error("Metal: foreign vertex buffer");drawMetal3D(*impl_,*vb,nullptr,w,vi,p,pt,pc,nullptr);}
void MetalGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&v,const IIndexBufferBackend&i,const Matrix&w,const Matrix&vi,const Matrix&p,PrimitiveType pt,int pc){auto*vb=dynamic_cast<const MetalVertexBuffer*>(&v);auto*ib=dynamic_cast<const MetalIndexBuffer*>(&i);if(!vb||!ib)throw std::runtime_error("Metal: foreign buffer");drawMetal3D(*impl_,*vb,ib,w,vi,p,pt,pc,nullptr);}
void MetalGraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend&v,const Matrix&w,const Matrix&vi,const Matrix&p,PrimitiveType pt,int pc,const GpuDrawParams&gp){auto*vb=dynamic_cast<const MetalVertexBuffer*>(&v);if(!vb)throw std::runtime_error("Metal: foreign vertex buffer");drawMetal3D(*impl_,*vb,nullptr,w,vi,p,pt,pc,&gp);}
void MetalGraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend&v,const IIndexBufferBackend&i,const Matrix&w,const Matrix&vi,const Matrix&p,PrimitiveType pt,int pc,const GpuDrawParams&gp){auto*vb=dynamic_cast<const MetalVertexBuffer*>(&v);auto*ib=dynamic_cast<const MetalIndexBuffer*>(&i);if(!vb||!ib)throw std::runtime_error("Metal: foreign buffer");drawMetal3D(*impl_,*vb,ib,w,vi,p,pt,pc,&gp);}
void MetalGraphicsBackend::SetStringMarkerEXT(const char* m){impl_->ensureFrame();if(m)[impl_->encoder insertDebugSignpost:[NSString stringWithUTF8String:m]];}

bool MetalGraphicsBackend::SupportsCapability(CNA::GraphicsCapability capability) const
{
    // plan_metal.md Phase 20: IGraphicsBackend's own default is an unconditional `true`, which is
    // a false positive for these -- a caller that checks this capability before relying on the
    // feature would otherwise get a wrong answer. Revert each case to the inherited default
    // (remove the explicit `false`) as its own phase lands. OcclusionQuery flipped to real/true
    // 2026-07-19 (METAL-136-139) now that CreateOcclusionQuery() is real, not a stub.
    switch (capability) {
        case CNA::GraphicsCapability::MultipleRenderTargets:     return false; // plan_metal.md METAL-112
        case CNA::GraphicsCapability::CustomEffects:              return false; // plan_metal.md METAL-144
        // plan_metal.md METAL-191: CreateRenderTarget2D() silently ignores its multiSampleCount
        // parameter (METAL-103/104 still open, see the comment right above that function) -- the
        // backbuffer itself is also never allocated with a sample count above 1. A blanket `true`
        // here would be the same class of false-positive METAL-192/195/196 already fixed for MRT/
        // OcclusionQuery/CustomEffects, just not caught until this same pass reviewed Phase 20 fresh.
        case CNA::GraphicsCapability::MultiSampleAntiAliasing:    return false; // plan_metal.md METAL-104
        default: return true;
    }
}

#ifdef CNA_BACKEND_METAL
std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args){return std::make_unique<MetalGraphicsBackend>(args);}
#endif
}
#else
#error "CNA Metal backend must be compiled on an Apple platform"
#endif
