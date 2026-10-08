// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Metal/MetalRenderer.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
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
#include "CNA/Internal/Renderers/Metal/MetalDeclaredVertexInput.hpp"
#include "CNA/Internal/Renderers/Metal/MetalDepthPolicy.hpp"
#include "CNA/Internal/Renderers/Metal/MetalPolicy.hpp"
#if defined(CNA_METAL_COMPILED_EFFECTS)
#include "CNA/Internal/Renderers/Metal/MetalCompiledEffect.hpp"
#include "Fna3dStockEffectBlobs.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "System/InvalidOperationException.hpp"
#endif
// plans/plan_metal.md Phase 14 (METAL-142-152): needs Effect's complete type (not just
// IGraphicsRenderer.hpp's own forward declaration) to call Apply()/GetEffectRendererPtr() from
// MetalSpriteBatch's custom-effect wiring below.
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/NotSupportedException.hpp"

#ifdef __APPLE__
#include <TargetConditionals.h>
// plans/plan_apple_m4.md AM4-037: the renderer draws into a CAMetalLayer-backed view it adds to the
// platform's native window -- an NSView in the NSWindow on macOS, a UIView in the UIWindow on iOS.
// Everything below the view is the same Metal on both.
#if TARGET_OS_OSX
#import <Cocoa/Cocoa.h>
#else
#import <UIKit/UIKit.h>
#endif
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
// plans/plan_apple_m4.md AM4-099: MTLSamplerDescriptor.lodBias is declared only by the macOS/iOS 26
// SDKs. @available is a run-time check, so with an older SDK the property does not exist and the
// renderer must not name it at all; a non-zero bias is then refused like on an older OS.
#ifndef CNA_METAL_SDK_HAS_SAMPLER_LOD_BIAS
#if (defined(__MAC_26_0) && defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && \
     __MAC_OS_X_VERSION_MAX_ALLOWED >= __MAC_26_0) ||                  \
    (defined(__IPHONE_26_0) && defined(__IPHONE_OS_VERSION_MAX_ALLOWED) && \
     __IPHONE_OS_VERSION_MAX_ALLOWED >= __IPHONE_26_0)
#define CNA_METAL_SDK_HAS_SAMPLER_LOD_BIAS 1
#else
#define CNA_METAL_SDK_HAS_SAMPLER_LOD_BIAS 0
#endif
#endif

namespace {
// plans/plan_apple_m4.md AM4-102: this file is compiled without ARC, and neither the renderer nor
// the game loop had an autorelease pool, so every autoreleased object a call produced -- each
// frame's command buffer, encoder, render-pass descriptor and drawable among them -- was never
// released (objc "MISSING POOLS ... just leaking"). Every entry point that sends Objective-C
// messages opens one of these, so its temporaries are released when it returns. Objects kept
// across calls are retained explicitly (MetalObjectOwner::Reset, [x retain]); none of the scoped
// entry points returns an Objective-C object, and the pools nest strictly with any the
// application holds.
// plans/plan_apple_m4.md AM4-138: a failed command buffer's own NSError, for the exception that
// reports it -- code and description, rather than only the fact that something failed.
std::string describeMetalCommandBufferError(NSError* error)
{
    if (error == nil) return std::string("no error object attached");
    std::string text = "MTLCommandBufferError " + std::to_string(static_cast<long>(error.code));
    if (NSString* description = error.localizedDescription)
    {
        const char* utf8 = [description UTF8String];
        if (utf8 != nullptr && *utf8 != '\0') { text += ": "; text += utf8; }
    }
    return text;
}

class MetalAutoreleaseScope
{
public:
    MetalAutoreleaseScope() : pool_([[NSAutoreleasePool alloc] init]) {}
    ~MetalAutoreleaseScope() { [pool_ drain]; }
    MetalAutoreleaseScope(const MetalAutoreleaseScope&) = delete;
    MetalAutoreleaseScope& operator=(const MetalAutoreleaseScope&) = delete;

private:
    NSAutoreleasePool* pool_;
};
}
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <bit>
#include <map>
#include <memory>
#include <tuple>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#if TARGET_OS_OSX
@interface CNAMetalView : NSView
#else
@interface CNAMetalView : UIView
#endif
- (void)updateDrawableWidth:(int)width height:(int)height displayScale:(float)displayScale;
@end

@implementation CNAMetalView
+ (Class)layerClass
{
    return [CAMetalLayer class];
}

#if TARGET_OS_OSX
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
#else
// UIView builds its backing layer from +layerClass. Touches must keep reaching SDL's own view
// underneath, which is what the NSView's nil hitTest: does on macOS.
- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil)
    {
        self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        self.userInteractionEnabled = NO;
        self.layer.opaque = YES;
    }
    return self;
}
#endif

- (void)updateDrawableWidth:(int)width height:(int)height displayScale:(float)displayScale
{
    CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer;
    metalLayer.contentsScale = displayScale > 0.0f ? displayScale : 1.0f;
    metalLayer.drawableSize = CGSizeMake(MAX(1, width), MAX(1, height));
}

#if TARGET_OS_OSX
- (NSView*)hitTest:(NSPoint)point
{
    (void)point;
    return nil;
}
#endif
@end

namespace CNA::Internal::Renderers::Metal
{
// plans/plan_metal.md METAL-131/122/125: forward-declared at this (non-anonymous-namespace) scope so
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
// plans/plan_apple_m4.md AM4-141: BlendState.MultiSampleMask that keeps some samples of a
// multisampled target. Metal has no pipeline sample mask, so every stock fragment function can
// also write [[sample_mask]] -- only in the pipeline variant specialized with cnaSampleMaskOut,
// so an ordinary draw neither carries the output nor binds buffer 28.
constant bool cnaSampleMaskOut [[function_constant(0)]];
struct CnaFragOut { float4 color [[color(0)]]; uint mask [[sample_mask, function_constant(cnaSampleMaskOut)]]; };
// plans/plan_apple_m4.md AM4-143: CNA's per-instance world matrix (EasyGL's REMED-GFX-122), the
// first four elements of an instanced draw's per-instance streams at attributes 12..15, applied
// before the effect's World: XNA's `position * InstanceWorld * World * View * Projection`. Only the
// pipeline variant specialized with cnaInstanced declares the attributes, so an ordinary draw's
// vertex function is the one it always was.
constant bool cnaInstanced [[function_constant(1)]];
#define CNA_INSTANCE_INPUTS \
    float4 cnaInst0 [[attribute(12), function_constant(cnaInstanced)]]; \
    float4 cnaInst1 [[attribute(13), function_constant(cnaInstanced)]]; \
    float4 cnaInst2 [[attribute(14), function_constant(cnaInstanced)]]; \
    float4 cnaInst3 [[attribute(15), function_constant(cnaInstanced)]];
#define CNA_INSTANCE_MATRIX(in) float4x4(in.cnaInst0, in.cnaInst1, in.cnaInst2, in.cnaInst3)
#define CNA_INSTANCE_POSITION(in, p) (cnaInstanced ? CNA_INSTANCE_MATRIX(in) * (p) : (p))
#define CNA_INSTANCE_DIRECTION(in, d) \
    (cnaInstanced ? float3x3(in.cnaInst0.xyz, in.cnaInst1.xyz, in.cnaInst2.xyz) * (d) : (d))

struct U3D { float4x4 wvp; };
// plans/plan_metal.md METAL-35/36/37/51-63: DiffuseColor/VertexColorEnabled/AlphaTest/DualTexture
// material uniforms, shared by every unlit-textured fragment variant below. alphaTest defaults
// to {0,0,1,1} (CNA's documented "always pass" convention -- tolerance=0 forces the `a<refVal`
// branch, which is always false since alpha is never negative, so failWeight is selected but
// `1.0 < 0.0` is false and nothing discards) so folding this check into every fragment shader
// unconditionally is provably a no-op for draws that never touch AlphaTestEffect, exactly
// mirroring EasyGLRenderer::EnsureDualTextured3DProgram()'s own fsrc, which does the same.
struct UMaterialParams { float4 diffuseColor; float4 alphaTest; float4 flags; float4 fogColor; }; // flags.x = vertexColorEnabled (0/1)
// plans/plan_apple_m4.md AM4-137: the unlit BasicEffect, AlphaTestEffect and DualTextureEffect
// functions fog too. fogVector (vertex buffer 2) is GpuDrawParams.fogVector, zero when fog is off,
// so the keep factor is 1 and nothing changes; the fragment applies XNA's Common.fxh ApplyFog,
// lerp(colour, FogColor * alpha, fogFactor), as EasyGL does.
struct V3Out { float4 position [[position]]; float4 color; float2 uv; float fogFactor; };
inline float cna_fog_keep(float4 position, float4 fogVector) {
    return 1.0 - clamp(dot(position, fogVector), 0.0, 1.0);
}
struct V3ColorIn { float3 position [[attribute(0)]]; float4 color [[attribute(1)]];  CNA_INSTANCE_INPUTS };
struct V3TexIn { float3 position [[attribute(0)]]; float2 uv [[attribute(1)]];  CNA_INSTANCE_INPUTS };
struct V3ColorTexIn { float3 position [[attribute(0)]]; float4 color [[attribute(1)]]; float2 uv [[attribute(2)]];  CNA_INSTANCE_INPUTS };
struct V3NormalTexIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float2 uv [[attribute(2)]];  CNA_INSTANCE_INPUTS };
// plans/plan_apple_m4.md AM4-081: the lit BasicEffect functions also read COLOR0. A draw whose effect
// permutation does not use vertex colour gets opaque white from the constant block (AM4-080).
struct V3NormalTexColorIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float2 uv [[attribute(2)]]; float4 color [[attribute(3)]];  CNA_INSTANCE_INPUTS };
vertex V3Out cna_v3d_color(V3ColorIn in [[stage_in]], constant U3D& u [[buffer(1)]], constant float4& fogVector [[buffer(2)]]) {
    V3Out o; float4 p=CNA_INSTANCE_POSITION(in, float4(in.position,1.0)); o.position=u.wvp*p; o.color=in.color; o.uv=float2(0.0); o.fogFactor=cna_fog_keep(p,fogVector); return o;
}
vertex V3Out cna_v3d_tex(V3TexIn in [[stage_in]], constant U3D& u [[buffer(1)]], constant float4& fogVector [[buffer(2)]]) {
    V3Out o; float4 p=CNA_INSTANCE_POSITION(in, float4(in.position,1.0)); o.position=u.wvp*p; o.color=float4(1.0); o.uv=in.uv; o.fogFactor=cna_fog_keep(p,fogVector); return o;
}
vertex V3Out cna_v3d_colortex(V3ColorTexIn in [[stage_in]], constant U3D& u [[buffer(1)]], constant float4& fogVector [[buffer(2)]]) {
    V3Out o; float4 p=CNA_INSTANCE_POSITION(in, float4(in.position,1.0)); o.position=u.wvp*p; o.color=in.color; o.uv=in.uv; o.fogFactor=cna_fog_keep(p,fogVector); return o;
}
// Returns discard-tested output alpha via `outA`; callers that don't need a second sample (the
// non-textured colored path) just pass the already-known alpha straight through.
inline bool cna_alpha_test_fails(float a, float4 at) {
    bool pass = (at.y > 0.0) ? (abs(a - at.x) < at.y) : (a < at.x);
    float w = pass ? at.z : at.w;
    return w < 0.0;
}
// plans/plan_apple_m4.md AM4-106: XNA's unlit vertex shaders hand DiffuseColor (times the vertex colour
// in their Vc variants) to COLOR0, which Direct3D 9 saturates before interpolation and texturing --
// EasyGL's SOFTWARE-153/155 and WebGPU's AM4-096 rule. The product with a texel is not clamped.
fragment CnaFragOut cna_f3d_color(V3Out in [[stage_in]], constant UMaterialParams& m [[buffer(2)]], constant uint& cnaSampleMask [[buffer(28), function_constant(cnaSampleMaskOut)]]) {
    float4 vcolor = (m.flags.x > 0.5) ? in.color : float4(1.0);
    float4 c = saturate(vcolor * m.diffuseColor);
    if (cna_alpha_test_fails(c.a, m.alphaTest)) discard_fragment();
    c.rgb = mix(m.fogColor.rgb * c.a, c.rgb, in.fogFactor);
    { CnaFragOut cnaOut; cnaOut.color = (c); if (cnaSampleMaskOut) cnaOut.mask = cnaSampleMask; return cnaOut; }
}
fragment CnaFragOut cna_f3d_texture(V3Out in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]], constant UMaterialParams& m [[buffer(2)]], constant uint& cnaSampleMask [[buffer(28), function_constant(cnaSampleMaskOut)]]) {
    float4 vcolor = (m.flags.x > 0.5) ? in.color : float4(1.0);
    float4 c = tex.sample(smp, in.uv) * saturate(vcolor * m.diffuseColor);
    if (cna_alpha_test_fails(c.a, m.alphaTest)) discard_fragment();
    c.rgb = mix(m.fogColor.rgb * c.a, c.rgb, in.fogFactor);
    { CnaFragOut cnaOut; cnaOut.color = (c); if (cnaSampleMaskOut) cnaOut.mask = cnaSampleMask; return cnaOut; }
}
// DualTextureEffect (plans/plan_metal.md METAL-58/59): ported from FNA's real DualTextureEffect.fx
// PSDualTexture -- `color.rgb *= 2; color *= overlay * diffuse;` (a lightmap-style RGB-doubling
// factor on the FIRST texture only, alpha untouched) -- already found, fixed, and pixel-verified
// on EasyGL/Vulkan/Bgfx (docs/dualtextureeffect-support.md Task 383).
// plans/plan_apple_m4.md AM4-035: the second texture is sampled with TEXCOORD1, as XNA's
// DualTextureEffect does and Vulkan's has since VULKAN-150; a record without a second set feeds
// TEXCOORD0 to both (MetalDeclaredVertexInput.hpp), which is what one shared UV used to mean.
struct V3DualOut { float4 position [[position]]; float4 color; float2 uv; float2 uv1; float fogFactor; };
struct V3DualTexIn { float3 position [[attribute(0)]]; float2 uv [[attribute(1)]]; float2 uv1 [[attribute(2)]];  CNA_INSTANCE_INPUTS };
struct V3DualColorTexIn { float3 position [[attribute(0)]]; float4 color [[attribute(1)]]; float2 uv [[attribute(2)]]; float2 uv1 [[attribute(3)]];  CNA_INSTANCE_INPUTS };
vertex V3DualOut cna_v3d_dualtex(V3DualTexIn in [[stage_in]], constant U3D& u [[buffer(1)]], constant float4& fogVector [[buffer(2)]]) {
    V3DualOut o; float4 p=CNA_INSTANCE_POSITION(in, float4(in.position,1.0)); o.position=u.wvp*p; o.color=float4(1.0); o.uv=in.uv; o.uv1=in.uv1; o.fogFactor=cna_fog_keep(p,fogVector); return o;
}
vertex V3DualOut cna_v3d_dualtex_color(V3DualColorTexIn in [[stage_in]], constant U3D& u [[buffer(1)]], constant float4& fogVector [[buffer(2)]]) {
    V3DualOut o; float4 p=CNA_INSTANCE_POSITION(in, float4(in.position,1.0)); o.position=u.wvp*p; o.color=in.color; o.uv=in.uv; o.uv1=in.uv1; o.fogFactor=cna_fog_keep(p,fogVector); return o;
}
fragment CnaFragOut cna_f3d_dualtex(V3DualOut in [[stage_in]], texture2d<float> tex0 [[texture(0)]], sampler smp0 [[sampler(0)]], texture2d<float> tex1 [[texture(1)]], sampler smp1 [[sampler(1)]], constant UMaterialParams& m [[buffer(2)]], constant uint& cnaSampleMask [[buffer(28), function_constant(cnaSampleMaskOut)]]) {
    float4 vcolor = (m.flags.x > 0.5) ? in.color : float4(1.0);
    float4 base = tex0.sample(smp0, in.uv);
    base.rgb *= 2.0;
    float4 c = base * tex1.sample(smp1, in.uv1) * saturate(vcolor * m.diffuseColor);
    if (cna_alpha_test_fails(c.a, m.alphaTest)) discard_fragment();
    c.rgb = mix(m.fogColor.rgb * c.a, c.rgb, in.fogFactor);
    { CnaFragOut cnaOut; cnaOut.color = (c); if (cnaSampleMaskOut) cnaOut.mask = cnaSampleMask; return cnaOut; }
}

// BasicEffect per-pixel lighting (plans/plan_metal.md METAL-38/40-47), ported line-for-line from
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
struct VLitOut { float4 position [[position]]; float3 normal; float2 uv; float3 worldPos; float fogFactor; float4 color; };
vertex VLitOut cna_v3d_lit(V3NormalTexColorIn in [[stage_in]], constant LitTransform& t [[buffer(1)]], constant LitUniforms& lu [[buffer(2)]]) {
    VLitOut o;
    float4 p = CNA_INSTANCE_POSITION(in, float4(in.position, 1.0));
    o.position = t.wvp * p;
    o.color = in.color;
    float3x3 normalMat = float3x3(t.normalCol0.xyz, t.normalCol1.xyz, t.normalCol2.xyz);
    o.normal = normalMat * CNA_INSTANCE_DIRECTION(in, in.normal);
    o.uv = in.uv;
    o.fogFactor = 1.0 - clamp(dot(p, lu.fogVector), 0.0, 1.0);
    o.worldPos = (t.world * p).xyz;
    return o;
}
fragment CnaFragOut cna_f3d_lit(VLitOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]], constant LitUniforms& lu [[buffer(2)]], constant uint& cnaSampleMask [[buffer(28), function_constant(cnaSampleMaskOut)]]) {
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
    // XNA's PSBasicPixelLightingVc: the interpolated vertex colour scales the texel, then the light.
    float4 c = tex.sample(smp, in.uv) * float4(litRGB * in.color.rgb, lu.diffuseColor.w * in.color.a);
    c.rgb += specularRGB * c.a;
    if (cna_alpha_test_fails(c.a, lu.alphaTest)) discard_fragment();
    c.rgb = mix(lu.fogColorEnabled.xyz * c.a, c.rgb, in.fogFactor);   // AM4-137: XNA ApplyFog
    { CnaFragOut cnaOut; cnaOut.color = (c); if (cnaSampleMaskOut) cnaOut.mask = cnaSampleMask; return cnaOut; }
}

// plans/plan_metal.md METAL-39: real XNA BasicEffect defaults PreferPerPixelLighting=false, which selects
// a per-vertex (Gouraud) lit shader family -- lighting is computed ONCE per vertex and interpolated
// across the triangle, not re-evaluated per fragment. cna_v3d_lit/cna_f3d_lit above are the
// PreferPerPixelLighting=true family; this is its per-vertex-lit sibling, ported line-for-line from
// EasyGLRenderer::EnsureLit3DVertexLitProgram()'s real GLSL (identical Blinn-Phong math to
// EnsureLit3DProgram() -- only the STAGE it runs in changes). Reuses the SAME LitTransform/
// LitUniforms structs as the per-pixel variant (just consumed differently), so fillLitUniforms()
// needs no changes at all.
struct VLitVertexLitOut { float4 position [[position]]; float2 uv; float fogFactor; float3 litRGB; float3 specularRGB; float alpha; };
vertex VLitVertexLitOut cna_v3d_lit_vertexlit(V3NormalTexColorIn in [[stage_in]], constant LitTransform& t [[buffer(1)]], constant LitUniforms& lu [[buffer(2)]]) {
    VLitVertexLitOut o;
    float4 p = CNA_INSTANCE_POSITION(in, float4(in.position, 1.0));
    o.position = t.wvp * p;
    o.uv = in.uv;
    float3x3 normalMat = float3x3(t.normalCol0.xyz, t.normalCol1.xyz, t.normalCol2.xyz);
    float3 N = normalize(normalMat * CNA_INSTANCE_DIRECTION(in, in.normal));
    float3 worldPos = (t.world * p).xyz;
    float3 E = normalize(lu.eyePosition.xyz - worldPos);
    float dotL0 = dot(N, -lu.light0Dir.xyz); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -lu.light1Dir.xyz); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -lu.light2Dir.xyz); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
    float3 lightSum = lu.ambientColor.xyz + lu.light0Diffuse.xyz*NdotL0 + lu.light1Diffuse.xyz*NdotL1 + lu.light2Diffuse.xyz*NdotL2;
    // XNA's VSBasicVertexLightingVc: `vout.Diffuse *= vin.Color` -- the lit colour and the alpha --
    // written to COLOR0, which Direct3D 9 saturates before interpolation (AM4-106; EasyGL FX-123/125).
    o.litRGB = saturate((lightSum * lu.diffuseColor.xyz + lu.emissiveColor.xyz) * in.color.rgb);
    o.alpha = saturate(lu.diffuseColor.w * in.color.a);
    float3 h0 = normalize(E - lu.light0Dir.xyz); float spec0 = pow(max(dot(h0,N),0.0)*zeroL0, lu.specularColorPower.w);
    float3 h1 = normalize(E - lu.light1Dir.xyz); float spec1 = pow(max(dot(h1,N),0.0)*zeroL1, lu.specularColorPower.w);
    float3 h2 = normalize(E - lu.light2Dir.xyz); float spec2 = pow(max(dot(h2,N),0.0)*zeroL2, lu.specularColorPower.w);
    o.specularRGB = saturate((spec0*lu.light0Specular.xyz + spec1*lu.light1Specular.xyz + spec2*lu.light2Specular.xyz) * lu.specularColorPower.xyz);
    o.fogFactor = 1.0 - clamp(dot(float4(in.position, 1.0), lu.fogVector), 0.0, 1.0);
    return o;
}
fragment CnaFragOut cna_f3d_lit_vertexlit(VLitVertexLitOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]], constant LitUniforms& lu [[buffer(2)]], constant uint& cnaSampleMask [[buffer(28), function_constant(cnaSampleMaskOut)]]) {
    float4 c = tex.sample(smp, in.uv) * float4(in.litRGB, in.alpha);
    c.rgb += in.specularRGB * c.a;
    if (cna_alpha_test_fails(c.a, lu.alphaTest)) discard_fragment();
    c.rgb = mix(lu.fogColorEnabled.xyz * c.a, c.rgb, in.fogFactor);   // AM4-137: XNA ApplyFog
    { CnaFragOut cnaOut; cnaOut.color = (c); if (cnaSampleMaskOut) cnaOut.mask = cnaSampleMask; return cnaOut; }
}

// EnvironmentMapEffect (plans/plan_metal.md METAL-64/66-68), ported line-for-line from
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
    float4 p = CNA_INSTANCE_POSITION(in, float4(in.position, 1.0));
    o.position = t.wvp * p;
    float3 worldPos = (t.world * p).xyz;
    float3x3 normalMat = float3x3(t.normalCol0.xyz, t.normalCol1.xyz, t.normalCol2.xyz);
    float3 worldNormal = normalize(normalMat * CNA_INSTANCE_DIRECTION(in, in.normal));
    float3 eyeVector = normalize(eu.eyePosition.xyz - worldPos);
    o.worldNormal = worldNormal;
    o.eyeDir = eyeVector;
    o.uv = in.uv;
    float viewAngle = dot(eyeVector, worldNormal);
    // AM4-140: EnvironmentMapEffect.fx carries this factor to the pixel shader in COLOR1, which
    // Direct3D 9 saturates before interpolation -- EasyGL's SAMPLE-037 rule. EnvironmentMapAmount
    // reaches past 1, and the unclamped value made the lerp below extrapolate past the cube's colour.
    o.fresnel = saturate((eu.envParams.y > 0.5)
        ? pow(max(1.0 - abs(viewAngle), 0.0), eu.envParams.z) * eu.envParams.x
        : eu.envParams.x);
    o.fogFactor = 1.0 - clamp(dot(p, eu.fogVector), 0.0, 1.0);
    return o;
}
fragment CnaFragOut cna_f3d_envmap(VEnvOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]], texturecube<float> envMap [[texture(1)]], sampler envSmp [[sampler(1)]], constant EnvUniforms& eu [[buffer(2)]], constant uint& cnaSampleMask [[buffer(28), function_constant(cnaSampleMaskOut)]]) {
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
    c.rgb = mix(eu.fogColorEnabled.xyz * c.a, c.rgb, in.fogFactor);   // AM4-137: XNA ApplyFog
    { CnaFragOut cnaOut; cnaOut.color = (c); if (cnaSampleMaskOut) cnaOut.mask = cnaSampleMask; return cnaOut; }
}

// SkinnedEffect (plans/plan_metal.md METAL-72-80), ported line-for-line from
// EasyGLRenderer::EnsureSkinnedProgram()'s real GLSL. Vertex layout: position(12)+
// normal(12)+uv(8)+boneWeights(16, real float4, not packed/normalized)+boneIndices(4, packed
// UChar4, unnormalized -- read as an integer type in-shader, not auto-converted to float like a
// Normalized format would be) = 52 bytes; +color(4, packed UChar4Normalized) = 56 -- confirmed
// against WebGPURenderer::GetOrCreatePipelineSkinned3D's own `hasVertexColor=(stride==56)`.
// The normal is transformed by `mat3(skinMat)` (the bone blend's own upper-left 3x3, XNA's
// direct-bone transform), normalized, and then -- plans/plan_apple_m4.md AM4-145 -- taken to world
// space through World's inverse transpose, as XNA's SkinnedEffect.fx does in
// ComputeCommonVSOutputWithLighting and EasyGL has since REMED-GFX-006. The port this replaced
// stopped after the bone transform, so a rotated or non-uniformly scaled model was lit as if World
// were the identity (Metal_SkinnedEffect_WorldNormal).
// skinParams.x = weightsPerVertex; normalCol0..2 = World's inverse transpose (AM4-145).
struct SkinnedTransform { float4x4 wvp; float4x4 world; float4 skinParams; float4 normalCol0; float4 normalCol1; float4 normalCol2; };
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
struct VSkinnedIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float2 uv [[attribute(2)]]; float4 boneWeights [[attribute(3)]]; uchar4 boneIndices [[attribute(4)]];  CNA_INSTANCE_INPUTS };
struct VSkinnedColorIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float2 uv [[attribute(2)]]; float4 boneWeights [[attribute(3)]]; uchar4 boneIndices [[attribute(4)]]; float4 color [[attribute(5)]];  CNA_INSTANCE_INPUTS };
struct VSkinnedOut { float4 position [[position]]; float3 normal; float2 uv; float3 worldPos; float fogFactor; float4 color; };
inline VSkinnedOut cna_skin_common(float3 position, float3 normal, float2 uv, float4 boneWeights, uchar4 boneIndices, float4 vcolor,
                                    constant SkinnedTransform& t, constant float4x4* bones,
                                    float4 fogVector, float4x4 instance) {
    VSkinnedOut o;
    int weightsPerVertex = int(t.skinParams.x);
    // Task 895: real XNA Skin(vin, boneCount) only sums the first WeightsPerVertex (1, 2, or 4)
    // weight/index pairs.
    float4x4 skinMat = bones[boneIndices.x] * boneWeights.x;
    if (weightsPerVertex >= 2) skinMat += bones[boneIndices.y] * boneWeights.y;
    if (weightsPerVertex >= 4) skinMat += bones[boneIndices.z] * boneWeights.z + bones[boneIndices.w] * boneWeights.w;
    float4 skinnedPos = skinMat * float4(position, 1.0);
    if (cnaInstanced) skinnedPos = instance * skinnedPos;   // AM4-143
    o.position = t.wvp * skinnedPos;
    // Safe-normalize guard (ported, not invented): a vertex blended near-evenly between two bones
    // whose relative rotation is near 180 degrees can make the linearly-blended skinMat's
    // rotational part nearly cancel for a given normal, collapsing its transformed length toward
    // zero; normalize() of a near-zero vector is unstable (can yield NaN). Falls back to the
    // untransformed bind-pose normal for just that vertex.
    float3x3 skinMat3 = float3x3(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    float3 skinnedNormal = skinMat3 * normal;
    float skinnedNormalLen = length(skinnedNormal);
    float3 boneNormal = (skinnedNormalLen > 1e-6) ? (skinnedNormal / skinnedNormalLen) : normal;
    if (cnaInstanced) boneNormal = float3x3(instance[0].xyz, instance[1].xyz, instance[2].xyz) * boneNormal;
    // AM4-145: XNA's SkinnedEffect.fx then takes the normal to world space through
    // WorldInverseTranspose (EasyGL's REMED-GFX-006); the fragment renormalizes.
    o.normal = float3x3(t.normalCol0.xyz, t.normalCol1.xyz, t.normalCol2.xyz) * boneNormal;
    o.uv = uv;
    o.worldPos = (t.world * skinnedPos).xyz;
    o.color = vcolor;
    o.fogFactor = 1.0 - clamp(dot(skinnedPos, fogVector), 0.0, 1.0);
    return o;
}
vertex VSkinnedOut cna_v3d_skinned(VSkinnedIn in [[stage_in]], constant SkinnedTransform& t [[buffer(1)]], constant SkinnedUniforms& su [[buffer(2)]], constant float4x4* bones [[buffer(3)]]) {
    return cna_skin_common(in.position, in.normal, in.uv, in.boneWeights, in.boneIndices, float4(1.0), t, bones, su.fogVector, cnaInstanced ? CNA_INSTANCE_MATRIX(in) : float4x4(1.0));
}
vertex VSkinnedOut cna_v3d_skinned_color(VSkinnedColorIn in [[stage_in]], constant SkinnedTransform& t [[buffer(1)]], constant SkinnedUniforms& su [[buffer(2)]], constant float4x4* bones [[buffer(3)]]) {
    return cna_skin_common(in.position, in.normal, in.uv, in.boneWeights, in.boneIndices, in.color, t, bones, su.fogVector, cnaInstanced ? CNA_INSTANCE_MATRIX(in) : float4x4(1.0));
}
fragment CnaFragOut cna_f3d_skinned(VSkinnedOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]], constant SkinnedUniforms& su [[buffer(2)]], constant uint& cnaSampleMask [[buffer(28), function_constant(cnaSampleMaskOut)]]) {
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
    c.rgb = mix(su.fogColorEnabled.xyz * c.a, c.rgb, in.fogFactor);   // AM4-137: XNA ApplyFog
    { CnaFragOut cnaOut; cnaOut.color = (c); if (cnaSampleMaskOut) cnaOut.mask = cnaSampleMask; return cnaOut; }
}

// plans/plan_metal.md METAL-76: real XNA SkinnedEffect defaults PreferPerPixelLighting=false too, same as
// BasicEffect (METAL-39) -- this is its per-vertex-lit sibling, ported line-for-line from
// EasyGLRenderer::EnsureSkinnedVertexLitProgram()'s real GLSL (same technique as METAL-39:
// move the Blinn-Phong math from the fragment stage into the vertex stage, Gouraud-interpolate the
// result). The skinning itself (bone blend, degenerate-normal guard) is unchanged from
// cna_skin_common -- only WHERE lighting is evaluated moves. No separate ambient uniform here,
// matching cna_f3d_skinned's own shape (SkinnedEffect pre-folds ambient into emissiveColor at the
// C++ effect layer), so SkinnedUniforms/fillSkinnedUniforms() need no changes.
struct VSkinnedVertexLitOut { float4 position [[position]]; float2 uv; float fogFactor; float3 litRGB; float3 specularRGB; float4 color; };
inline VSkinnedVertexLitOut cna_skin_vertexlit_common(float3 position, float3 normal, float2 uv, float4 boneWeights, uchar4 boneIndices, float4 vcolor,
                                    constant SkinnedTransform& t, constant SkinnedUniforms& su, constant float4x4* bones,
                                    float4x4 instance) {
    VSkinnedVertexLitOut o;
    int weightsPerVertex = int(t.skinParams.x);
    float4x4 skinMat = bones[boneIndices.x] * boneWeights.x;
    if (weightsPerVertex >= 2) skinMat += bones[boneIndices.y] * boneWeights.y;
    if (weightsPerVertex >= 4) skinMat += bones[boneIndices.z] * boneWeights.z + bones[boneIndices.w] * boneWeights.w;
    float4 skinnedPos = skinMat * float4(position, 1.0);
    if (cnaInstanced) skinnedPos = instance * skinnedPos;   // AM4-143
    o.position = t.wvp * skinnedPos;
    float3x3 skinMat3 = float3x3(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    float3 skinnedNormal = skinMat3 * normal;
    float skinnedNormalLen = length(skinnedNormal);
    float3 boneNormal = (skinnedNormalLen > 1e-6) ? (skinnedNormal / skinnedNormalLen) : normal;
    if (cnaInstanced) boneNormal = float3x3(instance[0].xyz, instance[1].xyz, instance[2].xyz) * boneNormal;
    // AM4-145: WorldInverseTranspose, as in cna_skin_common.
    float3 N = normalize(float3x3(t.normalCol0.xyz, t.normalCol1.xyz, t.normalCol2.xyz) * boneNormal);
    o.uv = uv;
    o.color = vcolor;
    float3 worldPos = (t.world * skinnedPos).xyz;
    float3 E = normalize(su.eyePosition.xyz - worldPos);
    float dotL0 = dot(N, -su.light0Dir.xyz); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -su.light1Dir.xyz); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -su.light2Dir.xyz); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
    float3 lightSum = su.light0Diffuse.xyz*NdotL0 + su.light1Diffuse.xyz*NdotL1 + su.light2Diffuse.xyz*NdotL2;
    // AM4-106: COLOR0/COLOR1 of XNA's per-vertex-lit SkinnedEffect, saturated by Direct3D 9.
    o.litRGB = saturate(lightSum * su.diffuseColor.xyz + su.emissiveColor.xyz);
    float3 h0 = normalize(E - su.light0Dir.xyz); float spec0 = pow(max(dot(h0,N),0.0)*zeroL0, su.specularColorPower.w);
    float3 h1 = normalize(E - su.light1Dir.xyz); float spec1 = pow(max(dot(h1,N),0.0)*zeroL1, su.specularColorPower.w);
    float3 h2 = normalize(E - su.light2Dir.xyz); float spec2 = pow(max(dot(h2,N),0.0)*zeroL2, su.specularColorPower.w);
    o.specularRGB = saturate((spec0*su.light0Specular.xyz + spec1*su.light1Specular.xyz + spec2*su.light2Specular.xyz) * su.specularColorPower.xyz);
    o.fogFactor = 1.0 - clamp(dot(skinnedPos, su.fogVector), 0.0, 1.0);
    return o;
}
vertex VSkinnedVertexLitOut cna_v3d_skinned_vertexlit(VSkinnedIn in [[stage_in]], constant SkinnedTransform& t [[buffer(1)]], constant SkinnedUniforms& su [[buffer(2)]], constant float4x4* bones [[buffer(3)]]) {
    return cna_skin_vertexlit_common(in.position, in.normal, in.uv, in.boneWeights, in.boneIndices, float4(1.0), t, su, bones, cnaInstanced ? CNA_INSTANCE_MATRIX(in) : float4x4(1.0));
}
vertex VSkinnedVertexLitOut cna_v3d_skinned_color_vertexlit(VSkinnedColorIn in [[stage_in]], constant SkinnedTransform& t [[buffer(1)]], constant SkinnedUniforms& su [[buffer(2)]], constant float4x4* bones [[buffer(3)]]) {
    return cna_skin_vertexlit_common(in.position, in.normal, in.uv, in.boneWeights, in.boneIndices, in.color, t, su, bones, cnaInstanced ? CNA_INSTANCE_MATRIX(in) : float4x4(1.0));
}
fragment CnaFragOut cna_f3d_skinned_vertexlit(VSkinnedVertexLitOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]], constant SkinnedUniforms& su [[buffer(2)]], constant uint& cnaSampleMask [[buffer(28), function_constant(cnaSampleMaskOut)]]) {
    float4 texColor = tex.sample(smp, in.uv);
    float4 vc = (su.vertexColorEnabled.x > 0.5) ? in.color : float4(1.0);
    float4 c = float4(in.litRGB * texColor.rgb, su.diffuseColor.w * texColor.a * vc.a);
    c.rgb += in.specularRGB * c.a;
    c.rgb *= vc.rgb;
    if (cna_alpha_test_fails(c.a, su.alphaTest)) discard_fragment();
    c.rgb = mix(su.fogColorEnabled.xyz * c.a, c.rgb, in.fogFactor);   // AM4-137: XNA ApplyFog
    { CnaFragOut cnaOut; cnaOut.color = (c); if (cnaSampleMaskOut) cnaOut.mask = cnaSampleMask; return cnaOut; }
}

// CNAEXT PBR (plans/plan_metal.md METAL-81/83-86, plans/plan_cnj.md CNB-58), ported line-for-line from
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
    float4 srgbFlags;          // x=base decode, y=emissive decode, z=output encode, w=specular colour decode
    float4 specularFresnelInputs; // xyz=unclamped dielectric F0, w=KHR_materials_specular factor
    float4 textureTransformRows[10];
    float4 specularTransformRows[4];
    float4 textureCoordinateSets; // x=bit i selects TEXCOORD_1 for PBR texture slot i
};
// plans/plan_apple_m4.md AM4-084: `color` is glTF's COLOR_0 when the effect enables it and the record
// carries one, else constant opaque white (BuildMetalDeclaredVertexInput).
struct VPbrIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float4 tangent [[attribute(2)]]; float2 uv [[attribute(3)]]; float4 color [[attribute(4)]]; float2 uv1 [[attribute(5)]];  CNA_INSTANCE_INPUTS };
struct VPbrOut { float4 position [[position]]; float3 normal; float3 tangent; float bitangentSign; float2 uv; float fogFactor; float3 worldPos; float4 color; float2 uv1; };
float cna_direction_handedness(float3x3 m) {
    return dot(m[0], cross(m[1], m[2])) < 0.0 ? -1.0 : 1.0;
}
vertex VPbrOut cna_v3d_pbr(VPbrIn in [[stage_in]], constant PbrTransform& t [[buffer(1)]], constant PbrUniforms& pu [[buffer(2)]]) {
    VPbrOut o;
    float4 p = CNA_INSTANCE_POSITION(in, float4(in.position, 1.0));
    o.position = t.wvp * p;
    float3x3 normalMat = float3x3(t.normalCol0.xyz, t.normalCol1.xyz, t.normalCol2.xyz);
    o.normal = normalMat * CNA_INSTANCE_DIRECTION(in, in.normal);
    float3x3 world3 = float3x3(t.world[0].xyz, t.world[1].xyz, t.world[2].xyz);
    o.tangent = world3 * CNA_INSTANCE_DIRECTION(in, in.tangent.xyz);
    const float instanceHandedness = cnaInstanced
        ? cna_direction_handedness(float3x3(in.cnaInst0.xyz, in.cnaInst1.xyz, in.cnaInst2.xyz)) : 1.0;
    o.bitangentSign = in.tangent.w * cna_direction_handedness(world3) * instanceHandedness;
    o.uv = in.uv;
    o.uv1 = in.uv1;
    o.worldPos = (t.world * p).xyz;
    o.fogFactor = 1.0 - clamp(dot(p, pu.fogVector), 0.0, 1.0);
    o.color = in.color;
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
inline float2 cna_pbr_specular_transform_uv(float2 uv, int slot, constant PbrUniforms& pu) {
    float3 value = float3(uv, 1.0);
    return float2(dot(value, pu.specularTransformRows[slot * 2].xyz),
                  dot(value, pu.specularTransformRows[slot * 2 + 1].xyz));
}
// plans/plan_apple_m4.md AM4-085: GLTF-182/183's per-map coordinate set. Bit `slot` of the transported
// mask puts that map on TEXCOORD_1 (slots: base colour, normal, metallic-roughness, emissive,
// occlusion, specular, specular colour).
inline float2 cna_pbr_uv(float2 uv0, float2 uv1, int slot, constant PbrUniforms& pu) {
    uint mask = uint(pu.textureCoordinateSets.x + 0.5);
    return ((mask >> uint(slot)) & 1u) != 0u ? uv1 : uv0;
}
fragment CnaFragOut cna_f3d_pbr(VPbrOut in [[stage_in]],
    texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]],
    texture2d<float> normalMap [[texture(1)]], sampler normalSmp [[sampler(1)]],
    texture2d<float> mrMap [[texture(2)]], sampler mrSmp [[sampler(2)]],
    texture2d<float> emissiveMap [[texture(3)]], sampler emissiveSmp [[sampler(3)]],
    texture2d<float> occlusionMap [[texture(4)]], sampler occlusionSmp [[sampler(4)]],
    texture2d<float> specularMap [[texture(5)]], sampler specularSmp [[sampler(5)]],
    texture2d<float> specularColorMap [[texture(6)]], sampler specularColorSmp [[sampler(6)]],
    constant PbrUniforms& pu [[buffer(2)]], constant uint& cnaSampleMask [[buffer(28), function_constant(cnaSampleMaskOut)]])
{
    float4 baseColorTex = tex.sample(smp, cna_pbr_transform_uv(cna_pbr_uv(in.uv, in.uv1, 0, pu), 0, pu));
    float3 baseColor = mix(baseColorTex.rgb, cna_srgb_to_linear(baseColorTex.rgb), pu.srgbFlags.x);
    // glTF 2.0 3.9.2: COLOR_0 multiplies the base colour, alpha included.
    float4 cnaVertexColor = in.color;
    float3 albedo = baseColor * pu.diffuseColor.rgb * cnaVertexColor.rgb;
    float alpha = baseColorTex.a * pu.diffuseColor.a * cnaVertexColor.a;
    float3 N = normalize(in.normal);
    float3 T = normalize(in.tangent - N*dot(N, in.tangent));
    float3 B = cross(N, T) * in.bitangentSign;
    float3x3 TBN = float3x3(T, B, N);
    float3 sampledNormal = normalMap.sample(normalSmp, cna_pbr_transform_uv(cna_pbr_uv(in.uv, in.uv1, 1, pu), 1, pu)).rgb*2.0 - 1.0;
    sampledNormal.xy *= pu.pbrFactors.z;
    float3 finalNormal = normalize(TBN * sampledNormal);
    float4 mr = mrMap.sample(mrSmp, cna_pbr_transform_uv(cna_pbr_uv(in.uv, in.uv1, 2, pu), 2, pu));
    float roughness = clamp(mr.g * pu.pbrFactors.y, 0.045, 1.0);
    float metallic = clamp(mr.b * pu.pbrFactors.x, 0.0, 1.0);
    float3 V = normalize(pu.eyePosition.xyz - in.worldPos);
    // KHR_materials_specular: the strength map's alpha weights the dielectric lobe and the colour
    // map tints its F0, clamped per channel after the tint as the extension specifies.
    float specularWeight = pu.specularFresnelInputs.w * specularMap.sample(specularSmp, cna_pbr_specular_transform_uv(cna_pbr_uv(in.uv, in.uv1, 5, pu), 0, pu)).a;
    float3 specularColorTex = specularColorMap.sample(specularColorSmp, cna_pbr_specular_transform_uv(cna_pbr_uv(in.uv, in.uv1, 6, pu), 1, pu)).rgb;
    specularColorTex = mix(specularColorTex, cna_srgb_to_linear(specularColorTex), pu.srgbFlags.w);
    float3 dielectricF0 = min(pu.specularFresnelInputs.xyz * specularColorTex, float3(1.0)) * specularWeight;
    float3 F0 = mix(dielectricF0, albedo, metallic);
    float3 F90 = mix(float3(specularWeight), float3(1.0), metallic);
    float3 Lo = float3(0.0);
    Lo += cna_pbr_light(finalNormal, V, normalize(-pu.light0Dir.xyz), pu.light0Diffuse.xyz, albedo, F0, F90, roughness, metallic);
    Lo += cna_pbr_light(finalNormal, V, normalize(-pu.light1Dir.xyz), pu.light1Diffuse.xyz, albedo, F0, F90, roughness, metallic);
    Lo += cna_pbr_light(finalNormal, V, normalize(-pu.light2Dir.xyz), pu.light2Diffuse.xyz, albedo, F0, F90, roughness, metallic);
    float occlusionSample = occlusionMap.sample(occlusionSmp, cna_pbr_transform_uv(cna_pbr_uv(in.uv, in.uv1, 4, pu), 4, pu)).r;
    float occlusion = 1.0 + pu.pbrFactors.w * (occlusionSample - 1.0);
    float3 ambient = pu.ambientColor.xyz * albedo * occlusion;
    float3 emissiveSample = emissiveMap.sample(emissiveSmp, cna_pbr_transform_uv(cna_pbr_uv(in.uv, in.uv1, 3, pu), 3, pu)).rgb;
    emissiveSample = mix(emissiveSample, cna_srgb_to_linear(emissiveSample), pu.srgbFlags.y);
    float3 emissive = pu.emissiveColor.xyz * emissiveSample;
    float4 c = float4(ambient + Lo + emissive, alpha);
    if (cna_alpha_test_fails(c.a, pu.alphaTest)) discard_fragment();
    float3 fogLinear = mix(pu.fogColorEnabled.xyz,
                           cna_srgb_to_linear(pu.fogColorEnabled.xyz), pu.srgbFlags.z);
    c.rgb = mix(fogLinear, c.rgb, in.fogFactor);
    c.rgb = mix(c.rgb, cna_linear_to_srgb(c.rgb), pu.srgbFlags.z);
    { CnaFragOut cnaOut; cnaOut.color = (c); if (cnaSampleMaskOut) cnaOut.mask = cnaSampleMask; return cnaOut; }
}

// CNAEXT SkinnedPbrEffect (plans/plan_metal.md METAL-82, GLTF-264): GPU skinning plus the same PBR
// interpolants as cna_v3d_pbr. Normals use inverse-transpose joint and world matrices while
// tangents remain ordinary directions.
struct SkinnedPbrTransform { float4x4 wvp; float4x4 world; float4 normalCol0; float4 normalCol1; float4 normalCol2; float4 skinParams; }; // skinParams.x = weightsPerVertex
struct VSkinnedPbrIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float4 tangent [[attribute(2)]]; float2 uv [[attribute(3)]]; float4 boneWeights [[attribute(4)]]; uchar4 boneIndices [[attribute(5)]]; float4 color [[attribute(6)]]; float2 uv1 [[attribute(7)]];  CNA_INSTANCE_INPUTS };
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
    float4 skinnedPos = CNA_INSTANCE_POSITION(in, skinMat * float4(in.position, 1.0));
    o.position = t.wvp * skinnedPos;
    float3x3 skinMat3 = float3x3(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
    float3 skinnedNormal = cna_skin_normal(skinMat3, in.normal);
    float skinnedNormalLen = length(skinnedNormal);
    float3 boneNormal = (skinnedNormalLen > 1e-6) ? (skinnedNormal / skinnedNormalLen) : in.normal;
    float3x3 normalMat = float3x3(t.normalCol0.xyz, t.normalCol1.xyz, t.normalCol2.xyz);
    o.normal = normalize(normalMat * CNA_INSTANCE_DIRECTION(in, boneNormal));
    // Not renormalized here (matches the unskinned cna_v3d_pbr's own o.tangent = world3*tangent.xyz,
    // which is also left unnormalized) -- cna_f3d_pbr's Gram-Schmidt orthogonalization against the
    // interpolated normal already renormalizes it per-pixel regardless.
    float3x3 world3 = float3x3(t.world[0].xyz, t.world[1].xyz, t.world[2].xyz);
    o.tangent = world3 * CNA_INSTANCE_DIRECTION(in, skinMat3 * in.tangent.xyz);
    const float instanceHandedness = cnaInstanced
        ? cna_direction_handedness(float3x3(in.cnaInst0.xyz, in.cnaInst1.xyz, in.cnaInst2.xyz)) : 1.0;
    o.bitangentSign = in.tangent.w * cna_direction_handedness(world3) * instanceHandedness
                                   * cna_direction_handedness(skinMat3);
    o.uv = in.uv;
    o.uv1 = in.uv1;
    o.worldPos = (t.world * skinnedPos).xyz;
    o.fogFactor = 1.0 - clamp(dot(skinnedPos, pu.fogVector), 0.0, 1.0);
    o.color = in.color;
    return o;
}

struct V2In { float2 position; float2 uv; float4 color; };
// plans/plan_metal.md METAL-157/158: was `float2 viewport` (raw physical drawable pixels), completely
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
fragment CnaFragOut cna_f2d(V2Out in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]], constant uint& cnaSampleMask [[buffer(28), function_constant(cnaSampleMaskOut)]]) {
    { CnaFragOut cnaOut; cnaOut.color = (tex.sample(smp, in.uv) * in.color); if (cnaSampleMaskOut) cnaOut.mask = cnaSampleMask; return cnaOut; }
}
)MSL";

    // plans/plan_metal.md METAL-34-style extraction: this formula's real logic now lives in the plain-C++
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
            case PT::PointListEXT: return MTLPrimitiveTypePoint; // plans/plan_metal.md METAL-12: was falling to Triangle
            default: return MTLPrimitiveTypeTriangle;
        }
    }

    // plans/plan_metal.md METAL-19: the enum-reordering-sensitive XNA-ordinal logic now lives in the
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

    // plans/plan_metal.md METAL-19: same extraction as metalCompareFunction() above, now backed by the
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

    // plans/plan_metal.md METAL-19: same extraction as metalCompareFunction() above, now backed by the
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

    // plans/plan_metal.md METAL-19: same extraction as metalCompareFunction() above, now backed by the
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

    // plans/plan_metal.md: real bug found and fixed 2026-07-20 -- the previous implementation here used
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

    // plans/plan_metal.md METAL-34: the pipeline-cache key/hash types now live in the plain-C++
    // MetalPipelineKey.hpp (no Objective-C, buildable and unit-tested on any platform without an
    // Apple toolchain) -- these aliases let every existing call site in this file keep using the
    // short, unprefixed names unchanged.
    using PipelineKind = MetalPipelineKind;
    using BlendKey = MetalBlendKey;
    using PipelineCacheKey = MetalPipelineCacheKey;
    using PipelineCacheKeyHash = MetalPipelineCacheKeyHash;

    // plans/plan_metal.md METAL-27: translates one MetalVertexAttribKind (plain C++, see
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

    // plans/plan_metal.md METAL-26/27: builds a real MTLVertexDescriptor from an arbitrary
    // VertexElement list -- the generic counterpart to vertexDescriptorForStride()'s 8 hand-written
    // fixed layouts above. The actual attribute-shape decisions (which MetalVertexAttribKind, which
    // offset, which location) are made by the already-tested, plain-C++
    // BuildMetalVertexDescriptorPlan() (MetalVertexDescriptorPlan.hpp); this function only
    // mechanically walks that plan and calls the real Metal API. Not yet called from any live draw
    // path -- see MetalVertexBuffer::SetVertexDeclaration()'s own comment for why (no built-in
    // PipelineKind shader currently pairs with an arbitrary declaration; this becomes reachable
    // once Phase 14's custom ShaderEffect exists).
    // plans/plan_apple_m4.md AM4-035: the descriptor of every built-in 3D pipeline. It replaces the
    // fixed per-stride table; MetalCanonicalElementsFor reproduces that table for an undeclared
    // buffer (portable test CanonicalLayoutsMatchTheFixedStrideTable pins it).
    static MTLVertexDescriptor* vertexDescriptorFromInput(const MetalDeclaredVertexInput& input)
    {
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        if(!vd) throw std::runtime_error("Metal: failed to allocate vertex descriptor");
        for (const auto& attr : input.attributes) {
            vd.attributes[attr.location].format = metalVertexFormat(attr.kind);
            vd.attributes[attr.location].offset = (NSUInteger)attr.offset;
            vd.attributes[attr.location].bufferIndex =
                attr.constant ? (NSUInteger)kMetalConstantAttributeBufferIndex : (NSUInteger)attr.bufferIndex;
        }
        if (input.layouts.empty()) {
            vd.layouts[0].stride = (NSUInteger)input.stride;
        } else {
            // plans/plan_apple_m4.md AM4-143: one layout per stream that supplies an attribute; a
            // per-instance one steps once every InstanceFrequency instances, as D3D9's divisor does.
            for (const auto& layout : input.layouts) {
                vd.layouts[(NSUInteger)layout.bufferIndex].stride = (NSUInteger)layout.stride;
                if (layout.stepRate > 0) {
                    vd.layouts[(NSUInteger)layout.bufferIndex].stepFunction = MTLVertexStepFunctionPerInstance;
                    vd.layouts[(NSUInteger)layout.bufferIndex].stepRate = (NSUInteger)layout.stepRate;
                }
            }
        }
        // plans/plan_apple_m4.md AM4-080: inputs the effect permutation does not use, from a constant.
        if (input.UsesConstantAttributes()) {
            vd.layouts[kMetalConstantAttributeBufferIndex].stepFunction = MTLVertexStepFunctionConstant;
            vd.layouts[kMetalConstantAttributeBufferIndex].stepRate = 0;
            vd.layouts[kMetalConstantAttributeBufferIndex].stride = sizeof(kMetalConstantAttributeBlock);
        }
        return vd;
    }

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

    // plans/plan_metal.md METAL-6/24: real per-BlendState blend factors/operation, replacing the
    // previous hardcoded-into-every-pipeline straight-alpha blend. When !blend.enabled, blending
    // is left off entirely (matches BlendState.Opaque's real observable behavior).
    //
    // plans/plan_metal.md METAL-112/113: `colorCount` (default 1, every pre-MRT call site unaffected)
    // declares the SAME format/blend for `colorCount` simultaneous attachments, matching
    // VulkanRenderer's own identical "one VkPipelineColorBlendAttachmentState replicated
    // colorAttachmentCount times" precedent (confirmed by reading it directly) -- XNA's own
    // BlendState is a single global state, not per-render-target, so there is no per-attachment
    // blend to vary here even during real MRT. Every attachment always shares
    // MTLPixelFormatBGRA8Unorm (MetalRenderTargetRenderer's own hardcoded choice, see narrative item
    // 77), so only the loop bound needs to change, not a per-slot format lookup.
    // plans/plan_metal.md METAL-104: `sampleCount` (default 1, every pre-MSAA call site unaffected) must
    // match the active render pass's own sample count exactly, or pipeline creation is the same
    // class of genuine Metal API validation error `colorCount` above already documents for
    // attachment count -- an independent, orthogonal axis from MRT (a pipeline can be
    // multisampled with 1 color attachment, single-sampled with several, or both/neither).
    static id<MTLRenderPipelineState> makePipeline(id<MTLDevice> dev, id<MTLLibrary> lib,
                                                    NSString* vs, NSString* fs,
                                                    MTLVertexDescriptor* vd, const BlendKey& blend,
        int colorCount=1, int sampleCount=1, bool sampleMaskOutput=false,
        const std::array<MTLPixelFormat,8>* colorFormats=nullptr, bool instanceMatrix=false)
    {
        MTLRenderPipelineDescriptor* d=[[MTLRenderPipelineDescriptor alloc] init];
        if(!d) throw std::runtime_error("Metal: failed to allocate render-pipeline descriptor");
        // AM4-141: every stock fragment function is specialized on cnaSampleMaskOut; AM4-143: every
        // stock vertex function on cnaInstanced. A function ignores a constant it does not declare.
        MTLFunctionConstantValues* constants=[[MTLFunctionConstantValues alloc] init];
        const bool maskOutput=sampleMaskOutput;
        const bool instanced=instanceMatrix;
        [constants setConstantValue:&maskOutput type:MTLDataTypeBool atIndex:0];
        [constants setConstantValue:&instanced type:MTLDataTypeBool atIndex:1];
        NSError* functionError=nil;
        d.vertexFunction=[lib newFunctionWithName:vs constantValues:constants error:&functionError];
        d.fragmentFunction=[lib newFunctionWithName:fs constantValues:constants error:&functionError];
        [constants release];
        if(!d.vertexFunction||!d.fragmentFunction){
            [d.vertexFunction release]; [d.fragmentFunction release]; [d release];
            throw std::runtime_error("Metal: built-in shader function lookup failed");
        }
        d.vertexDescriptor=vd;
        d.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8; d.stencilAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8;
        d.rasterSampleCount=(NSUInteger)sampleCount; // sampleCount: deprecated since macOS 13 / iOS 16
        for (int i=0;i<colorCount;++i) {
            // AM4-142: each attachment's own format (BGRA8 for the backbuffer and Color targets).
            d.colorAttachments[i].pixelFormat=colorFormats ? (*colorFormats)[(std::size_t)i] : MTLPixelFormatBGRA8Unorm;
            // plans/plan_apple_m4.md AM4-097: the built-in functions return COLOR0 only, so attachments
            // 1..N-1 of an MRT set are masked out and keep their contents.
            if (i>0) { d.colorAttachments[i].writeMask=MTLColorWriteMaskNone; continue; }
            // plans/plan_apple_m4.md AM4-079: render target 0's ColorWriteChannels.
            d.colorAttachments[i].writeMask=(MTLColorWriteMask)MetalColorWriteMaskBits(blend.writeMask);
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
    struct UMaterialParams { float diffuseColor[4]; float alphaTest[4]; float flags[4]; float fogColor[4]; };

    // plans/plan_metal.md METAL-34-style extraction: this row-major 4x4 matrix helper set's real logic
    // now lives in the plain-C++ MetalMat4.hpp (no Objective-C, buildable and unit-tested on any
    // platform without an Apple toolchain) -- kept as thin same-name aliases/wrappers here so every
    // existing call site in this file is unaffected.
    using Mat4 = MetalMat4;
    static Mat4 multiply(const Mat4& a, const Mat4& b) { return MetalMat4Multiply(a, b); }
    static Mat4 fromXna(const Matrix& x) { return MetalMat4FromXna(x); }

    // plans/plan_metal.md METAL-34-style extraction: these struct mirrors and their fill functions now
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

    // plans/plan_metal.md METAL-34-style extraction: this formula's real logic now lives in the plain-C++
    // MetalNormalMatrix.hpp (no Objective-C, buildable and unit-tested on any platform without an
    // Apple toolchain) -- kept as a thin same-signature wrapper here so all 3 existing call sites
    // in this file are unaffected.
    static void computeNormalMatrixCols(const float* w, float col0[4], float col1[4], float col2[4])
    {
        ComputeMetalNormalMatrixCols(w, col0, col1, col2);
    }

// plans/plan_apple_m4.md AM4-142: a render target's native pixel format.
static MTLPixelFormat metalPixelFormat(MetalColorStorage storage)
{
    switch(storage)
    {
        case MetalColorStorage::Bgra8:       return MTLPixelFormatBGRA8Unorm;
        case MetalColorStorage::Rgb10A2:     return MTLPixelFormatRGB10A2Unorm;
        case MetalColorStorage::Rg16Unorm:   return MTLPixelFormatRG16Unorm;
        case MetalColorStorage::Rgba16Unorm: return MTLPixelFormatRGBA16Unorm;
        case MetalColorStorage::R32Float:    return MTLPixelFormatR32Float;
        case MetalColorStorage::Rg32Float:   return MTLPixelFormatRG32Float;
        case MetalColorStorage::Rgba32Float: return MTLPixelFormatRGBA32Float;
        case MetalColorStorage::R16Float:    return MTLPixelFormatR16Float;
        case MetalColorStorage::Rg16Float:   return MTLPixelFormatRG16Float;
        case MetalColorStorage::Rgba16Float: return MTLPixelFormatRGBA16Float;
    }
    return MTLPixelFormatBGRA8Unorm;
}

// AM4-142: a Texture2D's native pixel format.
static MTLPixelFormat metalTexturePixelFormat(MetalTextureStorage storage)
{
    switch(storage)
    {
        case MetalTextureStorage::Rgba8:       return MTLPixelFormatRGBA8Unorm;
        case MetalTextureStorage::A8:          return MTLPixelFormatA8Unorm;
        case MetalTextureStorage::Rg8Snorm:    return MTLPixelFormatRG8Snorm;
        case MetalTextureStorage::Rgba8Snorm:  return MTLPixelFormatRGBA8Snorm;
        case MetalTextureStorage::B5G6R5:      return MTLPixelFormatB5G6R5Unorm;
        case MetalTextureStorage::Bgr5A1:      return MTLPixelFormatBGR5A1Unorm;
        case MetalTextureStorage::Abgr4:       return MTLPixelFormatABGR4Unorm;
        case MetalTextureStorage::Rgb10A2:     return MTLPixelFormatRGB10A2Unorm;
        case MetalTextureStorage::Rg16Unorm:   return MTLPixelFormatRG16Unorm;
        case MetalTextureStorage::Rgba16Unorm: return MTLPixelFormatRGBA16Unorm;
        case MetalTextureStorage::R32Float:    return MTLPixelFormatR32Float;
        case MetalTextureStorage::Rg32Float:   return MTLPixelFormatRG32Float;
        case MetalTextureStorage::Rgba32Float: return MTLPixelFormatRGBA32Float;
        case MetalTextureStorage::R16Float:    return MTLPixelFormatR16Float;
        case MetalTextureStorage::Rg16Float:   return MTLPixelFormatRG16Float;
        case MetalTextureStorage::Rgba16Float: return MTLPixelFormatRGBA16Float;
    }
    return MTLPixelFormatRGBA8Unorm;
}

// AM4-142: XNA's texel bytes as the native format wants them (only Bgra4444 differs).
static std::vector<uint8_t> metalTextureUploadBytes(const MetalTextureStorageInfo& storage,const uint8_t* bytes,std::size_t length)
{
    std::vector<uint8_t> out(bytes,bytes+length);
    if(storage.rotate4444)
        for(std::size_t i=0;i+1<out.size();i+=2){
            const uint16_t xna=(uint16_t)(out[i]|(out[i+1]<<8));
            const uint16_t metal=MetalBgra4444ToAbgr4(xna);
            out[i]=(uint8_t)(metal&0xFF); out[i+1]=(uint8_t)(metal>>8);
        }
    return out;
}

// AM4-142: the view a shader samples a one- or two-channel target through. Direct3D 9 (XNA)
// returns 1 for the channels such a format does not store -- (r,1,1,1) and (r,g,1,1) -- where
// Metal returns 0 for colour; a +1 retained view, or nil when the texture samples as it is.
static id<MTLTexture> makeXnaSamplingView(id<MTLTexture> texture,int channels)
{
    if(!texture||channels>=4) return nil;
    const MTLTextureSwizzleChannels swizzle=MTLTextureSwizzleChannelsMake(
        MTLTextureSwizzleRed,
        channels>=2 ? MTLTextureSwizzleGreen : MTLTextureSwizzleOne,
        MTLTextureSwizzleOne,MTLTextureSwizzleOne);
    return [texture newTextureViewWithPixelFormat:texture.pixelFormat textureType:texture.textureType
        levels:NSMakeRange(0,texture.mipmapLevelCount)
        slices:NSMakeRange(0,texture.textureType==MTLTextureTypeCube ? 6 : texture.arrayLength)
        swizzle:swizzle];
}

    class MetalTexture final : public ITextureRenderer
    {
    public:
        MetalTexture(id<MTLDevice> dev, id<MTLCommandQueue> queue, const ImageData& data,
                     std::shared_ptr<MetalResourceHealth> resourceHealth,
                     std::function<void()> ownerHealthCheck,
                     MetalTextureStorageInfo storage=MetalTextureStorageInfo{})
            : w_(data.width), h_(data.height), surfaceFormat_(data.surfaceFormat), storage_(storage), resourceHealth_(std::move(resourceHealth)),
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
            MTLTextureDescriptor* d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:metalTexturePixelFormat(storage_.storage)
                width:w_ height:h_ mipmapped:(data.mipLevels > 1)];
            if(!d) throw std::runtime_error("Metal: failed to allocate texture descriptor");
            d.usage=MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
            textureOwner.Adopt([dev newTextureWithDescriptor:d]);
            if (!textureOwner.HasValue()) throw std::runtime_error("Metal: failed to create texture");
            if (!data.pixels.empty()) {
                MTLRegion r=MTLRegionMake2D(0,0,w_,h_);
                const std::vector<uint8_t> bytes=metalTextureUploadBytes(storage_,data.pixels.data(),data.pixels.size());
                [(id<MTLTexture>)textureOwner.Get() replaceRegion:r mipmapLevel:0
                    withBytes:bytes.data() bytesPerRow:(NSUInteger)w_*(NSUInteger)storage_.bytesPerTexel];
            }
            dev_=(id<MTLDevice>)deviceOwner.ReleaseOwnership();
            queue_=(id<MTLCommandQueue>)queueOwner.ReleaseOwnership();
            texture_=(id<MTLTexture>)textureOwner.ReleaseOwnership();
        }
        ~MetalTexture() override { [samplingView_ release]; [texture_ release]; [queue_ release]; [dev_ release]; }
        // AM4-142: the shared draw validation reads the format (XNA's point-filter-only rule).
        int GetSurfaceFormatEXT() const noexcept override { return surfaceFormat_; }
        int GetWidth() const override { return w_; }
        int GetHeight() const override { return h_; }

        // plans/plan_metal.md METAL-256: real bug found (Phase 18's own resource-lifetime audit, METAL-175)
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
            MTLTextureDescriptor* d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:metalTexturePixelFormat(storage_.storage)
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
                    throw std::runtime_error("Metal: mip-preservation blit failed: "+describeMetalCommandBufferError(cmd.error));
                ownerHealthCheck_();
            }
            MTLRegion r=MTLRegionMake2D(0,0,(NSUInteger)levelW,(NSUInteger)levelH);
            const std::vector<uint8_t> bytes=metalTextureUploadBytes(
                storage_,rgba,(std::size_t)bytesPerRow*(std::size_t)levelH);
            [newTex replaceRegion:r mipmapLevel:(NSUInteger)targetLevel withBytes:bytes.data() bytesPerRow:(NSUInteger)bytesPerRow];
            [texture_ release];
            texture_=(id<MTLTexture>)newTextureOwner.ReleaseOwnership();
            [samplingView_ release]; samplingView_=nil;   // AM4-142: it named the old texture
        }
        void UpdatePixels(const uint8_t* rgba, int stride) override {
            const MetalAutoreleaseScope autoreleaseScope;
            ownerHealthCheck_();
            MetalTextureTransferLayout layout{};
            const int bytesPerTexel=storage_.bytesPerTexel;   // AM4-142
            if(!rgba||w_>std::numeric_limits<int>::max()/bytesPerTexel||
               !TryBuildMetalTextureTransferLayout(w_,h_,1,1,layout,(std::size_t)bytesPerTexel)||
               stride!=static_cast<int>(static_cast<std::size_t>(w_)*(std::size_t)bytesPerTexel))
                throw std::invalid_argument("Metal: invalid Texture2D level-zero upload");
            reallocateAndUpdate(0, rgba, stride, w_, h_);
            ownerHealthCheck_();
        }
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int lw, int lh) override {
            const MetalAutoreleaseScope autoreleaseScope;
            ownerHealthCheck_();
            if(!rgba||level<0||level>=(int)texture_.mipmapLevelCount||
               lw!=MetalTextureTransferDetail::MipDimension(w_,level)||
               lh!=MetalTextureTransferDetail::MipDimension(h_,level)||
               static_cast<std::size_t>(lw)>static_cast<std::size_t>(std::numeric_limits<int>::max()/storage_.bytesPerTexel))
                throw std::invalid_argument("Metal: invalid Texture2D mip upload");
            reallocateAndUpdate(level, rgba, lw*storage_.bytesPerTexel, lw, lh);
            ownerHealthCheck_();
        }
        bool GetData(int level,int x,int y,int w,int h,void* data,int dataLength) const override
        {
            const MetalAutoreleaseScope autoreleaseScope;
            ownerHealthCheck_();
            (void)level; (void)x; (void)y; (void)w; (void)h; (void)data; (void)dataLength;
            return false;
        }
        id<MTLTexture> native() const
        {
            ownerHealthCheck_();
            return texture_;
        }
        /// AM4-142: what a shader samples (see makeXnaSamplingView).
        id<MTLTexture> samplingTexture() const
        {
            ownerHealthCheck_();
            if (storage_.channels>=4) return texture_;
            if (!samplingView_) samplingView_=makeXnaSamplingView(texture_,storage_.channels);
            return samplingView_ ? samplingView_ : texture_;
        }
    private:
        id<MTLDevice> dev_=nil; id<MTLCommandQueue> queue_=nil;
        int w_, h_; int surfaceFormat_=0; MetalTextureStorageInfo storage_; id<MTLTexture> texture_ = nil;
        mutable id<MTLTexture> samplingView_=nil;   // AM4-142
        std::shared_ptr<MetalResourceHealth> resourceHealth_;
        std::function<void()> ownerHealthCheck_;
    };

    // The factory validates SurfaceFormat::Color before construction. Metal allocates its RGBA8
    // cube mip chain as one texture object, so each requested level exists before SetData.
    class MetalTextureCube final : public ITextureCubeRenderer
    {
    public:
        // plans/plan_metal.md METAL-122: `queue` is stored (retained) purely so GetData() below can blit
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
            const MetalAutoreleaseScope autoreleaseScope;
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
            const MetalAutoreleaseScope autoreleaseScope;
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
            if(command.status!=MTLCommandBufferStatusCompleted) throw std::runtime_error("Metal: cube preservation blit failed: "+describeMetalCommandBufferError(command.error));
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
        // plans/plan_metal.md METAL-122 (3D analog): same finding as MetalTextureCube above --
        // Texture3D.cpp's real code always calls renderer_->GetData() unconditionally, no CPU-side
        // shadow shortcut, so this was a real, previously-shipping gap for any Texture3D, not just
        // a render-target-only concern.
        MetalTexture3D(id<MTLDevice> dev, id<MTLCommandQueue> queue, int w,int h,int depth,bool mipMap,
                       std::shared_ptr<MetalResourceHealth> resourceHealth,
                       std::function<void()> ownerHealthCheck)
            : w_(w), h_(h), depth_(depth),
              levelCount_(MetalVolumeMipLevelCount(w,h,depth,mipMap)),
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
            const MetalAutoreleaseScope autoreleaseScope;
            ownerHealthCheck_();
            MetalTextureTransferLayout layout{};
            if(!TryPrepareMetalTextureTransfer(
                    w_,h_,depth_,levelCount_,level,x,y,z,w,h,depth,data,dataLength,
                    MetalTransferLengthRule::AtLeastTightBytes,1,layout)) return false;
            reallocateAndUpdate(level,x,y,z,w,h,depth,data,layout);
            ownerHealthCheck_();
            return true;
        }
        // plans/plan_metal.md METAL-125: real per-level readback via the shared blit helper -- slice is
        // always 0 for a 3D texture (there is no per-slice concept the way a cube's 6 faces have;
        // `z`/`depth` address the volume directly within slice 0).
        bool GetData(int level,int x,int y,int z,int w,int h,int depth,void* data,int dataLength) const override
        {
            const MetalAutoreleaseScope autoreleaseScope;
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
            if(command.status!=MTLCommandBufferStatusCompleted) throw std::runtime_error("Metal: 3D preservation blit failed: "+describeMetalCommandBufferError(command.error));
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
            const MetalAutoreleaseScope autoreleaseScope;
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
        // plans/plan_metal.md METAL-26: generic-layout hook (IVertexBufferRenderer's own doc comment,
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

// plans/plan_metal.md Phase 10: forward-declared so Impl can hold a non-owning pointer to whichever
// MetalRenderTargetRenderer is currently bound (ownership lives in the RenderTarget2D C++ object
// via its own unique_ptr<IRenderTargetRenderer>, matching every other renderer's convention) --
// MetalRenderTargetRenderer itself is defined later, after Impl, since it needs Impl to be a
// complete type. Impl::resolveActiveAttachments() (used by ensureFrame()/clear()) is declared
// inside Impl below but its body is defined out-of-line, after MetalRenderTargetRenderer, for the
// same reason -- C++ allows this: a member function's body has complete-class access to sibling
// members regardless of textual declaration/definition order.
class MetalRenderTargetRenderer;
// plans/plan_metal.md METAL-109: same forward-declaration reasoning as MetalRenderTargetRenderer above,
// for the cube-map analog -- Impl needs a non-owning pointer to whichever RenderTargetCube face is
// currently bound.
class MetalRenderTargetCubeRenderer;
class MetalOcclusionQueryRenderer;

// plans/plan_metal.md Phase 10: a previously-rendered-to RenderTarget2D used as an ordinary texture in a
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

// plans/plan_metal.md METAL-109/METAL-64ff: same reasoning as nativeTextureFor() above, for the cube
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

// plans/plan_metal.md METAL-104/105: real MSAA. `texture2DDescriptorWithPixelFormat:...mipmapped:`
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
    // plans/plan_metal.md METAL-33: no eviction by design, not an oversight -- see
    // MetalPipelineKey.hpp's own MetalPipelineCacheKey comment for the real bounded-size reasoning.
    std::unordered_map<PipelineCacheKey, id<MTLRenderPipelineState>, PipelineCacheKeyHash> pipelineCache;
    id<MTLDepthStencilState> depthState=nil;
    id<MTLSamplerState> sampler=nil;
    // (filter, addressU, addressV, anisotropy, maxMipLevel, lodBias bits)
    std::map<std::tuple<int,int,int,int,int,std::uint32_t,int>, id<MTLSamplerState>> samplerCache;
    id<MTLSamplerState> samplerSlots[16]={};
    // plans/plan_apple_m4.md AM4-034: what each slot was last asked for, so ApplySamplerState and
    // ApplySamplerMipState -- two calls for one XNA SamplerState -- each rebuild the whole sampler.
    struct SamplerSlotRequest { int filter=0, addressU=0, addressV=0, maxAnisotropy=4, maxMipLevel=0; float lodBias=0.0f; };
    SamplerSlotRequest samplerSlotRequests[16]={};
    id<MTLCommandBuffer> command=nil;
    id<MTLRenderCommandEncoder> encoder=nil;
    MetalRetainedResource<id<CAMetalDrawable>> drawable{retainMetalDrawable, releaseMetalDrawable};
    std::shared_ptr<MetalCommandFailureLatch> commandFailureLatch=
        std::make_shared<MetalCommandFailureLatch>();
    std::shared_ptr<MetalResourceHealth> resourceHealth=
        std::make_shared<MetalResourceHealth>(commandFailureLatch);
    MetalFrameAvailabilityState frameAvailability{};
    id<MTLTexture> depthTexture=nil;
    // plans/plan_metal.md METAL-104/105: real backbuffer MSAA. `deviceSampleCount` is the real,
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
    // AM4-141: MetalSampleCountBit of every count supportsTextureSampleCount: accepts.
    unsigned supportedSampleCountMask=0;
    // AM4-142: B5G6R5/BGR5A1/ABGR4 textures exist on Apple GPUs only.
    bool packed16Formats=false;
    // AM4-142: the colour formats of the open pass's attachments, as the pipelines must declare them.
    std::array<MTLPixelFormat,8> activeColorFormats=[]{ std::array<MTLPixelFormat,8> f{}; f.fill(MTLPixelFormatBGRA8Unorm); return f; }();
    void recordActiveColorFormats(const std::vector<id<MTLTexture>>& colors)
    {
        activeColorFormats.fill(MTLPixelFormatBGRA8Unorm);
        for(std::size_t i=0;i<colors.size()&&i<activeColorFormats.size();++i)
            if(colors[i]) activeColorFormats[i]=colors[i].pixelFormat;
    }
    // AM4-141: BlendState.MultiSampleMask, judged per draw against activeSampleCount.
    unsigned sampleMask=0xFFFFFFFFu;
    /// AM4-141: the current draw writes [[sample_mask]] (set by admitSampleMask, read by
    /// getOrCreatePipeline and bindSampleMask).
    bool sampleMaskOutput=false;
    /// AM4-141: RasterizerState.MultiSampleAntiAlias, and whether the open pass was built with
    /// every sample at the pixel centre (what "off" means on a multisampled target).
    bool multisampleRasterization=true;
    bool encoderCentreSamples=false;

    /// AM4-141: sample positions for a pass being built -- all at the pixel centre while
    /// multisample rasterization is off on a multisampled pass.
    void applyRasterizationSamplePositions(MTLRenderPassDescriptor* rp,int sampleCount)
    {
        encoderCentreSamples=false;
        if(sampleCount<=1||multisampleRasterization||!device.programmableSamplePositionsSupported) return;
        std::array<MTLSamplePosition,8> centre{};
        for(auto& position:centre) position=MTLSamplePositionMake(0.5f,0.5f);
        [rp setSamplePositions:centre.data() count:(NSUInteger)sampleCount];
        encoderCentreSamples=true;
    }

    /// AM4-141: false when the draw keeps no sample of a multisampled target (it writes nothing);
    /// a partial mask selects the pipeline variant whose fragment writes it. Call after ensureFrame().
    bool admitSampleMask()
    {
        sampleMaskOutput=false;
        switch(DescribeMetalSampleMaskAdmission(sampleMask,activeSampleCount))
        {
            case MetalSampleMaskAdmission::Draw: return true;
            case MetalSampleMaskAdmission::Skip: return false;
            case MetalSampleMaskAdmission::Masked: sampleMaskOutput=true; return true;
        }
        return true;
    }

    /// AM4-141: binds the mask the masked pipeline variant reads; call after setRenderPipelineState.
    void bindSampleMask()
    {
        if(!sampleMaskOutput) return;
        const uint32_t bits=sampleMask;
        [encoder setFragmentBytes:&bits length:sizeof(bits) atIndex:28];
    }
    int activeSampleCount=1;
    int virtualW=0,virtualH=0,presentationMode=0,swapInterval=1;
    bool depthEnabled=true,depthWrite=true;
    int refStencil=0;
    MetalRasterState rasterState{};
    MTLCullMode cull=MTLCullModeNone;
    MTLTriangleFillMode fill=MTLTriangleFillModeFill;
    float depthBias=0,slopeBias=0;
    BlendKey currentBlend; // real per-BlendState pipeline selection key, see ApplyBlendState() below
    // plans/plan_metal.md METAL-7/9/10: real DepthStencilState fields, defaults matching
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

    // plans/plan_metal.md METAL-136/137: real occlusion queries via MTLVisibilityResultBuffer. This
    // buffer must be attached to the MTLRenderPassDescriptor at render-pass-creation time (Metal
    // has no way to attach it mid-encoder the way setVisibilityResultMode:offset: can be called
    // mid-encoder) -- so it is allocated once here and referenced by every render pass
    // ensureFrame()/clear() create, with each MetalOcclusionQueryRenderer instance owning one
    // 8-byte slot (a uint64_t sample-passed count written by the GPU) via a simple incrementing
    // counter. kMaxOcclusionQuerySlots is a practical, generous cap, not an API-imposed one.
    static constexpr int kMaxOcclusionQuerySlots = 1024;
    id<MTLBuffer> visibilityBuffer=nil;
    int nextQuerySlot=0;
    // plans/plan_apple_m4.md AM4-038 (closing METAL-266): slots are recycled, and the query between
    // Begin and End -- Metal counts into one offset per encoder -- is re-armed on every encoder that
    // starts while it is open (a Clear, a render-target switch or a readback all start one), each
    // encoder counting into a slot of its own that PixelCount sums.
    //
    // plans/plan_apple_m4.md AM4-108: Metal writes each offset's count when its render pass ends, and
    // draws of ONE pass that count into the same offset add up. A slot released while the encoder
    // that used it is still open (XNA lets a query be re-begun once IsComplete was asked, done or
    // not) must therefore not be handed out again in that same encoder, or the new run would read
    // the old run's samples too. Each released slot remembers the encoder generation it was
    // released in, and is reused only from a later encoder; across passes Metal overwrites.
    std::vector<std::pair<int,std::uint64_t>> freeQuerySlots;
    std::uint64_t encoderGeneration=0;
    MetalOcclusionQueryRenderer* activeQuery=nullptr;
    int allocateQuerySlot()
    {
        int slot=-1;
        for (auto it=freeQuerySlots.begin(); it!=freeQuerySlots.end(); ++it) {
            if (it->second<encoderGeneration) { slot=it->first; freeQuerySlots.erase(it); break; }
        }
        if(slot<0) {
            if(nextQuerySlot<kMaxOcclusionQuerySlots) slot=nextQuerySlot++;
            else throw std::runtime_error("Metal: every occlusion-query visibility slot is in use");
        }
        static_cast<std::uint64_t*>([visibilityBuffer contents])[slot]=0;
        return slot;
    }
    void releaseQuerySlot(int slot) { freeQuerySlots.emplace_back(slot,encoderGeneration); }
    // Defined after MetalOcclusionQueryRenderer, which it needs complete.
    void rearmActiveOcclusionQuery();

    // plans/plan_metal.md Phase 10 (METAL-98/107): non-owning pointer to whichever RenderTarget2D is
    // currently bound; nullptr means "drawing to the backbuffer" (the default). Ownership lives in
    // the RenderTarget2D C++ object's own unique_ptr<IRenderTargetRenderer>, matching every other
    // renderer's convention -- MetalRenderTargetRenderer's own destructor clears this pointer if it
    // is destroyed while still bound, so it never dangles.
    MetalRenderTargetRenderer* currentRenderTarget=nullptr;

    // plans/plan_metal.md METAL-109/110: same non-owning-pointer convention as currentRenderTarget above,
    // for whichever face of whichever RenderTargetCube is currently bound (mutually exclusive with
    // currentRenderTarget -- SetRenderTarget2D()/SetRenderTargetCubeFace() each cross-unbind the
    // other before binding, matching EasyGLRenderer::SetRenderTarget2D/SetRenderTargetCubeFace's
    // own established real contract exactly, not an invented convention).
    MetalRenderTargetCubeRenderer* currentRenderTargetCube=nullptr;
    NSUInteger currentRenderTargetCubeFace=0;

    // plans/plan_metal.md METAL-112/113: real MRT. `currentMRT.size()>=2` means true simultaneous
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
    // plans/plan_apple_m4.md AM4-097: a member is a RenderTarget2D or one face of a RenderTargetCube,
    // the two kinds XNA's RenderTargetBinding names; slot 0's kind is the one mirrored above.
    struct MrtMember {
        MetalRenderTargetRenderer* target=nullptr;
        MetalRenderTargetCubeRenderer* cube=nullptr;
        NSUInteger face=0;
    };
    std::vector<MrtMember> currentMRT;
    int activeColorAttachmentCount=1;
    void unbindCurrentMRT();
    // AM4-097: a destroyed member leaves the set; the live peers stay, to be finalized at unbind.
    void detachMrtMember(const void* resource);

    // plans/plan_metal.md METAL-87: fallback textures for PbrEffect's 4 optional maps (normalMap/
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
    // AM4-140: XNA's opaque black for an unbound classic stock texture (GSC-0004).
    id<MTLTexture> defaultBlackTexture=nil;
    id<MTLTexture> defaultBlackCubeTexture=nil;
#if defined(CNA_METAL_COMPILED_EFFECTS)
    // plans/plan_apple_m4.md AM4-144: compiled XNA effects -- the renderer-wide MojoShader context,
    // the MSL functions by linked-SPIR-V hash, the pipelines by pass/layout/state, and the opaque
    // black volume an unbound 3D sampler reads.
    std::unique_ptr<MetalMojoShaderContextEXT> mojoShaderContext;
    std::unordered_map<std::uint64_t, id<MTLFunction>> compiledFunctions;
    std::unordered_map<MetalPipelineCacheKey, id<MTLRenderPipelineState>, MetalPipelineCacheKeyHash> compiledPipelines;
    id<MTLTexture> defaultBlackVolumeTexture=nil;
#endif

    // Re-applies every piece of encoder-scoped dynamic state this renderer tracks. Metal has no
    // persistent-across-encoders state at all (unlike, say, retained GL context state) -- a fresh
    // MTLRenderCommandEncoder starts with undefined cull/fill/bias/stencil-ref/blend-color, so
    // ensureFrame()/clear() must both call this every time they create one. Previously ensureFrame()
    // inlined a partial version of this (missing stencil reference and blend color entirely) and
    // clear() didn't reapply cull/fill/depthBias/stencil-reference at all -- a real, pre-existing
    // inconsistency between the two encoder-creation paths, fixed here by sharing one function.
    void applyTrackedEncoderState()
    {
        // plans/plan_metal.md METAL-5: explicit, not relied-on-by-accident. XNA CullMode's
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
        bool targetHasDepth=true, targetHasStencil=true;
        activeTargetDepthPlanes(targetHasDepth,targetHasStencil);
        if(targetHasDepth!=depthStateTargetHasDepth||targetHasStencil!=depthStateTargetHasStencil)
            rebuildDepthState();
        [encoder setDepthBias:depthBias slopeScale:slopeBias clamp:0]; [encoder setDepthStencilState:depthState];
        [encoder setStencilReferenceValue:(uint32_t)refStencil];
        [encoder setBlendColorRed:blendColor[0] green:blendColor[1] blue:blendColor[2] alpha:blendColor[3]];
        rearmActiveOcclusionQuery();
    }

    // plans/plan_metal.md Phase 10 (METAL-98/100/107): resolves the color+depth textures for whatever's
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
    // plans/plan_metal.md METAL-109/110: sliceOut is always 0 except when a RenderTargetCube face is the
    // active target (MTLRenderPassColorAttachmentDescriptor.slice selects which cube face/array
    // layer of colorOut a render pass actually writes to; 0 is simply ignored/inert for a plain 2D
    // texture, so callers can set it unconditionally with no branching of their own).
    // plans/plan_metal.md METAL-104: `resolveOut` (nil unless MSAA is engaged for whatever's currently
    // active) and `sampleCountOut` (always 1 unless `resolveOut` is non-nil) were added alongside
    // the pre-existing 3 out-params for real backbuffer/RenderTarget2D MSAA -- a non-nil
    // `resolveOut` means `colorOut` is a multisampled texture that must be resolved into
    // `resolveOut` at render-pass-end (`MTLStoreActionStoreAndMultisampleResolve`, not plain
    // `MTLStoreActionMultisampleResolve` -- see msaaColorTexture's own field comment for why).
    // RenderTargetCube deliberately stays out of MSAA scope for this pass (matches METAL-112's own
    // MRT+MSAA scope decision below) -- its own branch below never sets resolveOut.
    bool resolveActiveAttachments(id<MTLTexture>& colorOut, id<MTLTexture>& resolveOut, id<MTLTexture>& depthOut, NSUInteger& sliceOut, int& sampleCountOut);
    // plans/plan_apple_m4.md AM4-032: which planes the bound target's DepthFormat has (the
    // backbuffer: both). Defined after the render-target classes, like resolveActiveAttachments.
    void activeTargetDepthPlanes(bool& hasDepth, bool& hasStencil) const;
    // The planes depthState was built for; a target switch to different planes rebuilds it.
    bool depthStateTargetHasDepth=true;
    bool depthStateTargetHasStencil=true;

    // plans/plan_metal.md METAL-112: the MRT-aware sibling of resolveActiveAttachments() above, used only
    // by ensureFrame()/clear() (every other caller -- computeSpriteTransform(), etc. -- only ever
    // needs target 0, already available via currentRenderTarget directly). Declared here, defined
    // out-of-line after MetalRenderTargetRenderer for the same incomplete-type reason as
    // resolveActiveAttachments()/computeSpriteTransform() above -- its body calls
    // currentMRT[i]->colorTexture()/depthTextureNative(), which need the complete type.
    //
    // plans/plan_metal.md METAL-104: true MRT (currentMRT.size()>=2) and MSAA are deliberately never
    // combined in this pass -- every MRT draw always runs at sample count 1 regardless of
    // deviceSampleCount, matching real-world XNA usage (MSAA is a single-target scene-
    // anti-aliasing feature; deferred/G-buffer-style MRT rendering practically never also wants
    // MSAA on the G-buffer itself). `resolvesOut` is parallel to `colorsOut`, always all-nil
    // during real MRT.
    bool resolveActiveColorAttachments(std::vector<id<MTLTexture>>& colorsOut, std::vector<id<MTLTexture>>& resolvesOut, std::vector<NSUInteger>& slicesOut, id<MTLTexture>& depthOut, int& sampleCountOut);

    // Ends whatever encoder/command-buffer is currently active -- shared by endFrame() and
    // MetalRenderTargetRenderer's own Bind/UnbindAsRenderTarget() (Metal render passes are
    // fixed-attachment for their whole encoder lifetime, unlike GL's dynamic FBO rebinding, so
    // switching what's being rendered to always means ending the current pass first).
    //
    // plans/plan_metal.md METAL-180 (Phase 18 audit): `presentBackbuffer` MUST be false for every call
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
                    failureLatch->RecordFailure(describeMetalCommandBufferError(completed.error));
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
        // AM4-138: the native error is part of the report, not just the fact that one occurred.
        const std::string detail=commandFailureLatch->TakeFailureDetail();
        const std::string message=detail.empty()
            ? std::string("Metal: a previously submitted command buffer failed")
            : "Metal: a previously submitted command buffer failed: "+detail;
        abandonCommandStateAndThrow(message.c_str());
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
        const std::string exactDetail=completed?std::string():describeMetalCommandBufferError(submitted.error);
        [submitted release];
        switch(DescribeMetalSynchronousCommandResult(
            completed,commandFailureLatch->HasFailure()))
        {
            case MetalSynchronousCommandResult::ExactSubmissionFailed:
            {
                const std::string message=std::string(failureMessage)+": "+exactDetail;   // AM4-138
                abandonCommandStateAndThrow(message.c_str());
            }
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
        const MetalAutoreleaseScope autoreleaseScope;
        throwPendingCommandFailure();
        if (encoder) return true;
        try {
            std::vector<id<MTLTexture>> colors, resolves; std::vector<NSUInteger> slices; id<MTLTexture> depthTex=nil; int sampleCount=1;
            if(DescribeMetalFrameEntryPolicy(resolveActiveColorAttachments(
                   colors,resolves,slices,depthTex,sampleCount))==
               MetalFrameEntryPolicy::SkipUnavailableBackbuffer)
                return false;
            command=[queue commandBuffer];
            if(!command) throw std::runtime_error("Metal: failed to create render command buffer");
            [command retain];
            MTLRenderPassDescriptor* rp=[MTLRenderPassDescriptor renderPassDescriptor];
            if(!rp) throw std::runtime_error("Metal: failed to allocate render-pass descriptor");
            // plans/plan_metal.md METAL-112: loop bound is 1 outside MRT (colors.size()==1, identical to the
            // original single-attachment code this replaced), up to Metal's own 8-attachment hardware
            // limit during real SetRenderTargets() MRT.
            //
            // plans/plan_metal.md METAL-104: a non-nil resolves[i] means colors[i] is this frame's
            // multisampled render target -- StoreAndMultisampleResolve both keeps colors[i]'s own
            // content (needed across this file's own mid-frame encoder boundaries, see
            // msaaColorTexture's field comment) and resolves it into resolves[i] (the real drawable/
            // RT's single-sample texture) every time this encoder ends.
            for (NSUInteger i=0;i<colors.size();++i) {
                rp.colorAttachments[i].texture=colors[i];
                rp.colorAttachments[i].loadAction=MTLLoadActionLoad;
                // AM4-141: a resolved attachment renders into a 2D multisample texture and resolves
                // into slices[i] -- a cube target's face; a plain one renders into slices[i] itself.
                if (resolves[i]) { rp.colorAttachments[i].resolveTexture=resolves[i]; rp.colorAttachments[i].resolveSlice=slices[i]; rp.colorAttachments[i].storeAction=MTLStoreActionStoreAndMultisampleResolve; }
                else { rp.colorAttachments[i].slice=slices[i]; rp.colorAttachments[i].storeAction=MTLStoreActionStore; }
            }
            rp.depthAttachment.texture=depthTex; rp.depthAttachment.loadAction=MTLLoadActionLoad; rp.depthAttachment.storeAction=MTLStoreActionStore;
            rp.stencilAttachment.texture=depthTex; rp.stencilAttachment.loadAction=MTLLoadActionLoad; rp.stencilAttachment.storeAction=MTLStoreActionStore;
            rp.visibilityResultBuffer=visibilityBuffer;
            applyRasterizationSamplePositions(rp,sampleCount);   // AM4-141
            recordActiveColorFormats(colors);                     // AM4-142
            encoder=[command renderCommandEncoderWithDescriptor:rp];
            if(!encoder) throw std::runtime_error("Metal: failed to create render command encoder");
            [encoder retain];
            ++encoderGeneration;   // AM4-108
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
        const MetalAutoreleaseScope autoreleaseScope;
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
        // Every Clear* entry point forwards here, so the scope lives here (AM4-102).
        const MetalAutoreleaseScope autoreleaseScope;
        throwPendingCommandFailure();
        endActiveEncoding(false); // mid-frame boundary only -- see endActiveEncoding()'s own METAL-180 note.
        try {
            std::vector<id<MTLTexture>> colors, resolves; std::vector<NSUInteger> slices; id<MTLTexture> depthTex=nil; int sampleCount=1;
            if (!resolveActiveColorAttachments(colors, resolves, slices, depthTex, sampleCount)) return;
            command=[queue commandBuffer];
            if(!command) throw std::runtime_error("Metal: failed to create clear command buffer");
            [command retain];
            MTLRenderPassDescriptor* rp=[MTLRenderPassDescriptor renderPassDescriptor];
            if(!rp) throw std::runtime_error("Metal: failed to allocate clear render-pass descriptor");
            // plans/plan_metal.md METAL-112: Clear()'s own real XNA contract clears every currently-bound
            // render target to the same color -- matches GraphicsDevice.Clear(color)'s documented
            // behavior regardless of how many targets SetRenderTargets() bound.
            //
            // plans/plan_metal.md METAL-104: same StoreAndMultisampleResolve reasoning as ensureFrame() above.
            for (NSUInteger i=0;i<colors.size();++i) {
                rp.colorAttachments[i].texture=colors[i]; rp.colorAttachments[i].loadAction=color?MTLLoadActionClear:MTLLoadActionLoad; rp.colorAttachments[i].clearColor=MTLClearColorMake(r,g,b,a);
                if (resolves[i]) { rp.colorAttachments[i].resolveTexture=resolves[i]; rp.colorAttachments[i].resolveSlice=slices[i]; rp.colorAttachments[i].storeAction=MTLStoreActionStoreAndMultisampleResolve; }
                else { rp.colorAttachments[i].slice=slices[i]; rp.colorAttachments[i].storeAction=MTLStoreActionStore; }
            }
            rp.depthAttachment.texture=depthTex; rp.depthAttachment.loadAction=depth?MTLLoadActionClear:MTLLoadActionLoad; rp.depthAttachment.storeAction=MTLStoreActionStore; rp.depthAttachment.clearDepth=dv;
            rp.stencilAttachment.texture=depthTex; rp.stencilAttachment.loadAction=stencil?MTLLoadActionClear:MTLLoadActionLoad; rp.stencilAttachment.storeAction=MTLStoreActionStore; rp.stencilAttachment.clearStencil=sv;
            rp.visibilityResultBuffer=visibilityBuffer;
            applyRasterizationSamplePositions(rp,sampleCount);   // AM4-141
            recordActiveColorFormats(colors);                     // AM4-142
            encoder=[command renderCommandEncoderWithDescriptor:rp];
            if(!encoder) throw std::runtime_error("Metal: failed to create clear render encoder");
            [encoder retain];
            ++encoderGeneration;   // AM4-108
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
        // plans/plan_apple_m4.md AM4-032: a plane the bound target's DepthFormat does not have is
        // inert, as it is on XNA -- the shared attachment behind it is never tested or written.
        bool targetHasDepth=true, targetHasStencil=true;
        activeTargetDepthPlanes(targetHasDepth,targetHasStencil);
        depthStateTargetHasDepth=targetHasDepth; depthStateTargetHasStencil=targetHasStencil;
        const bool effectiveDepth=depthEnabled&&targetHasDepth;
        const bool effectiveStencil=stencilEnabled&&targetHasStencil;
        d.depthCompareFunction = effectiveDepth ? metalCompareFunction(depthFunc) : MTLCompareFunctionAlways;
        d.depthWriteEnabled=MetalEffectiveDepthWriteEnabled(effectiveDepth,depthWrite);
        // plans/plan_metal.md METAL-9/10: real front/back stencil test, replacing the previous
        // reference-value-only plumbing. Front face carries XNA's "normal" stencil fields; back
        // face carries the CounterClockwise fields when TwoSidedStencilMode is set, else mirrors
        // front exactly -- matches FNA's own real behavior (CCW fields are simply ignored when
        // TwoSidedStencilMode=false, not reset to any default) and EasyGLRenderer's
        // identical fallback-to-front pattern. UNLIKE VulkanRenderer::FillDepthStencilState
        // (see its own long comment), this front/back assignment is NOT swapped -- Metal has no
        // Vulkan-style NDC Y-flip in this codebase's vertex shaders (Vulkan's own swap was an
        // empirically-found compensation for that Y-flip's winding interaction, root-caused to
        // Vulkan specifically, not a general rule) -- but this has NOT been empirically verified
        // on real Metal hardware and must be treated as unproven until it is (plans/plan_metal.md
        // Testing strategy tier 2/3).
        if (effectiveStencil) {
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

    // plans/plan_metal.md METAL-23/29: replaces the 5 eagerly-built named pipeline fields with a
    // lazily-populated cache keyed by (shader/vertex-layout variant, current blend state).
    id<MTLRenderPipelineState> getOrCreatePipeline(PipelineKind kind,
                                                   const MetalDeclaredVertexInput* vertexInput=nullptr)
    {
        // plans/plan_metal.md METAL-112/113: clamped the same way SetRenderTargets() itself clamps
        // count, and again here as a defensive bound on whatever activeColorAttachmentCount
        // currently holds -- MTLPipelineCacheKey's own field is a uint8_t, and 8 is Metal's own
        // hardware attachment limit either way.
        const uint8_t colorCount = (uint8_t)std::clamp(activeColorAttachmentCount, 1, 8);
        // plans/plan_metal.md METAL-104: same defensive-clamp reasoning as colorCount above, for the
        // orthogonal MSAA axis -- activeSampleCount is always one of {1,2,4,8} in practice
        // Historical code only ever produced {1,2,4,8}; the supported contract currently keeps 1.
        const uint8_t sampleCountKey = (uint8_t)std::clamp(activeSampleCount, 1, 8);
        PipelineCacheKey key{kind, currentBlend, colorCount, sampleCountKey,
                             vertexInput ? vertexInput->LayoutKey() : 0, sampleMaskOutput};
        for (std::size_t i=0;i<key.colorFormats.size();++i)
            key.colorFormats[i]=(uint16_t)activeColorFormats[i];   // AM4-142
        auto it = pipelineCache.find(key);
        if (it != pipelineCache.end()) return it->second;
        NSString* vs=nil; NSString* fs=nil; std::size_t stride=0;
        switch (kind) {
            case PipelineKind::Colored16:        vs=@"cna_v3d_color";    fs=@"cna_f3d_color";   stride=16; break;
            case PipelineKind::Textured20:       vs=@"cna_v3d_tex";      fs=@"cna_f3d_texture"; stride=20; break;
            case PipelineKind::ColorTex24:       vs=@"cna_v3d_colortex"; fs=@"cna_f3d_texture"; stride=24; break;
            case PipelineKind::LitTex32:         vs=@"cna_v3d_lit";      fs=@"cna_f3d_lit";     stride=32; break;
            case PipelineKind::LitTex32VertexLit: vs=@"cna_v3d_lit_vertexlit"; fs=@"cna_f3d_lit_vertexlit"; stride=32; break;
            case PipelineKind::DualTex20:        vs=@"cna_v3d_dualtex";       fs=@"cna_f3d_dualtex"; stride=20; break;
            case PipelineKind::DualTex24Colored: vs=@"cna_v3d_dualtex_color"; fs=@"cna_f3d_dualtex"; stride=24; break;
            case PipelineKind::EnvMap32:         vs=@"cna_v3d_envmap";   fs=@"cna_f3d_envmap";  stride=32; break;
            case PipelineKind::Skinned52:        vs=@"cna_v3d_skinned";       fs=@"cna_f3d_skinned"; stride=52; break;
            case PipelineKind::Skinned56:        vs=@"cna_v3d_skinned_color"; fs=@"cna_f3d_skinned"; stride=56; break;
            case PipelineKind::Skinned52VertexLit: vs=@"cna_v3d_skinned_vertexlit";       fs=@"cna_f3d_skinned_vertexlit"; stride=52; break;
            case PipelineKind::Skinned56VertexLit: vs=@"cna_v3d_skinned_color_vertexlit"; fs=@"cna_f3d_skinned_vertexlit"; stride=56; break;
            case PipelineKind::Pbr48:            vs=@"cna_v3d_pbr";           fs=@"cna_f3d_pbr";     stride=48; break;
            case PipelineKind::SkinnedPbr68:      vs=@"cna_v3d_skinned_pbr";  fs=@"cna_f3d_pbr";      stride=68; break;
            case PipelineKind::Sprite2D:         vs=@"cna_v2d";          fs=@"cna_f2d";          stride=0;  break;
        }
        // plans/plan_metal.md: real bug found and fixed 2026-07-20 -- MTLVertexDescriptor is a concrete
        // Objective-C class (unlike MTLTexture/MTLBuffer/MTLRenderPipelineState etc., which are
        // protocols), so it needs a plain `MTLVertexDescriptor*` pointer, not the `id<Protocol>`
        // syntax used everywhere else in this file -- vertexDescriptorForStride() itself already
        // correctly declared this way, only this one call site got it wrong. Never caught until
        // this was compiled for the first time ever on real Apple hardware -- Clang's own "type
        // argument 'MTLVertexDescriptor' must be a pointer (requires a '*')" error.
        // plans/plan_apple_m4.md AM4-035: every 3D pipeline's descriptor comes from the declared
        // input drawMetal3D resolved; the canonical declarations reproduce the old fixed table.
        if (kind!=PipelineKind::Sprite2D && !vertexInput)
            throw std::logic_error("Metal: a 3D pipeline needs its resolved vertex input");
        (void)stride;
        MTLVertexDescriptor* vd = (kind==PipelineKind::Sprite2D) ? nil : vertexDescriptorFromInput(*vertexInput);
        MetalObjectOwner pipelineOwner(retainMetalObject,releaseMetalObject);
        pipelineOwner.Adopt(makePipeline(device, library, vs, fs, vd, currentBlend, colorCount,
                                         sampleCountKey, sampleMaskOutput, &activeColorFormats,
                                         vertexInput && vertexInput->instanceMatrix));   // AM4-143
        const auto inserted=EmplaceMetalOwnedResource(pipelineCache,key,pipelineOwner);
        return inserted->second;
    }

    // Builds (or reuses) a cached MTLSamplerState for the given raw XNA TextureFilter/
    // TextureAddressMode/maxAnisotropy combination. Cache is owned by this Impl and released
    // once, in its destructor -- samplerSlots[] below only holds non-owning references into it.
    id<MTLSamplerState> samplerFor(int filter,int addressU,int addressV,int maxAnisotropy,
                                   int maxMipLevel=0,float lodBias=0.0f,int addressW=-1)
    {
        const uint32_t aniso=(uint32_t)std::clamp(maxAnisotropy,1,16);
        const auto key=std::make_tuple(filter,addressU,addressV,(int)aniso,maxMipLevel,std::bit_cast<std::uint32_t>(lodBias),addressW);
        auto it=samplerCache.find(key);
        if(it!=samplerCache.end()) return it->second;
        // plans/plan_apple_m4.md AM4-034: MipMapLevelOfDetailBias is a sampler property only from
        // macOS/iOS 26 (MTLSamplerDescriptor.lodBias). Below that it is refused, never dropped.
        bool lodBiasAvailable=false;
#if CNA_METAL_SDK_HAS_SAMPLER_LOD_BIAS
        if (@available(macOS 26.0, iOS 26.0, *)) lodBiasAvailable=true;
#endif
        if(lodBias!=0.0f && !lodBiasAvailable)
            throw System::NotSupportedException(
                "Metal SamplerState.MipMapLevelOfDetailBias needs macOS/iOS 26 and a build against their "
                "SDK (MTLSamplerDescriptor.lodBias).");
        MTLSamplerDescriptor* sd=[[MTLSamplerDescriptor alloc] init];
        if(!sd) throw std::runtime_error("Metal: failed to allocate sampler descriptor");
        sd.minFilter=metalMinFilter(filter); sd.magFilter=metalMagFilter(filter); sd.mipFilter=metalMipFilter(filter);
        sd.sAddressMode=metalAddressMode(addressU); sd.tAddressMode=metalAddressMode(addressV);
        // AM4-144: a volume sampler's third coordinate (SamplerState.AddressW); -1 keeps Metal's default.
        if(addressW>=0) sd.rAddressMode=metalAddressMode(addressW);
        if(filter==2) sd.maxAnisotropy=aniso;
        // XNA's MaxMipLevel is the index of the most detailed level the sampler may use -- FNA3D
        // sets it as GL_TEXTURE_BASE_LEVEL -- so it clamps the level of detail from below. XNA
        // stores it unsigned, so a negative value is a huge index and selects the last level
        // (Metal clamps the level of detail to the texture's own chain).
        sd.lodMinClamp=static_cast<float>(static_cast<std::uint32_t>(maxMipLevel));
#if CNA_METAL_SDK_HAS_SAMPLER_LOD_BIAS
        if(lodBias!=0.0f){
            if (@available(macOS 26.0, iOS 26.0, *)) sd.lodBias=lodBias;
        }
#endif
        MetalObjectOwner samplerOwner(retainMetalObject,releaseMetalObject);
        samplerOwner.Adopt([device newSamplerStateWithDescriptor:sd]); [sd release];
        if(!samplerOwner.HasValue()) throw std::runtime_error("Metal: failed to create sampler state");
        const auto inserted=EmplaceMetalOwnedResource(samplerCache,key,samplerOwner);
        return inserted->second;
    }

    // plans/plan_metal.md Phase 15 (METAL-153/155/156/158/159): the shared letterbox/overscan/stretch/
    // native/fixed-height-dynamic-width viewport math, ported near-verbatim from the already-
    // shipped GPU presentation implementation rather than re-derived from scratch.
    // `width`/`height`/`x`/`y` are the logical canvas's rectangle in
    // physical window pixels; `logicalWidth`/`logicalHeight` are the virtual-resolution size that
    // rectangle represents (equal to the physical size whenever no virtual resolution is set,
    // which is also this struct's all-zero-input-safe degenerate case).
    // plans/plan_metal.md METAL-34-style extraction: the real arithmetic now lives in the plain-C++
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

    // plans/plan_metal.md METAL-157/158: previously `cna_v2d` mapped sprite coordinates directly from
    // raw physical drawable pixels, completely bypassing virtual resolution/letterboxing -- a
    // real, currently-shipping bug whenever the physical window size differs from the requested
    // virtual resolution. Algebraically folding computeLogicalViewport()'s rect + the physical
    // drawable size into one scale+offset pair keeps `cna_v2d` a single multiply-add per vertex;
    // when no virtual resolution is set this reduces exactly (verified by hand, not just by
    // inspection) to the original `px/dw*2-1, 1-py/dh*2` formula -- zero behavior change for every
    // draw that isn't using virtual resolution today.
    struct Sprite2DTransform { float scaleX=1, scaleY=1, offsetX=0, offsetY=0; };
    // plans/plan_metal.md Phase 10: real bug found and fixed while adding RenderTarget2D support -- this
    // used to read `drawable.texture.width/height` unconditionally, but `drawable` is nil whenever
    // a RenderTarget2D is currently bound (see resolveActiveAttachments()'s render-target branch,
    // which never touches `drawable` at all), so a message-to-nil would silently degrade to
    // dw=dh=0 and this function would return the identity-ish default -- SpriteBatch draws into a
    // bound render target would have used the WRONG (backbuffer-shaped) transform, or worse,
    // whatever stale identity default, not the render target's own real dimensions. Declared here,
    // defined out-of-line after MetalRenderTargetRenderer (same reason as resolveActiveAttachments:
    // its body now calls currentRenderTarget->colorTexture(), which needs the complete type).
    Sprite2DTransform computeSpriteTransform() const;

    // plans/plan_metal.md METAL-153/154: real window<->logical coordinate transforms, previously
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
    [defaultBlackCubeTexture release]; defaultBlackCubeTexture=nil;
    [defaultBlackTexture release]; defaultBlackTexture=nil;
#if defined(CNA_METAL_COMPILED_EFFECTS)
    for (auto& entry : compiledPipelines) [entry.second release];
    compiledPipelines.clear();
    for (auto& entry : compiledFunctions) [entry.second release];
    compiledFunctions.clear();
    [defaultBlackVolumeTexture release]; defaultBlackVolumeTexture=nil;
#endif
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

// plans/plan_metal.md Phase 14 (METAL-142-152): Custom ShaderEffect / MSL contract.
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
        const MetalAutoreleaseScope autoreleaseScope;
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
    // plans/plan_metal.md METAL-104: also rebuilds when owner_.activeSampleCount changes, the same
    // "genuine Metal API validation error otherwise" reasoning makePipeline()'s own sampleCount
    // parameter documents -- a custom SpriteBatch effect drawn while the backbuffer/RenderTarget2D
    // happens to be MSAA-enabled needs a pipeline whose own sampleCount matches, exactly like every
    // built-in PipelineKind already gets via getOrCreatePipeline()'s own MetalPipelineCacheKey.
    id<MTLRenderPipelineState> pipelineFor(const BlendKey& blend)
    {
        if (pipeline_ && blend == lastBlend_ && owner_.activeSampleCount == lastSampleCount_ &&
            owner_.activeColorAttachmentCount == lastColorCount_ &&
            owner_.activeColorFormats == lastColorFormats_) return pipeline_;
        if (pipeline_) { [pipeline_ release]; pipeline_ = nil; }
        MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
        d.vertexFunction = vertFn_;
        d.fragmentFunction = fragFn_;
        d.colorAttachments[0].pixelFormat = owner_.activeColorFormats[0];   // AM4-142
        d.colorAttachments[0].writeMask = (MTLColorWriteMask)MetalColorWriteMaskBits(blend.writeMask);
        // plans/plan_apple_m4.md AM4-097: a pipeline must declare every attachment of the pass; the
        // effect's single output reaches attachment 0 only.
        const int colorCount = std::clamp(owner_.activeColorAttachmentCount, 1, 8);
        for (int i = 1; i < colorCount; ++i) {
            d.colorAttachments[i].pixelFormat = owner_.activeColorFormats[(std::size_t)i];
            d.colorAttachments[i].writeMask = MTLColorWriteMaskNone;
        }
        d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        d.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        d.rasterSampleCount = (NSUInteger)owner_.activeSampleCount;
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
        lastColorFormats_ = owner_.activeColorFormats;
        lastColorCount_ = owner_.activeColorAttachmentCount;
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
    int lastSampleCount_=1; // plans/plan_metal.md METAL-104
    std::array<MTLPixelFormat,8> lastColorFormats_{};   // AM4-142
    int lastColorCount_=1; // plans/plan_apple_m4.md AM4-097
    bool valid_ = false;
    std::string compileError_;
    // Defaults match the identity matrix / transparent-black color / zero scalar a fresh custom
    // effect should read before any SetUniformXxx() call, mirroring Vulkan/D3D11's own
    // zero-initialized push-constant/constant-buffer default.
    float uMatrix_[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float uColor_[4] = {0,0,0,0};
    float uFloat0_ = 0.0f;
};

#if defined(CNA_METAL_COMPILED_EFFECTS)
static void drawMetalCompiledSprites(MetalRenderer::Impl& p, const MetalVertexBuffer& vb, int vertexCount,
                                     const GpuDrawParams& params, id<MTLTexture> texture, id<MTLSamplerState> sampler);
#endif

class MetalSpriteBatch final : public ISpriteBatchRenderer
{
public:
    explicit MetalSpriteBatch(MetalRenderer& b):b_(b){}
#if defined(CNA_METAL_COMPILED_EFFECTS)
    ~MetalSpriteBatch() override { clearPendingCompiled(); }
    void Begin() override { begun_=true; compiledStockApplied_=false; }
    // AM4-144: the compiled route draws each same-texture run once per pass, so End() submits the last.
    void End() override { flushCompiled(); begun_=false; }
#else
    void Begin() override { begun_=true; }
    void End() override { begun_=false; }
#endif
    void SetSamplerFilter(int f) override { filter_=f; }
    void SetSamplerAddressMode(int addressU,int addressV) override { addressU_=addressU; addressV_=addressV; }
    // plans/plan_apple_m4.md AM4-034: the rest of Begin's SamplerState, which the sprite sampler
    // used to replace with anisotropy 1, MaxMipLevel 0 and no LOD bias.
    void SetSamplerMaxAnisotropy(int maxAnisotropy) override { maxAnisotropy_=maxAnisotropy; }
    void SetSamplerMipState(int maxMipLevel,float lodBias) override { maxMipLevel_=maxMipLevel; lodBias_=lodBias; }
    // plans/plan_metal.md METAL-182/183: previously entirely unimplemented (base no-op) -- `cna_v2d` had
    // no matrix uniform at all, so `SpriteBatch.Begin(transformMatrix)` had zero effect on Metal.
    // Applied as a 2D point transform (z=0) on the already-screen-space quad corners, matching the
    // same convention this plan's own research found `SOFTWARE`'s `SetTransformMatrix` already
    // uses -- CPU-side, not threaded through the vertex shader, so identity (the default) costs
    // nothing extra and is provably a no-op.
    void SetTransformMatrix(const Matrix& m) override { transform_=m; }
    // plans/plan_apple_m4.md AM4-027: SpriteBatch projects over the LOGICAL viewport it hands over
    // here at its flush boundary, as Vulkan's sprite renderer does. GraphicsDevice has already
    // mapped that viewport to its physical rectangle -- letterbox offset and Retina scale included
    // -- and set it on the encoder, so the projection must not apply them again. Folding the
    // window letterbox into the transform as well (computeSpriteTransform's backbuffer branch)
    // applied it twice: a 64x64 batch in a 160x96 window landed at x 51..109 instead of 32..127.
    void SetViewportSizeEXT(int width,int height) override { projectionWidth_=width; projectionHeight_=height; }
    // plans/plan_metal.md Phase 14 (METAL-145/148/149): mirrors D3D11SpriteBatchRenderer's own
    // `customEffect_ = effect;` (just stores the raw Effect* -- the actual IEffectRenderer is
    // resolved fresh in Draw() via GetEffectRendererPtr(), never cached here, so a mid-batch
    // Effect::Clone()/reassignment can't leave this pointing at a stale renderer).
    // plans/plan_apple_m4.md AM4-077: enabled again on the evidence docs/metal-shader-effect-contract.md
    // asked for (Metal_SpriteBatch_CustomEffect, under Metal API and shader validation).
    void SetCustomEffect(Effect* effect) override
    {
#if defined(CNA_METAL_COMPILED_EFFECTS)
        // plans/plan_apple_m4.md AM4-144: a compiled Effect-Framework effect gets its own route --
        // SpriteBatch.Begin(..., effect) with one is what Microsoft's SpriteEffects sample does.
        if (effect && effect->GetCompiledRuntimePtr()) {
            if (compiledEffect_!=effect) flushCompiled();
            compiledEffect_=effect; customEffect_=nullptr;
            return;
        }
        flushCompiled(); compiledEffect_=nullptr;
#endif
        customEffect_=effect;
    }
    void Draw(const ITextureRenderer& t,float x,float y) override { Rectangle d((int)x,(int)y,t.GetWidth(),t.GetHeight()); Rectangle s(0,0,t.GetWidth(),t.GetHeight()); Draw(t,d,s,Color::White); }
    void Draw(const ITextureRenderer& t,const Rectangle& d,const Rectangle& s,const Color& c) override { Draw(t,d,s,c,0,Vector2::Zero,SpriteEffects::None,0); }
    void Draw(const ITextureRenderer& t,const Rectangle& d,const Rectangle& s,const Color& c,float rotation,const Vector2& origin,SpriteEffects effects,float layerDepth) override
    {
        drawQuad(t,(float)d.X,(float)d.Y,(float)d.Width,(float)d.Height,s,c,rotation,origin,effects,layerDepth);
    }
    // plans/plan_apple_m4.md AM4-136: SpriteBatch hands its unrounded destination here (Vector2
    // positions and scales). The interface default truncates it to a Rectangle, which snapped
    // every sprite to whole pixels -- the jitter of smooth scrolling and scaling.
    void Draw(const ITextureRenderer& t,float dx,float dy,float dw,float dh,const Rectangle& s,const Color& c,float rotation,const Vector2& origin,SpriteEffects effects,float layerDepth) override
    {
        drawQuad(t,dx,dy,dw,dh,s,c,rotation,origin,effects,layerDepth);
    }
private:
    void drawQuad(const ITextureRenderer& t,float dx,float dy,float dw,float dh,const Rectangle& s,const Color& c,float rotation,const Vector2& origin,SpriteEffects effects,float layerDepth)
    {
        (void)layerDepth;
        const MetalAutoreleaseScope autoreleaseScope;
        if(!begun_) throw std::runtime_error("Metal SpriteBatch.Draw called outside Begin/End");
        // plans/plan_metal.md Phase 10: nativeTextureFor() (not a bare MetalTexture dynamic_cast) so
        // drawing a previously-rendered-to RenderTarget2D as a sprite works, not just a plain Texture2D.
        id<MTLTexture> nativeTex=nativeTextureFor(&t); if(!nativeTex) throw std::runtime_error("Metal: foreign texture renderer");
        auto& p=b_.impl();
        if(!p.ensureFrame()||p.rasterState.ShouldSkipDraw()||!p.admitSampleMask()) return;
        struct V{float x,y,u,v,r,g,b,a;}; V q[6];
        // plans/plan_metal.md: real bug found and fixed 2026-07-20 -- Rectangle/Vector2 in this codebase
        // use plain public fields (X/Y/Width/Height), matching real XNA's own struct convention,
        // not the getXProperty()-style getter this line originally guessed at (that convention is
        // real for Color, which this same function correctly uses just below, but Rectangle/
        // Vector2 were never converted to properties). Never caught until this was compiled for
        // the first time ever on real Apple hardware -- Clang's own "no member named
        // 'getXProperty'" error.
        float x0=dx, y0=dy, x1=x0+dw, y1=y0+dh;
        float u0=(float)s.X/t.GetWidth(), v0=(float)s.Y/t.GetHeight();
        float u1=(float)(s.X+s.Width)/t.GetWidth(), v1=(float)(s.Y+s.Height)/t.GetHeight();
        if((int)effects & 1) std::swap(u0,u1); if((int)effects & 2) std::swap(v0,v1);
        const float cr=c.getRProperty()/255.f,cg=c.getGProperty()/255.f,cb=c.getBProperty()/255.f,ca=c.getAProperty()/255.f;
        // plans/plan_apple_m4.md AM4-033: `origin` is in SOURCE texels (ISpriteBatchRenderer), and
        // XNA puts that point of the sprite at the destination position: the quad starts at
        // -origin * (destination / source) and rotates about the destination's top-left. This used
        // to pivot about x0 + origin unscaled without moving the quad, so any non-zero origin drew
        // the sprite displaced (point_sampling_contract_test L2). Same arithmetic as WebGPU's.
        const float originScaleX=s.Width!=0 ? dw/static_cast<float>(s.Width) : 0.0f;
        const float originScaleY=s.Height!=0 ? dh/static_cast<float>(s.Height) : 0.0f;
        const float originX=origin.X*originScaleX, originY=origin.Y*originScaleY;
        const float cs=std::cos(rotation), sn=std::sin(rotation);
        auto xf=[&](float x,float y){ const float px=x-x0-originX, py=y-y0-originY; return std::array<float,2>{x0+px*cs-py*sn,y0+px*sn+py*cs};};
        auto tf=[&](std::array<float,2> q){ float x=q[0],y=q[1]; return std::array<float,2>{x*transform_.M11+y*transform_.M21+transform_.M41, x*transform_.M12+y*transform_.M22+transform_.M42}; };
        auto a=tf(xf(x0,y0)),bb=tf(xf(x1,y0)),cc=tf(xf(x1,y1)),dd=tf(xf(x0,y1));
#if defined(CNA_METAL_COMPILED_EFFECTS)
        // AM4-144: a compiled effect owns the whole sprite, the transform to clip space included,
        // so it receives these sprite-space points (and the layer depth) rather than NDC.
        if (compiledEffect_) {
            if (pendingTexture_!=nativeTex) { flushCompiled(); pendingTexture_=[nativeTex retain]; }
            const CompiledSpriteVertex quad[6]={
                {a[0],a[1],layerDepth,u0,v0,cr,cg,cb,ca},{bb[0],bb[1],layerDepth,u1,v0,cr,cg,cb,ca},
                {cc[0],cc[1],layerDepth,u1,v1,cr,cg,cb,ca},{a[0],a[1],layerDepth,u0,v0,cr,cg,cb,ca},
                {cc[0],cc[1],layerDepth,u1,v1,cr,cg,cb,ca},{dd[0],dd[1],layerDepth,u0,v1,cr,cg,cb,ca}};
            pendingCompiled_.insert(pendingCompiled_.end(),std::begin(quad),std::end(quad));
            return;
        }
#endif
        V vs[6]={{a[0],a[1],u0,v0,cr,cg,cb,ca},{bb[0],bb[1],u1,v0,cr,cg,cb,ca},{cc[0],cc[1],u1,v1,cr,cg,cb,ca},{a[0],a[1],u0,v0,cr,cg,cb,ca},{cc[0],cc[1],u1,v1,cr,cg,cb,ca},{dd[0],dd[1],u0,v1,cr,cg,cb,ca}};
        // plans/plan_metal.md METAL-157/158: was raw physical-drawable-pixel NDC mapping (`{w,h}`),
        // ignoring virtual resolution/letterboxing entirely -- now the real scale+offset transform.
        auto st=p.computeSpriteTransform();
        if(projectionWidth_>0&&projectionHeight_>0)
        {
            st.scaleX=2.0f/static_cast<float>(projectionWidth_); st.offsetX=-1.0f;
            st.scaleY=-2.0f/static_cast<float>(projectionHeight_); st.offsetY=1.0f;
        }
        struct U{float sx,sy,ox,oy;} u{st.scaleX,st.scaleY,st.offsetX,st.offsetY};
        // plans/plan_metal.md Phase 14 (METAL-145/148): resolved fresh every Draw() call, not cached
        // across the Begin/End block -- see MetalEffectRenderer::pipelineFor()'s own comment for why
        // this (deliberately) makes a SetUniformXxx() call between two Draw()s take effect on the
        // very next sprite, unlike the D3D11/Vulkan once-per-flush precedent.
        MetalEffectRenderer* ceb=nullptr;
        if (customEffect_) {
            customEffect_->Apply();
            ceb=dynamic_cast<MetalEffectRenderer*>(customEffect_->GetEffectRendererPtr());
            if (ceb && !ceb->IsValid()) ceb=nullptr;
        }
        id<MTLRenderPipelineState> pipe = nil;
        if (ceb && p.sampleMaskOutput)
            throw System::NotSupportedException(
                "Metal: a SpriteBatch custom effect's MSL cannot be given a [[sample_mask]] output, so "
                "a BlendState.MultiSampleMask that keeps only some samples of a multisampled target "
                "is refused for it (AM4-141).");
        if (ceb) {
            // plans/plan_apple_m4.md AM4-077: a valid effect whose pipeline cannot be built for the
            // active blend state is reported rather than drawn with the stock shader in its place.
            pipe=ceb->pipelineFor(p.currentBlend);
            if (!pipe) throw std::runtime_error(ceb->GetCompileError());
        } else {
            pipe=p.getOrCreatePipeline(PipelineKind::Sprite2D);
        }
        [p.encoder setRenderPipelineState:pipe]; [p.encoder setVertexBytes:vs length:sizeof(vs) atIndex:0]; [p.encoder setVertexBytes:&u length:sizeof(u) atIndex:1];
        p.bindSampleMask();   // AM4-141
        if (ceb) {
            const float* m=ceb->GetMatrix(); const float* col=ceb->GetColor(); float f0=ceb->GetFloat0();
            [p.encoder setVertexBytes:m length:16*sizeof(float) atIndex:2]; [p.encoder setVertexBytes:col length:4*sizeof(float) atIndex:3]; [p.encoder setVertexBytes:&f0 length:sizeof(float) atIndex:4];
            [p.encoder setFragmentBytes:m length:16*sizeof(float) atIndex:2]; [p.encoder setFragmentBytes:col length:4*sizeof(float) atIndex:3]; [p.encoder setFragmentBytes:&f0 length:sizeof(float) atIndex:4];
        }
        [p.encoder setFragmentTexture:nativeTex atIndex:0]; [p.encoder setFragmentSamplerState:p.samplerFor(filter_,addressU_,addressV_,maxAnisotropy_,maxMipLevel_,lodBias_) atIndex:0]; [p.encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    }
#if defined(CNA_METAL_COMPILED_EFFECTS)
    struct CompiledSpriteVertex { float x,y,z,u,v,r,g,b,a; };

    void clearPendingCompiled()
    {
        pendingCompiled_.clear();
        [pendingTexture_ release]; pendingTexture_=nil;
    }

    // XNA's SpriteEffect supplies the vertex stage a pixel-only custom pass keeps (WebGPU's
    // ApplyCompiledSpriteVertexShaderEXT): applied once per batch, before the custom passes.
    void applyCompiledStockVertexStage()
    {
        if (compiledStockApplied_) return;
        if (!spriteEffect_) {
            const auto& bytes=CNA::Internal::Renderers::Fna3d::StockEffectBlobs::kSpriteEffectFxb;
            spriteEffect_=b_.CreateCompiledEffect(bytes,sizeof(bytes));
            const auto& parameters=spriteEffect_->GetDescription().parameters;
            const auto matrix=std::find_if(parameters.begin(),parameters.end(),
                [](const CompiledEffectParameterDescription& parameter){ return parameter.name=="MatrixTransform"; });
            if (matrix==parameters.end())
                throw std::runtime_error("Metal SpriteBatch: the embedded XNA SpriteEffect has no MatrixTransform parameter.");
            spriteMatrixParameterIndex_=matrix->runtimeIndex;
        }
        float width=(float)projectionWidth_, height=(float)projectionHeight_;
        if (width<=0.0f||height<=0.0f) {
            const MetalViewportState viewport=b_.impl().rasterState.EffectiveViewport();
            width=(float)viewport.width; height=(float)viewport.height;
        }
        const Matrix projection=Matrix::CreateOrthographicOffCenter(0.0f,width,height,0.0f,0.0f,-1.0f);
        float values[16];
        // Effect Framework storage is the transposed layout EffectParameter::SetValue(Matrix) writes.
        Matrix::Transpose(projection).ToColumnMajor(values);
        spriteEffect_->SetParameterValue(spriteMatrixParameterIndex_,values,sizeof(values));
        spriteEffect_->SetTechnique(0);
        auto& device=compiledEffect_->getGraphicsDeviceInternal();
        CompiledEffectDeviceState state;
        state.blend=&device.getBlendStateProperty();
        state.depthStencil=&device.getDepthStencilStateProperty();
        state.rasterizer=&device.getRasterizerStateProperty();
        state.samplerStates=&device.getSamplerStatesProperty();
        state.vertexSamplerStates=&device.getVertexSamplerStatesProperty();
        CompiledEffectPassStateChanges ignored;
        spriteEffect_->ApplyPass(0,state,ignored);
        compiledStockApplied_=true;
    }

    // XNA draws a run once per pass of the effect's current technique (plans/plan_fx.md FX-102),
    // pass-major over the run.
    void flushCompiled()
    {
        if (pendingCompiled_.empty()||!compiledEffect_) { clearPendingCompiled(); return; }
        using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
        static const VertexDeclaration kSpriteDeclaration((int)sizeof(CompiledSpriteVertex), {
            VertexElement(0,VertexElementFormat::Vector3,VertexElementUsage::Position,0),
            VertexElement(12,VertexElementFormat::Vector2,VertexElementUsage::TextureCoordinate,0),
            VertexElement(20,VertexElementFormat::Vector4,VertexElementUsage::Color,0)});
        const int count=(int)pendingCompiled_.size();
        try {
            if (!compiledVertexBuffer_||compiledVertexCapacity_<count) {
                compiledVertexBuffer_=b_.CreateVertexBuffer(count);
                compiledVertexBuffer_->SetVertexDeclaration(kSpriteDeclaration);
                compiledVertexCapacity_=count;
            }
            compiledVertexBuffer_->SetData(pendingCompiled_.data(),count,sizeof(CompiledSpriteVertex));
            auto* technique=compiledEffect_->getCurrentTechniqueProperty();
            const int passCount=technique ? technique->getPassesProperty().getCountProperty() : 0;
            if (passCount==0)
                throw System::InvalidOperationException(
                    "Metal: a compiled Effect used with SpriteBatch must have a current technique with at least one pass.");
            applyCompiledStockVertexStage();
            auto& p=b_.impl();
            id<MTLSamplerState> sampler=p.samplerFor(filter_,addressU_,addressV_,maxAnisotropy_,maxMipLevel_,lodBias_);
            const auto& buffer=static_cast<const MetalVertexBuffer&>(*compiledVertexBuffer_);
            for (int pass=0;pass<passCount;++pass) {
                technique->getPassesProperty()[pass]->Apply();
                GpuDrawParams params{};
                compiledEffect_->FillGpuDrawParams(params);
                drawMetalCompiledSprites(p,buffer,count,params,pendingTexture_,sampler);
            }
        } catch (...) {
            clearPendingCompiled();
            throw;
        }
        clearPendingCompiled();
    }

    Effect* compiledEffect_=nullptr;
    std::unique_ptr<ICompiledEffectRuntime> spriteEffect_;
    std::uint32_t spriteMatrixParameterIndex_=0;
    std::vector<CompiledSpriteVertex> pendingCompiled_;
    id<MTLTexture> pendingTexture_=nil;
    std::unique_ptr<IVertexBufferRenderer> compiledVertexBuffer_;
    int compiledVertexCapacity_=0;
    bool compiledStockApplied_=false;
#endif
    MetalRenderer& b_; bool begun_=false; int filter_=0; int addressU_=1; int addressV_=1; Matrix transform_=Matrix::getIdentityProperty(); Effect* customEffect_=nullptr;
    int projectionWidth_=0; int projectionHeight_=0;
    int maxAnisotropy_=1; int maxMipLevel_=0; float lodBias_=0.0f;
};

// plans/plan_metal.md METAL-136-139: real occlusion queries via a shared MTLVisibilityResultBuffer slot
// per instance. `completed_` is a heap-allocated flag (not a plain bool member) because Objective-C
// completion-handler blocks capture it by reference into GPU-driven, asynchronously-invoked code
// that must outlive this object's own Begin()/End() call stack -- a std::shared_ptr keeps it alive
// exactly as long as either this object or the in-flight block still needs it.
//
// plans/plan_apple_m4.md AM4-038: the split described by METAL-266 is handled -- every encoder
// the query spans gets a slot of its own and a completion handler on its own command buffer, and the
// query is complete once End was called and every one of those command buffers has completed.
// Metal counts visibility per encoder and has one active offset, so one query at a time can be
// between Begin and End; a second Begin is refused rather than silently merged.
class MetalOcclusionQueryRenderer final : public IOcclusionQueryRenderer
{
public:
    explicit MetalOcclusionQueryRenderer(MetalRenderer::Impl& owner) : owner_(owner) {}
    ~MetalOcclusionQueryRenderer() override
    {
        if (owner_.activeQuery==this) {
            if (owner_.encoder) [owner_.encoder setVisibilityResultMode:MTLVisibilityResultModeDisabled offset:0];
            owner_.activeQuery=nullptr;
        }
        releaseSlots();
    }
    void Begin() override
    {
        const MetalAutoreleaseScope autoreleaseScope;
        if (owner_.activeQuery && owner_.activeQuery!=this)
            throw System::NotSupportedException(
                "Metal counts visibility into one offset at a time: end the open OcclusionQuery before beginning another.");
        // XNA permits a re-Begin once IsComplete was asked, whether or not the result had arrived;
        // the previous run is abandoned. Its slots return to the pool, which keeps them out of the
        // encoder they were used in (AM4-108), so the new run never counts the old run's samples.
        releaseSlots();
        state_=std::make_shared<State>();
        owner_.activeQuery=this;
        if (owner_.encoder) armOnCurrentEncoder();
    }
    void End() override
    {
        const MetalAutoreleaseScope autoreleaseScope;
        if (owner_.activeQuery!=this) return;
        if (owner_.encoder) [owner_.encoder setVisibilityResultMode:MTLVisibilityResultModeDisabled offset:0];
        owner_.activeQuery=nullptr;
        state_->ended.store(true);
    }
    bool IsComplete() const override
    {
        const MetalAutoreleaseScope autoreleaseScope;
        if (!state_ || !state_->ended.load()) return false;
        // AM4-108: XNA's IsComplete flushes (D3D9 GetData with D3DGETDATA_FLUSH), so a game may poll
        // it until it turns true. A query whose last counting encoder is still in the uncommitted
        // command buffer would never complete before the frame's Present; commit that buffer here,
        // the same mid-frame boundary a readback or a render-target switch takes.
        if (state_->pendingCommands.load()!=0 && owner_.command!=nil &&
            (__bridge const void*)owner_.command==state_->lastArmedCommand)
            owner_.endActiveEncoding(false);
        return state_->pendingCommands.load()==0;
    }
    int PixelCount() const override
    {
        const MetalAutoreleaseScope autoreleaseScope;
        if (!IsComplete()) return 0;
        const auto* data = static_cast<const std::uint64_t*>([owner_.visibilityBuffer contents]);
        std::uint64_t total=0;
        for (const int slot : slots_) total+=data[slot];
        return total>static_cast<std::uint64_t>(std::numeric_limits<int>::max())
                   ? std::numeric_limits<int>::max() : static_cast<int>(total);
    }
    // Called for every encoder that starts while this query is open, including the one Begin
    // finds already running.
    void armOnCurrentEncoder()
    {
        const int slot=owner_.allocateQuerySlot();
        slots_.push_back(slot);
        [owner_.encoder setVisibilityResultMode:MTLVisibilityResultModeCounting offset:(NSUInteger)(slot*8)];
        auto state=state_;
        state->pendingCommands.fetch_add(1);
        state->lastArmedCommand=(__bridge const void*)owner_.command;
        [owner_.command addCompletedHandler:^(id<MTLCommandBuffer>) { state->pendingCommands.fetch_sub(1); }];
    }
private:
    struct State
    {
        std::atomic<bool> ended{false};
        std::atomic<int> pendingCommands{0};
        // Identity only (never dereferenced): the command buffer the newest counting encoder of this
        // run belongs to, so IsComplete can tell whether that buffer is still uncommitted.
        const void* lastArmedCommand=nullptr;
    };
    void releaseSlots()
    {
        for (const int slot : slots_) owner_.releaseQuerySlot(slot);
        slots_.clear();
    }
    MetalRenderer::Impl& owner_;
    std::vector<int> slots_;
    std::shared_ptr<State> state_;
};

void MetalRenderer::Impl::rearmActiveOcclusionQuery()
{
    if (activeQuery && encoder) activeQuery->armOnCurrentEncoder();
}

// plans/plan_metal.md METAL-131/122/125: shared blit-to-staging-buffer readback helper, used by
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
    [cmd waitUntilCompleted]; // plans/plan_metal.md METAL-133: same intentional correctness-over-throughput stall as ReadBackbuffer().
    if(cmd.status!=MTLCommandBufferStatusCompleted) throw std::runtime_error("Metal: GetData blit command failed: "+describeMetalCommandBufferError(cmd.error));
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
    // plans/plan_apple_m4.md AM4-141: appliedSampleCount is already rounded to what the device
    // supports (MetalAppliedMultiSampleCount); 0 is a single-sampled target.
    MetalRenderTargetRenderer(std::shared_ptr<MetalRenderer::Impl> owner, int w, int h,
                             int depthFormat, bool mipMap, int appliedSampleCount=0,
                             MetalColorStorageInfo storage=MetalColorStorageInfo{})
        : owner_(owner), resourceHealth_(owner ? owner->resourceHealth : nullptr),
          storage_(storage), colorFormat_(metalPixelFormat(storage.storage)),
          w_(w), h_(h), mipMap_(mipMap),
          appliedDepthFormat_(MetalAppliedRenderTargetDepthFormat(depthFormat)),
          levelCount_(MetalMipLevelCount(w,h,mipMap)), appliedSampleCount_(appliedSampleCount),
          definedMipLevels_(levelCount_)
    {
        if(!owner||!resourceHealth_)
            throw std::invalid_argument("Metal RenderTarget2D requires an active owner");
        owner->throwPendingCommandFailure();
        MetalObjectOwner colorOwner(retainMetalObject,releaseMetalObject);
        MetalObjectOwner msaaOwner(retainMetalObject,releaseMetalObject);
        MetalObjectOwner depthOwner(retainMetalObject,releaseMetalObject);
        // plans/plan_metal.md METAL-101: MUST be BGRA8Unorm, matching every pipeline's own hardcoded
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
        // plans/plan_metal.md METAL-103: `mipmapped:mipMap` makes this convenience initializer allocate
        // the full mip chain (mipmapLevelCount = floor(log2(max(w,h)))+1) when requested, matching
        // MTLTextureDescriptor's own documented behavior for this factory method.
        MTLTextureDescriptor* cd=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:colorFormat_ width:(NSUInteger)w height:(NSUInteger)h mipmapped:mipMap];
        if(!cd) throw std::runtime_error("Metal: failed to allocate RenderTarget2D color descriptor");
        cd.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
        colorOwner.Adopt([owner->device newTextureWithDescriptor:cd]);
        if(!colorOwner.HasValue()) throw std::runtime_error("Metal: failed to create RenderTarget2D color texture");
        // plans/plan_metal.md METAL-104: engaging MSAA needs a SECOND color texture -- colorTexture_
        // above stays the single-sample, sampleable, GetData()-readable texture every existing
        // caller already expects (unaffected either way); msaaColorTexture_ is the real multisampled
        // render target this instance's own render passes actually write into when appliedSampleCount_
        // >0, resolved into colorTexture_ at every encoder boundary (see msaaColorTexture's own
        // field comment on Impl for why StoreAndMultisampleResolve, not plain MultisampleResolve).
        if (appliedSampleCount_ > 0) {
            msaaOwner.Adopt(makeMultisampleTexture(owner->device, colorFormat_, (NSUInteger)w, (NSUInteger)h, (NSUInteger)appliedSampleCount_, MTLTextureUsageRenderTarget));
            if(!msaaOwner.HasValue()) throw std::runtime_error("Metal: failed to create RenderTarget2D MSAA color texture");
        }
        // plans/plan_metal.md METAL-104: the depth attachment's own sample count must match the color
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
        // plans/plan_metal.md METAL-112: same safety net, extended to a real MRT set -- only currentMRT[0]
        // is ever mirrored into currentRenderTarget above, so the check above alone would miss
        // target 1..N-1 being destroyed while still part of the active binding. Invalidates the
        // whole set rather than leave the other targets' own destructors with no way to detect a
        // now-dangling entry (matches this file's own established "the game forgot to unbind, but
        // it must not corrupt Impl's own state" convention, just applied to N pointers not one).
            owner->detachMrtMember(this);
        }
        [samplingView_ release]; samplingView_=nil;
        [depthTexture_ release]; [colorTexture_ release]; [msaaColorTexture_ release];
    }
    int GetWidth() const override { return w_; }
    int GetHeight() const override { return h_; }

    // Public count zero is the deterministic unsupported/no-MSAA value.
    int GetMultiSampleCount() const override { return appliedSampleCount_; }
    // AM4-142: the format the target was created in (the shared draw validation reads it).
    int GetSurfaceFormatEXT() const noexcept override { return storage_.surfaceFormat; }
    // plans/plan_apple_m4.md AM4-032: the planes the requested DepthFormat has, not the storage
    // behind them -- see MetalAppliedRenderTargetDepthFormat. Impl::rebuildDepthState makes a
    // missing plane inert while this target is bound.
    int GetAppliedDepthStencilFormatEXT(int /*requestedDepthStencilFormat*/) const override
    {
        return appliedDepthFormat_;
    }
    bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override { return MetalDepthFormatHasDepth(appliedDepthFormat_); }
    bool HasRealStencilBuffer(bool /*stencilFormatWasRequested*/) const override { return MetalDepthFormatHasStencil(appliedDepthFormat_); }
    int appliedDepthFormat() const noexcept { return appliedDepthFormat_; }
    void BindAsRenderTarget() override
    {
        const MetalAutoreleaseScope autoreleaseScope;
        auto owner=lockOwner();
        owner->endActiveEncoding(false); // mid-frame switch, never presents -- see METAL-180 note.
        // plans/plan_metal.md METAL-112: a single-target bind always means "not MRT" -- clears any stale
        // MRT state a previous SetRenderTargets() call left behind, whether this was reached via
        // SetRenderTarget2D() directly or via SetRenderTargets()'s own count==1 delegation.
        owner->currentMRT.clear(); owner->activeColorAttachmentCount=1;
        owner->currentRenderTarget=this;
    }
    // plans/plan_metal.md METAL-103/112: factored out of UnbindAsRenderTarget() so SetRenderTargets()'s
    // own MRT teardown (Impl::unbindCurrentMRT()) can regenerate mips for every target in an
    // outgoing MRT set, not just whichever one BindAsRenderTarget() itself last tracked.
    void regenerateMipsIfNeeded()
    {
        // plans/plan_apple_m4.md AM4-031: a mipMap=true 1x1 target has a one-level chain, and Metal
        // rejects generateMipmapsForTexture: on it (validation: "mipmapLevelCount(1) must be > 1").
        // FNA3D gates the same call on `levelCount > 1`; there is nothing to generate.
        if (!mipMap_ || levelCount_<=1) return;
        auto owner=lockOwner();
        // AM4-142: generateMipmapsForTexture: filters, and a GPU without 32-bit float filtering
        // cannot do that for a Single/Vector2/Vector4 target; its chain is left as rendered.
        const bool float32=storage_.storage==MetalColorStorage::R32Float||storage_.storage==MetalColorStorage::Rg32Float||
                           storage_.storage==MetalColorStorage::Rgba32Float;
        if (float32 && ![owner->device supports32BitFloatFiltering]) return;
        // plans/plan_metal.md METAL-103: regenerate the full mip chain from level 0's just-rendered
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
        const MetalAutoreleaseScope autoreleaseScope;
        auto owner=lockOwner();
        if (owner->currentRenderTarget==this) {
            regenerateMipsIfNeeded();
            owner->endActiveEncoding(false); owner->currentRenderTarget=nullptr;
        }
    }
    void UpdatePixels(const uint8_t* rgba,int stride) override
    {
        const MetalAutoreleaseScope autoreleaseScope;
        auto owner=lockOwner();
        const int bytesPerTexel=storage_.bytesPerTexel;   // AM4-142
        if(!rgba||w_>std::numeric_limits<int>::max()/bytesPerTexel||
           stride!=static_cast<int>(static_cast<std::size_t>(w_)*(std::size_t)bytesPerTexel))
            throw std::invalid_argument("Metal: invalid RenderTarget2D level-zero upload");
        MetalTextureTransferLayout layout{};
        if(!TryBuildMetalTextureTransferLayout(w_,h_,1,1,layout,(std::size_t)bytesPerTexel))
            throw std::invalid_argument("Metal: invalid RenderTarget2D level-zero upload size");
        reallocateAndUploadRgba(0,rgba,w_,h_,layout,owner);
        owner->throwPendingCommandFailure();
    }
    void UpdatePixelsLevel(int level,const uint8_t* rgba,int levelWidth,int levelHeight) override
    {
        const MetalAutoreleaseScope autoreleaseScope;
        auto owner=lockOwner();
        if(!rgba||level<0||level>=levelCount_||
           levelWidth!=MetalTextureTransferDetail::MipDimension(w_,level)||
           levelHeight!=MetalTextureTransferDetail::MipDimension(h_,level))
            throw std::invalid_argument("Metal: invalid RenderTarget2D mip upload");
        MetalTextureTransferLayout layout{};
        if(!TryBuildMetalTextureTransferLayout(levelWidth,levelHeight,1,1,layout,(std::size_t)storage_.bytesPerTexel))
            throw std::invalid_argument("Metal: invalid RenderTarget2D mip upload size");
        reallocateAndUploadRgba(level,rgba,levelWidth,levelHeight,layout,owner);
        owner->throwPendingCommandFailure();
    }
    bool HasDefinedMipLevel(int level) const noexcept override
    {
        return definedMipLevels_.IsDefined(level);
    }
    // plans/plan_metal.md METAL-131: real readback via the shared blit helper, replacing ITextureRenderer's
    // inherited no-op default. If this target is still the currently active render target, its
    // pending source-render command is committed, awaited, and checked exactly before the later
    // independent blit. Otherwise the blit could run before a still-uncommitted render pass, or a
    // failed source command could be mistaken for successful current pixels.
    bool GetData(int level,int x,int y,int w,int h,void* data,int dataLength) const override
    {
        const MetalAutoreleaseScope autoreleaseScope;
        auto owner=lockOwner();
        MetalTextureTransferLayout layout{};
        if(!TryPrepareMetalTextureTransfer(
                w_,h_,1,levelCount_,level,x,y,0,w,h,1,data,dataLength,
                MetalTransferLengthRule::ExactlyTightBytes,
                MetalMacOsTextureBufferRowAlignment,layout,(std::size_t)storage_.bytesPerTexel)) return false;
        if (DescribeMetalReadbackSourcePolicy(owner->currentRenderTarget==this)==
            MetalReadbackSourcePolicy::SynchronizeActiveSource)
            owner->finishActiveCommandSynchronously(
                "Metal: RenderTarget2D source render command failed before readback");
        blitTextureToClientBuffer(owner->device,owner->queue,colorTexture_,0,level,x,y,0,w,h,1,
                                  layout,transferOrder(),data,
                                  [owner] { owner->throwPendingCommandFailure(); });
        owner->throwPendingCommandFailure();
        return true;
    }
    // plans/plan_metal.md METAL-104: colorTexture() is UNCHANGED -- still always the single-sample,
    // sampleable, GetData()-readable texture, whether or not this target engages MSAA (every
    // existing caller -- shader sampling, GetData() above, blit sources -- keeps working
    // unmodified). colorTextureForRenderPass()/resolveTargetForRenderPass() are the two new,
    // narrowly-scoped accessors resolveActiveAttachments() alone needs to build a real MSAA render
    // pass: the former is what a render pass actually writes into (the MSAA texture when engaged,
    // else colorTexture_ itself -- the exact same texture, no branch needed by the caller);
    // resolveTargetForRenderPass() is nil unless MSAA is engaged, in which case it's colorTexture_
    // (the resolve destination).
    id<MTLTexture> colorTexture() const { (void)lockOwner(); return colorTexture_; }
    /// AM4-142: what a shader samples -- the colour texture, through a view that reads the
    /// channels a one- or two-channel format lacks as 1, as Direct3D 9 does.
    id<MTLTexture> samplingTexture() const
    {
        (void)lockOwner();
        if (storage_.channels>=4) return colorTexture_;
        if (!samplingView_) samplingView_=makeXnaSamplingView(colorTexture_,storage_.channels);
        return samplingView_ ? samplingView_ : colorTexture_;
    }
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
        MTLTextureDescriptor* descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:colorFormat_
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
            if(command.status!=MTLCommandBufferStatusCompleted) throw std::runtime_error("Metal: RenderTarget2D preservation blit failed: "+describeMetalCommandBufferError(command.error));
            owner->throwPendingCommandFailure();
        }
        std::vector<std::uint8_t> bgra(layout.tightTotalBytes);
        if(!CopyMetalTightRgbaToTextureBytes(rgba,layout,transferOrder(),bgra.data(),bgra.size()))
            throw std::runtime_error("Metal: RenderTarget2D upload conversion failed");
        [replacement replaceRegion:MTLRegionMake2D(0,0,(NSUInteger)levelWidth,(NSUInteger)levelHeight)
                      mipmapLevel:(NSUInteger)targetLevel withBytes:bgra.data()
                      bytesPerRow:(NSUInteger)layout.tightRowBytes];
        [colorTexture_ release];colorTexture_=(id<MTLTexture>)replacementOwner.ReleaseOwnership();
        [samplingView_ release]; samplingView_=nil;   // AM4-142: the view named the old texture
        definedMipLevels_.MarkUploaded(targetLevel);
    }
    MetalTransferPixelOrder transferOrder() const noexcept
    {
        return storage_.bgraOrder ? MetalTransferPixelOrder::Bgra : MetalTransferPixelOrder::Raw;
    }
    std::weak_ptr<MetalRenderer::Impl> owner_;
    std::shared_ptr<MetalResourceHealth> resourceHealth_;
    MetalColorStorageInfo storage_;              // AM4-142
    MTLPixelFormat colorFormat_=MTLPixelFormatBGRA8Unorm;
    mutable id<MTLTexture> samplingView_=nil;
    int w_, h_;
    bool mipMap_;
    int appliedDepthFormat_;
    int levelCount_=1;
    int appliedSampleCount_=0;
    MetalMipDefinitionState definedMipLevels_;
    id<MTLTexture> colorTexture_=nil;
    id<MTLTexture> msaaColorTexture_=nil;
    id<MTLTexture> depthTexture_=nil;
};

// plans/plan_metal.md METAL-109/110/111: RenderTargetCube renderer. A single MTLTextureTypeCube color
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
                                 int depthFormat, bool mipMap, int appliedSampleCount=0,
                                 MetalColorStorageInfo storage=MetalColorStorageInfo{})
        : owner_(owner), resourceHealth_(owner ? owner->resourceHealth : nullptr),
          storage_(storage), colorFormat_(metalPixelFormat(storage.storage)),
          size_(size), mipMap_(mipMap),
          appliedDepthFormat_(MetalAppliedRenderTargetDepthFormat(depthFormat)),
          levelCount_(MetalMipLevelCount(size,size,mipMap)), appliedSampleCount_(appliedSampleCount)
    {
        if(!owner||!resourceHealth_)
            throw std::invalid_argument("Metal RenderTargetCube requires an active owner");
        owner->throwPendingCommandFailure();
        MetalObjectOwner colorOwner(retainMetalObject,releaseMetalObject);
        MetalObjectOwner depthOwner(retainMetalObject,releaseMetalObject);
        MTLTextureDescriptor* cd=[MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:colorFormat_ size:(NSUInteger)size mipmapped:mipMap];
        if(!cd) throw std::runtime_error("Metal: failed to allocate RenderTargetCube color descriptor");
        cd.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
        colorOwner.Adopt([owner->device newTextureWithDescriptor:cd]);
        if(!colorOwner.HasValue()) throw std::runtime_error("Metal: failed to create RenderTargetCube color texture");
        if (appliedSampleCount_ > 0) {
            // AM4-141: the faces share one depth buffer, as the single-sampled cube's do.
            depthOwner.Adopt(makeMultisampleTexture(owner->device, MTLPixelFormatDepth32Float_Stencil8, (NSUInteger)size, (NSUInteger)size, (NSUInteger)appliedSampleCount_, MTLTextureUsageRenderTarget));
        } else {
            MTLTextureDescriptor* dd=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:(NSUInteger)size height:(NSUInteger)size mipmapped:NO];
            if(!dd) throw std::runtime_error("Metal: failed to allocate RenderTargetCube depth descriptor");
            dd.storageMode=MTLStorageModePrivate; dd.usage=MTLTextureUsageRenderTarget;
            depthOwner.Adopt([owner->device newTextureWithDescriptor:dd]);
        }
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
            owner->detachMrtMember(this);  // plans/plan_apple_m4.md AM4-097
        }
        for (id<MTLTexture>& face : msaaFaces_) { [face release]; face=nil; }
        [samplingView_ release]; samplingView_=nil;
        [depthTexture_ release]; [colorTexture_ release];
    }
    int GetSize() const override { return size_; }
    int GetMultiSampleCount() const override { return appliedSampleCount_; }
    int GetSizeEXT() const noexcept override { return size_; }
    // AM4-142: as MetalRenderTargetRenderer's.
    int GetSurfaceFormatEXT() const noexcept override { return storage_.surfaceFormat; }
    // plans/plan_apple_m4.md AM4-032: as MetalRenderTargetRenderer's.
    int GetAppliedDepthStencilFormatEXT(int /*requestedDepthStencilFormat*/) const override
    {
        return appliedDepthFormat_;
    }
    bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override { return MetalDepthFormatHasDepth(appliedDepthFormat_); }
    bool HasRealStencilBuffer(bool /*stencilFormatWasRequested*/) const override { return MetalDepthFormatHasStencil(appliedDepthFormat_); }
    int appliedDepthFormat() const noexcept { return appliedDepthFormat_; }
    void BindAsRenderTargetFace(int face) override
    {
        const MetalAutoreleaseScope autoreleaseScope;
        if (face<0 || face>=6) throw std::out_of_range("Metal: RenderTargetCube face must be in [0, 5]");
        auto owner=lockOwner();
        owner->endActiveEncoding(false); // mid-frame switch, never presents -- see METAL-180 note.
        owner->currentRenderTargetCube=this;
        owner->currentRenderTargetCubeFace=(NSUInteger)face;
    }
    void UnbindAsRenderTarget() override
    {
        const MetalAutoreleaseScope autoreleaseScope;
        auto owner=lockOwner();
        if (owner->currentRenderTargetCube==this) {
            regenerateMipsIfNeeded();
            owner->endActiveEncoding(false);
            owner->currentRenderTargetCube=nullptr;
        }
    }
    // plans/plan_metal.md METAL-103 (cube analog): same unconditional mip-regeneration-on-unbind
    // policy as MetalRenderTargetRenderer's own, applied to the whole cube map at once (all 6 faces'
    // mip chains regenerate together in one generateMipmapsForTexture: call -- Metal's own documented
    // behavior for a cube texture, matching EasyGLRenderTargetCubeRenderer::UnbindAsRenderTarget's
    // own single glGenerateMipmap call over the whole cube map, not a separate call per face).
    // plans/plan_apple_m4.md AM4-097: factored out so an MRT teardown can finalize a cube member.
    void regenerateMipsIfNeeded()
    {
        if (!mipMap_ || levelCount_<=1) return; // AM4-031: a one-level chain has nothing to generate
        auto owner=lockOwner();
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
    bool SetData(int face,int level,int x,int y,int w,int h,
                 const void* data,int dataLength) override
    {
        const MetalAutoreleaseScope autoreleaseScope;
        (void)lockOwner();
        (void)face; (void)level; (void)x; (void)y; (void)w; (void)h;
        (void)data; (void)dataLength;
        return MetalRenderTargetCubeUploadSupported();
    }
    // plans/plan_metal.md METAL-131 (cube analog): same shared-helper readback and exact source-command
    // verification as MetalRenderTargetRenderer::GetData() -- checked against
    // currentRenderTargetCube==this regardless of which face, since every face shares the same
    // underlying MTLTexture object and command-buffer ordering concern.
    bool GetData(int face,int level,int x,int y,int w,int h,void* data,int dataLength) const override
    {
        const MetalAutoreleaseScope autoreleaseScope;
        auto owner=lockOwner();
        if (face<0||face>=6) return false;
        MetalTextureTransferLayout layout{};
        if(!TryPrepareMetalTextureTransfer(
                size_,size_,1,levelCount_,level,x,y,0,w,h,1,data,dataLength,
                MetalTransferLengthRule::ExactlyTightBytes,
                MetalMacOsTextureBufferRowAlignment,layout,(std::size_t)storage_.bytesPerTexel)) return false;
        if (DescribeMetalReadbackSourcePolicy(owner->currentRenderTargetCube==this)==
            MetalReadbackSourcePolicy::SynchronizeActiveSource)
            owner->finishActiveCommandSynchronously(
                "Metal: RenderTargetCube source render command failed before readback");
        blitTextureToClientBuffer(owner->device,owner->queue,colorTexture_,(NSUInteger)face,level,
                                  x,y,0,w,h,1,layout,
                                  storage_.bgraOrder ? MetalTransferPixelOrder::Bgra : MetalTransferPixelOrder::Raw,data,
                                  [owner] { owner->throwPendingCommandFailure(); });
        owner->throwPendingCommandFailure();
        return true;
    }
    id<MTLTexture> colorTexture() const { (void)lockOwner(); return colorTexture_; }
    /// AM4-142: as MetalRenderTargetRenderer::samplingTexture.
    id<MTLTexture> samplingTexture() const
    {
        (void)lockOwner();
        if (storage_.channels>=4) return colorTexture_;
        if (!samplingView_) samplingView_=makeXnaSamplingView(colorTexture_,storage_.channels);
        return samplingView_ ? samplingView_ : colorTexture_;
    }
    id<MTLTexture> depthTextureNative() const { (void)lockOwner(); return depthTexture_; }
    /// AM4-141: what a pass renders a face into -- that face's own 2D multisample texture when the
    /// cube is multisampled (allocated on first use, so a face keeps its samples across binds),
    /// otherwise the cube itself.
    id<MTLTexture> colorTextureForFace(int face) const
    {
        auto owner=lockOwner();
        if (appliedSampleCount_<=0) return colorTexture_;
        id<MTLTexture>& slot=msaaFaces_[static_cast<std::size_t>(std::clamp(face,0,5))];
        if (!slot) {
            slot=makeMultisampleTexture(owner->device, colorFormat_, (NSUInteger)size_, (NSUInteger)size_,
                                        (NSUInteger)appliedSampleCount_, MTLTextureUsageRenderTarget);
            if (!slot) throw std::runtime_error("Metal: failed to create RenderTargetCube MSAA face texture");
        }
        return slot;
    }
    /// AM4-141: the cube a multisampled face resolves into; nil when single-sampled.
    id<MTLTexture> resolveTargetForFace() const { (void)lockOwner(); return appliedSampleCount_>0 ? colorTexture_ : nil; }
private:
    [[nodiscard]] std::shared_ptr<MetalRenderer::Impl> lockOwner() const
    {
        auto owner=RequireMetalResourceOwner(resourceHealth_,owner_);
        owner->throwPendingCommandFailure();
        return owner;
    }

    std::weak_ptr<MetalRenderer::Impl> owner_;
    std::shared_ptr<MetalResourceHealth> resourceHealth_;
    MetalColorStorageInfo storage_;              // AM4-142
    MTLPixelFormat colorFormat_=MTLPixelFormatBGRA8Unorm;
    int size_;
    bool mipMap_;
    int appliedDepthFormat_;
    int levelCount_=1;
    int appliedSampleCount_=0;   // AM4-141
    id<MTLTexture> colorTexture_=nil;
    id<MTLTexture> depthTexture_=nil;
    mutable std::array<id<MTLTexture>,6> msaaFaces_{};   // AM4-141, allocated per face on first use
    mutable id<MTLTexture> samplingView_=nil;             // AM4-142
};

void MetalRenderer::Impl::activeTargetDepthPlanes(bool& hasDepth, bool& hasStencil) const
{
    int applied=static_cast<int>(Microsoft::Xna::Framework::Graphics::DepthFormat::Depth24Stencil8);
    if(currentRenderTarget) applied=currentRenderTarget->appliedDepthFormat();
    else if(currentRenderTargetCube) applied=currentRenderTargetCube->appliedDepthFormat();
    hasDepth=MetalDepthFormatHasDepth(applied);
    hasStencil=MetalDepthFormatHasStencil(applied);
}

bool MetalRenderer::Impl::resolveActiveAttachments(id<MTLTexture>& colorOut, id<MTLTexture>& resolveOut, id<MTLTexture>& depthOut, NSUInteger& sliceOut, int& sampleCountOut)
{
    sliceOut = 0; resolveOut = nil; sampleCountOut = 1;
    if (currentRenderTarget) {
        // plans/plan_metal.md METAL-104: colorTextureForRenderPass()/resolveTargetForRenderPass() collapse
        // to plain colorTexture()/nil whenever this target doesn't engage MSAA -- this branch's own
        // behavior is byte-identical to before MSAA existed in that (the overwhelmingly common) case.
        colorOut = currentRenderTarget->colorTextureForRenderPass();
        resolveOut = currentRenderTarget->resolveTargetForRenderPass();
        depthOut = currentRenderTarget->depthTextureNative();
        if (resolveOut) sampleCountOut = currentRenderTarget->GetMultiSampleCount();
        return true;
    }
    if (currentRenderTargetCube) {
        // plans/plan_apple_m4.md AM4-141: a multisampled cube renders the face into that face's own
        // 2D multisample texture and resolves into the face (slice) of the cube.
        colorOut = currentRenderTargetCube->colorTextureForFace(currentRenderTargetCubeFace);
        resolveOut = currentRenderTargetCube->resolveTargetForFace();
        depthOut = currentRenderTargetCube->depthTextureNative();
        sliceOut = currentRenderTargetCubeFace;
        if (resolveOut) sampleCountOut = currentRenderTargetCube->GetMultiSampleCount();
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
    // plans/plan_metal.md METAL-104: real backbuffer MSAA -- msaaColorTexture is lazily (re)allocated on
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
        // plans/plan_metal.md METAL-104: same "depth sample count must match its paired color attachment"
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

bool MetalRenderer::Impl::resolveActiveColorAttachments(std::vector<id<MTLTexture>>& colorsOut, std::vector<id<MTLTexture>>& resolvesOut, std::vector<NSUInteger>& slicesOut, id<MTLTexture>& depthOut, int& sampleCountOut)
{
    if (currentMRT.size() >= 2) {
        colorsOut.clear(); resolvesOut.clear(); slicesOut.clear();
        // plans/plan_apple_m4.md AM4-141: a multisampled member renders into its own multisampled
        // texture and resolves into its colour texture, as a single target does; XNA requires one
        // sample count across the set, so slot 0's is the pass's.
        // plans/plan_apple_m4.md AM4-097: a cube member contributes its cube texture at its own face.
        for (const MrtMember& m : currentMRT) {
            colorsOut.push_back(m.target ? m.target->colorTextureForRenderPass() : m.cube->colorTextureForFace(m.face));
            resolvesOut.push_back(m.target ? m.target->resolveTargetForRenderPass() : m.cube->resolveTargetForFace());
            slicesOut.push_back(m.target ? 0 : m.face);
        }
        // plans/plan_metal.md METAL-112: reuses currentMRT[0]'s own depthTextureNative() for the whole
        // MRT render pass rather than allocating a dedicated shared one (unlike VulkanMRTProxy,
        // which must -- Vulkan's explicit VkFramebuffer object needs one concrete depth
        // VkImageView chosen up front; Metal's MTLRenderPassDescriptor has no such constraint, and
        // every MetalRenderTargetRenderer already unconditionally owns its own real
        // Depth32Float_Stencil8 texture, so borrowing target 0's avoids a second depth allocation
        // for every MRT session).
        // AM4-097: slot 0 owns depth, whichever kind it is.
        depthOut = currentMRT[0].target ? currentMRT[0].target->depthTextureNative()
                                        : currentMRT[0].cube->depthTextureNative();
        sampleCountOut = std::max(1,currentMRT[0].target ? currentMRT[0].target->GetMultiSampleCount()
                                                         : currentMRT[0].cube->GetMultiSampleCount());
        return true;
    }
    id<MTLTexture> c=nil, r=nil; NSUInteger slice=0;
    if (!resolveActiveAttachments(c, r, depthOut, slice, sampleCountOut)) return false;
    colorsOut.assign(1, c);
    resolvesOut.assign(1, r);
    slicesOut.assign(1, slice);
    return true;
}

// plans/plan_metal.md METAL-112: the single chokepoint that tears down a real MRT set correctly --
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
    currentRenderTargetCube = nullptr;  // AM4-097: slot 0 may have been a cube face
    activeColorAttachmentCount = 1;
    endActiveEncoding(false);
    for (const MrtMember& m : old) {
        if (m.target) m.target->regenerateMipsIfNeeded();
        else m.cube->regenerateMipsIfNeeded();
    }
}

// plans/plan_apple_m4.md AM4-097: XNA leaves destroying a bound target undefined; CNA's rule is that
// the destroyed member leaves the set and the live peers stay bound, so the unbind that follows
// still finalizes them (their mip chains). A set left with one member keeps it as a set of one.
void MetalRenderer::Impl::detachMrtMember(const void* resource)
{
    const auto dead = [resource](const MrtMember& m) {
        return static_cast<const void*>(m.target) == resource || static_cast<const void*>(m.cube) == resource;
    };
    if (std::none_of(currentMRT.begin(), currentMRT.end(), dead)) return;
    endActiveEncoding(false);
    currentMRT.erase(std::remove_if(currentMRT.begin(), currentMRT.end(), dead), currentMRT.end());
    currentRenderTarget = nullptr;
    currentRenderTargetCube = nullptr;
    if (!currentMRT.empty()) {
        if (currentMRT[0].target) currentRenderTarget = currentMRT[0].target;
        else { currentRenderTargetCube = currentMRT[0].cube; currentRenderTargetCubeFace = currentMRT[0].face; }
    }
    activeColorAttachmentCount = std::max<int>(1, static_cast<int>(currentMRT.size()));
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
    if (auto* mt = dynamic_cast<const MetalTexture*>(t)) return mt->samplingTexture();   // AM4-142
    if (auto* rt = dynamic_cast<const MetalRenderTargetRenderer*>(t)) return rt->samplingTexture();   // AM4-142
    return nil;
}

static id<MTLTexture> nativeCubeTextureFor(const ITextureCubeRenderer* t)
{
    if (!t) return nil;
    if (auto* mt = dynamic_cast<const MetalTextureCube*>(t)) return mt->native();
    if (auto* rt = dynamic_cast<const MetalRenderTargetCubeRenderer*>(t)) return rt->samplingTexture();   // AM4-142
    return nil;
}

static id<MTLTexture> resolveMetal2DTextureBinding(
    MetalRenderer::Impl& owner,
    const ITextureRenderer* captured,
    MetalStockTextureSlot slot,
    bool xnaSampled)
{
    const id<MTLTexture> native=nativeTextureFor(captured);
    switch(DescribeMetalStockTextureBinding(slot,captured!=nullptr,native!=nil))
    {
        case MetalTextureBindingDecision::BindNative:
            return native;
        case MetalTextureBindingDecision::BindNeutralFallback:
        {
            id<MTLTexture> fallback=owner.defaultWhiteTexture;
            switch(MetalNeutralTextureForSlot(slot,xnaSampled))
            {
                case MetalNeutralTextureKind::FlatNormal2D: fallback=owner.defaultFlatNormalTexture; break;
                case MetalNeutralTextureKind::Black2D:      fallback=owner.defaultBlackTexture; break;
                default: break;
            }
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
    MetalStockTextureSlot slot,
    bool xnaSampled)
{
    const id<MTLTexture> native=nativeCubeTextureFor(captured);
    switch(DescribeMetalStockTextureBinding(slot,captured!=nullptr,native!=nil))
    {
        case MetalTextureBindingDecision::BindNative:
            return native;
        case MetalTextureBindingDecision::BindNeutralFallback:
        {
            id<MTLTexture> fallback=MetalNeutralTextureForSlot(slot,xnaSampled)==MetalNeutralTextureKind::BlackCube
                ? owner.defaultBlackCubeTexture : owner.defaultWhiteCubeTexture;
            if(!fallback)
                throw std::runtime_error("Metal: required neutral cube texture is unavailable");
            return fallback;
        }
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
    const MetalAutoreleaseScope autoreleaseScope;
    auto& p=*impl_; p.virtualW=args.virtualWidth; p.virtualH=args.virtualHeight; p.swapInterval=args.swapInterval;
    // plans/plan_metal.md Phase 15: real, previously-invisible bug -- args.presentationMode was never
    // read at all (Impl::presentationMode's own field default, Letterbox=0, silently won this
    // instead), even though GraphicsRendererCreateArgs::presentationMode's own doc comment states
    // its default is FixedHeightDynamicWidth (XNA/Windows-Phone-matching) and the GPU/EasyGL
    // constructors both already forward it correctly. Had zero observable effect before this
    // phase since nothing consumed `presentationMode` yet; matters now that computeLogicalViewport()
    // does.
    p.presentationMode=(int)args.presentationMode;
#if TARGET_OS_OSX
    CNA::Platform::CocoaNativeWindow nativeWindow;
    if(!CNA::Platform::TryGetCocoa(p.surface.GetNativeHandle(),nativeWindow))
        throw std::runtime_error("Metal renderer requires a Cocoa native window");
    NSWindow* cocoaWindow=(NSWindow*)nativeWindow.window;
    NSView* contentView=[cocoaWindow contentView];
    if(!contentView) throw std::runtime_error("Metal renderer requires a Cocoa content view");
#else
    // plans/plan_apple_m4.md AM4-037: SDL's UIWindow hosts its own view controller's view; the
    // Metal view goes on top of it, as the platform layer's own Metal view helper does.
    CNA::Platform::UIKitNativeWindow nativeWindow;
    if(!CNA::Platform::TryGetUIKit(p.surface.GetNativeHandle(),nativeWindow))
        throw std::runtime_error("Metal renderer requires a UIKit native window");
    UIWindow* uikitWindow=(UIWindow*)nativeWindow.window;
    UIView* contentView=uikitWindow.rootViewController ? uikitWindow.rootViewController.view : uikitWindow;
    if(!contentView) throw std::runtime_error("Metal renderer requires a UIKit content view");
#endif
    // MTLCreateSystemDefaultDevice follows the Create ownership convention and returns one owned
    // (+1) reference under MRR. Do not retain it again. Likewise, every `new*` result stored below
    // is already +1; only borrowed factory/getter results that survive their call scope
    // (CAMetalLayer, command buffers/encoders, and drawables) receive an explicit retain.
    p.device=MTLCreateSystemDefaultDevice(); if(!p.device) throw std::runtime_error("Metal: MTLCreateSystemDefaultDevice failed");
    // plans/plan_apple_m4.md AM4-141: MSAA is supported for the counts this device accepts.
    for (const int samples : {2,4,8})
        if ([p.device supportsTextureSampleCount:(NSUInteger)samples])
            p.supportedSampleCountMask|=MetalSampleCountBit(samples);
    p.packed16Formats=[p.device supportsFamily:MTLGPUFamilyApple1];   // AM4-142
    p.deviceSampleCount=std::max(1,MetalAppliedMultiSampleCount(args.multiSampleCount,p.supportedSampleCountMask));
    p.view=[[CNAMetalView alloc] initWithFrame:[contentView bounds]];
    if(!p.view) throw std::runtime_error("Metal: failed to create a layer-backed view");
    [contentView addSubview:p.view];
    const auto drawableSize=p.surface.GetDrawableSize();
    [p.view updateDrawableWidth:drawableSize.width height:drawableSize.height
                   displayScale:p.surface.GetDisplayScale()];
    p.layer=(CAMetalLayer*)p.view.layer;
    if(!p.layer) throw std::runtime_error("Metal: the view did not create a CAMetalLayer");
    [p.layer retain]; p.layer.device=p.device; p.layer.pixelFormat=MTLPixelFormatBGRA8Unorm; p.layer.framebufferOnly=NO;
    // plans/plan_metal.md METAL-168: swapInterval was previously stored but never applied -- CAMetalLayer
    // has no direct integer-interval knob (unlike OpenGL's 0/1/-1 swap interval or Vulkan's
    // present-mode choice, both real per-value behavior elsewhere in this codebase), only the
    // boolean displaySyncEnabled. XNA PresentInterval::Immediate(0) -> NO (uncapped); One(1, the
    // default)/Two(2) both -> YES (real, honest vsync) since Metal has no true half-rate present
    // mode to map Two to -- an approximation, not a silent gap, and documented as such rather than
    // pretending Two behaves differently from One.
#if TARGET_OS_OSX
    p.layer.displaySyncEnabled = (p.swapInterval != 0);
#endif
    p.queue=[p.device newCommandQueue];
    if(!p.queue) throw std::runtime_error("Metal: failed to create MTLCommandQueue");
    NSError* err=nil; NSString* src=[NSString stringWithUTF8String:kMetalShaderSource]; p.library=[p.device newLibraryWithSource:src options:nil error:&err];
    if(!p.library) throw std::runtime_error(std::string("Metal shader compile failed: ")+([[err localizedDescription] UTF8String]?:"unknown"));
    // plans/plan_metal.md METAL-23: pipelines are no longer built eagerly here -- getOrCreatePipeline()
    // lazily builds+caches each (PipelineKind, BlendKey) combination on first use instead.
    MTLSamplerDescriptor* sd=[[MTLSamplerDescriptor alloc]init];
    if(!sd) throw std::runtime_error("Metal: failed to allocate default sampler descriptor");
    sd.minFilter=MTLSamplerMinMagFilterLinear;sd.magFilter=MTLSamplerMinMagFilterLinear;
    sd.sAddressMode=MTLSamplerAddressModeClampToEdge;sd.tAddressMode=MTLSamplerAddressModeClampToEdge;
    p.sampler=[p.device newSamplerStateWithDescriptor:sd];[sd release];
    if(!p.sampler) throw std::runtime_error("Metal: failed to create default sampler state");
    // plans/plan_apple_m4.md AM4-038: one 8-byte counter per occlusion-query slot, attached to every
    // render pass ensureFrame()/clear() build.
    p.visibilityBuffer=[p.device newBufferWithLength:sizeof(std::uint64_t)*MetalRenderer::Impl::kMaxOcclusionQuerySlots
                                             options:MTLResourceStorageModeShared];
    if(!p.visibilityBuffer) throw std::runtime_error("Metal: failed to allocate the occlusion-query visibility buffer");
    // plans/plan_metal.md METAL-87: PbrEffect's 4 optional-map fallback textures (see Impl's own field
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
        const uint8_t black[4]={0,0,0,255};
        p.defaultBlackTexture=[p.device newTextureWithDescriptor:td];
        if(!p.defaultBlackTexture) throw std::runtime_error("Metal: failed to create neutral black texture");
        [p.defaultBlackTexture replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:black bytesPerRow:4];
        p.defaultBlackCubeTexture=[p.device newTextureWithDescriptor:cubeDescriptor];
        if(!p.defaultBlackCubeTexture) throw std::runtime_error("Metal: failed to create neutral black cube texture");
        for(NSUInteger face=0;face<6;++face)
            [p.defaultBlackCubeTexture replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0
                slice:face withBytes:black bytesPerRow:4 bytesPerImage:0];
    }
    p.rebuildDepthState();
}
MetalRenderer::~MetalRenderer()=default;
MetalRenderer::Impl& MetalRenderer::impl(){return *impl_;} const MetalRenderer::Impl& MetalRenderer::impl()const{return *impl_;}
void MetalRenderer::Clear(float r,float g,float b,float a){ const MetalAutoreleaseScope autoreleaseScope; impl_->clear(true,r,g,b,a,false,1,false,0);} void MetalRenderer::Present(){impl_->endFrame();}
void MetalRenderer::GetViewportSize(int&w,int&h){
    const MetalAutoreleaseScope autoreleaseScope;
    // plans/plan_metal.md METAL-156: now routed through the same computeLogicalViewport() the real
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
    const MetalAutoreleaseScope autoreleaseScope;
    impl_->surface.Update(surface);
    const auto drawableSize=impl_->surface.GetDrawableSize();
    [impl_->view updateDrawableWidth:drawableSize.width height:drawableSize.height
                        displayScale:impl_->surface.GetDisplayScale()];
}
void MetalRenderer::GetDefaultViewportRect(int&x,int&y,int&w,int&h)
{
    const MetalAutoreleaseScope autoreleaseScope;
    const auto vp=impl_->computeLogicalViewport();
    x=(int)std::lround(vp.x); y=(int)std::lround(vp.y);
    w=(int)std::lround(vp.width); h=(int)std::lround(vp.height);
}
void MetalRenderer::SetVirtualResolution(int w,int h){impl_->virtualW=w;impl_->virtualH=h;} void MetalRenderer::SetPresentationMode(int m){impl_->presentationMode=m;}
// plans/plan_metal.md METAL-168, same mapping as the constructor. iOS has no displaySyncEnabled: its
// CAMetalLayer always presents on the display's refresh, so only the request is recorded there.
void MetalRenderer::SetSwapInterval(int i)
{
    const MetalAutoreleaseScope autoreleaseScope;
    impl_->swapInterval=i;
#if TARGET_OS_OSX
    impl_->layer.displaySyncEnabled=(i!=0);
#endif
}
// plans/plan_apple_m4.md AM4-141: the backbuffer renders into a multisampled colour and depth
// texture and resolves into the drawable at every encoder boundary (StoreAndMultisampleResolve).
int MetalRenderer::ApplyMultiSampleCount(int requestedMultiSampleCount)
{
    const MetalAutoreleaseScope autoreleaseScope;
    auto& p=*impl_;
    const int applied=MetalAppliedMultiSampleCount(requestedMultiSampleCount,p.supportedSampleCountMask);
    // The textures are reallocated lazily at the new count; an open pass at the old count ends first.
    if(p.encoder||p.command) p.endActiveEncoding(false);
    p.deviceSampleCount=std::max(1,applied);
    [p.msaaColorTexture release]; p.msaaColorTexture=nil;
    [p.depthTexture release]; p.depthTexture=nil;
    return applied;
}
// Public count zero means no MSAA; Metal's native sample-count identity is one.
int MetalRenderer::GetMultiSampleCount() const { return impl_->deviceSampleCount>1 ? impl_->deviceSampleCount : 0; }
int MetalRenderer::GetAppliedMultiSampleCountEXT(int requestedMultiSampleCount) const
{
    return MetalAppliedMultiSampleCount(requestedMultiSampleCount,impl_->supportedSampleCountMask);
}
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
// plans/plan_apple_m4.md AM4-025: backbuffer readback, re-enabled after being disabled as METAL-258
// (clear-colour-only pixels after real draws; the 3D half of that was AM4-028's WVP transpose).
// Coordinates are the GAME's backbuffer coordinates -- IGraphicsRenderer's contract, and what
// EasyGL's ReadBackbuffer samples since KF-6 -- while this renderer draws straight into the
// drawable through the logical viewport, i.e. in drawable pixels, at Retina scale and with any
// letterbox offset. Each requested pixel is read at the drawable pixel under its centre, so
// GetBackBufferData answers in backbuffer pixels in every presentation mode and at every display
// scale (NativeBackBuffer makes the mapping the identity on drawable pixels). The frame's pending
// render pass completes first and the drawable stays retained, so the frame continues on the same
// surface. Pixels outside the drawable read as zero, and a frame with no drawable (minimised
// window) reads all zero, as WebGPU's does.
void MetalRenderer::ReadBackbuffer(int x,int y,int w,int h,uint8_t* pixels)
{
    const MetalAutoreleaseScope autoreleaseScope;
    auto& p=*impl_;
    p.throwPendingCommandFailure();
    if(w<=0||h<=0) return;
    if(!pixels) throw std::invalid_argument("Metal: ReadBackbuffer requires a destination buffer");
    if(p.currentRenderTarget||p.currentRenderTargetCube||!p.currentMRT.empty())
        throw std::runtime_error(
            "Metal: ReadBackbuffer requires the backbuffer to be the active render target");
    const std::size_t outputBytes=static_cast<std::size_t>(w)*static_cast<std::size_t>(h)*4u;
    if(!p.ensureFrame() || !p.drawable.HasValue()) { std::memset(pixels,0,outputBytes); return; }
    id<MTLTexture> source=p.drawable.Get().texture;
    p.finishActiveCommandSynchronously("Metal: backbuffer render command failed before readback");

    const auto viewport=p.computeLogicalViewport();
    const int drawableWidth=static_cast<int>(source.width);
    const int drawableHeight=static_cast<int>(source.height);
    const float scaleX=viewport.logicalWidth>0.0f ? viewport.width/viewport.logicalWidth : 1.0f;
    const float scaleY=viewport.logicalHeight>0.0f ? viewport.height/viewport.logicalHeight : 1.0f;
    auto drawableX=[&](int gameX){ return static_cast<int>(std::floor(viewport.x+(static_cast<float>(gameX)+0.5f)*scaleX)); };
    auto drawableY=[&](int gameY){ return static_cast<int>(std::floor(viewport.y+(static_cast<float>(gameY)+0.5f)*scaleY)); };

    const int left=std::clamp(drawableX(x),0,drawableWidth-1);
    const int right=std::clamp(drawableX(x+w-1),0,drawableWidth-1);
    const int top=std::clamp(drawableY(y),0,drawableHeight-1);
    const int bottom=std::clamp(drawableY(y+h-1),0,drawableHeight-1);
    const int regionWidth=right-left+1;
    const int regionHeight=bottom-top+1;
    std::vector<std::uint8_t> region(static_cast<std::size_t>(regionWidth)*static_cast<std::size_t>(regionHeight)*4u);
    MetalTextureTransferLayout layout{};
    if(!TryPrepareMetalTextureTransfer(
            drawableWidth,drawableHeight,1,1,0,left,top,0,regionWidth,regionHeight,1,
            region.data(),static_cast<int>(region.size()),MetalTransferLengthRule::ExactlyTightBytes,
            MetalMacOsTextureBufferRowAlignment,layout))
        throw std::runtime_error("Metal: ReadBackbuffer could not lay out the drawable region");
    auto owner=impl_;
    blitTextureToClientBuffer(p.device,p.queue,source,0,0,left,top,0,regionWidth,regionHeight,1,
                              layout,MetalTransferPixelOrder::Bgra,region.data(),
                              [owner] { owner->throwPendingCommandFailure(); });

    for(int row=0;row<h;++row)
    {
        const int sy=drawableY(y+row);
        for(int col=0;col<w;++col)
        {
            const int sx=drawableX(x+col);
            std::uint8_t* destination=pixels+(static_cast<std::size_t>(row)*w+col)*4u;
            if(sx<left||sx>right||sy<top||sy>bottom)
            {
                destination[0]=destination[1]=destination[2]=destination[3]=0;
                continue;
            }
            const std::uint8_t* sourcePixel=region.data()+
                (static_cast<std::size_t>(sy-top)*regionWidth+static_cast<std::size_t>(sx-left))*4u;
            std::memcpy(destination,sourcePixel,4);
        }
    }
}
std::unique_ptr<ITextureRenderer> MetalRenderer::CreateTexture(const ImageData& d)
{
    const MetalAutoreleaseScope autoreleaseScope;
    impl_->throwPendingCommandFailure();
    // AM4-142: the format's native storage; the shape rules are Color's, at the format's texel size.
    MetalTextureStorageInfo storage{};
    if(!MetalTextureStorageFor(d.surfaceFormat,impl_->packed16Formats,storage))
        throw System::NotSupportedException("Metal Texture2D does not store this SurfaceFormat.");
    switch(DescribeMetalTexture2DShapePolicy(d.width,d.height,d.mipLevels,d.pixels.size(),
                                             (std::size_t)storage.bytesPerTexel)){
        case MetalTexture2DImagePolicy::Supported: break;
        case MetalTexture2DImagePolicy::UnsupportedFormat:
            throw System::NotSupportedException("Metal Texture2D does not store this SurfaceFormat.");
        case MetalTexture2DImagePolicy::InvalidDimensionsOrMipCount:
            throw std::invalid_argument("Metal Texture2D dimensions or mip count are invalid.");
        case MetalTexture2DImagePolicy::InvalidBaseByteCount:
            throw std::invalid_argument("Metal Texture2D level-zero byte count is invalid.");
    }
    return std::make_unique<MetalTexture>(impl_->device,impl_->queue,d,
        impl_->resourceHealth,makeMetalResourceOwnerHealthCheck(impl_),storage);
}
std::unique_ptr<ISpriteBatchRenderer> MetalRenderer::CreateSpriteBatch(){return std::make_unique<MetalSpriteBatch>(*this);}
std::unique_ptr<ITextureCubeRenderer> MetalRenderer::CreateTextureCube(int size,bool mipMap,int surfaceFormat)
{
    const MetalAutoreleaseScope autoreleaseScope;
    impl_->throwPendingCommandFailure();
    if (!MetalSupportsSurfaceFormat(surfaceFormat))
        throw System::NotSupportedException("Metal TextureCube supports only SurfaceFormat::Color.");
    if(size<=0) throw std::invalid_argument("Metal TextureCube size must be positive.");
    return std::make_unique<MetalTextureCube>(impl_->device,impl_->queue,size,mipMap,
        impl_->resourceHealth,makeMetalResourceOwnerHealthCheck(impl_));
}
// plans/plan_apple_m4.md AM4-077: the SpriteBatch-scoped MSL facility, compiled from the two sources
// the way D3D11 and Vulkan compile theirs. A source that does not compile yields an invalid effect
// (ShaderEffect::IsEffectValid) rather than a throw; 3D draws with one stay refused in drawMetal3D.
std::unique_ptr<IEffectRenderer> MetalRenderer::CreateEffectRenderer(const std::string& vertSrc,const std::string& fragSrc)
{
    const MetalAutoreleaseScope autoreleaseScope;
    impl_->throwPendingCommandFailure();
    auto renderer=std::make_unique<MetalEffectRenderer>(*impl_);
    if(!vertSrc.empty() && !fragSrc.empty()) renderer->CompileProgram(vertSrc,fragSrc);
    return renderer;
}
ShaderDialectEXT MetalRenderer::GetShaderDialectEXT() const
{
    return ShaderDialectEXT::Msl;
}
bool MetalRenderer::SupportsShaderLanguageEXT(int language,int stage) const
{
    return language==static_cast<int>(CNA::ShaderLanguageEXT::Msl) &&
           (stage==static_cast<int>(CNA::ShaderStageEXT::Vertex) ||
            stage==static_cast<int>(CNA::ShaderStageEXT::Fragment));
}
std::unique_ptr<IOcclusionQueryRenderer> MetalRenderer::CreateOcclusionQuery()
{
    const MetalAutoreleaseScope autoreleaseScope;
    impl_->throwPendingCommandFailure();
    return std::make_unique<MetalOcclusionQueryRenderer>(*impl_);
}
std::unique_ptr<ITexture3DRenderer> MetalRenderer::CreateTexture3D(int w,int h,int depth,bool mipMap,int surfaceFormat)
{
    const MetalAutoreleaseScope autoreleaseScope;
    impl_->throwPendingCommandFailure();
    if (!MetalSupportsSurfaceFormat(surfaceFormat))
        throw System::NotSupportedException("Metal Texture3D supports only SurfaceFormat::Color.");
    if(w<=0||h<=0||depth<=0) throw std::invalid_argument("Metal Texture3D dimensions must be positive.");
    return std::make_unique<MetalTexture3D>(impl_->device,impl_->queue,w,h,depth,mipMap,
        impl_->resourceHealth,makeMetalResourceOwnerHealthCheck(impl_));
}
// Preserve/discard behavior remains owned by the shared GraphicsDevice binding path. Requested
// multisampling is deliberately clamped to zero at this boundary.
std::unique_ptr<IRenderTargetRenderer> MetalRenderer::CreateRenderTarget2D(int w,int h,int depthFormat,bool /*preserveContents*/,bool mipMap,int multiSampleCount)
{
    const MetalAutoreleaseScope autoreleaseScope;
    impl_->throwPendingCommandFailure();
    if(w<=0||h<=0) throw std::invalid_argument("Metal RenderTarget2D dimensions must be positive.");
    return std::make_unique<MetalRenderTargetRenderer>(impl_, w, h, depthFormat, mipMap,
        MetalAppliedMultiSampleCount(multiSampleCount,impl_->supportedSampleCountMask));   // AM4-141
}
std::unique_ptr<IRenderTargetRenderer> MetalRenderer::CreateRenderTarget2DEXT(
    int w,int h,int depthFormat,bool /*preserveContents*/,bool mipMap,int multiSampleCount,int surfaceFormat)
{
    const MetalAutoreleaseScope autoreleaseScope;
    impl_->throwPendingCommandFailure();
    MetalColorStorageInfo storage{};
    if (!MetalRenderTargetStorageFor(surfaceFormat,storage))   // AM4-142
        throw System::NotSupportedException("Metal does not render into this RenderTarget2D SurfaceFormat.");
    if(w<=0||h<=0) throw std::invalid_argument("Metal RenderTarget2D dimensions must be positive.");
    return std::make_unique<MetalRenderTargetRenderer>(impl_, w, h, depthFormat, mipMap,
        MetalAppliedMultiSampleCount(multiSampleCount,impl_->supportedSampleCountMask), storage);
}
std::unique_ptr<IRenderTargetCubeRenderer> MetalRenderer::CreateRenderTargetCubeEXT(
    int size,int depthFormat,bool /*preserveContents*/,bool mipMap,int multiSampleCount,int surfaceFormat)
{
    const MetalAutoreleaseScope autoreleaseScope;
    impl_->throwPendingCommandFailure();
    MetalColorStorageInfo storage{};
    if (!MetalRenderTargetStorageFor(surfaceFormat,storage))   // AM4-142
        throw System::NotSupportedException("Metal does not render into this RenderTargetCube SurfaceFormat.");
    if(size<=0) throw std::invalid_argument("Metal RenderTargetCube size must be positive.");
    return std::make_unique<MetalRenderTargetCubeRenderer>(impl_, size, depthFormat, mipMap,
        MetalAppliedMultiSampleCount(multiSampleCount,impl_->supportedSampleCountMask), storage);
}
RendererFormatVerdict MetalRenderer::ClassifyRenderTargetFormatEXT(int surfaceFormat) const
{
    MetalColorStorageInfo storage{};
    return MetalRenderTargetStorageFor(surfaceFormat,storage) ? RendererFormatVerdict::Supported
                                                              : RendererFormatVerdict::Unsupported;
}
RendererFormatVerdict MetalRenderer::ClassifySurfaceFormatEXT(int surfaceFormat) const
{
    MetalTextureStorageInfo storage{};
    return MetalTextureStorageFor(surfaceFormat,impl_->packed16Formats,storage) ? RendererFormatVerdict::Supported
                                                                                : RendererFormatVerdict::Defer;
}
RendererFormatVerdict MetalRenderer::ClassifyTextureCubeFormatEXT(int /*surfaceFormat*/) const
{
    // TextureCube storage is still RGBA8 here; the framework's Color-only rule answers it.
    return RendererFormatVerdict::Defer;
}
bool MetalRenderer::SupportsHalfFloatTextureLinearFilteringEXT() const
{
    return true;   // AM4-142: every Metal GPU filters 16-bit float formats
}
std::unique_ptr<IRenderTargetCubeRenderer> MetalRenderer::CreateRenderTargetCube(
    int size,int depthFormat,bool /*preserveContents*/,bool mipMap,int multiSampleCount)
{
    const MetalAutoreleaseScope autoreleaseScope;
    impl_->throwPendingCommandFailure();
    if(size<=0) throw std::invalid_argument("Metal RenderTargetCube size must be positive.");
    return std::make_unique<MetalRenderTargetCubeRenderer>(impl_, size, depthFormat, mipMap,
        MetalAppliedMultiSampleCount(multiSampleCount,impl_->supportedSampleCountMask));   // AM4-141
}
// plans/plan_metal.md METAL-109/110/111 (real bug fixed alongside RenderTargetCube's own addition): both
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
    const MetalAutoreleaseScope autoreleaseScope;
    auto* metalRt=dynamic_cast<MetalRenderTargetRenderer*>(rt);
    if (rt && !metalRt) throw std::runtime_error("Metal: foreign RenderTarget2D renderer");
    auto& p=*impl_;
    p.unbindCurrentMRT(); // plans/plan_metal.md METAL-112: tear down any active MRT set first, always.
    if (p.currentRenderTarget && p.currentRenderTarget != rt) p.currentRenderTarget->UnbindAsRenderTarget();
    if (p.currentRenderTargetCube) p.currentRenderTargetCube->UnbindAsRenderTarget();
    if (metalRt) metalRt->BindAsRenderTarget();
}
void MetalRenderer::SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt,int face)
{
    const MetalAutoreleaseScope autoreleaseScope;
    if (!rt) { SetRenderTarget2D(nullptr); return; }
    if (face<0 || face>=6) throw std::out_of_range("Metal: RenderTargetCube face must be in [0, 5]");
    auto* metalRt=dynamic_cast<MetalRenderTargetCubeRenderer*>(rt);
    if (!metalRt) throw std::runtime_error("Metal: foreign RenderTargetCube renderer");
    auto& p=*impl_;
    p.unbindCurrentMRT(); // plans/plan_metal.md METAL-112: same as SetRenderTarget2D() above.
    if (p.currentRenderTarget) p.currentRenderTarget->UnbindAsRenderTarget();
    if (p.currentRenderTargetCube && p.currentRenderTargetCube != rt) p.currentRenderTargetCube->UnbindAsRenderTarget();
    metalRt->BindAsRenderTargetFace(face);
}
void MetalRenderer::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,int count)
{
    const MetalAutoreleaseScope autoreleaseScope;
    if (count<0) throw std::invalid_argument("Metal: render-target count cannot be negative");
    if (count==0) { SetRenderTarget2D(nullptr); return; }
    if (!renderTargets) throw std::invalid_argument("Metal: render-target descriptors cannot be null");
    if (count>1) {
        // plans/plan_apple_m4.md AM4-097: METAL-112's MRT, re-enabled with macOS evidence and widened
        // to RenderTargetCube faces. Stock shaders write COLOR0 only; attachments 1..N-1 keep what
        // they hold (their pipelines mask them out), as XNA's stock effects leave them.
        if (count>8)
            throw System::NotSupportedException("Metal binds at most eight render targets at once.");
        std::vector<Impl::MrtMember> members;
        members.reserve(static_cast<std::size_t>(count));
        for (int i=0;i<count;++i) {
            const auto& binding=renderTargets[i];
            Impl::MrtMember member;
            if (binding.IsRenderTarget2D()) {
                if (binding.GetArraySlice()!=0)
                    throw System::NotSupportedException("Metal RenderTarget2D array slices are not supported.");
                if (!binding.GetRenderTarget2D())
                    throw std::invalid_argument("Metal: RenderTarget2D descriptor has no target");
                member.target=dynamic_cast<MetalRenderTargetRenderer*>(binding.GetRenderTarget2D());
                if (!member.target) throw std::runtime_error("Metal: foreign RenderTarget2D renderer");
            } else if (binding.IsRenderTargetCubeFace()) {
                if (!binding.GetRenderTargetCube())
                    throw std::invalid_argument("Metal: RenderTargetCube descriptor has no target");
                member.cube=dynamic_cast<MetalRenderTargetCubeRenderer*>(binding.GetRenderTargetCube());
                if (!member.cube) throw std::runtime_error("Metal: foreign RenderTargetCube renderer");
                const int face=binding.GetCubeFace();
                if (face<0 || face>=6) throw std::out_of_range("Metal: RenderTargetCube face must be in [0, 5]");
                member.face=static_cast<NSUInteger>(face);
            } else {
                throw std::invalid_argument("Metal: unknown render-target descriptor type");
            }
            members.push_back(member);
        }
        auto& p=*impl_;
        SetRenderTarget2D(nullptr);  // finalizes whatever was bound: a single target, a face or a set
        p.currentMRT=std::move(members);
        if (p.currentMRT[0].target) p.currentRenderTarget=p.currentMRT[0].target;
        else { p.currentRenderTargetCube=p.currentMRT[0].cube; p.currentRenderTargetCubeFace=p.currentMRT[0].face; }
        p.activeColorAttachmentCount=count;
        p.endActiveEncoding(false);
        return;
    }

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
void MetalRenderer::ClearColorAndDepth(float r,float g,float b,float a,float d){ const MetalAutoreleaseScope autoreleaseScope; impl_->clear(true,r,g,b,a,true,d,false,0);} void MetalRenderer::ClearDepth(float d){impl_->clear(false,0,0,0,0,true,d,false,0);} void MetalRenderer::ClearStencil(int s){impl_->clear(false,0,0,0,0,false,1,true,s);} void MetalRenderer::ClearDepthAndStencil(float d,int s){impl_->clear(false,0,0,0,0,true,d,true,s);} void MetalRenderer::ClearColorAndStencil(float r,float g,float b,float a,int s){impl_->clear(true,r,g,b,a,false,1,true,s);} void MetalRenderer::ClearColorDepthAndStencil(float r,float g,float b,float a,float d,int s){impl_->clear(true,r,g,b,a,true,d,true,s);}
void MetalRenderer::SetDepthTestEnabled(bool e)
{
    const MetalAutoreleaseScope autoreleaseScope;
    auto& p=*impl_;
    const auto previous=p.captureDepthState();
    p.depthEnabled=e;
    try { p.rebuildDepthState(); }
    catch (...) { p.restoreDepthState(previous); throw; }
}
void MetalRenderer::SetBlendEnabled(bool e){ const MetalAutoreleaseScope autoreleaseScope; impl_->currentBlend=MetalBlendKeyForSetBlendEnabled(e);}
void MetalRenderer::SetDepthWriteEnabled(bool e)
{
    const MetalAutoreleaseScope autoreleaseScope;
    auto& p=*impl_;
    const auto previous=p.captureDepthState();
    p.depthWrite=e;
    try { p.rebuildDepthState(); }
    catch (...) { p.restoreDepthState(previous); throw; }
}
void MetalRenderer::ApplyBlendState(int colorSrcBlend,int alphaSrcBlend,int colorDstBlend,int alphaDstBlend,int colorBlendFunc,int alphaBlendFunc,const BlendWriteState& writeState)
{
    const MetalAutoreleaseScope autoreleaseScope;
    if (!MetalSupportsBlendWriteState(writeState))
        throw System::NotSupportedException(
            "Metal: a ColorWriteChannels value outside Red|Green|Blue|Alpha.");
    // plans/plan_metal.md METAL-6/24: real per-BlendState pipeline selection, replacing the previous
    // complete no-op (every pipeline was hardcoded to a fixed straight-alpha blend regardless of
    // the actual requested BlendState). `enabled` derivation mirrors
    // EasyGLRenderer::ApplyBlendState's identical Blend::One/Blend::Zero Opaque-preset
    // check exactly (Blend::One=0, Blend::Zero=1).
    auto& p=*impl_;
    p.currentBlend.colorSrc=(uint8_t)colorSrcBlend; p.currentBlend.colorDst=(uint8_t)colorDstBlend;
    p.currentBlend.alphaSrc=(uint8_t)alphaSrcBlend; p.currentBlend.alphaDst=(uint8_t)alphaDstBlend;
    p.currentBlend.colorFunc=(uint8_t)colorBlendFunc; p.currentBlend.alphaFunc=(uint8_t)alphaBlendFunc;
    p.currentBlend.enabled = !(colorSrcBlend==0 && colorDstBlend==1 && alphaSrcBlend==0 && alphaDstBlend==1);
    p.currentBlend.writeMask=(uint8_t)writeState.colorWriteChannels[0];   // AM4-079
    p.currentBlend.writeMask1=(uint8_t)writeState.colorWriteChannels[1];  // AM4-146
    p.currentBlend.writeMask2=(uint8_t)writeState.colorWriteChannels[2];
    p.currentBlend.writeMask3=(uint8_t)writeState.colorWriteChannels[3];
    p.sampleMask=writeState.multiSampleMask;                               // AM4-141
}
void MetalRenderer::ApplyDepthStencilState(bool depthEnable,bool depthWriteEnable,int depthFunc,
                                                   bool stencilEnable,int stencilFunc,int stencilPass,int stencilFail,int stencilDepthFail,
                                                   int stencilMask,int stencilWriteMask,int referenceStencil,
                                                   bool twoSidedStencilMode,int ccwStencilFunc,int ccwStencilPass,int ccwStencilFail,int ccwStencilDepthFail)
{
    const MetalAutoreleaseScope autoreleaseScope;
    // plans/plan_metal.md METAL-7/9/10: real depthFunc + full front/back stencil-op wiring, replacing
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
// plans/plan_metal.md METAL-19: the `c==1?...:(c==2?...:...)` ternary chain this replaced switched on raw
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
    const MetalAutoreleaseScope autoreleaseScope;
    impl_->cull=metalCullMode(c);
    impl_->fill=f==1?MTLTriangleFillModeLines:MTLTriangleFillModeFill;
    impl_->rasterState.SetScissorEnabled(se);
    impl_->depthBias=MetalDepthBiasUnits(db);   // AM4-134
    impl_->slopeBias=sb;
    if(impl_->encoder){
        [impl_->encoder setFrontFacingWinding:MTLWindingClockwise];
        [impl_->encoder setCullMode:impl_->cull];
        [impl_->encoder setTriangleFillMode:impl_->fill];
        [impl_->encoder setDepthBias:impl_->depthBias slopeScale:sb clamp:0];
        const MetalScissorState s=impl_->rasterState.NativeScissor();
        const MTLScissorRect native={(NSUInteger)s.x,(NSUInteger)s.y,
            (NSUInteger)s.width,(NSUInteger)s.height};
        [impl_->encoder setScissorRect:native];
    }
}
void MetalRenderer::ApplyRasterizerMultiSampleState(bool enabled)
{
    const MetalAutoreleaseScope autoreleaseScope;
    auto& p=*impl_;
    p.multisampleRasterization=enabled;
    // AM4-141: sample positions belong to the pass, so a multisampled pass built for the other
    // setting ends here and the next draw starts one with the positions this setting needs.
    const bool wantCentre=!enabled&&p.activeSampleCount>1&&p.device.programmableSamplePositionsSupported;
    if(p.encoder&&p.activeSampleCount>1&&wantCentre!=p.encoderCentreSamples)
        p.endActiveEncoding(false);
}
void MetalRenderer::ApplySamplerState(int slot,int filter,int addressU,int addressV,int maxAnisotropy)
{
    const MetalAutoreleaseScope autoreleaseScope;
    if (slot<0 || slot>=16) throw std::out_of_range("Metal: sampler slot must be in [0, 15]");
    auto& request=impl_->samplerSlotRequests[slot];
    request.filter=filter; request.addressU=addressU; request.addressV=addressV; request.maxAnisotropy=maxAnisotropy;
    impl_->samplerSlots[slot]=impl_->samplerFor(filter,addressU,addressV,maxAnisotropy,
                                                request.maxMipLevel,request.lodBias);
}
void MetalRenderer::ApplySamplerMipState(int slot,int maxMipLevel,float lodBias)
{
    const MetalAutoreleaseScope autoreleaseScope;
    if (slot<0 || slot>=16) throw std::out_of_range("Metal: sampler slot must be in [0, 15]");
    auto& request=impl_->samplerSlotRequests[slot];
    impl_->samplerSlots[slot]=impl_->samplerFor(request.filter,request.addressU,request.addressV,
                                                request.maxAnisotropy,maxMipLevel,lodBias);
    request.maxMipLevel=maxMipLevel; request.lodBias=lodBias;
}
void MetalRenderer::SetBlendFactor(float r,float g,float b,float a){ const MetalAutoreleaseScope autoreleaseScope; impl_->blendColor[0]=r;impl_->blendColor[1]=g;impl_->blendColor[2]=b;impl_->blendColor[3]=a;if(impl_->encoder)[impl_->encoder setBlendColorRed:r green:g blue:b alpha:a];}
void MetalRenderer::SetReferenceStencil(int v){ const MetalAutoreleaseScope autoreleaseScope; impl_->refStencil=v;if(impl_->encoder)[impl_->encoder setStencilReferenceValue:v];}
void MetalRenderer::SetScissorRect(int x,int y,int w,int h)
{
    const MetalAutoreleaseScope autoreleaseScope;
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
    const MetalAutoreleaseScope autoreleaseScope;
    impl_->rasterState.SetViewport(x,y,w,h,mn,mx);
    if(impl_->encoder){
        const MetalViewportState v=impl_->rasterState.EffectiveViewport();
        const MTLViewport native={v.x,v.y,v.width,v.height,v.minDepth,v.maxDepth};
        [impl_->encoder setViewport:native];
    }
}
std::unique_ptr<IVertexBufferRenderer> MetalRenderer::CreateVertexBuffer(int c){ const MetalAutoreleaseScope autoreleaseScope; return std::make_unique<MetalVertexBuffer>(impl_->device,c);} std::unique_ptr<IIndexBufferRenderer> MetalRenderer::CreateIndexBuffer16(int){return std::make_unique<MetalIndexBuffer>(impl_->device,false);} std::unique_ptr<IIndexBufferRenderer> MetalRenderer::CreateIndexBuffer32(int){return std::make_unique<MetalIndexBuffer>(impl_->device,true);}

// plans/plan_metal.md METAL-34-style extraction: this dispatch logic's real body now lives in the
// plain-C++ MetalSelectPipelineKind.hpp (no Objective-C, buildable and unit-tested on any platform
// without an Apple toolchain) -- kept as a thin same-signature wrapper here so the existing call
// site in drawMetal3D() is unaffected.
static PipelineKind selectPipelineKind(std::size_t stride, const GpuDrawParams* params)
{
    return SelectMetalPipelineKind(stride, params);
}

// plans/plan_metal.md METAL-34-style extraction: fillLitUniforms/fillEnvUniforms/fillSkinnedUniforms/
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

// plans/plan_apple_m4.md AM4-029: XNA 4.0 rasterizes with Direct3D 9's pixel centres, so the
// exact screen-space triangle (x,y),(x+1,y),(x,y+1) of the Primitives sample covers one pixel;
// Metal samples pixel centres half a pixel further in and drops it. The same clip-space
// translation EasyGL, Vulkan and WebGPU apply (xnaPixelCenterScale_, 63/128 of a pixel: just under
// half so the centre stays on the covered side of the fill edge), for the same reasons limited to
// filled topologies and to single-sampled render targets -- see
// VulkanRenderer::XnaPixelCenterCorrectionEXT. The pixel is the XNA backbuffer's: on a HiDPI
// drawable the backbuffer is presented at several physical pixels per logical one, and shifting
// by half a PHYSICAL pixel would leave the logical pixel's centre uncovered.
static Matrix xnaPixelCenterCorrection(const MetalRenderer::Impl& p,PrimitiveType pt)
{
    if(pt!=PrimitiveType::TriangleList&&pt!=PrimitiveType::TriangleStrip) return Matrix::getIdentityProperty();
    const bool targetBound=p.currentRenderTarget||p.currentRenderTargetCube||!p.currentMRT.empty();
    if(targetBound&&p.activeSampleCount>1) return Matrix::getIdentityProperty();
    const MetalViewportState viewport=p.rasterState.EffectiveViewport();
    if(viewport.width<=0.0||viewport.height<=0.0) return Matrix::getIdentityProperty();
    double physicalPerLogicalX=1.0;
    double physicalPerLogicalY=1.0;
    if(!targetBound){
        const auto presentation=p.computeLogicalViewport();
        if(presentation.logicalWidth>0.0f&&presentation.logicalHeight>0.0f){
            physicalPerLogicalX=presentation.width/presentation.logicalWidth;
            physicalPerLogicalY=presentation.height/presentation.logicalHeight;
        }
    }
    constexpr double kXnaPixelCenterScale=63.0/64.0;
    // Metal's NDC y points up and its framebuffer rows run down, as in GL: +x/-y moves the
    // geometry right and down on screen, which is EasyGL's unchanged sign.
    return Matrix::CreateTranslation(
        static_cast<float>(kXnaPixelCenterScale*physicalPerLogicalX/viewport.width),
        static_cast<float>(-kXnaPixelCenterScale*physicalPerLogicalY/viewport.height),
        0.0f);
}

static void drawMetal3D(MetalRenderer::Impl& p,const MetalVertexBuffer& vb,const MetalIndexBuffer* ib,const Matrix&w,const Matrix&v,const Matrix&pr,PrimitiveType pt,int pc,const GpuDrawParams* params,
                       bool instancedRoute=false)
{
    // plans/plan_apple_m4.md AM4-028: uploaded in XNA's own M11..M44 order, like `world`
    // (GpuDrawParams::worldColMajor). MSL reads a float4x4 column by column, so the shader's
    // `wvp * position` then multiplies by the transpose -- which IS XNA's row-vector product
    // `position * W * V * P`. The transpose this used to apply on top put XNA's translation row
    // into w (w = 1 - x for an orthographic BasicEffect), clipping every 3D vertex: the
    // "clear colour only" readback of METAL-258 was 3D geometry that never rasterised.
    Mat4 wvp=multiply(multiply(fromXna(w),fromXna(v)),fromXna(pr));
    const std::size_t drawStride=params ? CombinedVertexStrideOr(*params,vb.stride()) : vb.stride();
    // plans/plan_apple_m4.md AM4-035: a declared buffer selects its pipeline by the channels it
    // carries and binds them where it declared them; an undeclared one is the canonical XNA vertex
    // type its stride names, as before. This replaces the declaration-fidelity refusal (METAL-263),
    // which existed because only the canonical layouts could be bound.
    // plans/plan_apple_m4.md AM4-143: several per-vertex streams, or any per-instance one, read
    // every stream's own declaration (BuildMetalStreamVertexInput); one stream keeps its route.
    // The instanced route folds no VertexOffset into baseVertex, so even its one stream binds at
    // its own offset.
    const bool streamRoute = params && params->vertexStreamCount > 0 &&
        (instancedRoute || HasMultipleVertexStreams(*params) || InstanceStreamCount(*params) > 0);
    // The stream route reads its own copy of the stream list: the shared layer describes a buffer
    // without a declaration as a zero-stride stream, whose stride only its upload knows.
    std::optional<GpuDrawParams> streamParams;
    MetalStreamDeclarations streamDeclarations{};
    std::array<const MetalVertexBuffer*, kMaxVertexStreams> streamBuffers{};
    std::vector<VertexElement> canonicalStreamElements;
    std::vector<VertexElement> combinedElements;
    int undeclaredStream=-1;
    std::size_t selectionStride=0;
    if (streamRoute) {
        GpuDrawParams& sp=streamParams.emplace(*params);
        int combinedBase=0;
        for (int i=0; i<sp.vertexStreamCount; ++i) {
            auto& stream=sp.vertexStreams[(std::size_t)i];
            const auto* buffer=dynamic_cast<const MetalVertexBuffer*>(stream.buffer);
            if (!buffer) throw std::runtime_error("Metal: foreign vertex buffer in a vertex stream");
            streamBuffers[(std::size_t)i]=buffer;
            if (stream.strideInBytes==0) stream.strideInBytes=static_cast<int>(buffer->stride());
            if (stream.instanceFrequency==0) {
                stream.combinedByteBase=combinedBase;
                combinedBase+=stream.strideInBytes;
            }
            if (!buffer->declaration().IsEmpty()) {
                streamDeclarations[(std::size_t)i]=&buffer->declaration().GetElements();
                continue;
            }
            // A buffer with no declaration is the canonical vertex its upload stride names, as it
            // is on the one-stream route; nothing names its channels to tell them apart from
            // another stream's, or to lay them out as instance data.
            if (stream.instanceFrequency>0 || PerVertexStreamCount(sp)>1)
                throw System::NotSupportedException(
                    "Metal: every per-instance vertex stream, and every stream of a multi-stream draw, "
                    "needs a VertexDeclaration.");
            undeclaredStream=i;
        }
        sp.combinedVertexStride=combinedBase;
        if (undeclaredStream>=0) {
            selectionStride=static_cast<std::size_t>(combinedBase);
            if (!sp.pbr) sp.vertexColorEnabled=false;   // AM4-081, as canonicalParams below
        }
    }
    const auto& declared=vb.declaration();
    if (streamRoute && undeclaredStream<0) {
        combinedElements=MetalCombinedPerVertexElements(*streamParams, streamDeclarations);
        selectionStride=MetalSelectionStrideForDeclaration(combinedElements, params);
    } else if (!streamRoute) {
        selectionStride = declared.IsEmpty() ? drawStride
            : MetalSelectionStrideForDeclaration(declared.GetElements(), params);
    }
    const PipelineKind kind = selectPipelineKind(selectionStride, params);
    if (streamRoute && undeclaredStream>=0) {
        canonicalStreamElements=MetalCanonicalElementsFor(kind, selectionStride);
        streamDeclarations[(std::size_t)undeclaredStream]=&canonicalStreamElements;
    }
    // plans/plan_apple_m4.md AM4-081: a buffer with no declaration is the canonical XNA type its stride
    // names, none of which a lit draw takes colour from, so its colour is the constant white, as the
    // lit functions read none before. The PBR records are the exception (AM4-084): stride 60 and 80
    // carry glTF's COLOR_0, which the effect's own switch gates.
    std::optional<GpuDrawParams> canonicalParams;
    if (!streamRoute && declared.IsEmpty() && params && !params->pbr) {
        canonicalParams.emplace(*params);
        canonicalParams->vertexColorEnabled=false;
    }
    const MetalDeclaredVertexInput vertexInput = streamRoute
        ? BuildMetalStreamVertexInput(kind, *streamParams, streamDeclarations)
        : BuildMetalDeclaredVertexInput(
            kind, declared.IsEmpty() ? MetalCanonicalElementsFor(kind, drawStride) : declared.GetElements(),
            static_cast<int>(drawStride), canonicalParams ? &*canonicalParams : params);
    if(!vertexInput.IsComplete())
        throw System::NotSupportedException("Metal: this VertexDeclaration cannot feed the selected stock effect: " +
                                            vertexInput.refusal + ".");
    if(streamRoute ? undeclaredStream<0 : !declared.IsEmpty()){
        const std::string dropped=MetalDroppedVertexColorRefusal(
            kind, streamRoute ? combinedElements : declared.GetElements(), params);
        if(!dropped.empty()) throw System::NotSupportedException("Metal: " + dropped + ".");
    }

    id<MTLTexture> texture0=nil;
    id<MTLTexture> texture1=nil;
    id<MTLTexture> environmentCube=nil;
    id<MTLTexture> normalMap=nil;
    id<MTLTexture> metallicRoughnessMap=nil;
    id<MTLTexture> emissiveMap=nil;
    id<MTLTexture> occlusionMap=nil;
    id<MTLTexture> specularMap=nil;
    id<MTLTexture> specularColorMap=nil;
    switch(kind)
    {
        case PipelineKind::Textured20:
        case PipelineKind::ColorTex24:
        case PipelineKind::LitTex32:
        case PipelineKind::LitTex32VertexLit:
            texture0=resolveMetal2DTextureBinding(
                p,params->texture0,MetalStockTextureSlot::BasicDiffuse,params->textureEnabled);
            break;
        case PipelineKind::DualTex20:
        case PipelineKind::DualTex24Colored:
            texture0=resolveMetal2DTextureBinding(
                p,params->texture0,MetalStockTextureSlot::DualFirst,params->textureEnabled);
            texture1=resolveMetal2DTextureBinding(
                p,params->texture1,MetalStockTextureSlot::DualSecond,params->textureEnabled);
            break;
        case PipelineKind::EnvMap32:
            texture0=resolveMetal2DTextureBinding(
                p,params->texture0,MetalStockTextureSlot::EnvironmentDiffuse,params->textureEnabled);
            environmentCube=resolveMetalCubeTextureBinding(
                p,params->envMap,MetalStockTextureSlot::EnvironmentCube,params->textureEnabled);
            break;
        case PipelineKind::Skinned52:
        case PipelineKind::Skinned56:
        case PipelineKind::Skinned52VertexLit:
        case PipelineKind::Skinned56VertexLit:
            texture0=resolveMetal2DTextureBinding(
                p,params->texture0,MetalStockTextureSlot::SkinnedDiffuse,params->textureEnabled);
            break;
        case PipelineKind::Pbr48:
        case PipelineKind::SkinnedPbr68:
            texture0=resolveMetal2DTextureBinding(
                p,params->texture0,MetalStockTextureSlot::PbrBaseColor,params->textureEnabled);
            normalMap=resolveMetal2DTextureBinding(
                p,params->pbrNormalMap,MetalStockTextureSlot::PbrNormal,params->textureEnabled);
            metallicRoughnessMap=resolveMetal2DTextureBinding(
                p,params->pbrMetallicRoughnessMap,MetalStockTextureSlot::PbrMetallicRoughness,params->textureEnabled);
            emissiveMap=resolveMetal2DTextureBinding(
                p,params->pbrEmissiveMap,MetalStockTextureSlot::PbrEmissive,params->textureEnabled);
            occlusionMap=resolveMetal2DTextureBinding(
                p,params->pbrOcclusionMap,MetalStockTextureSlot::PbrOcclusion,params->textureEnabled);
            // plans/plan_apple_m4.md AM4-085: KHR_materials_specular's maps, white when absent -- the
            // identity of both products, so a factor-only material shades exactly as before.
            specularMap=resolveMetal2DTextureBinding(
                p,params->pbrSpecularMap,MetalStockTextureSlot::PbrSpecular,params->textureEnabled);
            specularColorMap=resolveMetal2DTextureBinding(
                p,params->pbrSpecularColorMap,MetalStockTextureSlot::PbrSpecularColor,params->textureEnabled);
            break;
        case PipelineKind::Colored16:
        case PipelineKind::Sprite2D:
            break;
    }

    if(!p.ensureFrame()||p.rasterState.ShouldSkipDraw()||!p.admitSampleMask()) return;
    // After ensureFrame(): the correction depends on the pass's sample count.
    wvp=multiply(wvp,fromXna(xnaPixelCenterCorrection(p,pt)));
    id<MTLRenderPipelineState> pipeline = p.getOrCreatePipeline(kind, &vertexInput);
    [p.encoder setRenderPipelineState:pipeline];
    if (!streamRoute) {
        [p.encoder setVertexBuffer:vb.native() offset:0 atIndex:0];
    } else {
        // AM4-143: each stream that supplies an attribute, at its whole VertexOffset -- the shared
        // layer folds only a common base into vertexStart/baseVertex, which advance the per-vertex
        // streams alone; a per-instance stream is addressed by instance index.
        for (int i=0; i<streamParams->vertexStreamCount; ++i) {
            const auto& stream=streamParams->vertexStreams[(std::size_t)i];
            const int bufferIndex=MetalVertexStreamBufferIndex(stream.slot);
            bool bound=false;
            for (const auto& layout : vertexInput.layouts) bound = bound || layout.bufferIndex==bufferIndex;
            if (!bound) continue;
            [p.encoder setVertexBuffer:streamBuffers[(std::size_t)i]->native()
                                offset:(NSUInteger)stream.vertexOffset*(NSUInteger)stream.strideInBytes
                               atIndex:(NSUInteger)bufferIndex];
        }
    }
    p.bindSampleMask();   // AM4-141
    if (vertexInput.UsesConstantAttributes())
        [p.encoder setVertexBytes:kMetalConstantAttributeBlock length:sizeof(kMetalConstantAttributeBlock)
                          atIndex:kMetalConstantAttributeBufferIndex];
    [p.encoder setDepthStencilState:p.depthState]; [p.encoder setFrontFacingWinding:MTLWindingClockwise]; [p.encoder setCullMode:p.cull]; [p.encoder setTriangleFillMode:p.fill];

    if (kind == PipelineKind::LitTex32 || kind == PipelineKind::LitTex32VertexLit) {
        // plans/plan_metal.md METAL-38-47/39: real per-pixel (LitTex32) or per-vertex/Gouraud
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
        // plans/plan_metal.md METAL-64/66-68: real cube-map reflection/Fresnel path.
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
        // plans/plan_metal.md METAL-72-80/76: real skinned lit/fog/specular/emissive path, either
        // per-pixel (Skinned52/56) or per-vertex/Gouraud (Skinned52/56VertexLit, XNA's real
        // PreferPerPixelLighting=false default) -- both share the identical SkinnedTransform/
        // SkinnedUniforms uniform layout, bone buffer, and texture binding, only the vertex/
        // fragment shader pair (selected via `kind` by getOrCreatePipeline()) differs.
        SkinnedTransform t{}; SkinnedUniforms su{};
        fillSkinnedUniforms(t, su, wvp, *params);
        [p.encoder setVertexBytes:&t length:sizeof(t) atIndex:1];
        [p.encoder setVertexBytes:&su length:sizeof(su) atIndex:2];
        [p.encoder setFragmentBytes:&su length:sizeof(su) atIndex:2];
        // plans/plan_metal.md METAL-73: 72 bones x 4x4 = 4608 floats (18KB) exceeds setVertexBytes:'s
        // 4KB inline limit -- must be a real MTLBuffer, unlike every other uniform in this file.
        // Reallocated fresh each draw (newBufferWithBytes:), matching MetalVertexBuffer::SetData's
        // own established "always reallocate, never mutate in place" pattern (same
        // command-buffer-resource-lifetime assumption already relied on throughout this file, not
        // a new risk category -- see plans/plan_metal.md Phase 18's own still-open resource-lifetime
        // audit). GpuDrawParams::boneTransforms is already column-major (its own doc comment),
        // matching worldColMajor's convention, so no per-bone transpose is needed before upload.
        id<MTLBuffer> bonesBuf = [p.device newBufferWithBytes:params->boneTransforms length:sizeof(float)*72*16 options:MTLResourceStorageModeShared];
        if(!bonesBuf) throw std::runtime_error("Metal: failed to create skinned bone buffer");
        [p.encoder setVertexBuffer:bonesBuf offset:0 atIndex:3];
        [bonesBuf release];
        [p.encoder setFragmentTexture:texture0 atIndex:0];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[0]?p.samplerSlots[0]:p.sampler) atIndex:0];
    } else if (kind == PipelineKind::Pbr48) {
        // plans/plan_metal.md METAL-81/83-86: real metallic-roughness PBR path.
        PbrTransform t{}; PbrUniforms pu{};
        fillPbrUniforms(t, pu, wvp, *params);
        [p.encoder setVertexBytes:&t length:sizeof(t) atIndex:1];
        [p.encoder setVertexBytes:&pu length:sizeof(pu) atIndex:2];
        [p.encoder setFragmentBytes:&pu length:sizeof(pu) atIndex:2];
        // plans/plan_metal.md METAL-3: each of the 5 PBR texture units gets its own SamplerState slot
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
        [p.encoder setFragmentTexture:specularMap atIndex:5];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[5]?p.samplerSlots[5]:p.sampler) atIndex:5];
        [p.encoder setFragmentTexture:specularColorMap atIndex:6];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[6]?p.samplerSlots[6]:p.sampler) atIndex:6];
    } else if (kind == PipelineKind::SkinnedPbr68) {
        // plans/plan_metal.md METAL-82: real SkinnedPbrEffect path -- same bone-buffer handling as
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
        // plans/plan_metal.md METAL-3: same per-slot sampler fix as Pbr48 above, see its own comment.
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
        [p.encoder setFragmentTexture:specularMap atIndex:5];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[5]?p.samplerSlots[5]:p.sampler) atIndex:5];
        [p.encoder setFragmentTexture:specularColorMap atIndex:6];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[6]?p.samplerSlots[6]:p.sampler) atIndex:6];
    } else {
        // plans/plan_metal.md METAL-35/36/37/51-63: DiffuseColor/VertexColorEnabled/AlphaTest now
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
            mp.fogColor[0]=params->fogColor[0]; mp.fogColor[1]=params->fogColor[1];
            mp.fogColor[2]=params->fogColor[2]; mp.fogColor[3]=0.0f;
        } else {
            mp.diffuseColor[0]=mp.diffuseColor[1]=mp.diffuseColor[2]=mp.diffuseColor[3]=1.0f;
            mp.alphaTest[0]=0.0f; mp.alphaTest[1]=0.0f; mp.alphaTest[2]=1.0f; mp.alphaTest[3]=1.0f;
            mp.flags[0]=1.0f; mp.flags[1]=mp.flags[2]=mp.flags[3]=0.0f;
            mp.fogColor[0]=mp.fogColor[1]=mp.fogColor[2]=mp.fogColor[3]=0.0f;
        }
        // AM4-137: zero when fog is off (GpuDrawParams.fogVector), so the keep factor is 1.
        float fogVector[4]={0.0f,0.0f,0.0f,0.0f};
        if (params) std::memcpy(fogVector, params->fogVector, sizeof(fogVector));
        [p.encoder setVertexBytes:&wvp length:sizeof(wvp) atIndex:1];
        [p.encoder setVertexBytes:fogVector length:sizeof(fogVector) atIndex:2];
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
    // AM4-143: DrawInstancedPrimitives' count (1 for every other draw).
    const NSUInteger instanceCount=static_cast<NSUInteger>(params?params->instanceCount:1);
    const NSUInteger firstInstance=static_cast<NSUInteger>(params?params->firstInstance:0);
    // plans/plan_metal.md: real bug found and fixed 2026-07-20 -- every other renderer (EasyGL/Vulkan/
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
            instanceCount:instanceCount baseVertex:baseVertex baseInstance:firstInstance];
    } else {
        const NSUInteger vertexStart=static_cast<NSUInteger>(params?params->vertexStart:0);
        [p.encoder drawPrimitives:metalPrimitive(pt) vertexStart:vertexStart vertexCount:n
                    instanceCount:instanceCount baseInstance:firstInstance];
    }
}
void MetalRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& v,const Matrix& w,
                                                  const Matrix& vi,const Matrix& p,
                                                  PrimitiveType pt,int pc)
{
    const MetalAutoreleaseScope autoreleaseScope;
    const auto* vb=dynamic_cast<const MetalVertexBuffer*>(&v);
    if(!vb) throw std::runtime_error("Metal: foreign vertex buffer");
    drawMetal3D(*impl_,*vb,nullptr,w,vi,p,pt,pc,nullptr);
}

void MetalRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& v,
                                                         const IIndexBufferRenderer& i,
                                                         const Matrix& w,const Matrix& vi,
                                                         const Matrix& p,PrimitiveType pt,int pc)
{
    const MetalAutoreleaseScope autoreleaseScope;
    const auto* vb=dynamic_cast<const MetalVertexBuffer*>(&v);
    const auto* ib=dynamic_cast<const MetalIndexBuffer*>(&i);
    if(!vb||!ib) throw std::runtime_error("Metal: foreign buffer");
    drawMetal3D(*impl_,*vb,ib,w,vi,p,pt,pc,nullptr);
}
#if defined(CNA_METAL_COMPILED_EFFECTS)
// ---------------------------------------------------------------------------------------------
// plans/plan_apple_m4.md AM4-144: compiled XNA effects. MetalCompiledEffect links the bound pass
// against the draw's declarations and translates it to MSL; this is where it becomes a pipeline,
// bound resources and a draw.
// ---------------------------------------------------------------------------------------------
namespace
{
    MTLVertexFormat compiledVertexFormat(VertexElementFormat format)
    {
        switch (format)
        {
            case VertexElementFormat::Single:           return MTLVertexFormatFloat;
            case VertexElementFormat::Vector2:          return MTLVertexFormatFloat2;
            case VertexElementFormat::Vector3:          return MTLVertexFormatFloat3;
            case VertexElementFormat::Vector4:          return MTLVertexFormatFloat4;
            // The linked SPIR-V reads COLOR as normalized bytes and swizzles them itself
            // (MOJOSHADER_VERTEXELEMENTFORMAT_COLOR), as on WebGPU's Unorm8x4.
            case VertexElementFormat::Color:            return MTLVertexFormatUChar4Normalized;
            case VertexElementFormat::Byte4:            return MTLVertexFormatUChar4;
            case VertexElementFormat::Short2:           return MTLVertexFormatShort2;
            case VertexElementFormat::Short4:           return MTLVertexFormatShort4;
            case VertexElementFormat::NormalizedShort2: return MTLVertexFormatShort2Normalized;
            case VertexElementFormat::NormalizedShort4: return MTLVertexFormatShort4Normalized;
            case VertexElementFormat::HalfVector2:      return MTLVertexFormatHalf2;
            case VertexElementFormat::HalfVector4:      return MTLVertexFormatHalf4;
        }
        return MTLVertexFormatFloat4;
    }

    /// One stream a compiled draw binds: the buffer, its stride, where the draw's records begin,
    /// and its instance step rate (0 per vertex).
    struct MetalCompiledSource
    {
        const MetalVertexBuffer* buffer = nullptr;
        std::uint32_t stride = 0;
        std::size_t byteOffset = 0;
        int instanceFrequency = 0;
    };

    /// The native texture of a public texture, if it is of the kind the shader declares.
    id<MTLTexture> compiledNativeTexture(Microsoft::Xna::Framework::Graphics::Texture* texture,
                                         MetalCompiledTextureKind kind)
    {
        using namespace Microsoft::Xna::Framework::Graphics;
        if (texture == nullptr) return nil;
        switch (kind)
        {
            case MetalCompiledTextureKind::TextureCube:
                if (auto* cube = dynamic_cast<TextureCube*>(texture)) return nativeCubeTextureFor(&cube->GetRenderer());
                return nil;
            case MetalCompiledTextureKind::Texture3D:
                if (auto* volume = dynamic_cast<Texture3D*>(texture))
                    if (auto* metal = dynamic_cast<const MetalTexture3D*>(&volume->GetRenderer())) return metal->native();
                return nil;
            case MetalCompiledTextureKind::Texture2D:
                if (dynamic_cast<TextureCube*>(texture) || dynamic_cast<Texture3D*>(texture)) return nil;
                if (auto* flat = dynamic_cast<Texture2D*>(texture)) return nativeTextureFor(&flat->GetRenderer());
                return nil;
        }
        return nil;
    }

    /// What an unbound compiled sampler reads: opaque black, XNA's measured unbound-texture value
    /// (plans/plan_graphics_shared_cleanup.md GSC-0004), in the declared kind.
    id<MTLTexture> compiledNeutralTexture(MetalRenderer::Impl& p, MetalCompiledTextureKind kind)
    {
        switch (kind)
        {
            case MetalCompiledTextureKind::TextureCube: return p.defaultBlackCubeTexture;
            case MetalCompiledTextureKind::Texture2D:   return p.defaultBlackTexture;
            case MetalCompiledTextureKind::Texture3D:
                if (!p.defaultBlackVolumeTexture)
                {
                    MTLTextureDescriptor* d=[[MTLTextureDescriptor alloc] init];
                    d.textureType=MTLTextureType3D; d.pixelFormat=MTLPixelFormatRGBA8Unorm;
                    d.width=1; d.height=1; d.depth=1; d.usage=MTLTextureUsageShaderRead;
                    p.defaultBlackVolumeTexture=[p.device newTextureWithDescriptor:d];
                    [d release];
                    if (!p.defaultBlackVolumeTexture)
                        throw std::runtime_error("Metal: failed to create the neutral black volume texture");
                    const std::uint8_t black[4]={0,0,0,255};
                    [p.defaultBlackVolumeTexture replaceRegion:MTLRegionMake3D(0,0,0,1,1,1) mipmapLevel:0 slice:0
                                                      withBytes:black bytesPerRow:4 bytesPerImage:4];
                }
                return p.defaultBlackVolumeTexture;
        }
        return p.defaultBlackTexture;
    }

    /// One translated stage as a Metal function, compiled once per linked body.
    id<MTLFunction> compiledFunction(MetalRenderer::Impl& p, const MetalCompiledStageEXT& stage)
    {
        if (const auto it=p.compiledFunctions.find(stage.hash); it!=p.compiledFunctions.end())
            return it->second;
        MTLCompileOptions* options=[[MTLCompileOptions alloc] init];
        // XNA's shaders are IEEE arithmetic; fast math would reorder and flush what the reference
        // pixels depend on.
        if (@available(macOS 15.0, iOS 18.0, *)) options.mathMode=MTLMathModeSafe;
        NSString* source=[[NSString alloc] initWithBytes:stage.msl.data() length:stage.msl.size()
                                               encoding:NSUTF8StringEncoding];
        NSError* error=nil;
        id<MTLLibrary> library=[p.device newLibraryWithSource:source options:options error:&error];
        [source release]; [options release];
        if (!library)
            throw std::runtime_error(std::string("Metal: a compiled effect's MSL did not compile: ")+
                                     ([[error localizedDescription] UTF8String]?:"unknown"));
        NSString* name=[NSString stringWithUTF8String:stage.entryPoint.c_str()];
        id<MTLFunction> function=[library newFunctionWithName:name];
        [library release];
        if (!function) throw std::runtime_error("Metal: a compiled effect's MSL has no entry point "+stage.entryPoint);
        p.compiledFunctions.emplace(stage.hash, function);
        return function;
    }

    /// The pipeline of a linked pass for the active targets and blend state.
    id<MTLRenderPipelineState> compiledPipeline(MetalRenderer::Impl& p,
                                                const MetalCompiledEffect::LinkedPassEXT& linked,
                                                const std::vector<MetalCompiledSource>& sources)
    {
        std::uint64_t layout=linked.pipelineKey;
        for (const auto& source : sources) layout=layout*1099511628211ull ^ static_cast<std::uint64_t>(source.instanceFrequency);
        const std::uint8_t colorCount=(std::uint8_t)std::clamp(p.activeColorAttachmentCount,1,8);
        const std::uint8_t sampleCount=(std::uint8_t)std::clamp(p.activeSampleCount,1,8);
        MetalPipelineCacheKey key{PipelineKind::Sprite2D, p.currentBlend, colorCount, sampleCount, layout, false};
        for (std::size_t i=0;i<key.colorFormats.size();++i) key.colorFormats[i]=(uint16_t)p.activeColorFormats[i];
        if (const auto it=p.compiledPipelines.find(key); it!=p.compiledPipelines.end()) return it->second;

        MTLVertexDescriptor* vd=[MTLVertexDescriptor vertexDescriptor];
        std::array<bool,kMaxVertexStreams> used{};
        for (const auto& attribute : linked.attributes)
        {
            const NSUInteger buffer=(NSUInteger)MetalVertexStreamBufferIndex((int)attribute.streamIndex);
            vd.attributes[attribute.location].format=compiledVertexFormat(attribute.format);
            vd.attributes[attribute.location].offset=attribute.offset;
            vd.attributes[attribute.location].bufferIndex=buffer;
            used[attribute.streamIndex]=true;
        }
        for (std::size_t i=0;i<sources.size();++i)
        {
            if (!used[i]) continue;
            const NSUInteger buffer=(NSUInteger)MetalVertexStreamBufferIndex((int)i);
            vd.layouts[buffer].stride=sources[i].stride;
            if (sources[i].instanceFrequency>0) {
                vd.layouts[buffer].stepFunction=MTLVertexStepFunctionPerInstance;
                vd.layouts[buffer].stepRate=(NSUInteger)sources[i].instanceFrequency;
            }
        }
        MTLRenderPipelineDescriptor* d=[[MTLRenderPipelineDescriptor alloc] init];
        d.vertexFunction=compiledFunction(p,*linked.vertex);
        d.fragmentFunction=compiledFunction(p,*linked.pixel);
        d.vertexDescriptor=vd;
        d.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8; d.stencilAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8;
        d.rasterSampleCount=sampleCount;
        const MetalBlendKey& blend=p.currentBlend;
        for (int i=0;i<colorCount;++i) {
            d.colorAttachments[i].pixelFormat=p.activeColorFormats[(std::size_t)i];
            // A pixel shader writes the targets it declares (COLOR0..COLOR3); the others keep their
            // contents. XNA's one BlendState blends every target it writes.
            if ((linked.pixelColorOutputs & (1u<<i))==0) { d.colorAttachments[i].writeMask=MTLColorWriteMaskNone; continue; }
            // XNA's ColorWriteChannels0..3; a fifth to eighth target (CNA's larger MRT sets) is unmasked.
            const uint8_t channels=i==0 ? blend.writeMask : i==1 ? blend.writeMask1
                                 : i==2 ? blend.writeMask2 : i==3 ? blend.writeMask3 : (uint8_t)15;
            d.colorAttachments[i].writeMask=(MTLColorWriteMask)MetalColorWriteMaskBits(channels);
            d.colorAttachments[i].blendingEnabled=blend.enabled?YES:NO;
            if (blend.enabled) {
                d.colorAttachments[i].sourceRGBBlendFactor=metalBlendFactor(blend.colorSrc);
                d.colorAttachments[i].destinationRGBBlendFactor=metalBlendFactor(blend.colorDst);
                d.colorAttachments[i].rgbBlendOperation=metalBlendOp(blend.colorFunc);
                d.colorAttachments[i].sourceAlphaBlendFactor=metalBlendFactor(blend.alphaSrc);
                d.colorAttachments[i].destinationAlphaBlendFactor=metalBlendFactor(blend.alphaDst);
                d.colorAttachments[i].alphaBlendOperation=metalBlendOp(blend.alphaFunc);
            }
        }
        NSError* error=nil;
        id<MTLRenderPipelineState> pipeline=[p.device newRenderPipelineStateWithDescriptor:d error:&error];
        [d release];
        if (!pipeline)
            throw std::runtime_error(std::string("Metal: a compiled effect's pipeline failed: ")+
                                     ([[error localizedDescription] UTF8String]?:"unknown"));
        p.compiledPipelines.emplace(key,pipeline);
        return pipeline;
    }

    void bindCompiledUniforms(MetalRenderer::Impl& p, bool vertexStage, const std::vector<std::uint8_t>& bytes,
                              std::uint32_t index)
    {
        static const std::uint8_t kEmpty[16]={};
        const void* data=bytes.empty() ? kEmpty : bytes.data();
        const NSUInteger length=bytes.empty() ? sizeof(kEmpty) : bytes.size();
        if (length<=4096) {
            if (vertexStage) [p.encoder setVertexBytes:data length:length atIndex:index];
            else [p.encoder setFragmentBytes:data length:length atIndex:index];
            return;
        }
        id<MTLBuffer> buffer=[p.device newBufferWithBytes:data length:length options:MTLResourceStorageModeShared];
        if (!buffer) throw std::runtime_error("Metal: failed to allocate a compiled effect's uniform buffer");
        if (vertexStage) [p.encoder setVertexBuffer:buffer offset:0 atIndex:index];
        else [p.encoder setFragmentBuffer:buffer offset:0 atIndex:index];
        [buffer release];
    }
}

/// One draw through a compiled effect. @p spriteTexture0, when given, is SpriteBatch's texture: XNA
/// assigns it to Textures[0] after the pass is applied, so it wins over the pass's own slot 0.
static void drawMetalCompiled(MetalRenderer::Impl& p, const MetalVertexBuffer& vb, const MetalIndexBuffer* ib,
                              PrimitiveType pt, int pc, const GpuDrawParams& params, bool instancedRoute,
                              id<MTLTexture> spriteTexture0=nil, id<MTLSamplerState> spriteSampler0=nil)
{
    using namespace Microsoft::Xna::Framework::Graphics;
    auto* runtime=dynamic_cast<MetalCompiledEffect*>(params.compiledEffectRuntime);
    if (!runtime) throw std::runtime_error("Metal: the compiled effect belongs to another renderer");

    // Every stream the draw bound, at its whole VertexOffset (see drawMetal3D's stream route); the
    // internal routes that bind no public buffer draw the named one.
    std::vector<MetalCompiledSource> sources;
    for (int i=0;i<params.vertexStreamCount;++i) {
        const auto& stream=params.vertexStreams[(std::size_t)i];
        const auto* buffer=dynamic_cast<const MetalVertexBuffer*>(stream.buffer);
        if (!buffer) throw std::runtime_error("Metal: foreign vertex buffer in a vertex stream");
        MetalCompiledSource source;
        source.buffer=buffer;
        source.stride=stream.strideInBytes>0 ? (std::uint32_t)stream.strideInBytes : (std::uint32_t)buffer->stride();
        source.byteOffset=(std::size_t)std::max(stream.vertexOffset,0)*source.stride;
        source.instanceFrequency=stream.instanceFrequency;
        sources.push_back(source);
    }
    if (sources.empty()) sources.push_back(MetalCompiledSource{&vb,(std::uint32_t)vb.stride(),0,0});
    std::vector<MetalCompiledEffect::CompiledVertexStreamEXT> declared;
    declared.reserve(sources.size());
    for (const auto& source : sources) {
        if (source.buffer->declaration().IsEmpty())
            throw System::NotSupportedException(
                "Metal: a compiled effect draw needs every bound vertex buffer's VertexDeclaration, and one "
                "of this draw's streams carries none.");
        declared.push_back({&source.buffer->declaration().GetElements(), source.stride, source.instanceFrequency>0});
    }
    const MetalCompiledEffect::LinkedPassEXT linked=runtime->LinkAndGetShadersEXT(declared);
    std::vector<std::uint8_t> vertexUniforms, pixelUniforms;
    runtime->CaptureUniformSnapshotEXT(vertexUniforms, pixelUniforms);

    // Each sampler the pixel shader declares. GraphicsDevice's texture and sampler slots are what
    // it samples: EffectPass.Apply() published the pass's assignments there and the game may have
    // replaced a slot since -- Direct3D 9 device state, which XNA keeps, and EasyGL's reading. The
    // runtime's own record of the pass is used only where a draw carries no device slots.
    struct BoundSampler { std::uint32_t slot; id<MTLTexture> texture; id<MTLSamplerState> sampler; };
    std::vector<BoundSampler> samplers;
    for (const auto& binding : linked.pixelSamplers) {
        Texture* texture=nullptr; SamplerState state; bool assigned=false;
        runtime->GetBoundSamplerEXT(binding.slot,false,texture,state,&assigned);
        if (params.compiledDeviceTextures!=nullptr && binding.slot<16u)
            texture=(*params.compiledDeviceTextures)[(int)binding.slot];
        const bool deviceSampler=params.compiledDeviceSamplerStates!=nullptr && binding.slot<16u;
        if (deviceSampler)
            state=(*params.compiledDeviceSamplerStates)[(int)binding.slot];
        id<MTLTexture> native=compiledNativeTexture(texture,binding.kind);
        id<MTLSamplerState> sampler=nil;
        if (binding.slot==0 && binding.kind==MetalCompiledTextureKind::Texture2D) {
            if (spriteTexture0) native=spriteTexture0;
            else if (params.compiledSpriteTexture0) native=nativeTextureFor(params.compiledSpriteTexture0);
            if (spriteSampler0 && !deviceSampler && !assigned) sampler=spriteSampler0;
        }
        if (!native) native=compiledNeutralTexture(p,binding.kind);
        if (!sampler)
            sampler=p.samplerFor((int)state.getFilterProperty(),(int)state.getAddressUProperty(),
                                 (int)state.getAddressVProperty(),state.getMaxAnisotropyProperty(),
                                 state.getMaxMipLevelProperty(),state.getMipMapLevelOfDetailBiasProperty(),
                                 (int)state.getAddressWProperty());
        samplers.push_back({binding.slot,native,sampler});
    }

    if(!p.ensureFrame()||p.rasterState.ShouldSkipDraw()||!p.admitSampleMask()) return;
    if (p.sampleMaskOutput)
        throw System::NotSupportedException(
            "Metal: a compiled effect's MSL is not given a [[sample_mask]] output, so a BlendState."
            "MultiSampleMask that keeps only some samples of a multisampled target is refused for it (AM4-141).");
    id<MTLRenderPipelineState> pipeline=compiledPipeline(p,linked,sources);
    [p.encoder setRenderPipelineState:pipeline];
    std::array<bool,kMaxVertexStreams> used{};
    for (const auto& attribute : linked.attributes) used[attribute.streamIndex]=true;
    for (std::size_t i=0;i<sources.size();++i)
        if (used[i])
            [p.encoder setVertexBuffer:sources[i].buffer->native() offset:sources[i].byteOffset
                               atIndex:(NSUInteger)MetalVertexStreamBufferIndex((int)i)];
    if (linked.vertexHasUniforms) bindCompiledUniforms(p,true,vertexUniforms,kMetalCompiledVertexUniformBuffer);
    if (linked.pixelHasUniforms) bindCompiledUniforms(p,false,pixelUniforms,kMetalCompiledPixelUniformBuffer);
    for (const auto& sampler : samplers) {
        [p.encoder setFragmentTexture:sampler.texture atIndex:sampler.slot];
        [p.encoder setFragmentSamplerState:sampler.sampler atIndex:sampler.slot];
    }
    [p.encoder setDepthStencilState:p.depthState]; [p.encoder setFrontFacingWinding:MTLWindingClockwise];
    [p.encoder setCullMode:p.cull]; [p.encoder setTriangleFillMode:p.fill];

    // XNA's Direct3D 9 pixel centres. The stock route folds this translation into its matrix; a
    // compiled shader computes its own position, so the viewport moves instead -- by the same
    // 63/128 of a logical pixel, under the same conditions (xnaPixelCenterCorrection).
    const Matrix centre=xnaPixelCenterCorrection(p,pt);
    const MetalViewportState viewport=p.rasterState.EffectiveViewport();
    const double shiftX=(double)centre.M41*viewport.width*0.5;
    const double shiftY=-(double)centre.M42*viewport.height*0.5;
    const bool shifted=shiftX!=0.0||shiftY!=0.0;
    if (shifted) {
        const MTLViewport moved={viewport.x+shiftX,viewport.y+shiftY,viewport.width,viewport.height,viewport.minDepth,viewport.maxDepth};
        [p.encoder setViewport:moved];
    }
    const NSUInteger n=(NSUInteger)primitiveVertexCount(pt,pc);
    const NSUInteger instances=(NSUInteger)(instancedRoute ? std::max(params.instanceCount,1) : 1);
    if (ib) {
        const NSUInteger indexSize=ib->IsThirtyTwoBit()?4:2;
        [p.encoder drawIndexedPrimitives:metalPrimitive(pt) indexCount:n
            indexType:ib->IsThirtyTwoBit()?MTLIndexTypeUInt32:MTLIndexTypeUInt16
            indexBuffer:ib->native() indexBufferOffset:(NSUInteger)params.startIndex*indexSize
            instanceCount:instances baseVertex:(NSInteger)params.baseVertex baseInstance:(NSUInteger)params.firstInstance];
    } else {
        [p.encoder drawPrimitives:metalPrimitive(pt) vertexStart:(NSUInteger)params.vertexStart vertexCount:n
                    instanceCount:instances baseInstance:(NSUInteger)params.firstInstance];
    }
    if (shifted) {
        const MTLViewport restored={viewport.x,viewport.y,viewport.width,viewport.height,viewport.minDepth,viewport.maxDepth};
        [p.encoder setViewport:restored];
    }
}

/// SpriteBatch's compiled route (MetalSpriteBatch::flushCompiled): one same-texture run through
/// every pass of the effect's current technique.
static void drawMetalCompiledSprites(MetalRenderer::Impl& p, const MetalVertexBuffer& vb, int vertexCount,
                                     const GpuDrawParams& params, id<MTLTexture> texture, id<MTLSamplerState> sampler)
{
    drawMetalCompiled(p,vb,nullptr,PrimitiveType::TriangleList,vertexCount/3,params,false,texture,sampler);
}

std::unique_ptr<ICompiledEffectRuntime> MetalRenderer::CreateCompiledEffect(
    const std::uint8_t* effectCode, std::size_t effectCodeBytes)
{
    return std::make_unique<MetalCompiledEffect>(*this, effectCode, effectCodeBytes);
}

bool MetalRenderer::SupportsCompiledEffects() const { return true; }

MetalMojoShaderContextEXT* MetalRenderer::GetMojoShaderContextEXT()
{
    if (!impl_->mojoShaderContext) impl_->mojoShaderContext=std::make_unique<MetalMojoShaderContextEXT>();
    return impl_->mojoShaderContext.get();
}

bool MetalRenderer::OwnsSampleableTextureEXT(Microsoft::Xna::Framework::Graphics::Texture* texture) const
{
    using namespace Microsoft::Xna::Framework::Graphics;
    if (texture==nullptr) return false;
    if (auto* cube=dynamic_cast<TextureCube*>(texture)) return nativeCubeTextureFor(&cube->GetRenderer())!=nil;
    if (auto* volume=dynamic_cast<Texture3D*>(texture)) return dynamic_cast<const MetalTexture3D*>(&volume->GetRenderer())!=nullptr;
    if (auto* flat=dynamic_cast<Texture2D*>(texture)) return nativeTextureFor(&flat->GetRenderer())!=nil;
    return false;
}
#endif  // CNA_METAL_COMPILED_EFFECTS

static void ValidateMetalDrawParams(const GpuDrawParams& gp,const MetalVertexBuffer& vb)
{
    switch (DescribeMetalDrawStreamPolicy(gp)) {
        case MetalDrawStreamPolicy::Supported: break;
        case MetalDrawStreamPolicy::InvalidBinding:
            throw std::invalid_argument("Metal: invalid or internally inconsistent vertex stream metadata");
    }
    if (!MetalStreamZeroMatchesUploadedBuffer(gp,vb,vb.stride()))
        throw std::invalid_argument(
            "Metal: GpuDrawParams stream 0 must name the draw vertex buffer and match its uploaded stride");
    if (gp.customEffectRenderer)
        throw System::NotSupportedException(
            "Metal custom effects are a SpriteBatch facility (docs/metal-shader-effect-contract.md); "
            "3D draws with one are not supported.");
}
void MetalRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& v,const Matrix& w,
                                             const Matrix& vi,const Matrix& p,
                                             PrimitiveType pt,int pc,const GpuDrawParams& gp)
{
    const MetalAutoreleaseScope autoreleaseScope;
    const auto* vb=dynamic_cast<const MetalVertexBuffer*>(&v);
    if(!vb) throw std::runtime_error("Metal: foreign vertex buffer");
    ValidateMetalDrawParams(gp,*vb);
#if defined(CNA_METAL_COMPILED_EFFECTS)
    if(gp.compiledEffectRuntime){ drawMetalCompiled(*impl_,*vb,nullptr,pt,pc,gp,false); return; }   // AM4-144
#endif
    drawMetal3D(*impl_,*vb,nullptr,w,vi,p,pt,pc,&gp);
}

void MetalRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& v,
                                                    const IIndexBufferRenderer& i,
                                                    const Matrix& w,const Matrix& vi,
                                                    const Matrix& p,PrimitiveType pt,int pc,
                                                    const GpuDrawParams& gp)
{
    const MetalAutoreleaseScope autoreleaseScope;
    const auto* vb=dynamic_cast<const MetalVertexBuffer*>(&v);
    const auto* ib=dynamic_cast<const MetalIndexBuffer*>(&i);
    if(!vb||!ib) throw std::runtime_error("Metal: foreign buffer");
    ValidateMetalDrawParams(gp,*vb);
#if defined(CNA_METAL_COMPILED_EFFECTS)
    if(gp.compiledEffectRuntime){ drawMetalCompiled(*impl_,*vb,ib,pt,pc,gp,false); return; }   // AM4-144
#endif
    drawMetal3D(*impl_,*vb,ib,w,vi,p,pt,pc,&gp);
}
// plans/plan_apple_m4.md AM4-143: the stock effects' instanced draw -- every stream bound at its own
// VertexOffset, the per-instance ones stepping per InstanceFrequency instances and supplying the
// per-instance world matrix (BuildMetalStreamVertexInput).
void MetalRenderer::DrawInstancedPrimitivesEx(const IVertexBufferRenderer& v,
                                                      const IIndexBufferRenderer& i,
                                                      const Matrix& w,const Matrix& vi,
                                                      const Matrix& p,PrimitiveType pt,int pc,
                                                      int instanceCount,const GpuDrawParams& gp)
{
    const MetalAutoreleaseScope autoreleaseScope;
    const auto* vb=dynamic_cast<const MetalVertexBuffer*>(&v);
    const auto* ib=dynamic_cast<const MetalIndexBuffer*>(&i);
    if(!vb||!ib) throw std::runtime_error("Metal: foreign buffer");
    if(instanceCount!=gp.instanceCount)
        throw std::invalid_argument("Metal: the instance count disagrees with GpuDrawParams.instanceCount");
    ValidateMetalDrawParams(gp,*vb);
#if defined(CNA_METAL_COMPILED_EFFECTS)
    if(gp.compiledEffectRuntime){ drawMetalCompiled(*impl_,*vb,ib,pt,pc,gp,true); return; }   // AM4-144
#endif
    drawMetal3D(*impl_,*vb,ib,w,vi,p,pt,pc,&gp,true);
}
void MetalRenderer::SetStringMarkerEXT(const char* m)
{
    const MetalAutoreleaseScope autoreleaseScope;
    if(!impl_->ensureFrame()) return;
    if(m) [impl_->encoder insertDebugSignpost:[NSString stringWithUTF8String:m]];
}

bool MetalRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
{
    // AM4-141: whether this device multisamples is the device's answer, not the static table's.
    if (capability==CNA::GraphicsCapability::MultiSampleAntiAliasing)
        return impl_->supportedSampleCountMask!=0;
    return MetalSupportsCapability(capability);
}

}

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_METAL
// plans/plan_runtimerenderer.md design decision 4: declared in this family's own
// namespace so several renderer archives can link into one binary, then defined
// below with a qualified name -- the body keeps its place unchanged.
namespace Metal { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

std::unique_ptr<IGraphicsRenderer> Metal::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args){return std::make_unique<Metal::MetalRenderer>(args);}
#endif
}
#else
#error "CNA Metal renderer must be compiled on an Apple platform"
#endif
