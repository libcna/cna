// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Metal/MetalRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformRendererSurfaceState.hpp"
#include "CNA/Internal/Renderers/Metal/MetalPipelineKey.hpp"
#include "CNA/Internal/Renderers/Metal/MetalCommandFailure.hpp"
#include "CNA/Internal/Renderers/Metal/MetalNormalMatrix.hpp"
#include "CNA/Internal/Renderers/Metal/MetalPrimitiveVertexCount.hpp"
#include "CNA/Internal/Renderers/Metal/MetalRetainedResource.hpp"
#include "CNA/Internal/Renderers/Metal/MetalRasterState.hpp"
#include "CNA/Internal/Renderers/Metal/MetalResourcePolicy.hpp"
#include "CNA/Internal/Renderers/Metal/MetalTextureBindingPolicy.hpp"
#include "CNA/Internal/Renderers/Metal/MetalTextureTransfer.hpp"
#include "CNA/Internal/Renderers/Metal/MetalVertexDeclarationPolicy.hpp"
#include "CNA/Internal/Renderers/Metal/MetalLogicalViewport.hpp"
#include "CNA/Internal/Renderers/Metal/MetalMat4.hpp"
#include "CNA/Internal/Renderers/Metal/MetalSelectPipelineKind.hpp"
#include "CNA/Internal/Renderers/Metal/MetalUniformFill.hpp"
#include "CNA/Internal/Renderers/Metal/MetalVertexAttribFormat.hpp"
#include "CNA/Internal/Renderers/Metal/MetalVertexDescriptorPlan.hpp"
#include "CNA/Internal/Renderers/Metal/MetalSamplerFilter.hpp"
#include "CNA/Internal/Renderers/Metal/MetalCompareFunction.hpp"
#include "CNA/Internal/Renderers/Metal/MetalStencilOperation.hpp"
#include "CNA/Internal/Renderers/Metal/MetalBlend.hpp"
#include "CNA/Internal/Renderers/Metal/MetalBlendFunction.hpp"
#include "CNA/Internal/Renderers/Metal/MetalCullMode.hpp"
#include "CNA/Internal/Renderers/Metal/MetalDepthPolicy.hpp"
#include "CNA/Internal/Renderers/Metal/MetalPolicy.hpp"
// plan_metal.md Phase 14 (METAL-142-152): needs Effect's complete type (not just
// IGraphicsRenderer.hpp's own forward declaration) to call Apply()/GetEffectRendererPtr() from
// MetalSpriteBatch's custom-effect wiring below.
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/NotSupportedException.hpp"

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

@interface CNAMetalView : NSView
- (void)updateDrawableWidth:(int)width height:(int)height displayScale:(float)displayScale;
@end

@implementation CNAMetalView
+ (Class)layerClass
{
    return [CAMetalLayer class];
}

- (BOOL)wantsUpdateLayer
{
    return YES;
}

- (CALayer*)makeBackingLayer
{
    return [CAMetalLayer layer];
}

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil)
    {
        self.wantsLayer = YES;
        self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        self.layer.opaque = YES;
    }
    return self;
}

- (void)updateDrawableWidth:(int)width height:(int)height displayScale:(float)displayScale
{
    CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer;
    metalLayer.contentsScale = displayScale > 0.0f ? displayScale : 1.0f;
    metalLayer.drawableSize = CGSizeMake(MAX(1, width), MAX(1, height));
}

- (NSView*)hitTest:(NSPoint)point
{
    (void)point;
    return nil;
}
@end

namespace CNA::Internal::Renderers::Metal
{
// plan_metal.md METAL-131/122/125: forward-declared at this (non-anonymous-namespace) scope so
// MetalTextureCube/MetalTexture3D's own GetData() overrides, defined inside the anonymous
// namespace below, can call it -- its real definition lives later in this file, also at this same
// scope (outside the anonymous namespace), not inside it, so the forward declaration must live out
// here too or it would silently become a different, unrelated, unlinkable declaration. Needs only
// ordinary Metal types, not MetalRenderer::Impl, so unlike nativeTextureFor()/
// resolveActiveAttachments() this one has no incomplete-type ordering constraint of its own.
static void blitTextureToClientBuffer(id<MTLDevice> device, id<MTLCommandQueue> queue,
                                      id<MTLTexture> src, NSUInteger slice, int level,
                                      int x, int y, int z, int w, int h, int depth,
                                      const MetalTextureTransferLayout& layout,
                                      MetalTransferPixelOrder pixelOrder, void* data,
                                      const std::function<void()>& commandHealthCheck);

namespace
{
    static id retainMetalObject(id value)
    {
        [value retain];
        return value;
    }

    static void releaseMetalObject(id value)
    {
        [value release];
    }

    using MetalObjectOwner = MetalRetainedResource<id>;

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
// mirroring EasyGLRenderer::EnsureDualTextured3DProgram()'s own fsrc, which does the same.
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
// on EasyGL/Vulkan/Bgfx (docs/dualtextureeffect-support.md Task 383). CNA's cross-renderer
// convention (confirmed against WebGPURenderer's shipped dual-texture dispatch) samples
// both textures at the SAME shared UV (stride 20/24), not FNA's real separate TexCoord/TexCoord2
// -- an intentional, already-established simplification this shader matches for consistency
// with every other CNA renderer rather than reintroducing a second UV set nothing else here uses.
fragment float4 cna_f3d_dualtex(V3Out in [[stage_in]], texture2d<float> tex0 [[texture(0)]], sampler smp0 [[sampler(0)]], texture2d<float> tex1 [[texture(1)]], sampler smp1 [[sampler(1)]], constant UMaterialParams& m [[buffer(2)]]) {
    float4 vcolor = (m.flags.x > 0.5) ? in.color : float4(1.0);
    float4 base = tex0.sample(smp0, in.uv);
    base.rgb *= 2.0;
    float4 c = base * tex1.sample(smp1, in.uv) * vcolor * m.diffuseColor;
    if (cna_alpha_test_fails(c.a, m.alphaTest)) discard_fragment();
    return c;
}

// BasicEffect per-pixel lighting (plan_metal.md METAL-38/40-47), ported line-for-line from
// EasyGLRenderer::EnsureLit3DProgram()'s real GLSL (both vertex and fragment stage), the
// same reference every other renderer's own lit-textured shader already matches. Every `vec3`
// uniform is carried as a `float4` here (xyz + unused pad) to sidestep MSL `constant`-address-space
// float3 column-padding ambiguity entirely -- deliberately NOT a float3x3 uniform either, for the
// same reason (a 3x3 matrix's columns are *also* individually padded to 16 bytes in `constant`
// address space); the normal matrix crosses the CPU/GPU boundary as 3 separate float4 columns and
// is reassembled into a real float3x3 inside the shader instead.
//
// Fog uses the current FNA-derived fogVector dot product, and both per-pixel and per-vertex
// (Gouraud) lighting variants are present. Pipeline dispatch selects the latter when lighting is
// enabled and PreferPerPixelLighting is false.
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
    float4 fogVector;          // FNA fog vector dotted with the object-space position
};
struct VLitOut { float4 position [[position]]; float3 normal; float2 uv; float3 worldPos; float fogFactor; };
vertex VLitOut cna_v3d_lit(V3NormalTexIn in [[stage_in]], constant LitTransform& t [[buffer(1)]], constant LitUniforms& lu [[buffer(2)]]) {
    VLitOut o;
    o.position = t.wvp * float4(in.position, 1.0);
    float3x3 normalMat = float3x3(t.normalCol0.xyz, t.normalCol1.xyz, t.normalCol2.xyz);
    o.normal = normalMat * in.normal;
    o.uv = in.uv;
    o.fogFactor = 1.0 - clamp(dot(float4(in.position, 1.0), lu.fogVector), 0.0, 1.0);
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
// EasyGLRenderer::EnsureLit3DVertexLitProgram()'s real GLSL (identical Blinn-Phong math to
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
    o.fogFactor = 1.0 - clamp(dot(float4(in.position, 1.0), lu.fogVector), 0.0, 1.0);
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
// EasyGLRenderer::EnsureEnvMapped3DProgram()'s real GLSL. Real XNA `EnvironmentMapEffect`
// has no separate AmbientLightColor uniform in its own shader at all -- `GpuDrawParams::
// emissiveColor`'s own doc comment already documents this: for EnvironmentMapEffect it carries
// "emissive+ambient combined," pre-baked by the C++ effect layer before reaching any renderer, so
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
    float4 fogVector;
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
    o.fogFactor = 1.0 - clamp(dot(float4(in.position, 1.0), eu.fogVector), 0.0, 1.0);
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
// EasyGLRenderer::EnsureSkinnedProgram()'s real GLSL. Vertex layout: position(12)+
// normal(12)+uv(8)+boneWeights(16, real float4, not packed/normalized)+boneIndices(4, packed
// UChar4, unnormalized -- read as an integer type in-shader, not auto-converted to float like a
// Normalized format would be) = 52 bytes; +color(4, packed UChar4Normalized) = 56 -- confirmed
// against WebGPURenderer::GetOrCreatePipelineSkinned3D's own `hasVertexColor=(stride==56)`.
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
    float4 fogVector;
    float4 vertexColorEnabled; // x = 0/1
};
struct VSkinnedIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float2 uv [[attribute(2)]]; float4 boneWeights [[attribute(3)]]; uchar4 boneIndices [[attribute(4)]]; };
struct VSkinnedColorIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float2 uv [[attribute(2)]]; float4 boneWeights [[attribute(3)]]; uchar4 boneIndices [[attribute(4)]]; float4 color [[attribute(5)]]; };
struct VSkinnedOut { float4 position [[position]]; float3 normal; float2 uv; float3 worldPos; float fogFactor; float4 color; };
inline VSkinnedOut cna_skin_common(float3 position, float3 normal, float2 uv, float4 boneWeights, uchar4 boneIndices, float4 vcolor,
                                    constant SkinnedTransform& t, constant float4x4* bones,
                                    float4 fogVector) {
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
    o.fogFactor = 1.0 - clamp(dot(skinnedPos, fogVector), 0.0, 1.0);
    return o;
}
vertex VSkinnedOut cna_v3d_skinned(VSkinnedIn in [[stage_in]], constant SkinnedTransform& t [[buffer(1)]], constant SkinnedUniforms& su [[buffer(2)]], constant float4x4* bones [[buffer(3)]]) {
    return cna_skin_common(in.position, in.normal, in.uv, in.boneWeights, in.boneIndices, float4(1.0), t, bones, su.fogVector);
}
vertex VSkinnedOut cna_v3d_skinned_color(VSkinnedColorIn in [[stage_in]], constant SkinnedTransform& t [[buffer(1)]], constant SkinnedUniforms& su [[buffer(2)]], constant float4x4* bones [[buffer(3)]]) {
    return cna_skin_common(in.position, in.normal, in.uv, in.boneWeights, in.boneIndices, in.color, t, bones, su.fogVector);
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
// EasyGLRenderer::EnsureSkinnedVertexLitProgram()'s real GLSL (same technique as METAL-39:
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
    o.fogFactor = 1.0 - clamp(dot(skinnedPos, su.fogVector), 0.0, 1.0);
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

// CNAEXT PBR (plan_metal.md METAL-81/83-86, plan_cnj.md CNB-58), ported line-for-line from
// EasyGLRenderer::EnsurePbrProgram()'s real GLSL -- the glTF 2.0 spec's own reference
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
    float4 pbrFactors;      // x=MetallicFactor, y=RoughnessFactor, z=NormalScale, w=OcclusionStrength
    float4 alphaTest;
    float4 fogColorEnabled;
    float4 fogVector;
    float4 srgbFlags;          // x=base decode, y=emissive decode, z=output encode
    float4 dielectricFresnel;  // xyz=dielectric F0, w=dielectric F90
    float4 textureTransformRows[10];
};
struct VPbrIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float4 tangent [[attribute(2)]]; float2 uv [[attribute(3)]]; };
struct VPbrOut { float4 position [[position]]; float3 normal; float3 tangent; float bitangentSign; float2 uv; float fogFactor; float3 worldPos; };
float cna_direction_handedness(float3x3 m) {
    return dot(m[0], cross(m[1], m[2])) < 0.0 ? -1.0 : 1.0;
}
vertex VPbrOut cna_v3d_pbr(VPbrIn in [[stage_in]], constant PbrTransform& t [[buffer(1)]], constant PbrUniforms& pu [[buffer(2)]]) {
    VPbrOut o;
    o.position = t.wvp * float4(in.position, 1.0);
    float3x3 normalMat = float3x3(t.normalCol0.xyz, t.normalCol1.xyz, t.normalCol2.xyz);
    o.normal = normalMat * in.normal;
    float3x3 world3 = float3x3(t.world[0].xyz, t.world[1].xyz, t.world[2].xyz);
    o.tangent = world3 * in.tangent.xyz;
    o.bitangentSign = in.tangent.w * cna_direction_handedness(world3);
    o.uv = in.uv;
    o.worldPos = (t.world * float4(in.position, 1.0)).xyz;
    o.fogFactor = 1.0 - clamp(dot(float4(in.position, 1.0), pu.fogVector), 0.0, 1.0);
    return o;
}
inline float3 cna_pbr_light(float3 N, float3 V, float3 L, float3 lightColor, float3 albedo, float3 F0, float3 F90, float roughness, float metallic) {
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
    float3 F = F0 + (F90-F0) * pow(clamp(1.0-VdotH, 0.0, 1.0), 5.0);
    float3 specular = (D*G*F) / max(4.0*NdotV*NdotL, 1e-4);
    float3 diffuseColor = albedo * (1.0-metallic);
    float3 kd = float3(1.0) - F;
    return (kd*diffuseColor/3.14159265 + specular) * lightColor * NdotL;
}
inline float3 cna_srgb_to_linear(float3 c) {
    float3 lo = c / 12.92;
    float3 hi = pow((c + 0.055) / 1.055, float3(2.4));
    return mix(lo, hi, step(float3(0.04045), c));
}
inline float3 cna_linear_to_srgb(float3 c) {
    float3 lo = c * 12.92;
    float3 hi = 1.055 * pow(max(c, float3(0.0)), float3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(float3(0.0031308), c));
}
inline float2 cna_pbr_transform_uv(float2 uv, int slot, constant PbrUniforms& pu) {
    float3 value = float3(uv, 1.0);
    return float2(dot(value, pu.textureTransformRows[slot * 2].xyz),
                  dot(value, pu.textureTransformRows[slot * 2 + 1].xyz));
}
fragment float4 cna_f3d_pbr(VPbrOut in [[stage_in]],
    texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]],
    texture2d<float> normalMap [[texture(1)]], sampler normalSmp [[sampler(1)]],
    texture2d<float> mrMap [[texture(2)]], sampler mrSmp [[sampler(2)]],
    texture2d<float> emissiveMap [[texture(3)]], sampler emissiveSmp [[sampler(3)]],
    texture2d<float> occlusionMap [[texture(4)]], sampler occlusionSmp [[sampler(4)]],
    constant PbrUniforms& pu [[buffer(2)]])
{
    float4 baseColorTex = tex.sample(smp, cna_pbr_transform_uv(in.uv, 0, pu));
    float3 baseColor = mix(baseColorTex.rgb, cna_srgb_to_linear(baseColorTex.rgb), pu.srgbFlags.x);
    float3 albedo = baseColor * pu.diffuseColor.rgb;
    float alpha = baseColorTex.a * pu.diffuseColor.a;
    float3 N = normalize(in.normal);
    float3 T = normalize(in.tangent - N*dot(N, in.tangent));
    float3 B = cross(N, T) * in.bitangentSign;
    float3x3 TBN = float3x3(T, B, N);
    float3 sampledNormal = normalMap.sample(normalSmp, cna_pbr_transform_uv(in.uv, 1, pu)).rgb*2.0 - 1.0;
    sampledNormal.xy *= pu.pbrFactors.z;
    float3 finalNormal = normalize(TBN * sampledNormal);
    float4 mr = mrMap.sample(mrSmp, cna_pbr_transform_uv(in.uv, 2, pu));
    float roughness = clamp(mr.g * pu.pbrFactors.y, 0.045, 1.0);
    float metallic = clamp(mr.b * pu.pbrFactors.x, 0.0, 1.0);
    float3 V = normalize(pu.eyePosition.xyz - in.worldPos);
    float3 F0 = mix(pu.dielectricFresnel.xyz, albedo, metallic);
    float3 F90 = mix(float3(pu.dielectricFresnel.w), float3(1.0), metallic);
    float3 Lo = float3(0.0);
    Lo += cna_pbr_light(finalNormal, V, normalize(-pu.light0Dir.xyz), pu.light0Diffuse.xyz, albedo, F0, F90, roughness, metallic);
    Lo += cna_pbr_light(finalNormal, V, normalize(-pu.light1Dir.xyz), pu.light1Diffuse.xyz, albedo, F0, F90, roughness, metallic);
    Lo += cna_pbr_light(finalNormal, V, normalize(-pu.light2Dir.xyz), pu.light2Diffuse.xyz, albedo, F0, F90, roughness, metallic);
    float occlusionSample = occlusionMap.sample(occlusionSmp, cna_pbr_transform_uv(in.uv, 4, pu)).r;
    float occlusion = 1.0 + pu.pbrFactors.w * (occlusionSample - 1.0);
    float3 ambient = pu.ambientColor.xyz * albedo * occlusion;
    float3 emissiveSample = emissiveMap.sample(emissiveSmp, cna_pbr_transform_uv(in.uv, 3, pu)).rgb;
    emissiveSample = mix(emissiveSample, cna_srgb_to_linear(emissiveSample), pu.srgbFlags.y);
    float3 emissive = pu.emissiveColor.xyz * emissiveSample;
    float4 c = float4(ambient + Lo + emissive, alpha);
    if (cna_alpha_test_fails(c.a, pu.alphaTest)) discard_fragment();
    float3 fogLinear = mix(pu.fogColorEnabled.xyz,
                           cna_srgb_to_linear(pu.fogColorEnabled.xyz), pu.srgbFlags.z);
    c.rgb = mix(fogLinear, c.rgb, in.fogFactor);
    c.rgb = mix(c.rgb, cna_linear_to_srgb(c.rgb), pu.srgbFlags.z);
    return c;
}

// CNAEXT SkinnedPbrEffect (plan_metal.md METAL-82, GLTF-264): GPU skinning plus the same PBR
// interpolants as cna_v3d_pbr. Normals use inverse-transpose joint and world matrices while
// tangents remain ordinary directions.
struct SkinnedPbrTransform { float4x4 wvp; float4x4 world; float4 normalCol0; float4 normalCol1; float4 normalCol2; float4 skinParams; }; // skinParams.x = weightsPerVertex
struct VSkinnedPbrIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float4 tangent [[attribute(2)]]; float2 uv [[attribute(3)]]; float4 boneWeights [[attribute(4)]]; uchar4 boneIndices [[attribute(5)]]; };
float3 cna_skin_normal(float3x3 m, float3 n) {
    float3 c0=m[0], c1=m[1], c2=m[2];
    float3 co0=cross(c1,c2), co1=cross(c2,c0), co2=cross(c0,c1);
    float det=dot(c0,co0);
    float3 transformed=float3x3(co0,co1,co2)*n;
    return (abs(det)>1e-6) ? transformed*((det<0.0)?-1.0:1.0) : m*n;
}
vertex VPbrOut cna_v3d_skinned_pbr(VSkinnedPbrIn in [[stage_in]], constant SkinnedPbrTransform& t [[buffer(1)]], constant PbrUniforms& pu [[buffer(2)]], constant float4x4* bones [[buffer(3)]]) {
    VPbrOut o;
    int weightsPerVertex = int(t.skinParams.x);
    float4x4 skinMat = bones[in.boneIndices.x] * in.boneWeights.x;
    if (weightsPerVertex >= 2) skinMat += bones[in.boneIndices.y] * in.boneWeights.y;
    if (weightsPerVertex >= 4) skinMat += bones[in.boneIndices.z] * in.boneWeights.z + bones[in.boneIndices.w] * in.boneWeights.w;
    float4 skinnedPos = skinMat * float4(in.position, 1.0);
    o.position = t.wvp * skinnedPos;
    float3x3 skinMat3 = float3x3(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    float3 skinnedNormal = cna_skin_normal(skinMat3, in.normal);
    float skinnedNormalLen = length(skinnedNormal);
    float3 boneNormal = (skinnedNormalLen > 1e-6) ? (skinnedNormal / skinnedNormalLen) : in.normal;
    float3x3 normalMat = float3x3(t.normalCol0.xyz, t.normalCol1.xyz, t.normalCol2.xyz);
    o.normal = normalize(normalMat * boneNormal);
    // Not renormalized here (matches the unskinned cna_v3d_pbr's own o.tangent = world3*tangent.xyz,
    // which is also left unnormalized) -- cna_f3d_pbr's Gram-Schmidt orthogonalization against the
    // interpolated normal already renormalizes it per-pixel regardless.
    float3x3 world3 = float3x3(t.world[0].xyz, t.world[1].xyz, t.world[2].xyz);
    o.tangent = world3 * (skinMat3 * in.tangent.xyz);
    o.bitangentSign = in.tangent.w * cna_direction_handedness(world3)
                                   * cna_direction_handedness(skinMat3);
    o.uv = in.uv;
    o.worldPos = (t.world * skinnedPos).xyz;
    o.fogFactor = 1.0 - clamp(dot(skinnedPos, pu.fogVector), 0.0, 1.0);
    return o;
}

struct V2In { float2 position; float2 uv; float4 color; };
// plan_metal.md METAL-157/158: was `float2 viewport` (raw physical drawable pixels), completely
// bypassing virtual-resolution/letterbox scaling -- a real, currently-shipping bug. `scale`/
// `offset` fold the logical-to-physical-to-NDC chain into one multiply-add; see
// MetalRenderer::Impl::computeSpriteTransform() for the derivation, hand-verified to
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

    // plan_metal.md METAL-19: the enum-reordering-sensitive XNA-ordinal logic now lives in the
    // plain-C++ MetalCompareFunction.hpp (no Objective-C, buildable and unit-tested on any platform
    // without an Apple toolchain) -- kept as a thin same-signature wrapper here, a trivial 1:1
    // name-matching switch onto the real Apple SDK enum, since only that final step genuinely needs
    // the Apple SDK.
    static MTLCompareFunction metalCompareFunction(int cmp)
    {
        using K = CNA::Internal::Renderers::Metal::MetalCompareFunctionKind;
        switch (CNA::Internal::Renderers::Metal::DescribeMetalCompareFunction(cmp)) {
            case K::Never:        return MTLCompareFunctionNever;
            case K::Less:         return MTLCompareFunctionLess;
            case K::LessEqual:    return MTLCompareFunctionLessEqual;
            case K::Equal:        return MTLCompareFunctionEqual;
            case K::GreaterEqual: return MTLCompareFunctionGreaterEqual;
            case K::Greater:      return MTLCompareFunctionGreater;
            case K::NotEqual:     return MTLCompareFunctionNotEqual;
            case K::Always:
            default:              return MTLCompareFunctionAlways;
        }
    }

    // plan_metal.md METAL-19: same extraction as metalCompareFunction() above, now backed by the
    // plain-C++ MetalStencilOperation.hpp.
    static MTLStencilOperation metalStencilOp(int op)
    {
        using K = CNA::Internal::Renderers::Metal::MetalStencilOperationKind;
        switch (CNA::Internal::Renderers::Metal::DescribeMetalStencilOperation(op)) {
            case K::Zero:            return MTLStencilOperationZero;
            case K::Replace:         return MTLStencilOperationReplace;
            case K::IncrementWrap:   return MTLStencilOperationIncrementWrap;
            case K::DecrementWrap:   return MTLStencilOperationDecrementWrap;
            case K::IncrementClamp:  return MTLStencilOperationIncrementClamp;
            case K::DecrementClamp:  return MTLStencilOperationDecrementClamp;
            case K::Invert:          return MTLStencilOperationInvert;
            case K::Keep:
            default:                 return MTLStencilOperationKeep;
        }
    }

    // plan_metal.md METAL-19: same extraction as metalCompareFunction() above, now backed by the
    // plain-C++ MetalBlend.hpp.
    static MTLBlendFactor metalBlendFactor(int xnaBlend)
    {
        using K = CNA::Internal::Renderers::Metal::MetalBlendFactorKind;
        switch (CNA::Internal::Renderers::Metal::DescribeMetalBlendFactor(xnaBlend)) {
            case K::Zero:                    return MTLBlendFactorZero;
            case K::SourceColor:             return MTLBlendFactorSourceColor;
            case K::OneMinusSourceColor:     return MTLBlendFactorOneMinusSourceColor;
            case K::SourceAlpha:             return MTLBlendFactorSourceAlpha;
            case K::OneMinusSourceAlpha:     return MTLBlendFactorOneMinusSourceAlpha;
            case K::DestinationColor:        return MTLBlendFactorDestinationColor;
            case K::OneMinusDestinationColor:return MTLBlendFactorOneMinusDestinationColor;
            case K::DestinationAlpha:        return MTLBlendFactorDestinationAlpha;
            case K::OneMinusDestinationAlpha:return MTLBlendFactorOneMinusDestinationAlpha;
            case K::BlendColor:              return MTLBlendFactorBlendColor;
            case K::OneMinusBlendColor:      return MTLBlendFactorOneMinusBlendColor;
            case K::SourceAlphaSaturated:    return MTLBlendFactorSourceAlphaSaturated;
            case K::One:
            default:                         return MTLBlendFactorOne;
        }
    }

    // plan_metal.md METAL-19: same extraction as metalCompareFunction() above, now backed by the
    // plain-C++ MetalBlendFunction.hpp.
    static MTLBlendOperation metalBlendOp(int xnaBlendFunc)
    {
        using K = CNA::Internal::Renderers::Metal::MetalBlendOperationKind;
        switch (CNA::Internal::Renderers::Metal::DescribeMetalBlendOperation(xnaBlendFunc)) {
            case K::Subtract:        return MTLBlendOperationSubtract;
            case K::ReverseSubtract: return MTLBlendOperationReverseSubtract;
            case K::Max:             return MTLBlendOperationMax;
            case K::Min:             return MTLBlendOperationMin;
            case K::Add:
            default:                 return MTLBlendOperationAdd;
        }
    }

    // plan_metal.md: real bug found and fixed 2026-07-20 -- the previous implementation here used
    // three independently-maintained case-set memberships (one per min/mag/mip axis), which had 3
    // of the 9 real XNA TextureFilter values (3/6/7) wrong (min/mag component swapped or dropped
    // relative to what each filter's own name specifies). The real per-filter logic now lives in
    // the plain-C++ MetalSamplerFilter.hpp (no Objective-C, buildable and unit-tested on any
    // platform without an Apple toolchain, unlike this translation to the real Metal enum type) --
    // these three functions are now thin wrappers so the existing call site is unaffected.
    static MTLSamplerMinMagFilter metalMinFilter(int filter)
    {
        return DescribeMetalSamplerFilter(filter).minIsPoint ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
    }
    static MTLSamplerMinMagFilter metalMagFilter(int filter)
    {
        return DescribeMetalSamplerFilter(filter).magIsPoint ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
    }
    static MTLSamplerMipFilter metalMipFilter(int filter)
    {
        return DescribeMetalSamplerFilter(filter).mipIsPoint ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear;
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

    // Builds the MTLVertexDescriptor for one of the 4 fixed byte-strides this renderer currently
    // recognizes -- byte-for-byte identical to the 4 descriptors the original constructor built
    // eagerly (vd16/vd20/vd24/vd32), just refactored so the now-lazy pipeline cache can build one
    // on demand instead of every stride having to exist up front.
    static MTLVertexDescriptor* vertexDescriptorForStride(std::size_t stride)
    {
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        if(!vd) throw std::runtime_error("Metal: failed to allocate vertex descriptor");
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

    // plan_metal.md METAL-27: translates one MetalVertexAttribKind (plain C++, see
    // MetalVertexAttribFormat.hpp) to the real Apple MTLVertexFormat it stands in for -- the one
    // piece of this table that must live here rather than in the plain-C++ header, since
    // MTLVertexFormat is declared only in <Metal/Metal.h>. Deliberately a trivial 1:1 rename (each
    // MetalVertexAttribKind enumerator is already named after its target), not a re-derivation, to
    // keep this translation as low-risk as possible.
    static MTLVertexFormat metalVertexFormat(MetalVertexAttribKind kind)
    {
        switch (kind) {
            case MetalVertexAttribKind::Float1:            return MTLVertexFormatFloat;
            case MetalVertexAttribKind::Float2:            return MTLVertexFormatFloat2;
            case MetalVertexAttribKind::Float3:            return MTLVertexFormatFloat3;
            case MetalVertexAttribKind::Float4:            return MTLVertexFormatFloat4;
            case MetalVertexAttribKind::UChar4Normalized:  return MTLVertexFormatUChar4Normalized;
            case MetalVertexAttribKind::UChar4:            return MTLVertexFormatUChar4;
            case MetalVertexAttribKind::Short2:            return MTLVertexFormatShort2;
            case MetalVertexAttribKind::Short4:             return MTLVertexFormatShort4;
            case MetalVertexAttribKind::Short2Normalized:  return MTLVertexFormatShort2Normalized;
            case MetalVertexAttribKind::Short4Normalized:  return MTLVertexFormatShort4Normalized;
            case MetalVertexAttribKind::Half2:             return MTLVertexFormatHalf2;
            case MetalVertexAttribKind::Half4:             return MTLVertexFormatHalf4;
        }
        return MTLVertexFormatFloat3;
    }

    // plan_metal.md METAL-26/27: builds a real MTLVertexDescriptor from an arbitrary
    // VertexElement list -- the generic counterpart to vertexDescriptorForStride()'s 8 hand-written
    // fixed layouts above. The actual attribute-shape decisions (which MetalVertexAttribKind, which
    // offset, which location) are made by the already-tested, plain-C++
    // BuildMetalVertexDescriptorPlan() (MetalVertexDescriptorPlan.hpp); this function only
    // mechanically walks that plan and calls the real Metal API. Not yet called from any live draw
    // path -- see MetalVertexBuffer::SetVertexDeclaration()'s own comment for why (no built-in
    // PipelineKind shader currently pairs with an arbitrary declaration; this becomes reachable
    // once Phase 14's custom ShaderEffect exists).
    static MTLVertexDescriptor* vertexDescriptorFromElements(int stride, const std::vector<VertexElement>& elements)
    {
        const MetalVertexDescriptorPlan plan = BuildMetalVertexDescriptorPlan(stride, elements);
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        if(!vd) throw std::runtime_error("Metal: failed to allocate generic vertex descriptor");
        for (const auto& attr : plan.attributes) {
            vd.attributes[attr.location].format = metalVertexFormat(attr.kind);
            vd.attributes[attr.location].offset = attr.offset;
            vd.attributes[attr.location].bufferIndex = attr.bufferIndex;
        }
        vd.layouts[0].stride = plan.stride;
        return vd;
    }

    // plan_metal.md METAL-6/24: real per-BlendState blend factors/operation, replacing the
    // previous hardcoded-into-every-pipeline straight-alpha blend. When !blend.enabled, blending
    // is left off entirely (matches BlendState.Opaque's real observable behavior).
    //
    // plan_metal.md METAL-112/113: `colorCount` (default 1, every pre-MRT call site unaffected)
    // declares the SAME format/blend for `colorCount` simultaneous attachments, matching
    // VulkanRenderer's own identical "one VkPipelineColorBlendAttachmentState replicated
    // colorAttachmentCount times" precedent (confirmed by reading it directly) -- XNA's own
    // BlendState is a single global state, not per-render-target, so there is no per-attachment
    // blend to vary here even during real MRT. Every attachment always shares
    // MTLPixelFormatBGRA8Unorm (MetalRenderTargetRenderer's own hardcoded choice, see narrative item
    // 77), so only the loop bound needs to change, not a per-slot format lookup.
    // plan_metal.md METAL-104: `sampleCount` (default 1, every pre-MSAA call site unaffected) must
    // match the active render pass's own sample count exactly, or pipeline creation is the same
    // class of genuine Metal API validation error `colorCount` above already documents for
    // attachment count -- an independent, orthogonal axis from MRT (a pipeline can be
    // multisampled with 1 color attachment, single-sampled with several, or both/neither).
    static id<MTLRenderPipelineState> makePipeline(id<MTLDevice> dev, id<MTLLibrary> lib,
                                                    NSString* vs, NSString* fs,
                                                    MTLVertexDescriptor* vd, const BlendKey& blend,
        int colorCount=1, int sampleCount=1)
    {
        MTLRenderPipelineDescriptor* d=[[MTLRenderPipelineDescriptor alloc] init];
        if(!d) throw std::runtime_error("Metal: failed to allocate render-pipeline descriptor");
        d.vertexFunction=[lib newFunctionWithName:vs];
        d.fragmentFunction=[lib newFunctionWithName:fs];
        if(!d.vertexFunction||!d.fragmentFunction){
            [d.vertexFunction release]; [d.fragmentFunction release]; [d release];
            throw std::runtime_error("Metal: built-in shader function lookup failed");
        }
        d.vertexDescriptor=vd;
        d.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8; d.stencilAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8;
        d.sampleCount=(NSUInteger)sampleCount;
        for (int i=0;i<colorCount;++i) {
            d.colorAttachments[i].pixelFormat=MTLPixelFormatBGRA8Unorm;
            d.colorAttachments[i].blendingEnabled = blend.enabled ? YES : NO;
            if (blend.enabled) {
                d.colorAttachments[i].sourceRGBBlendFactor=metalBlendFactor(blend.colorSrc);
                d.colorAttachments[i].destinationRGBBlendFactor=metalBlendFactor(blend.colorDst);
                d.colorAttachments[i].rgbBlendOperation=metalBlendOp(blend.colorFunc);
                d.colorAttachments[i].sourceAlphaBlendFactor=metalBlendFactor(blend.alphaSrc);
                d.colorAttachments[i].destinationAlphaBlendFactor=metalBlendFactor(blend.alphaDst);
                d.colorAttachments[i].alphaBlendOperation=metalBlendOp(blend.alphaFunc);
            }
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

    // plan_metal.md METAL-34-style extraction: these struct mirrors and their fill functions now
    // live in the plain-C++ MetalUniformFill.hpp (no Objective-C, buildable and unit-tested on any
    // platform without an Apple toolchain) -- kept as thin same-name aliases here so every existing
    // call site in this file is unaffected.
    using LitTransform = MetalLitTransform;
    using LitUniforms = MetalLitUniforms;
    using EnvTransform = MetalEnvTransform;
    using EnvUniforms = MetalEnvUniforms;
    using SkinnedTransform = MetalSkinnedTransform;
    using SkinnedUniforms = MetalSkinnedUniforms;
    using PbrTransform = MetalPbrTransform;
    using PbrUniforms = MetalPbrUniforms;
    using SkinnedPbrTransform = MetalSkinnedPbrTransform;

    // plan_metal.md METAL-34-style extraction: this formula's real logic now lives in the plain-C++
    // MetalNormalMatrix.hpp (no Objective-C, buildable and unit-tested on any platform without an
    // Apple toolchain) -- kept as a thin same-signature wrapper here so all 3 existing call sites
    // in this file are unaffected.
    static void computeNormalMatrixCols(const float* w, float col0[4], float col1[4], float col2[4])
    {
        ComputeMetalNormalMatrixCols(w, col0, col1, col2);
    }

    class MetalTexture final : public ITextureRenderer
    {
    public:
        MetalTexture(id<MTLDevice> dev, id<MTLCommandQueue> queue, const ImageData& data,
                     std::shared_ptr<MetalResourceHealth> resourceHealth,
                     std::function<void()> ownerHealthCheck)
            : w_(data.width), h_(data.height), resourceHealth_(std::move(resourceHealth)),
              ownerHealthCheck_(std::move(ownerHealthCheck))
        {
            if(!resourceHealth_||!ownerHealthCheck_)
                throw std::invalid_argument("Metal texture requires owner health");
            ownerHealthCheck_();
            MetalObjectOwner deviceOwner(retainMetalObject, releaseMetalObject);
            MetalObjectOwner queueOwner(retainMetalObject, releaseMetalObject);
            MetalObjectOwner textureOwner(retainMetalObject, releaseMetalObject);
            deviceOwner.Reset(dev);
            queueOwner.Reset(queue);
            MTLTextureDescriptor* d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                width:w_ height:h_ mipmapped:(data.mipLevels > 1)];
            if(!d) throw std::runtime_error("Metal: failed to allocate texture descriptor");
            d.usage=MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
            textureOwner.Adopt([dev newTextureWithDescriptor:d]);
            if (!textureOwner.HasValue()) throw std::runtime_error("Metal: failed to create texture");
            if (!data.pixels.empty()) {
                MTLRegion r=MTLRegionMake2D(0,0,w_,h_);
                [(id<MTLTexture>)textureOwner.Get() replaceRegion:r mipmapLevel:0
                    withBytes:data.pixels.data() bytesPerRow:(NSUInteger)w_*4];
            }
            dev_=(id<MTLDevice>)deviceOwner.ReleaseOwnership();
            queue_=(id<MTLCommandQueue>)queueOwner.ReleaseOwnership();
            texture_=(id<MTLTexture>)textureOwner.ReleaseOwnership();
        }
        ~MetalTexture() override { [texture_ release]; [queue_ release]; [dev_ release]; }
        int GetWidth() const override { return w_; }
        int GetHeight() const override { return h_; }

        // plan_metal.md METAL-256: real bug found (Phase 18's own resource-lifetime audit, METAL-175)
        // and fixed here -- the original implementation mutated `texture_` in place via
        // `replaceRegion:`, which Apple's own Metal synchronization guidance is explicit is NOT
        // automatically protected the way reference-counted object lifetime is (unlike
        // MetalVertexBuffer/MetalIndexBuffer's own SetData(), which already safely reallocates a
        // fresh id<MTLBuffer> instead of mutating in place, relying on Metal's documented "a command
        // buffer keeps every resource it references alive until its GPU work completes" guarantee --
        // that guarantee protects an object's *lifetime*, not its *contents*). A game calling
        // Texture2D.SetData() on a texture a still-GPU-executing prior frame is reading risked the
        // GPU observing torn/partially-updated content.
        //
        // Mirrors that already-safe reallocate-on-write pattern here too, extended with the one
        // genuine extra step a texture needs that a buffer does not: reallocating outright would
        // otherwise silently lose any *other*, separately-uploaded mip level (UpdatePixels() only
        // ever touches level 0; UpdatePixelsLevel() only ever touches its own one level), since a
        // freshly allocated MTLTexture starts completely uninitialized. Blit-copies every level
        // except the one being updated from the old texture into the new one first (skipped
        // entirely when there is only one level, the overwhelmingly common non-mipmapped case, so
        // this adds no extra GPU round-trip there), then writes the new pixel data into the new
        // texture's target level via the usual replaceRegion:, then swaps `texture_` to the new
        // object and releases the old one -- by the time this returns, `texture_` always refers to
        // a texture nothing has ever mutated in place while GPU-visible.
        void reallocateAndUpdate(int targetLevel, const uint8_t* rgba, int bytesPerRow, int levelW, int levelH)
        {
            const NSUInteger levelCount = texture_.mipmapLevelCount;
            MTLTextureDescriptor* d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                width:(NSUInteger)w_ height:(NSUInteger)h_ mipmapped:(levelCount>1)];
            if(!d) throw std::runtime_error("Metal: failed to allocate replacement texture descriptor");
            d.usage=MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
            MetalObjectOwner newTextureOwner(retainMetalObject, releaseMetalObject);
            newTextureOwner.Adopt([dev_ newTextureWithDescriptor:d]);
            if (!newTextureOwner.HasValue()) throw std::runtime_error("Metal: failed to allocate replacement texture for UpdatePixels");
            id<MTLTexture> newTex=(id<MTLTexture>)newTextureOwner.Get();
            if (levelCount>1) {
                MetalObjectOwner commandOwner(retainMetalObject, releaseMetalObject);
                commandOwner.Reset([queue_ commandBuffer]);
                if (!commandOwner.HasValue()) throw std::runtime_error("Metal: failed to allocate mip-preservation command buffer");
                id<MTLCommandBuffer> cmd=(id<MTLCommandBuffer>)commandOwner.Get();
                id<MTLBlitCommandEncoder> blit=[cmd blitCommandEncoder];
                if (!blit) throw std::runtime_error("Metal: failed to allocate mip-preservation blit encoder");
                for (NSUInteger lvl=0; lvl<levelCount; ++lvl) {
                    if ((int)lvl==targetLevel) continue;
                    const NSUInteger lw=std::max<NSUInteger>(1,(NSUInteger)w_>>lvl);
                    const NSUInteger lh=std::max<NSUInteger>(1,(NSUInteger)h_>>lvl);
                    [blit copyFromTexture:texture_ sourceSlice:0 sourceLevel:lvl
                              sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(lw,lh,1)
                                 toTexture:newTex destinationSlice:0 destinationLevel:lvl destinationOrigin:MTLOriginMake(0,0,0)];
                }
                [blit endEncoding];
                [cmd commit];
                [cmd waitUntilCompleted];
                if (cmd.status!=MTLCommandBufferStatusCompleted)
                    throw std::runtime_error("Metal: mip-preservation blit failed");
                ownerHealthCheck_();
            }
            MTLRegion r=MTLRegionMake2D(0,0,(NSUInteger)levelW,(NSUInteger)levelH);
            [newTex replaceRegion:r mipmapLevel:(NSUInteger)targetLevel withBytes:rgba bytesPerRow:(NSUInteger)bytesPerRow];
            [texture_ release];
            texture_=(id<MTLTexture>)newTextureOwner.ReleaseOwnership();
        }
        void UpdatePixels(const uint8_t* rgba, int stride) override {
            ownerHealthCheck_();
            MetalTextureTransferLayout layout{};
            if(!rgba||w_>std::numeric_limits<int>::max()/4||
               !TryBuildMetalTextureTransferLayout(w_,h_,1,1,layout)||
               stride!=static_cast<int>(static_cast<std::size_t>(w_)*4u))
                throw std::invalid_argument("Metal: invalid Texture2D level-zero upload");
            reallocateAndUpdate(0, rgba, stride, w_, h_);
            ownerHealthCheck_();
        }
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int lw, int lh) override {
            ownerHealthCheck_();
            if(!rgba||level<0||level>=(int)texture_.mipmapLevelCount||
               lw!=MetalTextureTransferDetail::MipDimension(w_,level)||
               lh!=MetalTextureTransferDetail::MipDimension(h_,level)||
               static_cast<std::size_t>(lw)>static_cast<std::size_t>(std::numeric_limits<int>::max()/4))
                throw std::invalid_argument("Metal: invalid Texture2D mip upload");
            reallocateAndUpdate(level, rgba, lw*4, lw, lh);
            ownerHealthCheck_();
        }
        bool GetData(int level,int x,int y,int w,int h,void* data,int dataLength) const override
        {
            ownerHealthCheck_();
            (void)level; (void)x; (void)y; (void)w; (void)h; (void)data; (void)dataLength;
            return false;
        }
        id<MTLTexture> native() const
        {
            ownerHealthCheck_();
            return texture_;
        }
    private:
        id<MTLDevice> dev_=nil; id<MTLCommandQueue> queue_=nil;
        int w_, h_; id<MTLTexture> texture_ = nil;
        std::shared_ptr<MetalResourceHealth> resourceHealth_;
        std::function<void()> ownerHealthCheck_;
    };

    // The factory validates SurfaceFormat::Color before construction. Metal allocates its RGBA8
    // cube mip chain as one texture object, so each requested level exists before SetData.
    class MetalTextureCube final : public ITextureCubeRenderer
    {
    public:
        // plan_metal.md METAL-122: `queue` is stored (retained) purely so GetData() below can blit
        // -- found while implementing RenderTargetCube's own GetData() that TextureCube.cpp's real
        // code (not just this interface's own doc comment) *always* calls renderer_->GetData()
        // unconditionally, with no CPU-side pixel-shadow shortcut the way Texture2D has; unlike the
        // 2D case, a plain (non-render-target) cube texture's GetData() genuinely reaches the
        // renderer for every call, so this was a real, previously-shipping gap, not a deliberate
        // scope match to Texture2D's own precedent.
        MetalTextureCube(id<MTLDevice> dev, id<MTLCommandQueue> queue, int size, bool mipMap,
                         std::shared_ptr<MetalResourceHealth> resourceHealth,
                         std::function<void()> ownerHealthCheck)
            : size_(size), mipMap_(mipMap), levelCount_(MetalMipLevelCount(size, size, mipMap)),
              resourceHealth_(std::move(resourceHealth)),
              ownerHealthCheck_(std::move(ownerHealthCheck))
        {
            if(!resourceHealth_||!ownerHealthCheck_)
                throw std::invalid_argument("Metal cube texture requires owner health");
            ownerHealthCheck_();
            MetalObjectOwner deviceOwner(retainMetalObject, releaseMetalObject);
            MetalObjectOwner queueOwner(retainMetalObject, releaseMetalObject);
            MetalObjectOwner textureOwner(retainMetalObject, releaseMetalObject);
            deviceOwner.Reset(dev);
            queueOwner.Reset(queue);
            MTLTextureDescriptor* d=[MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm size:(NSUInteger)size mipmapped:mipMap];
            if(!d) throw std::runtime_error("Metal: failed to allocate cube texture descriptor");
            d.usage=MTLTextureUsageShaderRead;
            textureOwner.Adopt([dev newTextureWithDescriptor:d]);
            if(!textureOwner.HasValue()) throw std::runtime_error("Metal: failed to create cube texture");
            dev_=(id<MTLDevice>)deviceOwner.ReleaseOwnership();
            queue_=(id<MTLCommandQueue>)queueOwner.ReleaseOwnership();
            texture_=(id<MTLTexture>)textureOwner.ReleaseOwnership();
        }
        ~MetalTextureCube() override { [texture_ release]; [queue_ release]; [dev_ release]; }
        // Face ordinals (0=+X,1=-X,2=+Y,3=-Y,4=+Z,5=-Z) already match Metal's own cube `slice`
        // ordering directly -- same convention documented on IRenderTargetCubeRenderer and already
        // relied upon, unchanged, by every other renderer (confirmed against
        // EasyGLTextureCubeRenderer's own kCubeFaceTargets order).
        bool SetData(int face,int level,int x,int y,int w,int h,const void* data,int dataLength) override
        {
            ownerHealthCheck_();
            if(face<0||face>=6) return false;
            MetalTextureTransferLayout layout{};
            if (!TryPrepareMetalTextureTransfer(
                    size_,size_,1,levelCount_,level,x,y,0,w,h,1,data,dataLength,
                    MetalTransferLengthRule::AtLeastTightBytes,1,layout)) return false;
            reallocateAndUpdate(face,level,x,y,w,h,data,layout);
            ownerHealthCheck_();
            return true;
        }
        bool GetData(int face,int level,int x,int y,int w,int h,void* data,int dataLength) const override
        {
            ownerHealthCheck_();
            if(face<0||face>=6) return false;
            MetalTextureTransferLayout layout{};
            if (!TryPrepareMetalTextureTransfer(
                    size_,size_,1,levelCount_,level,x,y,0,w,h,1,data,dataLength,
                    MetalTransferLengthRule::ExactlyTightBytes,
                    MetalMacOsTextureBufferRowAlignment,layout)) return false;
            blitTextureToClientBuffer(dev_,queue_,texture_,(NSUInteger)face,level,x,y,0,w,h,1,
                                      layout,MetalTransferPixelOrder::Rgba,data,
                                      ownerHealthCheck_);
            ownerHealthCheck_();
            return true;
        }
        int GetSizeEXT() const noexcept override { return size_; }
        id<MTLTexture> native() const
        {
            ownerHealthCheck_();
            return texture_;
        }
    private:
        void reallocateAndUpdate(int face,int level,int x,int y,int w,int h,const void* data,
                                 const MetalTextureTransferLayout& layout)
        {
            MetalObjectOwner replacementOwner(retainMetalObject,releaseMetalObject);
            MTLTextureDescriptor* d=[MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm size:(NSUInteger)size_ mipmapped:mipMap_];
            if(!d) throw std::runtime_error("Metal: failed to allocate replacement cube texture descriptor");
            d.usage=MTLTextureUsageShaderRead;
            replacementOwner.Adopt([dev_ newTextureWithDescriptor:d]);
            if(!replacementOwner.HasValue()) throw std::runtime_error("Metal: failed to allocate replacement cube texture");
            id<MTLTexture> replacement=(id<MTLTexture>)replacementOwner.Get();
            MetalObjectOwner commandOwner(retainMetalObject,releaseMetalObject);
            commandOwner.Reset([queue_ commandBuffer]);
            if(!commandOwner.HasValue()) throw std::runtime_error("Metal: failed to allocate cube preservation command buffer");
            id<MTLCommandBuffer> command=(id<MTLCommandBuffer>)commandOwner.Get();
            id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
            if(!blit) throw std::runtime_error("Metal: failed to allocate cube preservation blit encoder");
            for(int mip=0;mip<levelCount_;++mip){
                const NSUInteger dimension=(NSUInteger)MetalTextureTransferDetail::MipDimension(size_,mip);
                for(int slice=0;slice<6;++slice){
                    const bool replacesWholeSubresource=slice==face&&mip==level&&x==0&&y==0&&
                        (NSUInteger)w==dimension&&(NSUInteger)h==dimension;
                    if(replacesWholeSubresource) continue;
                    [blit copyFromTexture:texture_ sourceSlice:(NSUInteger)slice sourceLevel:(NSUInteger)mip
                              sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(dimension,dimension,1)
                                 toTexture:replacement destinationSlice:(NSUInteger)slice destinationLevel:(NSUInteger)mip
                        destinationOrigin:MTLOriginMake(0,0,0)];
                }
            }
            [blit endEncoding]; [command commit]; [command waitUntilCompleted];
            if(command.status!=MTLCommandBufferStatusCompleted) throw std::runtime_error("Metal: cube preservation blit failed");
            ownerHealthCheck_();
            MTLRegion r=MTLRegionMake2D((NSUInteger)x,(NSUInteger)y,(NSUInteger)w,(NSUInteger)h);
            [replacement replaceRegion:r mipmapLevel:(NSUInteger)level slice:(NSUInteger)face
                             withBytes:data bytesPerRow:(NSUInteger)layout.tightRowBytes bytesPerImage:0];
            [texture_ release];
            texture_=(id<MTLTexture>)replacementOwner.ReleaseOwnership();
        }
        id<MTLDevice> dev_=nil; id<MTLCommandQueue> queue_=nil;
        int size_=0; bool mipMap_=false; int levelCount_=1;
        id<MTLTexture> texture_=nil;
        std::shared_ptr<MetalResourceHealth> resourceHealth_;
        std::function<void()> ownerHealthCheck_;
    };

    class MetalTexture3D final : public ITexture3DRenderer
    {
    public:
        // plan_metal.md METAL-122 (3D analog): same finding as MetalTextureCube above --
        // Texture3D.cpp's real code always calls renderer_->GetData() unconditionally, no CPU-side
        // shadow shortcut, so this was a real, previously-shipping gap for any Texture3D, not just
        // a render-target-only concern.
        MetalTexture3D(id<MTLDevice> dev, id<MTLCommandQueue> queue, int w,int h,int depth,bool mipMap,
                       std::shared_ptr<MetalResourceHealth> resourceHealth,
                       std::function<void()> ownerHealthCheck)
            : w_(w), h_(h), depth_(depth), levelCount_(MetalMipLevelCount(w,h,mipMap)),
              resourceHealth_(std::move(resourceHealth)),
              ownerHealthCheck_(std::move(ownerHealthCheck))
        {
            if(!resourceHealth_||!ownerHealthCheck_)
                throw std::invalid_argument("Metal 3D texture requires owner health");
            ownerHealthCheck_();
            MetalObjectOwner deviceOwner(retainMetalObject,releaseMetalObject);
            MetalObjectOwner queueOwner(retainMetalObject,releaseMetalObject);
            MetalObjectOwner textureOwner(retainMetalObject,releaseMetalObject);
            deviceOwner.Reset(dev); queueOwner.Reset(queue);
            MTLTextureDescriptor* d=[[MTLTextureDescriptor alloc] init];
            if(!d) throw std::runtime_error("Metal: failed to allocate 3D texture descriptor");
            d.textureType=MTLTextureType3D; d.pixelFormat=MTLPixelFormatRGBA8Unorm;
            d.width=(NSUInteger)w; d.height=(NSUInteger)h; d.depth=(NSUInteger)depth;
            d.mipmapLevelCount=(NSUInteger)levelCount_;
            d.usage=MTLTextureUsageShaderRead;
            textureOwner.Adopt([dev newTextureWithDescriptor:d]); [d release];
            if(!textureOwner.HasValue()) throw std::runtime_error("Metal: failed to create 3D texture");
            dev_=(id<MTLDevice>)deviceOwner.ReleaseOwnership();
            queue_=(id<MTLCommandQueue>)queueOwner.ReleaseOwnership();
            texture_=(id<MTLTexture>)textureOwner.ReleaseOwnership();
        }
        ~MetalTexture3D() override { [texture_ release]; [queue_ release]; [dev_ release]; }
        bool SetData(int level,int x,int y,int z,int w,int h,int depth,const void* data,int dataLength) override
        {
            ownerHealthCheck_();
            MetalTextureTransferLayout layout{};
            if(!TryPrepareMetalTextureTransfer(
                    w_,h_,depth_,levelCount_,level,x,y,z,w,h,depth,data,dataLength,
                    MetalTransferLengthRule::AtLeastTightBytes,1,layout)) return false;
            reallocateAndUpdate(level,x,y,z,w,h,depth,data,layout);
            ownerHealthCheck_();
            return true;
        }
        // plan_metal.md METAL-125: real per-level readback via the shared blit helper -- slice is
        // always 0 for a 3D texture (there is no per-slice concept the way a cube's 6 faces have;
        // `z`/`depth` address the volume directly within slice 0).
        bool GetData(int level,int x,int y,int z,int w,int h,int depth,void* data,int dataLength) const override
        {
            ownerHealthCheck_();
            MetalTextureTransferLayout layout{};
            if(!TryPrepareMetalTextureTransfer(
                    w_,h_,depth_,levelCount_,level,x,y,z,w,h,depth,data,dataLength,
                    MetalTransferLengthRule::ExactlyTightBytes,
                    MetalMacOsTextureBufferRowAlignment,layout)) return false;
            blitTextureToClientBuffer(dev_,queue_,texture_,0,level,x,y,z,w,h,depth,layout,
                                      MetalTransferPixelOrder::Rgba,data,ownerHealthCheck_);
            ownerHealthCheck_();
            return true;
        }
        void GetDimensionsEXT(int& width, int& height, int& depth) const noexcept override
        {
            width=w_; height=h_; depth=depth_;
        }
        id<MTLTexture> native() const
        {
            ownerHealthCheck_();
            return texture_;
        }
    private:
        void reallocateAndUpdate(int level,int x,int y,int z,int w,int h,int depth,const void* data,
                                 const MetalTextureTransferLayout& layout)
        {
            MetalObjectOwner replacementOwner(retainMetalObject,releaseMetalObject);
            MTLTextureDescriptor* descriptor=[[MTLTextureDescriptor alloc] init];
            if(!descriptor) throw std::runtime_error("Metal: failed to allocate replacement 3D texture descriptor");
            descriptor.textureType=MTLTextureType3D; descriptor.pixelFormat=MTLPixelFormatRGBA8Unorm;
            descriptor.width=(NSUInteger)w_; descriptor.height=(NSUInteger)h_; descriptor.depth=(NSUInteger)depth_;
            descriptor.mipmapLevelCount=(NSUInteger)levelCount_; descriptor.usage=MTLTextureUsageShaderRead;
            replacementOwner.Adopt([dev_ newTextureWithDescriptor:descriptor]); [descriptor release];
            if(!replacementOwner.HasValue()) throw std::runtime_error("Metal: failed to allocate replacement 3D texture");
            id<MTLTexture> replacement=(id<MTLTexture>)replacementOwner.Get();
            MetalObjectOwner commandOwner(retainMetalObject,releaseMetalObject);
            commandOwner.Reset([queue_ commandBuffer]);
            if(!commandOwner.HasValue()) throw std::runtime_error("Metal: failed to allocate 3D preservation command buffer");
            id<MTLCommandBuffer> command=(id<MTLCommandBuffer>)commandOwner.Get();
            id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
            if(!blit) throw std::runtime_error("Metal: failed to allocate 3D preservation blit encoder");
            for(int mip=0;mip<levelCount_;++mip){
                const NSUInteger mw=(NSUInteger)MetalTextureTransferDetail::MipDimension(w_,mip);
                const NSUInteger mh=(NSUInteger)MetalTextureTransferDetail::MipDimension(h_,mip);
                const NSUInteger md=(NSUInteger)MetalTextureTransferDetail::MipDimension(depth_,mip);
                const bool replacesWholeSubresource=mip==level&&x==0&&y==0&&z==0&&
                    (NSUInteger)w==mw&&(NSUInteger)h==mh&&(NSUInteger)depth==md;
                if(replacesWholeSubresource) continue;
                [blit copyFromTexture:texture_ sourceSlice:0 sourceLevel:(NSUInteger)mip
                          sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(mw,mh,md)
                             toTexture:replacement destinationSlice:0 destinationLevel:(NSUInteger)mip
                    destinationOrigin:MTLOriginMake(0,0,0)];
            }
            [blit endEncoding]; [command commit]; [command waitUntilCompleted];
            if(command.status!=MTLCommandBufferStatusCompleted) throw std::runtime_error("Metal: 3D preservation blit failed");
            ownerHealthCheck_();
            MTLRegion region=MTLRegionMake3D((NSUInteger)x,(NSUInteger)y,(NSUInteger)z,
                                             (NSUInteger)w,(NSUInteger)h,(NSUInteger)depth);
            [replacement replaceRegion:region mipmapLevel:(NSUInteger)level slice:0 withBytes:data
                           bytesPerRow:(NSUInteger)layout.tightRowBytes
                         bytesPerImage:(NSUInteger)layout.tightImageBytes];
            [texture_ release]; texture_=(id<MTLTexture>)replacementOwner.ReleaseOwnership();
        }
        id<MTLDevice> dev_=nil; id<MTLCommandQueue> queue_=nil;
        int w_=0, h_=0, depth_=0; int levelCount_=1;
        id<MTLTexture> texture_=nil;
        std::shared_ptr<MetalResourceHealth> resourceHealth_;
        std::function<void()> ownerHealthCheck_;
    };

    class MetalVertexBuffer final : public IVertexBufferRenderer
    {
    public:
        explicit MetalVertexBuffer(id<MTLDevice> dev, int cap) : dev_(dev), capacity_(cap) { [dev_ retain]; }
        ~MetalVertexBuffer() override { [buffer_ release]; [dev_ release]; }
        void SetData(const void* data,int count,std::size_t stride) override {
            std::size_t bytes=0;
            if(!data||stride>static_cast<std::size_t>(std::numeric_limits<int>::max())||
               !TryMetalBufferByteCount(count,stride,bytes))
                throw std::invalid_argument("Metal: invalid vertex buffer upload");
            id<MTLBuffer> replacement=[dev_ newBufferWithBytes:data length:(NSUInteger)bytes options:MTLResourceStorageModeShared];
            if(!replacement) throw std::runtime_error("Metal: failed to create vertex buffer");
            [buffer_ release]; buffer_=replacement; count_=count; stride_=stride;
        }
        int GetVertexCount() const override { return count_; }
        id<MTLBuffer> native() const { return buffer_; }
        std::size_t stride() const { return stride_; }
        // plan_metal.md METAL-26: generic-layout hook (IVertexBufferRenderer's own doc comment,
        // Task 1080) -- just stores the declaration, mirroring
        // EasyGLVertexBufferRenderer::SetVertexDeclaration()'s own identical trivial-storage
        // pattern exactly. Every native draw route now validates this captured declaration against
        // the canonical layout selected by its stock pipeline before submission; declarations not
        // faithfully representable by that fixed pipeline are rejected deterministically.
        void SetVertexDeclaration(const VertexDeclaration& declaration) override
        {
            declaration_.Remember(declaration);
        }
        const CNA::Internal::Graphics::DeclaredVertexLayout& declaration() const { return declaration_; }
    private:
        id<MTLDevice> dev_; id<MTLBuffer> buffer_=nil; int capacity_=0,count_=0; std::size_t stride_=0;
        CNA::Internal::Graphics::DeclaredVertexLayout declaration_;
    };

    class MetalIndexBuffer final : public IIndexBufferRenderer
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
        void upload(const void* d,int n,int sz,bool v32){
            std::size_t bytes=0;
            if(!d||!TryMetalBufferByteCount(n,(std::size_t)sz,bytes)) throw std::invalid_argument("Metal: invalid index buffer upload");
            id<MTLBuffer> replacement=[dev_ newBufferWithBytes:d length:(NSUInteger)bytes options:MTLResourceStorageModeShared];
            if(!replacement) throw std::runtime_error("Metal: failed to create index buffer");
            [buffer_ release];buffer_=replacement;is32_=v32;count_=n;
        }
        id<MTLDevice> dev_; id<MTLBuffer> buffer_=nil; bool is32_; int count_=0;
    };
}

// plan_metal.md Phase 10: forward-declared so Impl can hold a non-owning pointer to whichever
// MetalRenderTargetRenderer is currently bound (ownership lives in the RenderTarget2D C++ object
// via its own unique_ptr<IRenderTargetRenderer>, matching every other renderer's convention) --
// MetalRenderTargetRenderer itself is defined later, after Impl, since it needs Impl to be a
// complete type. Impl::resolveActiveAttachments() (used by ensureFrame()/clear()) is declared
// inside Impl below but its body is defined out-of-line, after MetalRenderTargetRenderer, for the
// same reason -- C++ allows this: a member function's body has complete-class access to sibling
// members regardless of textual declaration/definition order.
class MetalRenderTargetRenderer;
// plan_metal.md METAL-109: same forward-declaration reasoning as MetalRenderTargetRenderer above,
// for the cube-map analog -- Impl needs a non-owning pointer to whichever RenderTargetCube face is
// currently bound.
class MetalRenderTargetCubeRenderer;

// plan_metal.md Phase 10: a previously-rendered-to RenderTarget2D used as an ordinary texture in a
// later draw (post-processing, portals, mirrors, etc.) is a real, common XNA pattern --
// MetalRenderTargetRenderer does NOT inherit from MetalTexture (they are separate sibling
// hierarchies that both implement ITextureRenderer directly, matching the real interface shape:
// IRenderTargetRenderer : ITextureRenderer, not IRenderTargetRenderer : ITextureRenderer, MetalTexture),
// so a bare `dynamic_cast<const MetalTexture*>` on an ITextureRenderer* slot would silently fail
// (return null) whenever it actually holds a render target -- every texture-binding call site in
// this file goes through this one helper instead of dynamic_cast<MetalTexture*> directly, so this
// gets fixed everywhere at once rather than risking a missed call site. Forward-declared here
// (needs MetalSpriteBatch, defined before MetalRenderTargetRenderer, to be able to call it), defined
// out-of-line after MetalRenderTargetRenderer for the same reason as resolveActiveAttachments above.
static id<MTLTexture> nativeTextureFor(const ITextureRenderer* t);

// plan_metal.md METAL-109/METAL-64ff: same reasoning as nativeTextureFor() above, for the cube
// case -- EnvironmentMapEffect's envMap slot is an ITextureCubeRenderer*, and a RenderTargetCube
// rendered into and then sampled as a reflection source (a very common real-time-reflection XNA
// pattern -- arguably RenderTargetCube's single most common real use) must resolve through here
// too, or the existing bare `dynamic_cast<const MetalTextureCube*>` at the EnvMap32 draw site would
// silently fail for it exactly the way the 2D case did before nativeTextureFor() existed.
static id<MTLTexture> nativeCubeTextureFor(const ITextureCubeRenderer* t);

static id<CAMetalDrawable> retainMetalDrawable(id<CAMetalDrawable> value)
{
    [value retain];
    return value;
}

static void releaseMetalDrawable(id<CAMetalDrawable> value)
{
    [value release];
}

// plan_metal.md METAL-104/105: real MSAA. `texture2DDescriptorWithPixelFormat:...mipmapped:`
// (used everywhere else in this file) always produces an `MTLTextureType2D` descriptor with no way
// to request multisampling -- a genuinely multisampled texture needs an explicit
// `MTLTextureType2DMultisample` descriptor with `sampleCount` set, built by hand instead of via
// that convenience initializer. Shared by the backbuffer's own `msaaColorTexture`/`depthTexture`
// (resolveActiveAttachments()) and `MetalRenderTargetRenderer`'s own per-target opt-in.
static id<MTLTexture> makeMultisampleTexture(id<MTLDevice> dev, MTLPixelFormat fmt, NSUInteger w, NSUInteger h, NSUInteger samples, MTLTextureUsage usage)
{
    MTLTextureDescriptor* d=[[MTLTextureDescriptor alloc] init];
    if(!d) throw std::runtime_error("Metal: failed to allocate multisample texture descriptor");
    d.textureType=MTLTextureType2DMultisample; d.pixelFormat=fmt; d.width=w; d.height=h;
    d.sampleCount=samples; d.mipmapLevelCount=1; d.storageMode=MTLStorageModePrivate; d.usage=usage;
    id<MTLTexture> t=[dev newTextureWithDescriptor:d];
    [d release];
    return t;
}

struct MetalRenderer::Impl
{
    explicit Impl(const RendererSurfaceInfo& surfaceInfo)
        : surface(surfaceInfo, "MetalRenderer")
    {
    }

    ~Impl();

    PlatformRendererSurfaceState surface;
    CNAMetalView* view=nil;
    CAMetalLayer* layer=nil;
    id<MTLDevice> device=nil;
    id<MTLCommandQueue> queue=nil;
    id<MTLLibrary> library=nil;
    // plan_metal.md METAL-33: no eviction by design, not an oversight -- see
    // MetalPipelineKey.hpp's own MetalPipelineCacheKey comment for the real bounded-size reasoning.
    std::unordered_map<PipelineCacheKey, id<MTLRenderPipelineState>, PipelineCacheKeyHash> pipelineCache;
    id<MTLDepthStencilState> depthState=nil;
    id<MTLSamplerState> sampler=nil;
    std::unordered_map<uint32_t, id<MTLSamplerState>> samplerCache;
    id<MTLSamplerState> samplerSlots[16]={};
    id<MTLCommandBuffer> command=nil;
    id<MTLRenderCommandEncoder> encoder=nil;
    MetalRetainedResource<id<CAMetalDrawable>> drawable{retainMetalDrawable, releaseMetalDrawable};
    std::shared_ptr<MetalCommandFailureLatch> commandFailureLatch=
        std::make_shared<MetalCommandFailureLatch>();
    std::shared_ptr<MetalResourceHealth> resourceHealth=
        std::make_shared<MetalResourceHealth>(commandFailureLatch);
    MetalFrameAvailabilityState frameAvailability{};
    id<MTLTexture> depthTexture=nil;
    // plan_metal.md METAL-104/105: real backbuffer MSAA. `deviceSampleCount` is the real,
    // device-clamped sample count (1 = no MSAA) -- set at construction from
    // GraphicsRendererCreateArgs::multiSampleCount, and reconfigurable at runtime via
    // ApplyMultiSampleCount() (GraphicsDevice::Reset() forwarding a changed
    // GraphicsDeviceManager.PreferMultiSampling). `msaaColorTexture` is the backbuffer's own
    // multisampled render target, lazily (re)allocated by resolveActiveAttachments() the same way
    // `depthTexture` above already lazily reallocates on a width/height change -- resolved into the
    // real drawable via MTLStoreActionStoreAndMultisampleResolve on every encoder boundary (not
    // plain MTLStoreActionMultisampleResolve, which would discard the MSAA texture's own content
    // and silently lose accumulated multisample data across this file's own established
    // multiple-encoders-per-logical-frame architecture -- Clear()/RenderTarget2D switches routinely
    // end and restart encoders mid-frame). `activeSampleCount` is the sample count of whichever
    // render pass ensureFrame()/clear() most recently built (1 outside MSAA, matching
    // activeColorAttachmentCount's own analogous per-frame tracking above) and feeds
    // MetalPipelineCacheKey the same way.
    id<MTLTexture> msaaColorTexture=nil;
    int deviceSampleCount=1;
    int activeSampleCount=1;
    int virtualW=0,virtualH=0,presentationMode=0,swapInterval=1;
    bool depthEnabled=true,depthWrite=true;
    int refStencil=0;
    MetalRasterState rasterState{};
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

    struct DepthStateSnapshot
    {
        bool depthEnabled;
        bool depthWrite;
        int depthFunc;
        bool stencilEnabled;
        int stencilFunc;
        int stencilPass;
        int stencilFail;
        int stencilDepthFail;
        int stencilMask;
        int stencilWriteMask;
        bool twoSidedStencil;
        int ccwStencilFunc;
        int ccwStencilPass;
        int ccwStencilFail;
        int ccwStencilDepthFail;
        int refStencil;
    };

    [[nodiscard]] DepthStateSnapshot captureDepthState() const noexcept
    {
        return {depthEnabled,depthWrite,depthFunc,stencilEnabled,stencilFunc,stencilPass,
            stencilFail,stencilDepthFail,stencilMask,stencilWriteMask,twoSidedStencil,
            ccwStencilFunc,ccwStencilPass,ccwStencilFail,ccwStencilDepthFail,refStencil};
    }

    void restoreDepthState(const DepthStateSnapshot& state) noexcept
    {
        depthEnabled=state.depthEnabled; depthWrite=state.depthWrite; depthFunc=state.depthFunc;
        stencilEnabled=state.stencilEnabled; stencilFunc=state.stencilFunc;
        stencilPass=state.stencilPass; stencilFail=state.stencilFail;
        stencilDepthFail=state.stencilDepthFail; stencilMask=state.stencilMask;
        stencilWriteMask=state.stencilWriteMask; twoSidedStencil=state.twoSidedStencil;
        ccwStencilFunc=state.ccwStencilFunc; ccwStencilPass=state.ccwStencilPass;
        ccwStencilFail=state.ccwStencilFail; ccwStencilDepthFail=state.ccwStencilDepthFail;
        refStencil=state.refStencil;
    }

    // plan_metal.md METAL-136/137: real occlusion queries via MTLVisibilityResultBuffer. This
    // buffer must be attached to the MTLRenderPassDescriptor at render-pass-creation time (Metal
    // has no way to attach it mid-encoder the way setVisibilityResultMode:offset: can be called
    // mid-encoder) -- so it is allocated once here and referenced by every render pass
    // ensureFrame()/clear() create, with each MetalOcclusionQueryRenderer instance owning one
    // 8-byte slot (a uint64_t sample-passed count written by the GPU) via a simple incrementing
    // counter. kMaxOcclusionQuerySlots is a practical, generous cap, not an API-imposed one.
    static constexpr int kMaxOcclusionQuerySlots = 1024;
    id<MTLBuffer> visibilityBuffer=nil;
    int nextQuerySlot=0;

    // plan_metal.md Phase 10 (METAL-98/107): non-owning pointer to whichever RenderTarget2D is
    // currently bound; nullptr means "drawing to the backbuffer" (the default). Ownership lives in
    // the RenderTarget2D C++ object's own unique_ptr<IRenderTargetRenderer>, matching every other
    // renderer's convention -- MetalRenderTargetRenderer's own destructor clears this pointer if it
    // is destroyed while still bound, so it never dangles.
    MetalRenderTargetRenderer* currentRenderTarget=nullptr;

    // plan_metal.md METAL-109/110: same non-owning-pointer convention as currentRenderTarget above,
    // for whichever face of whichever RenderTargetCube is currently bound (mutually exclusive with
    // currentRenderTarget -- SetRenderTarget2D()/SetRenderTargetCubeFace() each cross-unbind the
    // other before binding, matching EasyGLRenderer::SetRenderTarget2D/SetRenderTargetCubeFace's
    // own established real contract exactly, not an invented convention).
    MetalRenderTargetCubeRenderer* currentRenderTargetCube=nullptr;
    NSUInteger currentRenderTargetCubeFace=0;

    // plan_metal.md METAL-112/113: real MRT. `currentMRT.size()>=2` means true simultaneous
    // multi-attachment rendering is active; `currentMRT[0]` is always mirrored into
    // currentRenderTarget above too (so computeSpriteTransform()/the single-target destructor
    // safety net/etc. keep working unmodified against target 0), but ensureFrame()/clear() check
    // currentMRT directly first so they build a render pass with every bound attachment, not just
    // target 0. activeColorAttachmentCount feeds MetalPipelineCacheKey (1 outside MRT, up to
    // Metal's own 8-attachment hardware limit during MRT) -- every entry point that changes which
    // render target(s) are active (SetRenderTarget2D/SetRenderTargetCubeFace/SetRenderTargets/the
    // MetalRenderTargetRenderer destructor's own safety net) must keep this and currentMRT
    // consistent; unbindCurrentMRT() (defined out-of-line after MetalRenderTargetRenderer, same
    // reason as resolveActiveAttachments()/computeSpriteTransform() above) is the single chokepoint
    // that tears either down correctly, including per-target mip regeneration.
    std::vector<MetalRenderTargetRenderer*> currentMRT;
    int activeColorAttachmentCount=1;
    void unbindCurrentMRT();

    // plan_metal.md METAL-87: fallback textures for PbrEffect's 4 optional maps (normalMap/
    // metallicRoughnessMap/emissiveMap/occlusionMap) when left unbound, mirroring
    // EasyGLRenderer::EnsureDefaultWhiteTexture()/EnsureDefaultFlatNormalTexture() exactly
    // -- a null map must not sample garbage or crash. White (255,255,255,255) is the correct
    // neutral default for metallicRoughness (mr.g=1,mr.b=1 -> full requested roughness/metallic
    // via the *Factor multiply)/emissive(*1)/occlusion(*1, no darkening); flat normal
    // (128,128,255,255 -> decodes to (0,0,1) in tangent space, i.e. "no perturbation") for the
    // normal map specifically.
    id<MTLTexture> defaultWhiteTexture=nil;
    id<MTLTexture> defaultFlatNormalTexture=nil;
    id<MTLTexture> defaultWhiteCubeTexture=nil;

    // Re-applies every piece of encoder-scoped dynamic state this renderer tracks. Metal has no
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
        // VulkanRenderer's own VK_CULL_MODE_FRONT_BIT/BACK_BIT mapping (see its own comment:
        // "Pipeline uses VK_FRONT_FACE_CLOCKWISE, so CW faces are front faces") ONLY if Metal's front
        // face is also clockwise-winding -- Apple's own MTLRenderCommandEncoder docs state clockwise
        // IS the default, so this was already behaviorally correct, but silently depending on an
        // unstated default is exactly the kind of fragile assumption this project's own history has
        // been burned by before (see Vulkan's own front/back stencil swap, discovered the hard way).
        // Setting it explicitly here removes that risk permanently.
        [encoder setFrontFacingWinding:MTLWindingClockwise];
        const MetalViewportState requestedViewport=rasterState.EffectiveViewport();
        const MTLViewport nativeViewport={requestedViewport.x,requestedViewport.y,
            requestedViewport.width,requestedViewport.height,
            requestedViewport.minDepth,requestedViewport.maxDepth};
        const MetalScissorState requestedScissor=rasterState.NativeScissor();
        const MTLScissorRect nativeScissor={(NSUInteger)requestedScissor.x,(NSUInteger)requestedScissor.y,
            (NSUInteger)requestedScissor.width,(NSUInteger)requestedScissor.height};
        [encoder setViewport:nativeViewport]; [encoder setScissorRect:nativeScissor];
        [encoder setCullMode:cull]; [encoder setTriangleFillMode:fill];
        [encoder setDepthBias:depthBias slopeScale:slopeBias clamp:0]; [encoder setDepthStencilState:depthState];
        [encoder setStencilReferenceValue:(uint32_t)refStencil];
        [encoder setBlendColorRed:blendColor[0] green:blendColor[1] blue:blendColor[2] alpha:blendColor[3]];
    }

    // plan_metal.md Phase 10 (METAL-98/100/107): resolves the color+depth textures for whatever's
    // currently active -- either the bound MetalRenderTargetRenderer, or (if none) the backbuffer
    // drawable (acquiring a fresh one via nextDrawable if not already held). Declared here,
    // defined out-of-line after MetalRenderTargetRenderer's own definition (see the forward-decl's
    // own comment above Impl for why). Every RenderTarget2D always gets a real depth+stencil
    // texture regardless of the requested DepthFormat -- the same simplification tier Vulkan's own
    // CreateRenderTarget2D doc comment already documents and accepts project-wide (not EasyGL/
    // Bgfx's more honest "only allocate what was actually requested" tier) -- because every Metal
    // pipeline in this file already hardcodes depthAttachmentPixelFormat/stencilAttachmentPixelFormat
    // unconditionally, so a render pass with no depth attachment at all would be a real
    // pipeline/render-pass format mismatch, not just a missed optimization.
    //
    // Returns false only for the backbuffer case where nextDrawable legitimately returns nil (e.g.
    // a minimized/background window). That failed acquisition is latched for the rest of the
    // logical frame: Clear, draws, markers, and Present skip without throwing or retrying, while
    // offscreen render-target work remains available. Present resets the latch for the next frame.
    // plan_metal.md METAL-109/110: sliceOut is always 0 except when a RenderTargetCube face is the
    // active target (MTLRenderPassColorAttachmentDescriptor.slice selects which cube face/array
    // layer of colorOut a render pass actually writes to; 0 is simply ignored/inert for a plain 2D
    // texture, so callers can set it unconditionally with no branching of their own).
    // plan_metal.md METAL-104: `resolveOut` (nil unless MSAA is engaged for whatever's currently
    // active) and `sampleCountOut` (always 1 unless `resolveOut` is non-nil) were added alongside
    // the pre-existing 3 out-params for real backbuffer/RenderTarget2D MSAA -- a non-nil
    // `resolveOut` means `colorOut` is a multisampled texture that must be resolved into
    // `resolveOut` at render-pass-end (`MTLStoreActionStoreAndMultisampleResolve`, not plain
    // `MTLStoreActionMultisampleResolve` -- see msaaColorTexture's own field comment for why).
    // RenderTargetCube deliberately stays out of MSAA scope for this pass (matches METAL-112's own
    // MRT+MSAA scope decision below) -- its own branch below never sets resolveOut.
    bool resolveActiveAttachments(id<MTLTexture>& colorOut, id<MTLTexture>& resolveOut, id<MTLTexture>& depthOut, NSUInteger& sliceOut, int& sampleCountOut);

    // plan_metal.md METAL-112: the MRT-aware sibling of resolveActiveAttachments() above, used only
    // by ensureFrame()/clear() (every other caller -- computeSpriteTransform(), etc. -- only ever
    // needs target 0, already available via currentRenderTarget directly). Declared here, defined
    // out-of-line after MetalRenderTargetRenderer for the same incomplete-type reason as
    // resolveActiveAttachments()/computeSpriteTransform() above -- its body calls
    // currentMRT[i]->colorTexture()/depthTextureNative(), which need the complete type.
    //
    // plan_metal.md METAL-104: true MRT (currentMRT.size()>=2) and MSAA are deliberately never
    // combined in this pass -- every MRT draw always runs at sample count 1 regardless of
    // deviceSampleCount, matching real-world XNA usage (MSAA is a single-target scene-
    // anti-aliasing feature; deferred/G-buffer-style MRT rendering practically never also wants
    // MSAA on the G-buffer itself). `resolvesOut` is parallel to `colorsOut`, always all-nil
    // during real MRT.
    bool resolveActiveColorAttachments(std::vector<id<MTLTexture>>& colorsOut, std::vector<id<MTLTexture>>& resolvesOut, id<MTLTexture>& depthOut, NSUInteger& sliceOut, int& sampleCountOut);

    // Ends whatever encoder/command-buffer is currently active -- shared by endFrame() and
    // MetalRenderTargetRenderer's own Bind/UnbindAsRenderTarget() (Metal render passes are
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
        const bool releaseDrawable = presentBackbuffer && drawable.HasValue();
        if (command) {
            if (releaseDrawable) [command presentDrawable:drawable.Get()];
            auto failureLatch=commandFailureLatch;
            [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
                if (completed.status==MTLCommandBufferStatusError)
                    failureLatch->RecordFailure();
            }];
            [command commit];
            [command release]; command=nil;
        }
        // A committed command buffer retains resources needed for execution. Our explicit drawable
        // ownership ends only after presentation has been encoded and that buffer has been
        // committed. The same branch also releases a retained drawable if command acquisition
        // failed, preventing a failure-path leak.
        if (releaseDrawable) drawable.Reset();
    }

    void abandonCommandState()
    {
        if(encoder){[encoder endEncoding];[encoder release];encoder=nil;}
        if(command){[command release];command=nil;}
        drawable.Reset();
        activeColorAttachmentCount=1;
        activeSampleCount=1;
        frameAvailability.ResetAfterCommandFailure();
    }

    [[noreturn]] void abandonCommandStateAndThrow(const char* message)
    {
        abandonCommandState();
        throw std::runtime_error(message);
    }

    void throwPendingCommandFailure()
    {
        if(!commandFailureLatch->ConsumeFailure()) return;
        abandonCommandStateAndThrow("Metal: a previously submitted command buffer failed");
    }

    void finishActiveCommandSynchronously(const char* failureMessage)
    {
        // A failure already known before this operation belongs to an older submission. Surface it
        // through the common abandonment diagnostic instead of mislabeling it as this mip/readback
        // command's failure.
        throwPendingCommandFailure();
        if(!command) {
            throwPendingCommandFailure();
            return;
        }
        id<MTLCommandBuffer> submitted=command;
        [submitted retain];
        if(encoder){[encoder endEncoding];[encoder release];encoder=nil;}
        [submitted commit];
        [command release]; command=nil;
        [submitted waitUntilCompleted];
        const bool completed=submitted.status==MTLCommandBufferStatusCompleted;
        [submitted release];
        switch(DescribeMetalSynchronousCommandResult(
            completed,commandFailureLatch->HasFailure()))
        {
            case MetalSynchronousCommandResult::ExactSubmissionFailed:
                abandonCommandStateAndThrow(failureMessage);
            case MetalSynchronousCommandResult::OlderSubmissionFailed:
                // A separately committed older command can finish while this exact command is
                // awaited. It remains an older asynchronous failure and gets the common diagnostic.
                throwPendingCommandFailure();
                break;
            case MetalSynchronousCommandResult::Complete:
                break;
        }
    }

    bool ensureFrame()
    {
        throwPendingCommandFailure();
        if (encoder) return true;
        try {
            std::vector<id<MTLTexture>> colors, resolves; id<MTLTexture> depthTex=nil; NSUInteger slice=0; int sampleCount=1;
            if(DescribeMetalFrameEntryPolicy(resolveActiveColorAttachments(
                   colors,resolves,depthTex,slice,sampleCount))==
               MetalFrameEntryPolicy::SkipUnavailableBackbuffer)
                return false;
            command=[queue commandBuffer];
            if(!command) throw std::runtime_error("Metal: failed to create render command buffer");
            [command retain];
            MTLRenderPassDescriptor* rp=[MTLRenderPassDescriptor renderPassDescriptor];
            if(!rp) throw std::runtime_error("Metal: failed to allocate render-pass descriptor");
            // plan_metal.md METAL-112: loop bound is 1 outside MRT (colors.size()==1, identical to the
            // original single-attachment code this replaced), up to Metal's own 8-attachment hardware
            // limit during real SetRenderTargets() MRT.
            //
            // plan_metal.md METAL-104: a non-nil resolves[i] means colors[i] is this frame's
            // multisampled render target -- StoreAndMultisampleResolve both keeps colors[i]'s own
            // content (needed across this file's own mid-frame encoder boundaries, see
            // msaaColorTexture's field comment) and resolves it into resolves[i] (the real drawable/
            // RT's single-sample texture) every time this encoder ends.
            for (NSUInteger i=0;i<colors.size();++i) {
                rp.colorAttachments[i].texture=colors[i]; rp.colorAttachments[i].slice=slice;
                rp.colorAttachments[i].loadAction=MTLLoadActionLoad;
                if (resolves[i]) { rp.colorAttachments[i].resolveTexture=resolves[i]; rp.colorAttachments[i].storeAction=MTLStoreActionStoreAndMultisampleResolve; }
                else { rp.colorAttachments[i].storeAction=MTLStoreActionStore; }
            }
            rp.depthAttachment.texture=depthTex; rp.depthAttachment.loadAction=MTLLoadActionLoad; rp.depthAttachment.storeAction=MTLStoreActionStore;
            rp.stencilAttachment.texture=depthTex; rp.stencilAttachment.loadAction=MTLLoadActionLoad; rp.stencilAttachment.storeAction=MTLStoreActionStore;
            rp.visibilityResultBuffer=visibilityBuffer;
            encoder=[command renderCommandEncoderWithDescriptor:rp];
            if(!encoder) throw std::runtime_error("Metal: failed to create render command encoder");
            [encoder retain];
            const NSUInteger w=colors[0].width,h=colors[0].height;
            rasterState.BeginEncoder((std::size_t)w,(std::size_t)h);
            activeColorAttachmentCount=(int)colors.size();
            activeSampleCount=sampleCount;
            applyTrackedEncoderState();
            return true;
        } catch (...) {
            // Allocation/attachment resolution failed before native submission. Drop every
            // partially acquired frame object, including a retained drawable, without committing
            // an incomplete command and preserve the original deterministic diagnostic.
            abandonCommandState();
            throw;
        }
    }

    void endFrame()
    {
        frameAvailability.EndLogicalFrame();
        throwPendingCommandFailure();
        if(!command && !drawable.HasValue()) return;
        // A non-presenting encoder boundary commits and releases its single-use command while
        // deliberately retaining the frame's drawable. If no later backbuffer work created a new
        // command, Present still needs one in order to encode presentation of that same drawable.
        // ensureFrame() reuses the retained drawable without another nextDrawable attempt, then the
        // sole presenting boundary below commits and releases it exactly once.
        if(!command && !ensureFrame()) return;
        endActiveEncoding(true); // real end-of-frame -- the only call site allowed to present.
    }

    void clear(bool color,float r,float g,float b,float a,bool depth,float dv,bool stencil,int sv)
    {
        throwPendingCommandFailure();
        endActiveEncoding(false); // mid-frame boundary only -- see endActiveEncoding()'s own METAL-180 note.
        try {
            std::vector<id<MTLTexture>> colors, resolves; id<MTLTexture> depthTex=nil; NSUInteger slice=0; int sampleCount=1;
            if (!resolveActiveColorAttachments(colors, resolves, depthTex, slice, sampleCount)) return;
            command=[queue commandBuffer];
            if(!command) throw std::runtime_error("Metal: failed to create clear command buffer");
            [command retain];
            MTLRenderPassDescriptor* rp=[MTLRenderPassDescriptor renderPassDescriptor];
            if(!rp) throw std::runtime_error("Metal: failed to allocate clear render-pass descriptor");
            // plan_metal.md METAL-112: Clear()'s own real XNA contract clears every currently-bound
            // render target to the same color -- matches GraphicsDevice.Clear(color)'s documented
            // behavior regardless of how many targets SetRenderTargets() bound.
            //
            // plan_metal.md METAL-104: same StoreAndMultisampleResolve reasoning as ensureFrame() above.
            for (NSUInteger i=0;i<colors.size();++i) {
                rp.colorAttachments[i].texture=colors[i]; rp.colorAttachments[i].slice=slice; rp.colorAttachments[i].loadAction=color?MTLLoadActionClear:MTLLoadActionLoad; rp.colorAttachments[i].clearColor=MTLClearColorMake(r,g,b,a);
                if (resolves[i]) { rp.colorAttachments[i].resolveTexture=resolves[i]; rp.colorAttachments[i].storeAction=MTLStoreActionStoreAndMultisampleResolve; }
                else { rp.colorAttachments[i].storeAction=MTLStoreActionStore; }
            }
            rp.depthAttachment.texture=depthTex; rp.depthAttachment.loadAction=depth?MTLLoadActionClear:MTLLoadActionLoad; rp.depthAttachment.storeAction=MTLStoreActionStore; rp.depthAttachment.clearDepth=dv;
            rp.stencilAttachment.texture=depthTex; rp.stencilAttachment.loadAction=stencil?MTLLoadActionClear:MTLLoadActionLoad; rp.stencilAttachment.storeAction=MTLStoreActionStore; rp.stencilAttachment.clearStencil=sv;
            rp.visibilityResultBuffer=visibilityBuffer;
            encoder=[command renderCommandEncoderWithDescriptor:rp];
            if(!encoder) throw std::runtime_error("Metal: failed to create clear render encoder");
            [encoder retain];
            const NSUInteger w=colors[0].width,h=colors[0].height;
            rasterState.BeginEncoder((std::size_t)w,(std::size_t)h);
            activeColorAttachmentCount=(int)colors.size();
            activeSampleCount=sampleCount;
            applyTrackedEncoderState();
        } catch (...) {
            abandonCommandState();
            throw;
        }
    }

    void rebuildDepthState()
    {
        MetalObjectOwner descriptorOwner(retainMetalObject,releaseMetalObject);
        descriptorOwner.Adopt([[MTLDepthStencilDescriptor alloc] init]);
        if(!descriptorOwner.HasValue())
            throw std::runtime_error("Metal: failed to allocate depth/stencil descriptor");
        MTLDepthStencilDescriptor* d=(MTLDepthStencilDescriptor*)descriptorOwner.Get();
        d.depthCompareFunction = depthEnabled ? metalCompareFunction(depthFunc) : MTLCompareFunctionAlways;
        d.depthWriteEnabled=MetalEffectiveDepthWriteEnabled(depthEnabled,depthWrite);
        // plan_metal.md METAL-9/10: real front/back stencil test, replacing the previous
        // reference-value-only plumbing. Front face carries XNA's "normal" stencil fields; back
        // face carries the CounterClockwise fields when TwoSidedStencilMode is set, else mirrors
        // front exactly -- matches FNA's own real behavior (CCW fields are simply ignored when
        // TwoSidedStencilMode=false, not reset to any default) and EasyGLRenderer's
        // identical fallback-to-front pattern. UNLIKE VulkanRenderer::FillDepthStencilState
        // (see its own long comment), this front/back assignment is NOT swapped -- Metal has no
        // Vulkan-style NDC Y-flip in this codebase's vertex shaders (Vulkan's own swap was an
        // empirically-found compensation for that Y-flip's winding interaction, root-caused to
        // Vulkan specifically, not a general rule) -- but this has NOT been empirically verified
        // on real Metal hardware and must be treated as unproven until it is (plan_metal.md
        // Testing strategy tier 2/3).
        if (stencilEnabled) {
            MetalObjectOwner frontOwner(retainMetalObject,releaseMetalObject);
            frontOwner.Adopt([[MTLStencilDescriptor alloc] init]);
            if(!frontOwner.HasValue())
                throw std::runtime_error("Metal: failed to allocate front stencil descriptor");
            MTLStencilDescriptor* front=(MTLStencilDescriptor*)frontOwner.Get();
            front.stencilCompareFunction = metalCompareFunction(stencilFunc);
            front.stencilFailureOperation = metalStencilOp(stencilFail);
            front.depthFailureOperation = metalStencilOp(stencilDepthFail);
            front.depthStencilPassOperation = metalStencilOp(stencilPass);
            front.readMask = (uint32_t)stencilMask;
            front.writeMask = (uint32_t)stencilWriteMask;
            d.frontFaceStencil = front;
            if (twoSidedStencil) {
                MetalObjectOwner backOwner(retainMetalObject,releaseMetalObject);
                backOwner.Adopt([[MTLStencilDescriptor alloc] init]);
                if(!backOwner.HasValue())
                    throw std::runtime_error("Metal: failed to allocate back stencil descriptor");
                MTLStencilDescriptor* back=(MTLStencilDescriptor*)backOwner.Get();
                back.stencilCompareFunction = metalCompareFunction(ccwStencilFunc);
                back.stencilFailureOperation = metalStencilOp(ccwStencilFail);
                back.depthFailureOperation = metalStencilOp(ccwStencilDepthFail);
                back.depthStencilPassOperation = metalStencilOp(ccwStencilPass);
                back.readMask = (uint32_t)stencilMask;
                back.writeMask = (uint32_t)stencilWriteMask;
                d.backFaceStencil = back;
            } else {
                d.backFaceStencil = front;
            }
        }
        // else: leave frontFaceStencil/backFaceStencil nil (MTLDepthStencilDescriptor's default),
        // which Metal treats as "stencil test always passes, no writes" -- correct for
        // DepthStencilState.StencilEnable=false.
        MetalObjectOwner stateOwner(retainMetalObject,releaseMetalObject);
        stateOwner.Adopt([device newDepthStencilStateWithDescriptor:d]);
        if(!stateOwner.HasValue()) throw std::runtime_error("Metal: failed to create depth/stencil state");
        [depthState release]; depthState=(id<MTLDepthStencilState>)stateOwner.ReleaseOwnership();
        if(encoder) [encoder setDepthStencilState:depthState];
    }

    // plan_metal.md METAL-23/29: replaces the 5 eagerly-built named pipeline fields with a
    // lazily-populated cache keyed by (shader/vertex-layout variant, current blend state).
    id<MTLRenderPipelineState> getOrCreatePipeline(PipelineKind kind)
    {
        // plan_metal.md METAL-112/113: clamped the same way SetRenderTargets() itself clamps
        // count, and again here as a defensive bound on whatever activeColorAttachmentCount
        // currently holds -- MTLPipelineCacheKey's own field is a uint8_t, and 8 is Metal's own
        // hardware attachment limit either way.
        const uint8_t colorCount = (uint8_t)std::clamp(activeColorAttachmentCount, 1, 8);
        // plan_metal.md METAL-104: same defensive-clamp reasoning as colorCount above, for the
        // orthogonal MSAA axis -- activeSampleCount is always one of {1,2,4,8} in practice
        // Historical code only ever produced {1,2,4,8}; the supported contract currently keeps 1.
        const uint8_t sampleCountKey = (uint8_t)std::clamp(activeSampleCount, 1, 8);
        PipelineCacheKey key{kind, currentBlend, colorCount, sampleCountKey};
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
        // plan_metal.md: real bug found and fixed 2026-07-20 -- MTLVertexDescriptor is a concrete
        // Objective-C class (unlike MTLTexture/MTLBuffer/MTLRenderPipelineState etc., which are
        // protocols), so it needs a plain `MTLVertexDescriptor*` pointer, not the `id<Protocol>`
        // syntax used everywhere else in this file -- vertexDescriptorForStride() itself already
        // correctly declared this way, only this one call site got it wrong. Never caught until
        // this was compiled for the first time ever on real Apple hardware -- Clang's own "type
        // argument 'MTLVertexDescriptor' must be a pointer (requires a '*')" error.
        MTLVertexDescriptor* vd = (kind==PipelineKind::Sprite2D) ? nil : vertexDescriptorForStride(stride);
        MetalObjectOwner pipelineOwner(retainMetalObject,releaseMetalObject);
        pipelineOwner.Adopt(makePipeline(device, library, vs, fs, vd, currentBlend, colorCount,
                                         sampleCountKey));
        const auto inserted=EmplaceMetalOwnedResource(pipelineCache,key,pipelineOwner);
        return inserted->second;
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
        if(!sd) throw std::runtime_error("Metal: failed to allocate sampler descriptor");
        sd.minFilter=metalMinFilter(filter); sd.magFilter=metalMagFilter(filter); sd.mipFilter=metalMipFilter(filter);
        sd.sAddressMode=metalAddressMode(addressU); sd.tAddressMode=metalAddressMode(addressV);
        if(filter==2) sd.maxAnisotropy=aniso;
        MetalObjectOwner samplerOwner(retainMetalObject,releaseMetalObject);
        samplerOwner.Adopt([device newSamplerStateWithDescriptor:sd]); [sd release];
        if(!samplerOwner.HasValue()) throw std::runtime_error("Metal: failed to create sampler state");
        const auto inserted=EmplaceMetalOwnedResource(samplerCache,key,samplerOwner);
        return inserted->second;
    }

    // plan_metal.md Phase 15 (METAL-153/155/156/158/159): the shared letterbox/overscan/stretch/
    // native/fixed-height-dynamic-width viewport math, ported near-verbatim from the already-
    // shipped GPU presentation implementation rather than re-derived from scratch.
    // `width`/`height`/`x`/`y` are the logical canvas's rectangle in
    // physical window pixels; `logicalWidth`/`logicalHeight` are the virtual-resolution size that
    // rectangle represents (equal to the physical size whenever no virtual resolution is set,
    // which is also this struct's all-zero-input-safe degenerate case).
    // plan_metal.md METAL-34-style extraction: the real arithmetic now lives in the plain-C++
    // MetalLogicalViewport.hpp (no Objective-C, buildable and unit-tested on any platform without
    // an Apple toolchain) -- kept as a thin same-name alias plus a wrapper here so all existing call
    // sites in this file are unaffected; physical sizing comes from the latest platform snapshot.
    using LogicalViewport = MetalLogicalViewport;
    LogicalViewport computeLogicalViewport() const
    {
        const auto drawableSize=surface.GetDrawableSize();
        const int pw=drawableSize.width;
        const int ph=drawableSize.height;
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
    // defined out-of-line after MetalRenderTargetRenderer (same reason as resolveActiveAttachments:
    // its body now calls currentRenderTarget->colorTexture(), which needs the complete type).
    Sprite2DTransform computeSpriteTransform() const;

    // plan_metal.md METAL-153/154: real window<->logical coordinate transforms, previously
    // entirely unimplemented (base `IGraphicsRenderer` default returns false) -- the input bridge
    // depends on this for correct mouse coordinates on any letterboxed/scaled window, per this
    // method's own doc comment on IGraphicsRenderer.hpp. Ported from the established GPU
    // renderer transform (same LogicalViewport shape and formula) rather than re-derived.
    bool transformWindowToLogical(float windowX, float windowY, float& logX, float& logY) const
    {
        LogicalViewport vp = computeLogicalViewport();
        if (vp.width == 0.0f || vp.height == 0.0f) return false;
        const float drawableX=surface.WindowToDrawable(windowX);
        const float drawableY=surface.WindowToDrawable(windowY);
        logX = (drawableX - vp.x) * vp.logicalWidth / vp.width;
        logY = (drawableY - vp.y) * vp.logicalHeight / vp.height;
        return drawableX >= vp.x && drawableX < vp.x + vp.width &&
               drawableY >= vp.y && drawableY < vp.y + vp.height;
    }
    bool transformLogicalToWindow(float logX, float logY, float& windowX, float& windowY) const
    {
        LogicalViewport vp = computeLogicalViewport();
        if (vp.logicalWidth == 0.0f || vp.logicalHeight == 0.0f) return false;
        windowX = surface.DrawableToWindow(vp.x + logX * vp.width / vp.logicalWidth);
        windowY = surface.DrawableToWindow(vp.y + logY * vp.height / vp.logicalHeight);
        return true;
    }
};

MetalRenderer::Impl::~Impl()
{
    // Resources can be retained independently through copied wrappers or GetRendererWeak(). Publish
    // owner death before touching any native state so their next operation cannot dereference this
    // Impl while its queue/view/device chain is being torn down.
    resourceHealth->MarkOwnerInactive();
    // Destruction and constructor rollback never present a partial frame. Commit any encoded work,
    // then release the retained drawable before tearing down its layer/view ownership chain.
    endActiveEncoding(false);
    drawable.Reset();
    for (auto& entry : samplerCache) [entry.second release];
    samplerCache.clear();
    for (auto& entry : pipelineCache) [entry.second release];
    pipelineCache.clear();
    [defaultWhiteCubeTexture release]; defaultWhiteCubeTexture=nil;
    [defaultFlatNormalTexture release]; defaultFlatNormalTexture=nil;
    [defaultWhiteTexture release]; defaultWhiteTexture=nil;
    [visibilityBuffer release]; visibilityBuffer=nil;
    [depthTexture release]; depthTexture=nil;
    [msaaColorTexture release]; msaaColorTexture=nil;
    [depthState release]; depthState=nil;
    [sampler release]; sampler=nil;
    [library release]; library=nil;
    [queue release]; queue=nil;
    [layer release]; layer=nil;
    if (view) { [view removeFromSuperview]; [view release]; view=nil; }
    [device release]; device=nil;
}

// plan_metal.md Phase 14 (METAL-142-152): Custom ShaderEffect / MSL contract.
//
// Scope decision (METAL-142/143), based on reading VulkanEffectRenderer/D3D11EffectRenderer/
// D3D12EffectRenderer directly rather than assuming: each of those three carries an explicit
// "this mechanism is a SpriteBatch-custom-shader facility, not a general arbitrary-vertex-format
// one" comment. This corrects this plan's own earlier assumption (Phase 14's original header note)
// that Phase 14 was blocked in full on Phase 2's still-open generic VertexElement-driven descriptor
// builder (METAL-26/27) -- that broader "arbitrary 3D vertex layout" scope is EasyGL's own unique
// extra capability (GL's attribute binding is inherently layout-flexible; Vulkan/D3D11/D3D12's
// structured pipeline objects are not, and neither is Metal's), not something every renderer commits
// to. MetalEffectRenderer below is a SpriteBatch-only facility with a FIXED vertex layout matching
// Metal's own existing Sprite2D pipeline exactly -- no dependency on Phase 2's builder at all.
//
// Raw MSL only (METAL-143): no SPIRV-Cross/cross-compile step, matching every other renderer's own
// literal-source-string convention (D3D9/D3D11 take literal HLSL, EasyGL takes literal GLSL) --
// IEffectRenderer::CompileProgram()'s own doc comment already says "Compiles the program from
// GLSL/HLSL/SPIR-V sources", just MSL here.
//
// No fixed entry-point-name convention needed (part of METAL-146's documented choice): vertSrc and
// fragSrc are compiled as two SEPARATE MTLLibrary objects (matching IEffectRenderer::CompileProgram
// ()'s own two-separate-strings signature), each required to declare exactly one function --
// compileOneFunction() reads MTLLibrary.functionNames back directly rather than requiring a fixed
// name, so a custom shader's author is free to name their own entry point anything, the same
// freedom GLSL/HLSL's single-implicit-entry-point convention already gives every other renderer.
//
// Uniform contract (METAL-146): MSL has no GLSL-style named-uniform reflection simple enough to
// build a genuine "SetUniformFloat(name, ...)" on top of (MTLRenderPipelineReflection is argument-
// table introspection, not a name->offset uniform map) -- so, matching Vulkan's SPIR-V
// push-constant / D3D11's HLSL constant-buffer precedent (both hit the identical "no simple
// name-based uniform API" problem and both chose the same answer), this is a fixed, documented
// buffer-layout contract: every SetUniformXxx()'s `name` parameter is ignored, same as those two
// renderers. See docs/metal-shader-effect-contract.md for the full buffer-index layout a custom
// vertex/fragment shader pair must match: buffer(0) vertex data, buffer(1) the automatic
// letterbox-aware U2D transform, buffer(2)/(3)/(4) uMatrix/uColor/uFloat0 -- three separate
// buffers, each one natural, unpadded Metal type (float4x4/float4/float), deliberately not one
// combined struct, so there is no `constant`-address-space struct-padding ambiguity for a custom
// shader's own MSL struct declaration to get wrong.
//
// Blend state (a deliberate, documented improvement over the Vulkan/D3D11/D3D12 precedent, not a
// blind copy): those three hardcode a fixed alpha blend inside the custom pipeline, silently
// ignoring whatever BlendState SpriteBatch.Begin(sortMode, blendState, ..., effect) requested --
// Metal's own existing pipeline cache (getOrCreatePipeline()) is already blend-state-aware, so
// pipelineFor() below keys its own single-entry pipeline cache off the real currentBlend, matching
// real XNA/FNA behavior (blendState and effect are independent Begin() parameters; a custom effect
// does not turn off blend-state support) rather than reproducing a gap found only by inspection.
class MetalEffectRenderer final : public IEffectRenderer
{
public:
    explicit MetalEffectRenderer(MetalRenderer::Impl& owner) : owner_(owner) {}
    ~MetalEffectRenderer() override
    {
        if (pipeline_) [pipeline_ release];
        if (vertFn_) [vertFn_ release];
        if (fragFn_) [fragFn_ release];
    }

    bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override
    {
        compileError_.clear();
        if (pipeline_) { [pipeline_ release]; pipeline_ = nil; }
        if (vertFn_) { [vertFn_ release]; vertFn_ = nil; }
        if (fragFn_) { [fragFn_ release]; fragFn_ = nil; }
        valid_ = false;

        id<MTLFunction> vf = compileOneFunction(vertSrc, "vertex");
        if (!vf) return false;
        id<MTLFunction> ff = compileOneFunction(fragSrc, "fragment");
        if (!ff) { [vf release]; return false; }

        vertFn_ = vf;
        fragFn_ = ff;
        valid_ = true;
        return true;
    }

    // No GPU state to defer -- unlike D3D11 (whose own Bind() issues real IASetInputLayout/
    // VSSetShader/PSSetShader calls immediately, since D3D11 has no separate pipeline-object
    // assembly step), Metal's pipeline object is already fully assembled by CompileProgram()/
    // pipelineFor(); MetalSpriteBatch::Draw() reads this effect's compiled pipeline/uniform bytes
    // directly via GetEffectRendererPtr(), so Bind()/Unbind() only need to satisfy the interface
    // contract, matching Vulkan's own near-empty Bind()/Unbind() for the identical reason.
    void Bind() override {}
    void Unbind() override {}
    [[nodiscard]] bool IsValid() const override { return valid_; }
    [[nodiscard]] std::string GetCompileError() const override { return compileError_; }

    // Fixed-slot contract (see this class's own header comment): `name` is always ignored.
    void SetUniformMat4(const char*, const float* matrix) override { std::memcpy(uMatrix_, matrix, sizeof(uMatrix_)); }
    void SetUniformVec4(const char*, float x, float y, float z, float w) override { uColor_[0]=x; uColor_[1]=y; uColor_[2]=z; uColor_[3]=w; }
    void SetUniformVec3(const char*, float x, float y, float z) override { uColor_[0]=x; uColor_[1]=y; uColor_[2]=z; }
    void SetUniformVec2(const char*, float x, float y) override { uColor_[0]=x; uColor_[1]=y; }
    void SetUniformFloat(const char*, float value) override { uFloat0_ = value; }
    void SetUniformInt(const char*, int value) override { uFloat0_ = (float)value; }

    // Called by MetalSpriteBatch::Draw() once per draw call (not once per Begin(), unlike the
    // D3D11/Vulkan precedent's own once-per-flush Bind() -- Metal's SpriteBatch issues one
    // immediate draw per sprite rather than batching a whole Begin/End block into one flush, so
    // re-reading this effect's current uniform bytes on every draw call is both simpler and more
    // correct: a SetUniformXxx() call between two Draw()s in the same Begin/End genuinely takes
    // effect on the next sprite, not just the next batch).
    // plan_metal.md METAL-104: also rebuilds when owner_.activeSampleCount changes, the same
    // "genuine Metal API validation error otherwise" reasoning makePipeline()'s own sampleCount
    // parameter documents -- a custom SpriteBatch effect drawn while the backbuffer/RenderTarget2D
    // happens to be MSAA-enabled needs a pipeline whose own sampleCount matches, exactly like every
    // built-in PipelineKind already gets via getOrCreatePipeline()'s own MetalPipelineCacheKey.
    id<MTLRenderPipelineState> pipelineFor(const BlendKey& blend)
    {
        if (pipeline_ && blend == lastBlend_ && owner_.activeSampleCount == lastSampleCount_) return pipeline_;
        if (pipeline_) { [pipeline_ release]; pipeline_ = nil; }
        MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
        d.vertexFunction = vertFn_;
        d.fragmentFunction = fragFn_;
        d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        d.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        d.sampleCount = (NSUInteger)owner_.activeSampleCount;
        d.colorAttachments[0].blendingEnabled = blend.enabled ? YES : NO;
        if (blend.enabled) {
            d.colorAttachments[0].sourceRGBBlendFactor = metalBlendFactor(blend.colorSrc);
            d.colorAttachments[0].destinationRGBBlendFactor = metalBlendFactor(blend.colorDst);
            d.colorAttachments[0].rgbBlendOperation = metalBlendOp(blend.colorFunc);
            d.colorAttachments[0].sourceAlphaBlendFactor = metalBlendFactor(blend.alphaSrc);
            d.colorAttachments[0].destinationAlphaBlendFactor = metalBlendFactor(blend.alphaDst);
            d.colorAttachments[0].alphaBlendOperation = metalBlendOp(blend.alphaFunc);
        }
        NSError* err = nil;
        pipeline_ = [owner_.device newRenderPipelineStateWithDescriptor:d error:&err];
        [d release];
        if (!pipeline_) {
            compileError_ = std::string("Metal custom-effect pipeline compile failed: ") +
                             (err ? [[err localizedDescription] UTF8String] : "unknown");
            valid_ = false;
            return nil;
        }
        lastBlend_ = blend;
        lastSampleCount_ = owner_.activeSampleCount;
        return pipeline_;
    }

    [[nodiscard]] const float* GetMatrix() const { return uMatrix_; }
    [[nodiscard]] const float* GetColor() const { return uColor_; }
    [[nodiscard]] float GetFloat0() const { return uFloat0_; }

private:
    // Compiles `src` as its own standalone MTLLibrary and returns its sole function -- see this
    // class's own header comment for why no fixed entry-point name is required.
    id<MTLFunction> compileOneFunction(const std::string& src, const char* stageLabel)
    {
        NSString* srcStr = [NSString stringWithUTF8String:src.c_str()];
        NSError* err = nil;
        id<MTLLibrary> lib = [owner_.device newLibraryWithSource:srcStr options:nil error:&err];
        if (!lib) {
            compileError_ = std::string("Metal ") + stageLabel + " compile failed: " +
                             (err ? [[err localizedDescription] UTF8String] : "unknown");
            return nil;
        }
        NSArray<NSString*>* names = [lib functionNames];
        if (names.count != 1) {
            compileError_ = std::string("Metal ") + stageLabel +
                             " source must declare exactly one function, found " +
                             std::to_string((int)names.count);
            [lib release];
            return nil;
        }
        id<MTLFunction> fn = [lib newFunctionWithName:names[0]];
        [lib release];
        if (!fn) {
            compileError_ = std::string("Metal ") + stageLabel + " function lookup failed";
            return nil;
        }
        return fn;
    }

    MetalRenderer::Impl& owner_;
    id<MTLFunction> vertFn_ = nil;
    id<MTLFunction> fragFn_ = nil;
    id<MTLRenderPipelineState> pipeline_ = nil;
    BlendKey lastBlend_{};
    int lastSampleCount_=1; // plan_metal.md METAL-104
    bool valid_ = false;
    std::string compileError_;
    // Defaults match the identity matrix / transparent-black color / zero scalar a fresh custom
    // effect should read before any SetUniformXxx() call, mirroring Vulkan/D3D11's own
    // zero-initialized push-constant/constant-buffer default.
    float uMatrix_[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float uColor_[4] = {0,0,0,0};
    float uFloat0_ = 0.0f;
};

class MetalSpriteBatch final : public ISpriteBatchRenderer
{
public:
    explicit MetalSpriteBatch(MetalRenderer& b):b_(b){}
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
    // plan_metal.md Phase 14 (METAL-145/148/149): mirrors D3D11SpriteBatchRenderer's own
    // `customEffect_ = effect;` (just stores the raw Effect* -- the actual IEffectRenderer is
    // resolved fresh in Draw() via GetEffectRendererPtr(), never cached here, so a mid-batch
    // Effect::Clone()/reassignment can't leave this pointing at a stale renderer).
    void SetCustomEffect(Effect* effect) override
    {
        if (effect)
            throw System::NotSupportedException(
                "Metal SpriteBatch custom effects are disabled until the adapted renderer has "
                "passing macOS shader and pixel evidence.");
        customEffect_=nullptr;
    }
    void Draw(const ITextureRenderer& t,float x,float y) override { Rectangle d((int)x,(int)y,t.GetWidth(),t.GetHeight()); Rectangle s(0,0,t.GetWidth(),t.GetHeight()); Draw(t,d,s,Color::White); }
    void Draw(const ITextureRenderer& t,const Rectangle& d,const Rectangle& s,const Color& c) override { Draw(t,d,s,c,0,Vector2::Zero,SpriteEffects::None,0); }
    void Draw(const ITextureRenderer& t,const Rectangle& d,const Rectangle& s,const Color& c,float rotation,const Vector2& origin,SpriteEffects effects,float) override
    {
        if(!begun_) throw std::runtime_error("Metal SpriteBatch.Draw called outside Begin/End");
        // plan_metal.md Phase 10: nativeTextureFor() (not a bare MetalTexture dynamic_cast) so
        // drawing a previously-rendered-to RenderTarget2D as a sprite works, not just a plain Texture2D.
        id<MTLTexture> nativeTex=nativeTextureFor(&t); if(!nativeTex) throw std::runtime_error("Metal: foreign texture renderer");
        auto& p=b_.impl();
        if(!p.ensureFrame()||p.rasterState.ShouldSkipDraw()) return;
        struct V{float x,y,u,v,r,g,b,a;}; V q[6];
        // plan_metal.md: real bug found and fixed 2026-07-20 -- Rectangle/Vector2 in this codebase
        // use plain public fields (X/Y/Width/Height), matching real XNA's own struct convention,
        // not the getXProperty()-style getter this line originally guessed at (that convention is
        // real for Color, which this same function correctly uses just below, but Rectangle/
        // Vector2 were never converted to properties). Never caught until this was compiled for
        // the first time ever on real Apple hardware -- Clang's own "no member named
        // 'getXProperty'" error.
        float x0=(float)d.X, y0=(float)d.Y, x1=x0+d.Width, y1=y0+d.Height;
        float u0=(float)s.X/t.GetWidth(), v0=(float)s.Y/t.GetHeight();
        float u1=(float)(s.X+s.Width)/t.GetWidth(), v1=(float)(s.Y+s.Height)/t.GetHeight();
        if((int)effects & 1) std::swap(u0,u1); if((int)effects & 2) std::swap(v0,v1);
        const float cr=c.getRProperty()/255.f,cg=c.getGProperty()/255.f,cb=c.getBProperty()/255.f,ca=c.getAProperty()/255.f;
        auto xf=[&](float x,float y){ float px=x-(x0+origin.X), py=y-(y0+origin.Y); float cs=std::cos(rotation),sn=std::sin(rotation); return std::array<float,2>{x0+origin.X+px*cs-py*sn,y0+origin.Y+px*sn+py*cs};};
        auto tf=[&](std::array<float,2> q){ float x=q[0],y=q[1]; return std::array<float,2>{x*transform_.M11+y*transform_.M21+transform_.M41, x*transform_.M12+y*transform_.M22+transform_.M42}; };
        auto a=tf(xf(x0,y0)),bb=tf(xf(x1,y0)),cc=tf(xf(x1,y1)),dd=tf(xf(x0,y1));
        V vs[6]={{a[0],a[1],u0,v0,cr,cg,cb,ca},{bb[0],bb[1],u1,v0,cr,cg,cb,ca},{cc[0],cc[1],u1,v1,cr,cg,cb,ca},{a[0],a[1],u0,v0,cr,cg,cb,ca},{cc[0],cc[1],u1,v1,cr,cg,cb,ca},{dd[0],dd[1],u0,v1,cr,cg,cb,ca}};
        // plan_metal.md METAL-157/158: was raw physical-drawable-pixel NDC mapping (`{w,h}`),
        // ignoring virtual resolution/letterboxing entirely -- now the real scale+offset transform.
        auto st=p.computeSpriteTransform();
        struct U{float sx,sy,ox,oy;} u{st.scaleX,st.scaleY,st.offsetX,st.offsetY};
        // plan_metal.md Phase 14 (METAL-145/148): resolved fresh every Draw() call, not cached
        // across the Begin/End block -- see MetalEffectRenderer::pipelineFor()'s own comment for why
        // this (deliberately) makes a SetUniformXxx() call between two Draw()s take effect on the
        // very next sprite, unlike the D3D11/Vulkan once-per-flush precedent.
        MetalEffectRenderer* ceb=nullptr;
        if (customEffect_) {
            customEffect_->Apply();
            ceb=dynamic_cast<MetalEffectRenderer*>(customEffect_->GetEffectRendererPtr());
            if (ceb && !ceb->IsValid()) ceb=nullptr;
        }
        id<MTLRenderPipelineState> pipe = ceb ? ceb->pipelineFor(p.currentBlend) : nil;
        // pipelineFor() can return nil on a genuine (rare, hardware/driver-level) pipeline-compile
        // failure even though CompileProgram() itself already succeeded -- falls back to the stock
        // Sprite2D pipeline rather than passing nil to setRenderPipelineState: (a hard Metal API
        // misuse, not a recoverable-looking failure) so a real GPU-side error still degrades to
        // "sprite drew with the stock shader" instead of a crash.
        if (!pipe) { pipe=p.getOrCreatePipeline(PipelineKind::Sprite2D); ceb=nullptr; }
        [p.encoder setRenderPipelineState:pipe]; [p.encoder setVertexBytes:vs length:sizeof(vs) atIndex:0]; [p.encoder setVertexBytes:&u length:sizeof(u) atIndex:1];
        if (ceb) {
            const float* m=ceb->GetMatrix(); const float* col=ceb->GetColor(); float f0=ceb->GetFloat0();
            [p.encoder setVertexBytes:m length:16*sizeof(float) atIndex:2]; [p.encoder setVertexBytes:col length:4*sizeof(float) atIndex:3]; [p.encoder setVertexBytes:&f0 length:sizeof(float) atIndex:4];
            [p.encoder setFragmentBytes:m length:16*sizeof(float) atIndex:2]; [p.encoder setFragmentBytes:col length:4*sizeof(float) atIndex:3]; [p.encoder setFragmentBytes:&f0 length:sizeof(float) atIndex:4];
        }
        [p.encoder setFragmentTexture:nativeTex atIndex:0]; [p.encoder setFragmentSamplerState:p.samplerFor(filter_,addressU_,addressV_,1) atIndex:0]; [p.encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    }
private: MetalRenderer& b_; bool begun_=false; int filter_=0; int addressU_=1; int addressV_=1; Matrix transform_=Matrix::getIdentityProperty(); Effect* customEffect_=nullptr;
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
class MetalOcclusionQueryRenderer final : public IOcclusionQueryRenderer
{
public:
    MetalOcclusionQueryRenderer(MetalRenderer::Impl& owner, int slot) : owner_(owner), slot_(slot) {}
    void Begin() override
    {
        if(!owner_.ensureFrame()) return;
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
    MetalRenderer::Impl& owner_;
    int slot_;
    std::shared_ptr<std::atomic<bool>> completed_;
};

// plan_metal.md METAL-131/122/125: shared blit-to-staging-buffer readback helper, used by
// MetalRenderTargetRenderer/MetalRenderTargetCubeRenderer/MetalTextureCube/MetalTexture3D's
// GetData() overrides instead of four near-duplicate implementations (matching the task's own
// "sharing one helper" framing). Uses an independent, freshly-created command buffer rather than
// the frame's own in-flight one -- correct because callers are responsible for ensuring the source
// texture's content is actually GPU-complete first: an active render target synchronously verifies
// its exact source-render command, while an already-committed source relies on same-queue ordering
// plus the shared asynchronous failure latch. Plain SetData-populated textures have no render-source
// command of their own. Metal command buffers on one queue execute in submission order, so the blit
// sees every successfully completed write submitted before it.
//
// depth defaults to 1 (an ordinary 2D-region copy: RenderTarget2D, one RenderTargetCube/
// TextureCube face, or one Texture3D Z-slice at a time is still just a 2D blit region with a fixed
// z origin). destinationBytesPerImage is explicitly 0 whenever depth<=1 -- Apple's own
// documentation for this method states it is only meaningful (and only read) for a genuine
// multi-image copy (depth>1); passing a non-zero-but-otherwise-correct value for a single-image
// copy risks Metal's debug/validation layer flagging it as unused-but-invalid, so 0 is the
// unambiguously safe choice here rather than reusing the same byte count as destinationBytesPerRow.
static void blitTextureToClientBuffer(id<MTLDevice> device, id<MTLCommandQueue> queue,
                                      id<MTLTexture> src, NSUInteger slice, int level,
                                      int x, int y, int z, int w, int h, int depth,
                                      const MetalTextureTransferLayout& layout,
                                      MetalTransferPixelOrder pixelOrder, void* data,
                                      const std::function<void()>& commandHealthCheck)
{
    MetalObjectOwner stagingOwner(retainMetalObject,releaseMetalObject);
    stagingOwner.Adopt([device newBufferWithLength:(NSUInteger)layout.stagingTotalBytes
                                           options:MTLResourceStorageModeShared]);
    if(!stagingOwner.HasValue()) throw std::runtime_error("Metal: GetData failed to allocate staging buffer");
    id<MTLBuffer> staging=(id<MTLBuffer>)stagingOwner.Get();
    MetalObjectOwner commandOwner(retainMetalObject,releaseMetalObject);
    commandOwner.Reset([queue commandBuffer]);
    if(!commandOwner.HasValue()) throw std::runtime_error("Metal: GetData failed to allocate command buffer");
    id<MTLCommandBuffer> cmd=(id<MTLCommandBuffer>)commandOwner.Get();
    id<MTLBlitCommandEncoder> blit=[cmd blitCommandEncoder];
    if(!blit) throw std::runtime_error("Metal: GetData failed to allocate blit encoder");
    [blit copyFromTexture:src sourceSlice:slice sourceLevel:(NSUInteger)level
              sourceOrigin:MTLOriginMake((NSUInteger)x,(NSUInteger)y,(NSUInteger)z)
                sourceSize:MTLSizeMake((NSUInteger)w,(NSUInteger)h,(NSUInteger)depth)
                  toBuffer:staging destinationOffset:0
    destinationBytesPerRow:(NSUInteger)layout.alignedRowBytes
  destinationBytesPerImage:(depth>1?(NSUInteger)layout.alignedImageBytes:0)];
    [blit endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted]; // plan_metal.md METAL-133: same intentional correctness-over-throughput stall as ReadBackbuffer().
    if(cmd.status!=MTLCommandBufferStatusCompleted) throw std::runtime_error("Metal: GetData blit command failed");
    commandHealthCheck();
    const auto* stagingBytes=static_cast<const std::uint8_t*>([staging contents]);
    if(!CopyMetalTextureReadbackToTightRgba(
            stagingBytes,layout,w,h,depth,pixelOrder,static_cast<std::uint8_t*>(data),
            layout.tightTotalBytes))
        throw std::runtime_error("Metal: GetData staging layout conversion failed");
}

// RenderTarget2D renderer. Mips are supported, while requested MSAA is clamped to zero and reported
// as zero. Preserve/discard behavior remains owned by GraphicsDevice's shared binding path.
//
// A render target can be retained independently through Texture2D copies/cache handles. Its weak
// owner never prolongs the GraphicsDevice lifetime; every owner-dependent operation first locks the
// Impl and checks the shared resource-health token. Destruction performs active-target cleanup only
// when that weak lock succeeds, then always releases the target's independently owned textures.
class MetalRenderTargetRenderer final : public IRenderTargetRenderer
{
public:
    // MSAA stays allocated out of the supported contract: the historical Mac run engaged sample
    // count 4 but its rendered edge was binary. The target therefore reports the applied value 0.
    MetalRenderTargetRenderer(std::shared_ptr<MetalRenderer::Impl> owner, int w, int h,
                             bool mipMap, int /*requestedMultiSampleCount*/=0)
        : owner_(owner), resourceHealth_(owner ? owner->resourceHealth : nullptr),
          w_(w), h_(h), mipMap_(mipMap),
          levelCount_(MetalMipLevelCount(w,h,mipMap)), appliedSampleCount_(0),
          definedMipLevels_(levelCount_)
    {
        if(!owner||!resourceHealth_)
            throw std::invalid_argument("Metal RenderTarget2D requires an active owner");
        owner->throwPendingCommandFailure();
        MetalObjectOwner colorOwner(retainMetalObject,releaseMetalObject);
        MetalObjectOwner msaaOwner(retainMetalObject,releaseMetalObject);
        MetalObjectOwner depthOwner(retainMetalObject,releaseMetalObject);
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
        if(!cd) throw std::runtime_error("Metal: failed to allocate RenderTarget2D color descriptor");
        cd.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
        colorOwner.Adopt([owner->device newTextureWithDescriptor:cd]);
        if(!colorOwner.HasValue()) throw std::runtime_error("Metal: failed to create RenderTarget2D color texture");
        // plan_metal.md METAL-104: engaging MSAA needs a SECOND color texture -- colorTexture_
        // above stays the single-sample, sampleable, GetData()-readable texture every existing
        // caller already expects (unaffected either way); msaaColorTexture_ is the real multisampled
        // render target this instance's own render passes actually write into when appliedSampleCount_
        // >0, resolved into colorTexture_ at every encoder boundary (see msaaColorTexture's own
        // field comment on Impl for why StoreAndMultisampleResolve, not plain MultisampleResolve).
        if (appliedSampleCount_ > 0) {
            msaaOwner.Adopt(makeMultisampleTexture(owner->device, MTLPixelFormatBGRA8Unorm, (NSUInteger)w, (NSUInteger)h, (NSUInteger)appliedSampleCount_, MTLTextureUsageRenderTarget));
            if(!msaaOwner.HasValue()) throw std::runtime_error("Metal: failed to create RenderTarget2D MSAA color texture");
        }
        // plan_metal.md METAL-104: the depth attachment's own sample count must match the color
        // attachment it's paired with in the same render pass -- a real Metal API constraint, not a
        // style choice -- so this target's depth texture is multisampled too whenever it engages
        // MSAA. Never sampled externally by anything in this codebase (matches
        // VulkanRenderTargetRenderer's own identical "depthView_ is never sampled externally" note),
        // so there is no separate depth-resolve concern to handle the way color has one.
        if (appliedSampleCount_ > 0) {
            depthOwner.Adopt(makeMultisampleTexture(owner->device, MTLPixelFormatDepth32Float_Stencil8, (NSUInteger)w, (NSUInteger)h, (NSUInteger)appliedSampleCount_, MTLTextureUsageRenderTarget));
        } else {
            MTLTextureDescriptor* dd=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:(NSUInteger)w height:(NSUInteger)h mipmapped:NO];
            if(!dd) throw std::runtime_error("Metal: failed to allocate RenderTarget2D depth descriptor");
            dd.storageMode=MTLStorageModePrivate; dd.usage=MTLTextureUsageRenderTarget;
            depthOwner.Adopt([owner->device newTextureWithDescriptor:dd]);
        }
        if(!depthOwner.HasValue()) throw std::runtime_error("Metal: failed to create RenderTarget2D depth texture");
        colorTexture_=(id<MTLTexture>)colorOwner.ReleaseOwnership();
        msaaColorTexture_=(id<MTLTexture>)msaaOwner.ReleaseOwnership();
        depthTexture_=(id<MTLTexture>)depthOwner.ReleaseOwnership();
    }
    ~MetalRenderTargetRenderer() override
    {
        // If this target is still bound when destroyed (the game forgot to SetRenderTarget2D(null)
        // first, or just let it go out of scope), end the active encoding BEFORE releasing the
        // underlying textures -- otherwise Impl::encoder/command would be left referencing textures
        // about to be freed, a real use-after-free/dangling-attachment risk, not just untidy state.
        // false: destruction mid-frame must never present -- see endActiveEncoding()'s METAL-180 note.
        if (auto owner=LockMetalResourceOwnerForCleanup(owner_)) {
            if (owner->currentRenderTarget==this) {
                owner->endActiveEncoding(false);
                owner->currentRenderTarget=nullptr;
            }
        // plan_metal.md METAL-112: same safety net, extended to a real MRT set -- only currentMRT[0]
        // is ever mirrored into currentRenderTarget above, so the check above alone would miss
        // target 1..N-1 being destroyed while still part of the active binding. Invalidates the
        // whole set rather than leave the other targets' own destructors with no way to detect a
        // now-dangling entry (matches this file's own established "the game forgot to unbind, but
        // it must not corrupt Impl's own state" convention, just applied to N pointers not one).
            if (!owner->currentMRT.empty()) {
                auto it = std::find(owner->currentMRT.begin(), owner->currentMRT.end(), this);
                if (it != owner->currentMRT.end()) {
                    owner->endActiveEncoding(false);
                    owner->currentMRT.clear();
                    owner->currentRenderTarget=nullptr;
                    owner->activeColorAttachmentCount=1;
                }
            }
        }
        [depthTexture_ release]; [colorTexture_ release]; [msaaColorTexture_ release];
    }
    int GetWidth() const override { return w_; }
    int GetHeight() const override { return h_; }

    // Public count zero is the deterministic unsupported/no-MSAA value.
    int GetMultiSampleCount() const override { return appliedSampleCount_; }
    int GetAppliedDepthStencilFormatEXT(int /*requestedDepthStencilFormat*/) const override
    {
        return static_cast<int>(Microsoft::Xna::Framework::Graphics::DepthFormat::Depth24Stencil8);
    }
    bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override { return true; }
    bool HasRealStencilBuffer(bool /*stencilFormatWasRequested*/) const override { return true; }
    void BindAsRenderTarget() override
    {
        auto owner=lockOwner();
        owner->endActiveEncoding(false); // mid-frame switch, never presents -- see METAL-180 note.
        // plan_metal.md METAL-112: a single-target bind always means "not MRT" -- clears any stale
        // MRT state a previous SetRenderTargets() call left behind, whether this was reached via
        // SetRenderTarget2D() directly or via SetRenderTargets()'s own count==1 delegation.
        owner->currentMRT.clear(); owner->activeColorAttachmentCount=1;
        owner->currentRenderTarget=this;
    }
    // plan_metal.md METAL-103/112: factored out of UnbindAsRenderTarget() so SetRenderTargets()'s
    // own MRT teardown (Impl::unbindCurrentMRT()) can regenerate mips for every target in an
    // outgoing MRT set, not just whichever one BindAsRenderTarget() itself last tracked.
    void regenerateMipsIfNeeded()
    {
        if (!mipMap_) return;
        auto owner=lockOwner();
        // plan_metal.md METAL-103: regenerate the full mip chain from level 0's just-rendered
        // content on every unbind when mipMap was requested, unconditionally -- matches
        // EasyGLRenderTargetRenderer::UnbindAsRenderTarget()'s own established precedent exactly
        // (itself citing FNA3D's OPENGL_ResolveTarget: "if (target->levelCount > 1) { ...
        // glGenerateMipmap... }"), not gated on whether anything was actually drawn this bind
        // session -- EasyGL's glGenerateMipmap call isn't gated on that either.
        if (owner->encoder) { [owner->encoder endEncoding]; [owner->encoder release]; owner->encoder=nil; }
        // A blit encoder needs a real command buffer to encode into; if nothing was ever
        // drawn/cleared this bind session owner->command is still nil (ensureFrame() was
        // never called) -- create one just for the blit, mirroring ReadBackbuffer()'s own
        // precedent of using a small standalone command buffer for a one-off GPU operation.
        try {
            if (!owner->command) {
                owner->command=[owner->queue commandBuffer];
                if(!owner->command) throw std::runtime_error("Metal: failed to create mip-generation command buffer");
                [owner->command retain];
            }
            id<MTLBlitCommandEncoder> blit=[owner->command blitCommandEncoder];
            if(!blit) throw std::runtime_error("Metal: failed to create mip-generation blit encoder");
            [blit generateMipmapsForTexture:colorTexture_];
            [blit endEncoding];
            owner->finishActiveCommandSynchronously("Metal: RenderTarget2D mip generation failed");
            definedMipLevels_.MarkGenerated();
        } catch (...) {
            owner->endActiveEncoding(false);
            throw;
        }
    }
    void UnbindAsRenderTarget() override
    {
        auto owner=lockOwner();
        if (owner->currentRenderTarget==this) {
            regenerateMipsIfNeeded();
            owner->endActiveEncoding(false); owner->currentRenderTarget=nullptr;
        }
    }
    void UpdatePixels(const uint8_t* rgba,int stride) override
    {
        auto owner=lockOwner();
        if(!rgba||w_>std::numeric_limits<int>::max()/4||
           stride!=static_cast<int>(static_cast<std::size_t>(w_)*4u))
            throw std::invalid_argument("Metal: invalid RenderTarget2D level-zero upload");
        MetalTextureTransferLayout layout{};
        if(!TryBuildMetalTextureTransferLayout(w_,h_,1,1,layout))
            throw std::invalid_argument("Metal: invalid RenderTarget2D level-zero upload size");
        reallocateAndUploadRgba(0,rgba,w_,h_,layout,owner);
        owner->throwPendingCommandFailure();
    }
    void UpdatePixelsLevel(int level,const uint8_t* rgba,int levelWidth,int levelHeight) override
    {
        auto owner=lockOwner();
        if(!rgba||level<0||level>=levelCount_||
           levelWidth!=MetalTextureTransferDetail::MipDimension(w_,level)||
           levelHeight!=MetalTextureTransferDetail::MipDimension(h_,level))
            throw std::invalid_argument("Metal: invalid RenderTarget2D mip upload");
        MetalTextureTransferLayout layout{};
        if(!TryBuildMetalTextureTransferLayout(levelWidth,levelHeight,1,1,layout))
            throw std::invalid_argument("Metal: invalid RenderTarget2D mip upload size");
        reallocateAndUploadRgba(level,rgba,levelWidth,levelHeight,layout,owner);
        owner->throwPendingCommandFailure();
    }
    bool HasDefinedMipLevel(int level) const noexcept override
    {
        return definedMipLevels_.IsDefined(level);
    }
    // plan_metal.md METAL-131: real readback via the shared blit helper, replacing ITextureRenderer's
    // inherited no-op default. If this target is still the currently active render target, its
    // pending source-render command is committed, awaited, and checked exactly before the later
    // independent blit. Otherwise the blit could run before a still-uncommitted render pass, or a
    // failed source command could be mistaken for successful current pixels.
    bool GetData(int level,int x,int y,int w,int h,void* data,int dataLength) const override
    {
        auto owner=lockOwner();
        MetalTextureTransferLayout layout{};
        if(!TryPrepareMetalTextureTransfer(
                w_,h_,1,levelCount_,level,x,y,0,w,h,1,data,dataLength,
                MetalTransferLengthRule::ExactlyTightBytes,
                MetalMacOsTextureBufferRowAlignment,layout)) return false;
        if (DescribeMetalReadbackSourcePolicy(owner->currentRenderTarget==this)==
            MetalReadbackSourcePolicy::SynchronizeActiveSource)
            owner->finishActiveCommandSynchronously(
                "Metal: RenderTarget2D source render command failed before readback");
        blitTextureToClientBuffer(owner->device,owner->queue,colorTexture_,0,level,x,y,0,w,h,1,
                                  layout,MetalTransferPixelOrder::Bgra,data,
                                  [owner] { owner->throwPendingCommandFailure(); });
        owner->throwPendingCommandFailure();
        return true;
    }
    // plan_metal.md METAL-104: colorTexture() is UNCHANGED -- still always the single-sample,
    // sampleable, GetData()-readable texture, whether or not this target engages MSAA (every
    // existing caller -- shader sampling, GetData() above, blit sources -- keeps working
    // unmodified). colorTextureForRenderPass()/resolveTargetForRenderPass() are the two new,
    // narrowly-scoped accessors resolveActiveAttachments() alone needs to build a real MSAA render
    // pass: the former is what a render pass actually writes into (the MSAA texture when engaged,
    // else colorTexture_ itself -- the exact same texture, no branch needed by the caller);
    // resolveTargetForRenderPass() is nil unless MSAA is engaged, in which case it's colorTexture_
    // (the resolve destination).
    id<MTLTexture> colorTexture() const { (void)lockOwner(); return colorTexture_; }
    id<MTLTexture> colorTextureForRenderPass() const { (void)lockOwner(); return msaaColorTexture_ ? msaaColorTexture_ : colorTexture_; }
    id<MTLTexture> resolveTargetForRenderPass() const { (void)lockOwner(); return msaaColorTexture_ ? colorTexture_ : nil; }
    id<MTLTexture> depthTextureNative() const { (void)lockOwner(); return depthTexture_; }
private:
    [[nodiscard]] std::shared_ptr<MetalRenderer::Impl> lockOwner() const
    {
        auto owner=RequireMetalResourceOwner(resourceHealth_,owner_);
        owner->throwPendingCommandFailure();
        return owner;
    }

    void reallocateAndUploadRgba(int targetLevel,const uint8_t* rgba,int levelWidth,int levelHeight,
                                 const MetalTextureTransferLayout& layout,
                                 const std::shared_ptr<MetalRenderer::Impl>& owner)
    {
        if(owner->currentRenderTarget==this) owner->endActiveEncoding(false);
        MetalObjectOwner replacementOwner(retainMetalObject,releaseMetalObject);
        MTLTextureDescriptor* descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
            width:(NSUInteger)w_ height:(NSUInteger)h_ mipmapped:mipMap_];
        if(!descriptor) throw std::runtime_error("Metal: failed to allocate replacement RenderTarget2D descriptor");
        descriptor.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
        replacementOwner.Adopt([owner->device newTextureWithDescriptor:descriptor]);
        if(!replacementOwner.HasValue()) throw std::runtime_error("Metal: failed to allocate replacement RenderTarget2D texture");
        id<MTLTexture> replacement=(id<MTLTexture>)replacementOwner.Get();
        if(levelCount_>1){
            MetalObjectOwner commandOwner(retainMetalObject,releaseMetalObject);
            commandOwner.Reset([owner->queue commandBuffer]);
            if(!commandOwner.HasValue()) throw std::runtime_error("Metal: failed to create RenderTarget2D preservation command buffer");
            id<MTLCommandBuffer> command=(id<MTLCommandBuffer>)commandOwner.Get();
            id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
            if(!blit) throw std::runtime_error("Metal: failed to create RenderTarget2D preservation blit encoder");
            for(int level=0;level<levelCount_;++level){
                if(level==targetLevel) continue;
                const NSUInteger width=(NSUInteger)MetalTextureTransferDetail::MipDimension(w_,level);
                const NSUInteger height=(NSUInteger)MetalTextureTransferDetail::MipDimension(h_,level);
                [blit copyFromTexture:colorTexture_ sourceSlice:0 sourceLevel:(NSUInteger)level
                          sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(width,height,1)
                             toTexture:replacement destinationSlice:0 destinationLevel:(NSUInteger)level
                    destinationOrigin:MTLOriginMake(0,0,0)];
            }
            [blit endEncoding];[command commit];[command waitUntilCompleted];
            if(command.status!=MTLCommandBufferStatusCompleted) throw std::runtime_error("Metal: RenderTarget2D preservation blit failed");
            owner->throwPendingCommandFailure();
        }
        std::vector<std::uint8_t> bgra(layout.tightTotalBytes);
        if(!CopyMetalTightRgbaToTextureBytes(rgba,layout,MetalTransferPixelOrder::Bgra,bgra.data(),bgra.size()))
            throw std::runtime_error("Metal: RenderTarget2D upload conversion failed");
        [replacement replaceRegion:MTLRegionMake2D(0,0,(NSUInteger)levelWidth,(NSUInteger)levelHeight)
                      mipmapLevel:(NSUInteger)targetLevel withBytes:bgra.data()
                      bytesPerRow:(NSUInteger)layout.tightRowBytes];
        [colorTexture_ release];colorTexture_=(id<MTLTexture>)replacementOwner.ReleaseOwnership();
        definedMipLevels_.MarkUploaded(targetLevel);
    }
    std::weak_ptr<MetalRenderer::Impl> owner_;
    std::shared_ptr<MetalResourceHealth> resourceHealth_;
    int w_, h_;
    bool mipMap_;
    int levelCount_=1;
    int appliedSampleCount_=0;
    MetalMipDefinitionState definedMipLevels_;
    id<MTLTexture> colorTexture_=nil;
    id<MTLTexture> msaaColorTexture_=nil;
    id<MTLTexture> depthTexture_=nil;
};

// plan_metal.md METAL-109/110/111: RenderTargetCube renderer. A single MTLTextureTypeCube color
// texture (6 slices) plus ONE shared 2D depth texture reused across every face -- matches
// EasyGLRenderTargetCubeRenderer's own already-tested precedent exactly (its own comment: "single
// depth renderbuffer regardless of face (since only one face is ever rendered into at a time) --
// matches FNA's RenderTargetCube.cs, which also allocates a single glColorBuffer regardless of
// face"), not a Metal-specific shortcut. Per-face binding uses
// MTLRenderPassColorAttachmentDescriptor.slice (see resolveActiveAttachments() below) rather than
// 6 separate 2D texture views, mirroring MetalTextureCube's own existing SetData(face,...) ->
// replaceRegion:...slice:face convention (face ordinals already match Metal's own cube slice
// order, see MetalTextureCube's own comment on this).
class MetalRenderTargetCubeRenderer final : public IRenderTargetCubeRenderer
{
public:
    MetalRenderTargetCubeRenderer(std::shared_ptr<MetalRenderer::Impl> owner, int size,
                                 bool mipMap)
        : owner_(owner), resourceHealth_(owner ? owner->resourceHealth : nullptr),
          size_(size), mipMap_(mipMap),
          levelCount_(MetalMipLevelCount(size,size,mipMap))
    {
        if(!owner||!resourceHealth_)
            throw std::invalid_argument("Metal RenderTargetCube requires an active owner");
        owner->throwPendingCommandFailure();
        MetalObjectOwner colorOwner(retainMetalObject,releaseMetalObject);
        MetalObjectOwner depthOwner(retainMetalObject,releaseMetalObject);
        MTLTextureDescriptor* cd=[MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm size:(NSUInteger)size mipmapped:mipMap];
        if(!cd) throw std::runtime_error("Metal: failed to allocate RenderTargetCube color descriptor");
        cd.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
        colorOwner.Adopt([owner->device newTextureWithDescriptor:cd]);
        if(!colorOwner.HasValue()) throw std::runtime_error("Metal: failed to create RenderTargetCube color texture");
        MTLTextureDescriptor* dd=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:(NSUInteger)size height:(NSUInteger)size mipmapped:NO];
        if(!dd) throw std::runtime_error("Metal: failed to allocate RenderTargetCube depth descriptor");
        dd.storageMode=MTLStorageModePrivate; dd.usage=MTLTextureUsageRenderTarget;
        depthOwner.Adopt([owner->device newTextureWithDescriptor:dd]);
        if(!depthOwner.HasValue()) throw std::runtime_error("Metal: failed to create RenderTargetCube depth texture");
        colorTexture_=(id<MTLTexture>)colorOwner.ReleaseOwnership();
        depthTexture_=(id<MTLTexture>)depthOwner.ReleaseOwnership();
    }
    ~MetalRenderTargetCubeRenderer() override
    {
        // Same reasoning as MetalRenderTargetRenderer's own destructor: end any still-active
        // encoding referencing these textures before releasing them.
        if (auto owner=LockMetalResourceOwnerForCleanup(owner_)) {
            if (owner->currentRenderTargetCube==this) {
                owner->endActiveEncoding(false);
                owner->currentRenderTargetCube=nullptr;
            }
        }
        [depthTexture_ release]; [colorTexture_ release];
    }
    int GetSize() const override { return size_; }
    int GetSizeEXT() const noexcept override { return size_; }
    bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override { return true; }
    bool HasRealStencilBuffer(bool /*stencilFormatWasRequested*/) const override { return true; }
    void BindAsRenderTargetFace(int face) override
    {
        if (face<0 || face>=6) throw std::out_of_range("Metal: RenderTargetCube face must be in [0, 5]");
        auto owner=lockOwner();
        owner->endActiveEncoding(false); // mid-frame switch, never presents -- see METAL-180 note.
        owner->currentRenderTargetCube=this;
        owner->currentRenderTargetCubeFace=(NSUInteger)face;
    }
    void UnbindAsRenderTarget() override
    {
        auto owner=lockOwner();
        if (owner->currentRenderTargetCube==this) {
            // plan_metal.md METAL-103 (cube analog): same unconditional mip-regeneration-on-unbind
            // policy as MetalRenderTargetRenderer's own, applied to the whole cube map at once (all
            // 6 faces' mip chains regenerate together in one generateMipmapsForTexture: call --
            // Metal's own documented behavior for a cube texture, matching
            // EasyGLRenderTargetCubeRenderer::UnbindAsRenderTarget's own single glGenerateMipmap
            // call over the whole cube map, not a separate call per face).
            if (mipMap_) {
                if (owner->encoder) { [owner->encoder endEncoding]; [owner->encoder release]; owner->encoder=nil; }
                try {
                    if (!owner->command) {
                        owner->command=[owner->queue commandBuffer];
                        if(!owner->command) throw std::runtime_error("Metal: failed to create cube mip-generation command buffer");
                        [owner->command retain];
                    }
                    id<MTLBlitCommandEncoder> blit=[owner->command blitCommandEncoder];
                    if(!blit) throw std::runtime_error("Metal: failed to create cube mip-generation blit encoder");
                    [blit generateMipmapsForTexture:colorTexture_];
                    [blit endEncoding];
                    owner->finishActiveCommandSynchronously("Metal: RenderTargetCube mip generation failed");
                } catch (...) {
                    owner->endActiveEncoding(false);
                    throw;
                }
            }
            owner->endActiveEncoding(false);
            owner->currentRenderTargetCube=nullptr;
        }
    }
    bool SetData(int face,int level,int x,int y,int w,int h,
                 const void* data,int dataLength) override
    {
        (void)lockOwner();
        (void)face; (void)level; (void)x; (void)y; (void)w; (void)h;
        (void)data; (void)dataLength;
        return MetalRenderTargetCubeUploadSupported();
    }
    // plan_metal.md METAL-131 (cube analog): same shared-helper readback and exact source-command
    // verification as MetalRenderTargetRenderer::GetData() -- checked against
    // currentRenderTargetCube==this regardless of which face, since every face shares the same
    // underlying MTLTexture object and command-buffer ordering concern.
    bool GetData(int face,int level,int x,int y,int w,int h,void* data,int dataLength) const override
    {
        auto owner=lockOwner();
        if (face<0||face>=6) return false;
        MetalTextureTransferLayout layout{};
        if(!TryPrepareMetalTextureTransfer(
                size_,size_,1,levelCount_,level,x,y,0,w,h,1,data,dataLength,
                MetalTransferLengthRule::ExactlyTightBytes,
                MetalMacOsTextureBufferRowAlignment,layout)) return false;
        if (DescribeMetalReadbackSourcePolicy(owner->currentRenderTargetCube==this)==
            MetalReadbackSourcePolicy::SynchronizeActiveSource)
            owner->finishActiveCommandSynchronously(
                "Metal: RenderTargetCube source render command failed before readback");
        blitTextureToClientBuffer(owner->device,owner->queue,colorTexture_,(NSUInteger)face,level,
                                  x,y,0,w,h,1,layout,MetalTransferPixelOrder::Bgra,data,
                                  [owner] { owner->throwPendingCommandFailure(); });
        owner->throwPendingCommandFailure();
        return true;
    }
    id<MTLTexture> colorTexture() const { (void)lockOwner(); return colorTexture_; }
    id<MTLTexture> depthTextureNative() const { (void)lockOwner(); return depthTexture_; }
private:
    [[nodiscard]] std::shared_ptr<MetalRenderer::Impl> lockOwner() const
    {
        auto owner=RequireMetalResourceOwner(resourceHealth_,owner_);
        owner->throwPendingCommandFailure();
        return owner;
    }

    std::weak_ptr<MetalRenderer::Impl> owner_;
    std::shared_ptr<MetalResourceHealth> resourceHealth_;
    int size_;
    bool mipMap_;
    int levelCount_=1;
    id<MTLTexture> colorTexture_=nil;
    id<MTLTexture> depthTexture_=nil;
};

bool MetalRenderer::Impl::resolveActiveAttachments(id<MTLTexture>& colorOut, id<MTLTexture>& resolveOut, id<MTLTexture>& depthOut, NSUInteger& sliceOut, int& sampleCountOut)
{
    sliceOut = 0; resolveOut = nil; sampleCountOut = 1;
    if (currentRenderTarget) {
        // plan_metal.md METAL-104: colorTextureForRenderPass()/resolveTargetForRenderPass() collapse
        // to plain colorTexture()/nil whenever this target doesn't engage MSAA -- this branch's own
        // behavior is byte-identical to before MSAA existed in that (the overwhelmingly common) case.
        colorOut = currentRenderTarget->colorTextureForRenderPass();
        resolveOut = currentRenderTarget->resolveTargetForRenderPass();
        depthOut = currentRenderTarget->depthTextureNative();
        if (resolveOut) sampleCountOut = currentRenderTarget->GetMultiSampleCount();
        return true;
    }
    if (currentRenderTargetCube) {
        // plan_metal.md METAL-104: RenderTargetCube deliberately stays out of MSAA scope for this
        // pass (see resolveActiveColorAttachments()'s own MRT+MSAA scope-decision comment for the
        // same reasoning applied to a different axis) -- always single-sampled.
        colorOut = currentRenderTargetCube->colorTexture();
        depthOut = currentRenderTargetCube->depthTextureNative();
        sliceOut = currentRenderTargetCubeFace;
        return true;
    }
    if(!drawable.HasValue()){
        if(!frameAvailability.ShouldAttemptBackbufferAcquisition()) return false;
        drawable.Reset([layer nextDrawable]);
        frameAvailability.RecordBackbufferAcquisition(drawable.HasValue());
    }
    if (!drawable.HasValue()) return false;
    const id<CAMetalDrawable> activeDrawable=drawable.Get();
    const NSUInteger w=activeDrawable.texture.width, h=activeDrawable.texture.height;
    // plan_metal.md METAL-104: real backbuffer MSAA -- msaaColorTexture is lazily (re)allocated on
    // a width/height change exactly like depthTexture below already was, just for a second texture.
    if (deviceSampleCount > 1) {
        if (!msaaColorTexture || msaaColorTexture.width!=w || msaaColorTexture.height!=h) {
            id<MTLTexture> replacement=makeMultisampleTexture(
                device,MTLPixelFormatBGRA8Unorm,w,h,(NSUInteger)deviceSampleCount,
                MTLTextureUsageRenderTarget);
            if(!replacement) throw std::runtime_error("Metal: failed to allocate backbuffer MSAA texture");
            [msaaColorTexture release]; msaaColorTexture=replacement;
        }
        colorOut = msaaColorTexture;
        resolveOut = activeDrawable.texture;
        sampleCountOut = deviceSampleCount;
    } else {
        colorOut = activeDrawable.texture;
    }
    if(!depthTexture || depthTexture.width!=w || depthTexture.height!=h){
        id<MTLTexture> replacement=nil;
        // plan_metal.md METAL-104: same "depth sample count must match its paired color attachment"
        // constraint MetalRenderTargetRenderer's own constructor comment documents, applied to the
        // backbuffer's own depth texture.
        if (deviceSampleCount > 1) {
            replacement=makeMultisampleTexture(device,MTLPixelFormatDepth32Float_Stencil8,w,h,
                (NSUInteger)deviceSampleCount,MTLTextureUsageRenderTarget);
        } else {
            MTLTextureDescriptor* dd=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:w height:h mipmapped:NO];
            if(!dd) throw std::runtime_error("Metal: failed to allocate backbuffer depth descriptor");
            dd.storageMode=MTLStorageModePrivate; dd.usage=MTLTextureUsageRenderTarget;
            replacement=[device newTextureWithDescriptor:dd];
        }
        if(!replacement) throw std::runtime_error("Metal: failed to allocate backbuffer depth texture");
        [depthTexture release]; depthTexture=replacement;
    }
    depthOut = depthTexture;
    return true;
}

bool MetalRenderer::Impl::resolveActiveColorAttachments(std::vector<id<MTLTexture>>& colorsOut, std::vector<id<MTLTexture>>& resolvesOut, id<MTLTexture>& depthOut, NSUInteger& sliceOut, int& sampleCountOut)
{
    if (currentMRT.size() >= 2) {
        colorsOut.clear(); resolvesOut.clear();
        // plan_metal.md METAL-104: true MRT and MSAA are deliberately never combined in this pass
        // (see this method's own declaration comment for the full reasoning) -- every MRT target
        // contributes its plain, always-single-sampled colorTexture(), never
        // colorTextureForRenderPass(), and every resolvesOut entry stays nil.
        for (auto* rt : currentMRT) { colorsOut.push_back(rt->colorTexture()); resolvesOut.push_back(nil); }
        // plan_metal.md METAL-112: reuses currentMRT[0]'s own depthTextureNative() for the whole
        // MRT render pass rather than allocating a dedicated shared one (unlike VulkanMRTProxy,
        // which must -- Vulkan's explicit VkFramebuffer object needs one concrete depth
        // VkImageView chosen up front; Metal's MTLRenderPassDescriptor has no such constraint, and
        // every MetalRenderTargetRenderer already unconditionally owns its own real
        // Depth32Float_Stencil8 texture, so borrowing target 0's avoids a second depth allocation
        // for every MRT session).
        depthOut = currentMRT[0]->depthTextureNative();
        sliceOut = 0; sampleCountOut = 1;
        return true;
    }
    id<MTLTexture> c=nil, r=nil;
    if (!resolveActiveAttachments(c, r, depthOut, sliceOut, sampleCountOut)) return false;
    colorsOut.assign(1, c);
    resolvesOut.assign(1, r);
    return true;
}

// plan_metal.md METAL-112: the single chokepoint that tears down a real MRT set correctly --
// regenerates mips for every target in the outgoing set (matching MetalRenderTargetRenderer::
// UnbindAsRenderTarget()'s own per-target precedent, extended from one target to N) and resets
// currentMRT/activeColorAttachmentCount, before any caller goes on to bind whatever comes next.
// Called unconditionally as the first step of SetRenderTarget2D()/SetRenderTargetCubeFace()/
// SetRenderTargets() so every path that changes the active render target(s) tears down a
// previously-active MRT set the same way, regardless of which specific entry point the game used.
void MetalRenderer::Impl::unbindCurrentMRT()
{
    if (currentMRT.empty()) return;
    auto old = currentMRT;
    currentMRT.clear();
    currentRenderTarget = nullptr;
    activeColorAttachmentCount = 1;
    endActiveEncoding(false);
    for (auto* rt : old) rt->regenerateMipsIfNeeded();
}

MetalRenderer::Impl::Sprite2DTransform MetalRenderer::Impl::computeSpriteTransform() const
{
    Sprite2DTransform t{};
    if (currentRenderTarget) {
        // An offscreen RenderTarget2D's own pixel space IS the logical space -- no
        // window-relative letterbox scaling applies at all (computeLogicalViewport() queries the
        // live window size, which has nothing to do with an offscreen texture's own
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
    const id<CAMetalDrawable> activeDrawable=drawable.Get();
    const float dw=(float)activeDrawable.texture.width, dh=(float)activeDrawable.texture.height;
    if (dw<=0 || dh<=0) return t;
    LogicalViewport vp = computeLogicalViewport();
    if (vp.logicalWidth<=0 || vp.logicalHeight<=0) return t;
    t.scaleX = 2.0f*vp.width/(vp.logicalWidth*dw);
    t.offsetX = 2.0f*vp.x/dw - 1.0f;
    t.scaleY = -2.0f*vp.height/(vp.logicalHeight*dh);
    t.offsetY = 1.0f - 2.0f*vp.y/dh;
    return t;
}

static id<MTLTexture> nativeTextureFor(const ITextureRenderer* t)
{
    if (!t) return nil;
    if (auto* mt = dynamic_cast<const MetalTexture*>(t)) return mt->native();
    if (auto* rt = dynamic_cast<const MetalRenderTargetRenderer*>(t)) return rt->colorTexture();
    return nil;
}

static id<MTLTexture> nativeCubeTextureFor(const ITextureCubeRenderer* t)
{
    if (!t) return nil;
    if (auto* mt = dynamic_cast<const MetalTextureCube*>(t)) return mt->native();
    if (auto* rt = dynamic_cast<const MetalRenderTargetCubeRenderer*>(t)) return rt->colorTexture();
    return nil;
}

static id<MTLTexture> resolveMetal2DTextureBinding(
    MetalRenderer::Impl& owner,
    const ITextureRenderer* captured,
    MetalStockTextureSlot slot)
{
    const id<MTLTexture> native=nativeTextureFor(captured);
    switch(DescribeMetalStockTextureBinding(slot,captured!=nullptr,native!=nil))
    {
        case MetalTextureBindingDecision::BindNative:
            return native;
        case MetalTextureBindingDecision::BindNeutralFallback:
        {
            id<MTLTexture> fallback=MetalNeutralTextureForSlot(slot)==MetalNeutralTextureKind::FlatNormal2D
                ? owner.defaultFlatNormalTexture : owner.defaultWhiteTexture;
            if(!fallback) throw std::runtime_error("Metal: required neutral 2D texture is unavailable");
            return fallback;
        }
        case MetalTextureBindingDecision::Reject:
            throw System::NotSupportedException(
                "Metal: a stock draw captured a non-Metal 2D texture renderer.");
    }
    throw std::runtime_error("Metal: invalid stock 2D texture binding decision");
}

static id<MTLTexture> resolveMetalCubeTextureBinding(
    MetalRenderer::Impl& owner,
    const ITextureCubeRenderer* captured,
    MetalStockTextureSlot slot)
{
    const id<MTLTexture> native=nativeCubeTextureFor(captured);
    switch(DescribeMetalStockTextureBinding(slot,captured!=nullptr,native!=nil))
    {
        case MetalTextureBindingDecision::BindNative:
            return native;
        case MetalTextureBindingDecision::BindNeutralFallback:
            if(!owner.defaultWhiteCubeTexture)
                throw std::runtime_error("Metal: required neutral cube texture is unavailable");
            return owner.defaultWhiteCubeTexture;
        case MetalTextureBindingDecision::Reject:
            throw System::NotSupportedException(
                "Metal: a stock draw captured a non-Metal cube texture renderer.");
    }
    throw std::runtime_error("Metal: invalid stock cube texture binding decision");
}

static std::function<void()> makeMetalResourceOwnerHealthCheck(
    const std::shared_ptr<MetalRenderer::Impl>& owner)
{
    if(!owner) throw std::invalid_argument("Metal resource requires an owner");
    const std::shared_ptr<MetalResourceHealth> resourceHealth=owner->resourceHealth;
    const std::weak_ptr<MetalRenderer::Impl> weakOwner=owner;
    return [resourceHealth,weakOwner] {
        auto retainedOwner=RequireMetalResourceOwner(resourceHealth,weakOwner);
        retainedOwner->throwPendingCommandFailure();
    };
}

MetalRenderer::MetalRenderer(const GraphicsRendererCreateArgs& args):impl_(std::make_shared<Impl>(args.surface))
{
    auto& p=*impl_; p.virtualW=args.virtualWidth; p.virtualH=args.virtualHeight; p.swapInterval=args.swapInterval;
    // plan_metal.md Phase 15: real, previously-invisible bug -- args.presentationMode was never
    // read at all (Impl::presentationMode's own field default, Letterbox=0, silently won this
    // instead), even though GraphicsRendererCreateArgs::presentationMode's own doc comment states
    // its default is FixedHeightDynamicWidth (XNA/Windows-Phone-matching) and the GPU/EasyGL
    // constructors both already forward it correctly. Had zero observable effect before this
    // phase since nothing consumed `presentationMode` yet; matters now that computeLogicalViewport()
    // does.
    p.presentationMode=(int)args.presentationMode;
    CNA::Platform::CocoaNativeWindow nativeWindow;
    if(!CNA::Platform::TryGetCocoa(p.surface.GetNativeHandle(),nativeWindow))
        throw std::runtime_error("Metal renderer requires a Cocoa native window");
    NSWindow* cocoaWindow=(NSWindow*)nativeWindow.window;
    NSView* contentView=[cocoaWindow contentView];
    if(!contentView) throw std::runtime_error("Metal renderer requires a Cocoa content view");
    // MTLCreateSystemDefaultDevice follows the Create ownership convention and returns one owned
    // (+1) reference under MRR. Do not retain it again. Likewise, every `new*` result stored below
    // is already +1; only borrowed factory/getter results that survive their call scope
    // (CAMetalLayer, command buffers/encoders, and drawables) receive an explicit retain.
    p.device=MTLCreateSystemDefaultDevice(); if(!p.device) throw std::runtime_error("Metal: MTLCreateSystemDefaultDevice failed");
    // The historical MSAA path engaged on macOS CI but did not produce correct edge coverage.
    // Keep the supported contract deterministic until an adapted build has passing Mac evidence.
    p.deviceSampleCount=MetalAppliedMultiSampleCount(args.multiSampleCount)+1;
    p.view=[[CNAMetalView alloc] initWithFrame:[contentView bounds]];
    if(!p.view) throw std::runtime_error("Metal: failed to create a layer-backed Cocoa view");
    [contentView addSubview:p.view];
    const auto drawableSize=p.surface.GetDrawableSize();
    [p.view updateDrawableWidth:drawableSize.width height:drawableSize.height
                   displayScale:p.surface.GetDisplayScale()];
    p.layer=(CAMetalLayer*)p.view.layer;
    if(!p.layer) throw std::runtime_error("Metal: Cocoa view did not create a CAMetalLayer");
    [p.layer retain]; p.layer.device=p.device; p.layer.pixelFormat=MTLPixelFormatBGRA8Unorm; p.layer.framebufferOnly=NO;
    // plan_metal.md METAL-168: swapInterval was previously stored but never applied -- CAMetalLayer
    // has no direct integer-interval knob (unlike OpenGL's 0/1/-1 swap interval or Vulkan's
    // present-mode choice, both real per-value behavior elsewhere in this codebase), only the
    // boolean displaySyncEnabled. XNA PresentInterval::Immediate(0) -> NO (uncapped); One(1, the
    // default)/Two(2) both -> YES (real, honest vsync) since Metal has no true half-rate present
    // mode to map Two to -- an approximation, not a silent gap, and documented as such rather than
    // pretending Two behaves differently from One.
    p.layer.displaySyncEnabled = (p.swapInterval != 0);
    p.queue=[p.device newCommandQueue];
    if(!p.queue) throw std::runtime_error("Metal: failed to create MTLCommandQueue");
    NSError* err=nil; NSString* src=[NSString stringWithUTF8String:kMetalShaderSource]; p.library=[p.device newLibraryWithSource:src options:nil error:&err];
    if(!p.library) throw std::runtime_error(std::string("Metal shader compile failed: ")+([[err localizedDescription] UTF8String]?:"unknown"));
    // plan_metal.md METAL-23: pipelines are no longer built eagerly here -- getOrCreatePipeline()
    // lazily builds+caches each (PipelineKind, BlendKey) combination on first use instead.
    MTLSamplerDescriptor* sd=[[MTLSamplerDescriptor alloc]init];
    if(!sd) throw std::runtime_error("Metal: failed to allocate default sampler descriptor");
    sd.minFilter=MTLSamplerMinMagFilterLinear;sd.magFilter=MTLSamplerMinMagFilterLinear;
    sd.sAddressMode=MTLSamplerAddressModeClampToEdge;sd.tAddressMode=MTLSamplerAddressModeClampToEdge;
    p.sampler=[p.device newSamplerStateWithDescriptor:sd];[sd release];
    if(!p.sampler) throw std::runtime_error("Metal: failed to create default sampler state");
    // METAL-266: occlusion queries remain dormant and capability-false, so no visibility buffer
    // is allocated or attached as supported state.
    p.visibilityBuffer=nil;
    // plan_metal.md METAL-87: PbrEffect's 4 optional-map fallback textures (see Impl's own field
    // comment for why these exact 2 colors).
    {
        MTLTextureDescriptor* td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1 height:1 mipmapped:NO];
        if(!td) throw std::runtime_error("Metal: failed to allocate neutral 2D texture descriptor");
        td.usage=MTLTextureUsageShaderRead;
        p.defaultWhiteTexture=[p.device newTextureWithDescriptor:td];
        if(!p.defaultWhiteTexture) throw std::runtime_error("Metal: failed to create neutral white texture");
        const uint8_t white[4]={255,255,255,255};
        [p.defaultWhiteTexture replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:white bytesPerRow:4];
        p.defaultFlatNormalTexture=[p.device newTextureWithDescriptor:td];
        if(!p.defaultFlatNormalTexture) throw std::runtime_error("Metal: failed to create neutral flat-normal texture");
        const uint8_t flatNormal[4]={128,128,255,255};
        [p.defaultFlatNormalTexture replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:flatNormal bytesPerRow:4];
        MTLTextureDescriptor* cubeDescriptor=[MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm size:1 mipmapped:NO];
        if(!cubeDescriptor) throw std::runtime_error("Metal: failed to allocate neutral cube texture descriptor");
        cubeDescriptor.usage=MTLTextureUsageShaderRead;
        p.defaultWhiteCubeTexture=[p.device newTextureWithDescriptor:cubeDescriptor];
        if(!p.defaultWhiteCubeTexture) throw std::runtime_error("Metal: failed to create neutral white cube texture");
        for(NSUInteger face=0;face<6;++face)
            [p.defaultWhiteCubeTexture replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0
                slice:face withBytes:white bytesPerRow:4 bytesPerImage:0];
    }
    p.rebuildDepthState();
}
MetalRenderer::~MetalRenderer()=default;
MetalRenderer::Impl& MetalRenderer::impl(){return *impl_;} const MetalRenderer::Impl& MetalRenderer::impl()const{return *impl_;}
void MetalRenderer::Clear(float r,float g,float b,float a){impl_->clear(true,r,g,b,a,false,1,false,0);} void MetalRenderer::Present(){impl_->endFrame();}
void MetalRenderer::GetViewportSize(int&w,int&h){
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
void MetalRenderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
{
    impl_->surface.Update(surface);
    const auto drawableSize=impl_->surface.GetDrawableSize();
    [impl_->view updateDrawableWidth:drawableSize.width height:drawableSize.height
                        displayScale:impl_->surface.GetDisplayScale()];
}
void MetalRenderer::GetDefaultViewportRect(int&x,int&y,int&w,int&h)
{
    const auto vp=impl_->computeLogicalViewport();
    x=(int)std::lround(vp.x); y=(int)std::lround(vp.y);
    w=(int)std::lround(vp.width); h=(int)std::lround(vp.height);
}
void MetalRenderer::SetVirtualResolution(int w,int h){impl_->virtualW=w;impl_->virtualH=h;} void MetalRenderer::SetPresentationMode(int m){impl_->presentationMode=m;}
void MetalRenderer::SetSwapInterval(int i){impl_->swapInterval=i;impl_->layer.displaySyncEnabled=(i!=0);} // plan_metal.md METAL-168, same mapping as the constructor.
// The historical MSAA implementation engaged real sample-count-four attachments but did not
// produce correct coverage. Keep the public and internal values at their single-sample identity.
int MetalRenderer::ApplyMultiSampleCount(int requestedMultiSampleCount)
{
    (void)requestedMultiSampleCount;
    auto& p=*impl_;
    p.deviceSampleCount=MetalAppliedMultiSampleCount(requestedMultiSampleCount)+1;
    [p.msaaColorTexture release]; p.msaaColorTexture=nil;
    [p.depthTexture release]; p.depthTexture=nil;
    return 0;
}
// Public count zero means unsupported/no MSAA; Metal's native sample-count identity is one.
int MetalRenderer::GetMultiSampleCount() const { return 0; }
int MetalRenderer::GetAppliedBackBufferFormatEXT(int /*requestedFormat*/) const
{
    return static_cast<int>(Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color);
}
int MetalRenderer::GetAppliedDepthStencilFormatEXT(int /*requestedFormat*/) const
{
    return static_cast<int>(Microsoft::Xna::Framework::Graphics::DepthFormat::Depth24Stencil8);
}
bool MetalRenderer::SupportsDepthBuffer() const { return true; }
bool MetalRenderer::SupportsStencilBuffer() const { return true; }
bool MetalRenderer::TransformWindowToLogical(float windowX,float windowY,float& logX,float& logY) const{return impl_->transformWindowToLogical(windowX,windowY,logX,logY);}
bool MetalRenderer::TransformLogicalToWindow(float logX,float logY,float& windowX,float& windowY) const{return impl_->transformLogicalToWindow(logX,logY,windowX,windowY);}
// Historical macOS execution returned only the clear color after real 2D and 3D draws. Reject the
// operation until a replacement has adapted build and pixel proof instead of returning wrong data.
void MetalRenderer::ReadBackbuffer(int x,int y,int w,int h,uint8_t* pixels)
{
    impl_->throwPendingCommandFailure();
    (void)x; (void)y; (void)w; (void)h; (void)pixels;
    throw System::NotSupportedException(
        "GraphicsDevice::GetBackBufferData: Metal backbuffer readback is disabled because the "
        "historical macOS test run returned clear-color-only data after real draws.");
}
std::unique_ptr<ITextureRenderer> MetalRenderer::CreateTexture(const ImageData& d)
{
    impl_->throwPendingCommandFailure();
    switch(DescribeMetalTexture2DImagePolicy(d.surfaceFormat,d.width,d.height,d.mipLevels,d.pixels.size())){
        case MetalTexture2DImagePolicy::Supported: break;
        case MetalTexture2DImagePolicy::UnsupportedFormat:
            throw System::NotSupportedException("Metal Texture2D supports only SurfaceFormat::Color.");
        case MetalTexture2DImagePolicy::InvalidDimensionsOrMipCount:
            throw std::invalid_argument("Metal Texture2D dimensions or mip count are invalid.");
        case MetalTexture2DImagePolicy::InvalidBaseByteCount:
            throw std::invalid_argument("Metal Texture2D level-zero RGBA byte count is invalid.");
    }
    return std::make_unique<MetalTexture>(impl_->device,impl_->queue,d,
        impl_->resourceHealth,makeMetalResourceOwnerHealthCheck(impl_));
}
std::unique_ptr<ISpriteBatchRenderer> MetalRenderer::CreateSpriteBatch(){return std::make_unique<MetalSpriteBatch>(*this);}
std::unique_ptr<ITextureCubeRenderer> MetalRenderer::CreateTextureCube(int size,bool mipMap,int surfaceFormat)
{
    impl_->throwPendingCommandFailure();
    if (!MetalSupportsSurfaceFormat(surfaceFormat))
        throw System::NotSupportedException("Metal TextureCube supports only SurfaceFormat::Color.");
    if(size<=0) throw std::invalid_argument("Metal TextureCube size must be positive.");
    return std::make_unique<MetalTextureCube>(impl_->device,impl_->queue,size,mipMap,
        impl_->resourceHealth,makeMetalResourceOwnerHealthCheck(impl_));
}
std::unique_ptr<IEffectRenderer> MetalRenderer::CreateEffectRenderer(const std::string& vertSrc,const std::string& fragSrc)
{
    (void)vertSrc; (void)fragSrc;
    throw System::NotSupportedException(
        "Metal custom effects are disabled until the adapted renderer has passing macOS shader "
        "and pixel evidence.");
}
std::unique_ptr<IOcclusionQueryRenderer> MetalRenderer::CreateOcclusionQuery()
{
    throw System::NotSupportedException(
        "Metal OcclusionQuery is disabled until command-boundary completion and slot recycling "
        "have adapted macOS proof.");
}
std::unique_ptr<ITexture3DRenderer> MetalRenderer::CreateTexture3D(int w,int h,int depth,bool mipMap,int surfaceFormat)
{
    impl_->throwPendingCommandFailure();
    if (!MetalSupportsSurfaceFormat(surfaceFormat))
        throw System::NotSupportedException("Metal Texture3D supports only SurfaceFormat::Color.");
    if(w<=0||h<=0||depth<=0) throw std::invalid_argument("Metal Texture3D dimensions must be positive.");
    return std::make_unique<MetalTexture3D>(impl_->device,impl_->queue,w,h,depth,mipMap,
        impl_->resourceHealth,makeMetalResourceOwnerHealthCheck(impl_));
}
// Preserve/discard behavior remains owned by the shared GraphicsDevice binding path. Requested
// multisampling is deliberately clamped to zero at this boundary.
std::unique_ptr<IRenderTargetRenderer> MetalRenderer::CreateRenderTarget2D(int w,int h,int /*depthFormat*/,bool /*preserveContents*/,bool mipMap,int multiSampleCount)
{
    impl_->throwPendingCommandFailure();
    (void)multiSampleCount;
    if(w<=0||h<=0) throw std::invalid_argument("Metal RenderTarget2D dimensions must be positive.");
    return std::make_unique<MetalRenderTargetRenderer>(impl_, w, h, mipMap, 0);
}
std::unique_ptr<IRenderTargetRenderer> MetalRenderer::CreateRenderTarget2DEXT(
    int w,int h,int depthFormat,bool preserveContents,bool mipMap,int multiSampleCount,int surfaceFormat)
{
    if (!MetalSupportsSurfaceFormat(surfaceFormat))
        throw System::NotSupportedException("Metal RenderTarget2D supports only SurfaceFormat::Color.");
    return CreateRenderTarget2D(w,h,depthFormat,preserveContents,mipMap,multiSampleCount);
}
std::unique_ptr<IRenderTargetCubeRenderer> MetalRenderer::CreateRenderTargetCube(
    int size,int /*depthFormat*/,bool /*preserveContents*/,bool mipMap,int /*multiSampleCount*/)
{
    impl_->throwPendingCommandFailure();
    if(size<=0) throw std::invalid_argument("Metal RenderTargetCube size must be positive.");
    return std::make_unique<MetalRenderTargetCubeRenderer>(impl_, size, mipMap);
}
// plan_metal.md METAL-109/110/111 (real bug fixed alongside RenderTargetCube's own addition): both
// SetRenderTarget2D() and the new SetRenderTargetCubeFace() below must cross-unbind whichever OTHER
// kind of render target (2D vs. cube) is currently active before binding a new one -- mirroring
// EasyGLRenderer::SetRenderTarget2D/SetRenderTargetCubeFace's own real, already-tested
// contract exactly (both cross-check `currentRt2D_`/`currentRtCube_` against each other). Without
// this, switching directly from RenderTarget A to RenderTarget B (a normal XNA multi-pass pattern --
// no intervening SetRenderTarget2D(null) call) would never call UnbindAsRenderTarget() on A, so A's
// mip chain (METAL-103) would silently never regenerate. This gap was latent and harmless before
// METAL-103 existed (nothing depended on UnbindAsRenderTarget() actually running) but became a real
// correctness bug the moment mip generation started living there -- fixed here as part of the same
// pass that made it observable, not left for a later phase to rediscover.
void MetalRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
{
    auto* metalRt=dynamic_cast<MetalRenderTargetRenderer*>(rt);
    if (rt && !metalRt) throw std::runtime_error("Metal: foreign RenderTarget2D renderer");
    auto& p=*impl_;
    p.unbindCurrentMRT(); // plan_metal.md METAL-112: tear down any active MRT set first, always.
    if (p.currentRenderTarget && p.currentRenderTarget != rt) p.currentRenderTarget->UnbindAsRenderTarget();
    if (p.currentRenderTargetCube) p.currentRenderTargetCube->UnbindAsRenderTarget();
    if (metalRt) metalRt->BindAsRenderTarget();
}
void MetalRenderer::SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt,int face)
{
    if (!rt) { SetRenderTarget2D(nullptr); return; }
    if (face<0 || face>=6) throw std::out_of_range("Metal: RenderTargetCube face must be in [0, 5]");
    auto* metalRt=dynamic_cast<MetalRenderTargetCubeRenderer*>(rt);
    if (!metalRt) throw std::runtime_error("Metal: foreign RenderTargetCube renderer");
    auto& p=*impl_;
    p.unbindCurrentMRT(); // plan_metal.md METAL-112: same as SetRenderTarget2D() above.
    if (p.currentRenderTarget) p.currentRenderTarget->UnbindAsRenderTarget();
    if (p.currentRenderTargetCube && p.currentRenderTargetCube != rt) p.currentRenderTargetCube->UnbindAsRenderTarget();
    metalRt->BindAsRenderTargetFace(face);
}
void MetalRenderer::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,int count)
{
    if (count<0) throw std::invalid_argument("Metal: render-target count cannot be negative");
    if (count==0) { SetRenderTarget2D(nullptr); return; }
    if (!renderTargets) throw std::invalid_argument("Metal: render-target descriptors cannot be null");
    if (count>1)
        throw System::NotSupportedException(
            "Metal multiple render targets are disabled until the adapted renderer has passing "
            "macOS pixel evidence.");

    const auto& target=renderTargets[0];
    if (target.IsRenderTarget2D()) {
        if (target.GetArraySlice()!=0)
            throw System::NotSupportedException("Metal RenderTarget2D array slices are not supported.");
        if (!target.GetRenderTarget2D())
            throw std::invalid_argument("Metal: RenderTarget2D descriptor has no target");
        SetRenderTarget2D(target.GetRenderTarget2D());
        return;
    }
    if (target.IsRenderTargetCubeFace()) {
        if (!target.GetRenderTargetCube())
            throw std::invalid_argument("Metal: RenderTargetCube descriptor has no target");
        SetRenderTargetCubeFace(target.GetRenderTargetCube(),target.GetCubeFace());
        return;
    }
    throw std::invalid_argument("Metal: unknown render-target descriptor type");
}
void MetalRenderer::ClearColorAndDepth(float r,float g,float b,float a,float d){impl_->clear(true,r,g,b,a,true,d,false,0);} void MetalRenderer::ClearDepth(float d){impl_->clear(false,0,0,0,0,true,d,false,0);} void MetalRenderer::ClearStencil(int s){impl_->clear(false,0,0,0,0,false,1,true,s);} void MetalRenderer::ClearDepthAndStencil(float d,int s){impl_->clear(false,0,0,0,0,true,d,true,s);} void MetalRenderer::ClearColorAndStencil(float r,float g,float b,float a,int s){impl_->clear(true,r,g,b,a,false,1,true,s);} void MetalRenderer::ClearColorDepthAndStencil(float r,float g,float b,float a,float d,int s){impl_->clear(true,r,g,b,a,true,d,true,s);}
void MetalRenderer::SetDepthTestEnabled(bool e)
{
    auto& p=*impl_;
    const auto previous=p.captureDepthState();
    p.depthEnabled=e;
    try { p.rebuildDepthState(); }
    catch (...) { p.restoreDepthState(previous); throw; }
}
void MetalRenderer::SetBlendEnabled(bool e){impl_->currentBlend=MetalBlendKeyForSetBlendEnabled(e);}
void MetalRenderer::SetDepthWriteEnabled(bool e)
{
    auto& p=*impl_;
    const auto previous=p.captureDepthState();
    p.depthWrite=e;
    try { p.rebuildDepthState(); }
    catch (...) { p.restoreDepthState(previous); throw; }
}
void MetalRenderer::ApplyBlendState(int colorSrcBlend,int alphaSrcBlend,int colorDstBlend,int alphaDstBlend,int colorBlendFunc,int alphaBlendFunc,const BlendWriteState& writeState)
{
    if (!MetalSupportsBlendWriteState(writeState))
        throw System::NotSupportedException(
            "Metal currently supports only all-channel color writes and the default multisample mask.");
    // plan_metal.md METAL-6/24: real per-BlendState pipeline selection, replacing the previous
    // complete no-op (every pipeline was hardcoded to a fixed straight-alpha blend regardless of
    // the actual requested BlendState). `enabled` derivation mirrors
    // EasyGLRenderer::ApplyBlendState's identical Blend::One/Blend::Zero Opaque-preset
    // check exactly (Blend::One=0, Blend::Zero=1).
    auto& p=*impl_;
    p.currentBlend.colorSrc=(uint8_t)colorSrcBlend; p.currentBlend.colorDst=(uint8_t)colorDstBlend;
    p.currentBlend.alphaSrc=(uint8_t)alphaSrcBlend; p.currentBlend.alphaDst=(uint8_t)alphaDstBlend;
    p.currentBlend.colorFunc=(uint8_t)colorBlendFunc; p.currentBlend.alphaFunc=(uint8_t)alphaBlendFunc;
    p.currentBlend.enabled = !(colorSrcBlend==0 && colorDstBlend==1 && alphaSrcBlend==0 && alphaDstBlend==1);
}
void MetalRenderer::ApplyDepthStencilState(bool depthEnable,bool depthWriteEnable,int depthFunc,
                                                   bool stencilEnable,int stencilFunc,int stencilPass,int stencilFail,int stencilDepthFail,
                                                   int stencilMask,int stencilWriteMask,int referenceStencil,
                                                   bool twoSidedStencilMode,int ccwStencilFunc,int ccwStencilPass,int ccwStencilFail,int ccwStencilDepthFail)
{
    // plan_metal.md METAL-7/9/10: real depthFunc + full front/back stencil-op wiring, replacing
    // the previous depthEnable/depthWrite/referenceStencil-only plumbing (depthFunc and all 8
    // stencil-op/mask/twoSided fields were silently ignored before this).
    auto& p=*impl_;
    const auto previous=p.captureDepthState();
    p.depthEnabled=depthEnable; p.depthWrite=depthWriteEnable; p.depthFunc=depthFunc;
    p.stencilEnabled=stencilEnable; p.stencilFunc=stencilFunc; p.stencilPass=stencilPass;
    p.stencilFail=stencilFail; p.stencilDepthFail=stencilDepthFail;
    p.stencilMask=stencilMask; p.stencilWriteMask=stencilWriteMask;
    p.twoSidedStencil=twoSidedStencilMode; p.ccwStencilFunc=ccwStencilFunc; p.ccwStencilPass=ccwStencilPass;
    p.ccwStencilFail=ccwStencilFail; p.ccwStencilDepthFail=ccwStencilDepthFail;
    p.refStencil=referenceStencil;
    try { p.rebuildDepthState(); }
    catch (...) { p.restoreDepthState(previous); throw; }
    if(p.encoder)[p.encoder setStencilReferenceValue:referenceStencil];
}
// plan_metal.md METAL-19: the `c==1?...:(c==2?...:...)` ternary chain this replaced switched on raw
// XNA CullMode ordinals with no enum-reordering guard; now backed by the plain-C++, unit-tested
// MetalCullMode.hpp (METAL-5's own Front/Back mapping preserved exactly).
static MTLCullMode metalCullMode(int c)
{
    using K = CNA::Internal::Renderers::Metal::MetalCullModeKind;
    switch (CNA::Internal::Renderers::Metal::DescribeMetalCullMode(c)) {
        case K::Front: return MTLCullModeFront;
        case K::Back:  return MTLCullModeBack;
        case K::None:
        default:       return MTLCullModeNone;
    }
}

void MetalRenderer::ApplyRasterizerState(int c,int f,bool se,float db,float sb)
{
    impl_->cull=metalCullMode(c);
    impl_->fill=f==1?MTLTriangleFillModeLines:MTLTriangleFillModeFill;
    impl_->rasterState.SetScissorEnabled(se);
    impl_->depthBias=db;
    impl_->slopeBias=sb;
    if(impl_->encoder){
        [impl_->encoder setFrontFacingWinding:MTLWindingClockwise];
        [impl_->encoder setCullMode:impl_->cull];
        [impl_->encoder setTriangleFillMode:impl_->fill];
        [impl_->encoder setDepthBias:db slopeScale:sb clamp:0];
        const MetalScissorState s=impl_->rasterState.NativeScissor();
        const MTLScissorRect native={(NSUInteger)s.x,(NSUInteger)s.y,
            (NSUInteger)s.width,(NSUInteger)s.height};
        [impl_->encoder setScissorRect:native];
    }
}
void MetalRenderer::ApplySamplerState(int slot,int filter,int addressU,int addressV,int maxAnisotropy)
{
    if (slot<0 || slot>=16) throw std::out_of_range("Metal: sampler slot must be in [0, 15]");
    impl_->samplerSlots[slot]=impl_->samplerFor(filter,addressU,addressV,maxAnisotropy);
}
void MetalRenderer::ApplySamplerMipState(int slot,int maxMipLevel,float lodBias)
{
    if (slot<0 || slot>=16) throw std::out_of_range("Metal: sampler slot must be in [0, 15]");
    if (maxMipLevel!=0 || lodBias!=0.0f)
        throw System::NotSupportedException(
            "Metal sampler MaxMipLevel and MipMapLevelOfDetailBias are not implemented.");
}
void MetalRenderer::SetBlendFactor(float r,float g,float b,float a){impl_->blendColor[0]=r;impl_->blendColor[1]=g;impl_->blendColor[2]=b;impl_->blendColor[3]=a;if(impl_->encoder)[impl_->encoder setBlendColorRed:r green:g blue:b alpha:a];}
void MetalRenderer::SetReferenceStencil(int v){impl_->refStencil=v;if(impl_->encoder)[impl_->encoder setStencilReferenceValue:v];}
void MetalRenderer::SetScissorRect(int x,int y,int w,int h)
{
    impl_->rasterState.SetScissor(x,y,w,h);
    if(impl_->encoder&&impl_->rasterState.IsScissorEnabled()){
        const MetalScissorState s=impl_->rasterState.NativeScissor();
        const MTLScissorRect native={(NSUInteger)s.x,(NSUInteger)s.y,
            (NSUInteger)s.width,(NSUInteger)s.height};
        [impl_->encoder setScissorRect:native];
    }
}
void MetalRenderer::SetViewport(int x,int y,int w,int h,float mn,float mx)
{
    impl_->rasterState.SetViewport(x,y,w,h,mn,mx);
    if(impl_->encoder){
        const MetalViewportState v=impl_->rasterState.EffectiveViewport();
        const MTLViewport native={v.x,v.y,v.width,v.height,v.minDepth,v.maxDepth};
        [impl_->encoder setViewport:native];
    }
}
std::unique_ptr<IVertexBufferRenderer> MetalRenderer::CreateVertexBuffer(int c){return std::make_unique<MetalVertexBuffer>(impl_->device,c);} std::unique_ptr<IIndexBufferRenderer> MetalRenderer::CreateIndexBuffer16(int){return std::make_unique<MetalIndexBuffer>(impl_->device,false);} std::unique_ptr<IIndexBufferRenderer> MetalRenderer::CreateIndexBuffer32(int){return std::make_unique<MetalIndexBuffer>(impl_->device,true);}

// plan_metal.md METAL-34-style extraction: this dispatch logic's real body now lives in the
// plain-C++ MetalSelectPipelineKind.hpp (no Objective-C, buildable and unit-tested on any platform
// without an Apple toolchain) -- kept as a thin same-signature wrapper here so the existing call
// site in drawMetal3D() is unaffected.
static PipelineKind selectPipelineKind(std::size_t stride, const GpuDrawParams* params)
{
    return SelectMetalPipelineKind(stride, params);
}

// plan_metal.md METAL-34-style extraction: fillLitUniforms/fillEnvUniforms/fillSkinnedUniforms/
// fillPbrUniforms/fillSkinnedPbrUniforms' real logic now lives in the plain-C++
// MetalUniformFill.hpp (no Objective-C, buildable and unit-tested on any platform without an Apple
// toolchain) -- kept as thin same-signature wrappers here so every existing call site in this file
// is unaffected. `params` is never null at any of these call sites -- each PipelineKind reaching
// them is only selected when selectPipelineKind()'s corresponding stride/effect gate requires a
// non-null `params`; texture pointer presence never decides pipeline shape.
static void fillLitUniforms(LitTransform& t, LitUniforms& lu, const Mat4& wvp, const GpuDrawParams& params)
{
    FillMetalLitUniforms(t, lu, wvp, params);
}

static void fillEnvUniforms(EnvTransform& t, EnvUniforms& eu, const Mat4& wvp, const GpuDrawParams& params)
{
    FillMetalEnvUniforms(t, eu, wvp, params);
}

static void fillSkinnedUniforms(SkinnedTransform& t, SkinnedUniforms& su, const Mat4& wvp, const GpuDrawParams& params)
{
    FillMetalSkinnedUniforms(t, su, wvp, params);
}

static void fillPbrUniforms(PbrTransform& t, PbrUniforms& pu, const Mat4& wvp, const GpuDrawParams& params)
{
    FillMetalPbrUniforms(t, pu, wvp, params);
}

static void fillSkinnedPbrUniforms(SkinnedPbrTransform& t, PbrUniforms& pu, const Mat4& wvp, const GpuDrawParams& params)
{
    FillMetalSkinnedPbrUniforms(t, pu, wvp, params);
}

static void drawMetal3D(MetalRenderer::Impl& p,const MetalVertexBuffer& vb,const MetalIndexBuffer* ib,const Matrix&w,const Matrix&v,const Matrix&pr,PrimitiveType pt,int pc,const GpuDrawParams* params)
{
    Mat4 wvp=transpose(multiply(multiply(fromXna(w),fromXna(v)),fromXna(pr)));
    const std::size_t drawStride=params ? CombinedVertexStrideOr(*params,vb.stride()) : vb.stride();
    const PipelineKind kind = selectPipelineKind(drawStride, params);

    id<MTLTexture> texture0=nil;
    id<MTLTexture> texture1=nil;
    id<MTLTexture> environmentCube=nil;
    id<MTLTexture> normalMap=nil;
    id<MTLTexture> metallicRoughnessMap=nil;
    id<MTLTexture> emissiveMap=nil;
    id<MTLTexture> occlusionMap=nil;
    switch(kind)
    {
        case PipelineKind::Textured20:
        case PipelineKind::ColorTex24:
        case PipelineKind::LitTex32:
        case PipelineKind::LitTex32VertexLit:
            texture0=resolveMetal2DTextureBinding(
                p,params->texture0,MetalStockTextureSlot::BasicDiffuse);
            break;
        case PipelineKind::DualTex20:
        case PipelineKind::DualTex24Colored:
            texture0=resolveMetal2DTextureBinding(
                p,params->texture0,MetalStockTextureSlot::DualFirst);
            texture1=resolveMetal2DTextureBinding(
                p,params->texture1,MetalStockTextureSlot::DualSecond);
            break;
        case PipelineKind::EnvMap32:
            texture0=resolveMetal2DTextureBinding(
                p,params->texture0,MetalStockTextureSlot::EnvironmentDiffuse);
            environmentCube=resolveMetalCubeTextureBinding(
                p,params->envMap,MetalStockTextureSlot::EnvironmentCube);
            break;
        case PipelineKind::Skinned52:
        case PipelineKind::Skinned56:
        case PipelineKind::Skinned52VertexLit:
        case PipelineKind::Skinned56VertexLit:
            texture0=resolveMetal2DTextureBinding(
                p,params->texture0,MetalStockTextureSlot::SkinnedDiffuse);
            break;
        case PipelineKind::Pbr48:
        case PipelineKind::SkinnedPbr68:
            texture0=resolveMetal2DTextureBinding(
                p,params->texture0,MetalStockTextureSlot::PbrBaseColor);
            normalMap=resolveMetal2DTextureBinding(
                p,params->pbrNormalMap,MetalStockTextureSlot::PbrNormal);
            metallicRoughnessMap=resolveMetal2DTextureBinding(
                p,params->pbrMetallicRoughnessMap,MetalStockTextureSlot::PbrMetallicRoughness);
            emissiveMap=resolveMetal2DTextureBinding(
                p,params->pbrEmissiveMap,MetalStockTextureSlot::PbrEmissive);
            occlusionMap=resolveMetal2DTextureBinding(
                p,params->pbrOcclusionMap,MetalStockTextureSlot::PbrOcclusion);
            break;
        case PipelineKind::Colored16:
        case PipelineKind::Sprite2D:
            break;
    }

    if(!p.ensureFrame()||p.rasterState.ShouldSkipDraw()) return;
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
        [p.encoder setFragmentTexture:texture0 atIndex:0];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[0]?p.samplerSlots[0]:p.sampler) atIndex:0];
    } else if (kind == PipelineKind::EnvMap32) {
        // plan_metal.md METAL-64/66-68: real cube-map reflection/Fresnel path.
        EnvTransform t{}; EnvUniforms eu{};
        fillEnvUniforms(t, eu, wvp, *params);
        [p.encoder setVertexBytes:&t length:sizeof(t) atIndex:1];
        [p.encoder setVertexBytes:&eu length:sizeof(eu) atIndex:2];
        [p.encoder setFragmentBytes:&eu length:sizeof(eu) atIndex:2];
        [p.encoder setFragmentTexture:texture0 atIndex:0];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[0]?p.samplerSlots[0]:p.sampler) atIndex:0];
        [p.encoder setFragmentTexture:environmentCube atIndex:1];
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
        if(!bonesBuf) throw std::runtime_error("Metal: failed to create skinned bone buffer");
        [p.encoder setVertexBuffer:bonesBuf offset:0 atIndex:3];
        [bonesBuf release];
        [p.encoder setFragmentTexture:texture0 atIndex:0];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[0]?p.samplerSlots[0]:p.sampler) atIndex:0];
    } else if (kind == PipelineKind::Pbr48) {
        // plan_metal.md METAL-81/83-86: real metallic-roughness PBR path.
        PbrTransform t{}; PbrUniforms pu{};
        fillPbrUniforms(t, pu, wvp, *params);
        [p.encoder setVertexBytes:&t length:sizeof(t) atIndex:1];
        [p.encoder setVertexBytes:&pu length:sizeof(pu) atIndex:2];
        [p.encoder setFragmentBytes:&pu length:sizeof(pu) atIndex:2];
        // plan_metal.md METAL-3: each of the 5 PBR texture units gets its own SamplerState slot
        // (samplerSlots[0..4]), matching EasyGLRenderer's own real PBR binding exactly
        // (each map bound to its own GL texture unit, each unit sampled through its own
        // independently-configured GL sampler object, samplers_[0..4]) -- NOT one shared sampler
        // broadcast across all 5 units, which this block previously did (a real, silent divergence:
        // a game setting a distinct SamplerState on, say, the metallic-roughness slot would have
        // been ignored, since every unit sampled through whatever was set on slot 0 instead).
        [p.encoder setFragmentTexture:texture0 atIndex:0];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[0]?p.samplerSlots[0]:p.sampler) atIndex:0];
        [p.encoder setFragmentTexture:normalMap atIndex:1];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[1]?p.samplerSlots[1]:p.sampler) atIndex:1];
        [p.encoder setFragmentTexture:metallicRoughnessMap atIndex:2];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[2]?p.samplerSlots[2]:p.sampler) atIndex:2];
        [p.encoder setFragmentTexture:emissiveMap atIndex:3];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[3]?p.samplerSlots[3]:p.sampler) atIndex:3];
        [p.encoder setFragmentTexture:occlusionMap atIndex:4];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[4]?p.samplerSlots[4]:p.sampler) atIndex:4];
    } else if (kind == PipelineKind::SkinnedPbr68) {
        // plan_metal.md METAL-82: real SkinnedPbrEffect path -- same bone-buffer handling as
        // Skinned52/56, same 5-texture PBR-map binding as Pbr48.
        SkinnedPbrTransform t{}; PbrUniforms pu{};
        fillSkinnedPbrUniforms(t, pu, wvp, *params);
        [p.encoder setVertexBytes:&t length:sizeof(t) atIndex:1];
        [p.encoder setVertexBytes:&pu length:sizeof(pu) atIndex:2];
        [p.encoder setFragmentBytes:&pu length:sizeof(pu) atIndex:2];
        id<MTLBuffer> bonesBuf = [p.device newBufferWithBytes:params->boneTransforms length:sizeof(float)*72*16 options:MTLResourceStorageModeShared];
        if(!bonesBuf) throw std::runtime_error("Metal: failed to create skinned PBR bone buffer");
        [p.encoder setVertexBuffer:bonesBuf offset:0 atIndex:3];
        [bonesBuf release];
        // plan_metal.md METAL-3: same per-slot sampler fix as Pbr48 above, see its own comment.
        [p.encoder setFragmentTexture:texture0 atIndex:0];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[0]?p.samplerSlots[0]:p.sampler) atIndex:0];
        [p.encoder setFragmentTexture:normalMap atIndex:1];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[1]?p.samplerSlots[1]:p.sampler) atIndex:1];
        [p.encoder setFragmentTexture:metallicRoughnessMap atIndex:2];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[2]?p.samplerSlots[2]:p.sampler) atIndex:2];
        [p.encoder setFragmentTexture:emissiveMap atIndex:3];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[3]?p.samplerSlots[3]:p.sampler) atIndex:3];
        [p.encoder setFragmentTexture:occlusionMap atIndex:4];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[4]?p.samplerSlots[4]:p.sampler) atIndex:4];
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
        if(kind!=PipelineKind::Colored16){
            [p.encoder setFragmentTexture:texture0 atIndex:0];
            [p.encoder setFragmentSamplerState:(p.samplerSlots[0]?p.samplerSlots[0]:p.sampler) atIndex:0];
            if(kind==PipelineKind::DualTex20||kind==PipelineKind::DualTex24Colored){
                [p.encoder setFragmentTexture:texture1 atIndex:1];
                [p.encoder setFragmentSamplerState:(p.samplerSlots[1]?p.samplerSlots[1]:p.sampler) atIndex:1];
            }
        }
    }
    int n=primitiveVertexCount(pt,pc);
    // plan_metal.md: real bug found and fixed 2026-07-20 -- every other renderer (EasyGL/Vulkan/
    // Bgfx/native GPU/WebGPU) reads GpuDrawParams::vertexStart/startIndex/baseVertex and applies them;
    // this function silently hardcoded 0/0 for all three, so any draw with a nonzero offset into a
    // shared vertex/index buffer rendered the wrong vertex range. Never caught until
    // Metal_PbrEffect_Golden/Metal_SkinnedPbrEffect_Golden (METAL-89) became the first Metal test
    // to ever exercise multiple draws from nonzero offsets within one shared buffer.
    if(ib){
        const NSUInteger startIndex=static_cast<NSUInteger>(params?params->startIndex:0);
        const NSInteger baseVertex=static_cast<NSInteger>(params?params->baseVertex:0);
        const NSUInteger indexSize=ib->IsThirtyTwoBit()?4:2;
        [p.encoder drawIndexedPrimitives:metalPrimitive(pt) indexCount:n
            indexType:ib->IsThirtyTwoBit()?MTLIndexTypeUInt32:MTLIndexTypeUInt16
            indexBuffer:ib->native() indexBufferOffset:startIndex*indexSize
            instanceCount:1 baseVertex:baseVertex baseInstance:0];
    } else {
        const NSUInteger vertexStart=static_cast<NSUInteger>(params?params->vertexStart:0);
        [p.encoder drawPrimitives:metalPrimitive(pt) vertexStart:vertexStart vertexCount:n];
    }
}
void MetalRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& v,const Matrix& w,
                                                  const Matrix& vi,const Matrix& p,
                                                  PrimitiveType pt,int pc)
{
    const auto* vb=dynamic_cast<const MetalVertexBuffer*>(&v);
    if(!vb) throw std::runtime_error("Metal: foreign vertex buffer");
    RequireFaithfulDeclarationEXT(vb->declaration(),static_cast<int>(vb->stride()),
                                  "colored-nonindexed");
    drawMetal3D(*impl_,*vb,nullptr,w,vi,p,pt,pc,nullptr);
}

void MetalRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& v,
                                                         const IIndexBufferRenderer& i,
                                                         const Matrix& w,const Matrix& vi,
                                                         const Matrix& p,PrimitiveType pt,int pc)
{
    const auto* vb=dynamic_cast<const MetalVertexBuffer*>(&v);
    const auto* ib=dynamic_cast<const MetalIndexBuffer*>(&i);
    if(!vb||!ib) throw std::runtime_error("Metal: foreign buffer");
    RequireFaithfulDeclarationEXT(vb->declaration(),static_cast<int>(vb->stride()),
                                  "colored-indexed");
    drawMetal3D(*impl_,*vb,ib,w,vi,p,pt,pc,nullptr);
}
static void ValidateMetalDrawParams(const GpuDrawParams& gp,const MetalVertexBuffer& vb)
{
    switch (DescribeMetalDrawStreamPolicy(gp)) {
        case MetalDrawStreamPolicy::Supported: break;
        case MetalDrawStreamPolicy::InvalidBinding:
            throw std::invalid_argument("Metal: invalid or internally inconsistent vertex stream metadata");
        case MetalDrawStreamPolicy::MultiStreamUnsupported:
            throw System::NotSupportedException(
                "Metal multi-stream vertex input is disabled until it has adapted macOS proof.");
        case MetalDrawStreamPolicy::InstancingUnsupported:
            throw System::NotSupportedException(
                "Metal instancing is disabled until it has adapted macOS proof.");
    }
    if (!MetalSingleStreamMatchesUploadedBuffer(gp,vb,vb.stride()))
        throw std::invalid_argument(
            "Metal: GpuDrawParams stream 0 must name the draw vertex buffer and match its uploaded stride");
    if (gp.customEffectRenderer)
        throw System::NotSupportedException(
            "Metal custom-effect 3D draws are disabled until they have adapted macOS proof.");
}
void MetalRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& v,const Matrix& w,
                                             const Matrix& vi,const Matrix& p,
                                             PrimitiveType pt,int pc,const GpuDrawParams& gp)
{
    const auto* vb=dynamic_cast<const MetalVertexBuffer*>(&v);
    if(!vb) throw std::runtime_error("Metal: foreign vertex buffer");
    ValidateMetalDrawParams(gp,*vb);
    RequireFaithfulDeclarationEXT(vb->declaration(),static_cast<int>(vb->stride()),
                                  "ordinary-nonindexed");
    drawMetal3D(*impl_,*vb,nullptr,w,vi,p,pt,pc,&gp);
}

void MetalRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& v,
                                                    const IIndexBufferRenderer& i,
                                                    const Matrix& w,const Matrix& vi,
                                                    const Matrix& p,PrimitiveType pt,int pc,
                                                    const GpuDrawParams& gp)
{
    const auto* vb=dynamic_cast<const MetalVertexBuffer*>(&v);
    const auto* ib=dynamic_cast<const MetalIndexBuffer*>(&i);
    if(!vb||!ib) throw std::runtime_error("Metal: foreign buffer");
    ValidateMetalDrawParams(gp,*vb);
    RequireFaithfulDeclarationEXT(vb->declaration(),static_cast<int>(vb->stride()),
                                  "ordinary-indexed");
    drawMetal3D(*impl_,*vb,ib,w,vi,p,pt,pc,&gp);
}
void MetalRenderer::SetStringMarkerEXT(const char* m)
{
    if(!impl_->ensureFrame()) return;
    if(m) [impl_->encoder insertDebugSignpost:[NSString stringWithUTF8String:m]];
}

bool MetalRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
{
    return MetalSupportsCapability(capability);
}

}

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_METAL
std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args){return std::make_unique<Metal::MetalRenderer>(args);}
#endif
}
#else
#error "CNA Metal renderer must be compiled on an Apple platform"
#endif
