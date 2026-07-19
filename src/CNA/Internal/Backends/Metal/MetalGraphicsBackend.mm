#include "CNA/Internal/Backends/Metal/MetalGraphicsBackend.hpp"

#ifdef __APPLE__
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Backends::Metal
{
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
vertex V3Out cna_v3d_normaltex(V3NormalTexIn in [[stage_in]], constant U3D& u [[buffer(1)]]) {
    V3Out o; o.position=u.wvp*float4(in.position,1.0); o.color=float4(1.0); o.uv=in.uv; return o;
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

struct V2In { float2 position; float2 uv; float4 color; };
struct U2D { float2 viewport; };
struct V2Out { float4 position [[position]]; float2 uv; float4 color; };
vertex V2Out cna_v2d(uint vid [[vertex_id]], const device V2In* v [[buffer(0)]], constant U2D& u [[buffer(1)]]) {
    V2In i=v[vid]; V2Out o;
    float2 ndc=float2(i.position.x/u.viewport.x*2.0-1.0, 1.0-i.position.y/u.viewport.y*2.0);
    o.position=float4(ndc,0.0,1.0); o.uv=i.uv; o.color=i.color; return o;
}
fragment float4 cna_f2d(V2Out in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {
    return tex.sample(smp, in.uv) * in.color;
}
)MSL";

    static int primitiveVertexCount(PrimitiveType p, int count)
    {
        using PT = PrimitiveType;
        switch (p) {
            case PT::TriangleList: return count * 3;
            case PT::TriangleStrip: return count + 2;
            case PT::LineList: return count * 2;
            case PT::LineStrip: return count + 1;
            case PT::PointListEXT: return count; // plan_metal.md METAL-13: was falling to the *3 default
            default: return count * 3;
        }
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

    // plan_metal.md Phase 2 (simplified for a first, hardware-unverified pass -- a fully generic
    // VertexDeclaration-driven descriptor builder, METAL-27, stays open; this is a fixed-variant
    // enum, one entry per concrete shader+vertex-layout combination this file actually emits,
    // exactly mirroring the "one Prog3D per Ensure*Program()" shape EasyGLGraphicsBackend already
    // uses -- lower risk to get right without a compiler than inventing a hashed-VertexElement-list
    // key blind).
    enum class PipelineKind : uint8_t
    {
        Colored16, Textured20, ColorTex24, NormalTex32, DualTex20, DualTex24Colored, Sprite2D
    };

    // Metal bakes blend factors/operations into MTLRenderPipelineState (unlike depth/stencil/
    // cull/fill, which are genuine dynamic encoder state already handled elsewhere in this file)
    // -- so a real per-BlendState pipeline cache needs blend as part of its key. Defaults below
    // match Blend::One=0/Blend::Zero=1/BlendFunction::Add=0 for both channels, i.e. BlendState.
    // Opaque's own real values -- the correct answer for "no ApplyBlendState call happened yet"
    // (matches GraphicsDevice's own real XNA default BlendState).
    struct BlendKey
    {
        uint8_t colorSrc=0, colorDst=1, alphaSrc=0, alphaDst=1, colorFunc=0, alphaFunc=0;
        bool enabled=false;
        bool operator==(const BlendKey& o) const
        {
            return colorSrc==o.colorSrc && colorDst==o.colorDst && alphaSrc==o.alphaSrc &&
                   alphaDst==o.alphaDst && colorFunc==o.colorFunc && alphaFunc==o.alphaFunc &&
                   enabled==o.enabled;
        }
    };
    struct PipelineCacheKey
    {
        PipelineKind kind; BlendKey blend;
        bool operator==(const PipelineCacheKey& o) const { return kind==o.kind && blend==o.blend; }
    };
    struct PipelineCacheKeyHash
    {
        std::size_t operator()(const PipelineCacheKey& k) const
        {
            uint64_t h = (uint64_t)k.kind
                | ((uint64_t)k.blend.colorSrc  << 8)
                | ((uint64_t)k.blend.colorDst  << 16)
                | ((uint64_t)k.blend.alphaSrc  << 24)
                | ((uint64_t)k.blend.alphaDst  << 32)
                | ((uint64_t)k.blend.colorFunc << 40)
                | ((uint64_t)k.blend.alphaFunc << 48)
                | ((uint64_t)(k.blend.enabled ? 1 : 0) << 56);
            return std::hash<uint64_t>()(h);
        }
    };

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

    struct Mat4 { float m[16]; };
    static Mat4 multiply(const Mat4& a, const Mat4& b)
    {
        Mat4 r{};
        for (int row=0; row<4; ++row)
            for (int col=0; col<4; ++col)
                for (int k=0; k<4; ++k)
                    r.m[row*4+col] += a.m[row*4+k] * b.m[k*4+col];
        return r;
    }
    static Mat4 fromXna(const Matrix& x)
    {
        return {{x.M11,x.M12,x.M13,x.M14, x.M21,x.M22,x.M23,x.M24,
                 x.M31,x.M32,x.M33,x.M34, x.M41,x.M42,x.M43,x.M44}};
    }
    static Mat4 transpose(const Mat4& x)
    {
        Mat4 r{}; for(int i=0;i<4;++i) for(int j=0;j<4;++j) r.m[j*4+i]=x.m[i*4+j]; return r;
    }

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

    // Re-applies every piece of encoder-scoped dynamic state this backend tracks. Metal has no
    // persistent-across-encoders state at all (unlike, say, retained GL context state) -- a fresh
    // MTLRenderCommandEncoder starts with undefined cull/fill/bias/stencil-ref/blend-color, so
    // ensureFrame()/clear() must both call this every time they create one. Previously ensureFrame()
    // inlined a partial version of this (missing stencil reference and blend color entirely) and
    // clear() didn't reapply cull/fill/depthBias/stencil-reference at all -- a real, pre-existing
    // inconsistency between the two encoder-creation paths, fixed here by sharing one function.
    void applyTrackedEncoderState()
    {
        [encoder setViewport:viewport]; [encoder setCullMode:cull]; [encoder setTriangleFillMode:fill];
        [encoder setDepthBias:depthBias slopeScale:slopeBias clamp:0]; [encoder setDepthStencilState:depthState];
        [encoder setStencilReferenceValue:(uint32_t)refStencil];
        [encoder setBlendColorRed:blendColor[0] green:blendColor[1] blue:blendColor[2] alpha:blendColor[3]];
    }

    void ensureFrame()
    {
        if (encoder) return;
        drawable=[layer nextDrawable];
        if(!drawable) throw std::runtime_error("Metal: CAMetalLayer returned no drawable");
        const NSUInteger w=drawable.texture.width,h=drawable.texture.height;
        if(!depthTexture || depthTexture.width!=w || depthTexture.height!=h){
            [depthTexture release];
            MTLTextureDescriptor* dd=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:w height:h mipmapped:NO];
            dd.storageMode=MTLStorageModePrivate; dd.usage=MTLTextureUsageRenderTarget;
            depthTexture=[device newTextureWithDescriptor:dd];
        }
        command=[queue commandBuffer]; [command retain];
        MTLRenderPassDescriptor* rp=[MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture=drawable.texture;
        rp.colorAttachments[0].loadAction=MTLLoadActionLoad; rp.colorAttachments[0].storeAction=MTLStoreActionStore;
        rp.depthAttachment.texture=depthTexture; rp.depthAttachment.loadAction=MTLLoadActionLoad; rp.depthAttachment.storeAction=MTLStoreActionStore;
        rp.stencilAttachment.texture=depthTexture; rp.stencilAttachment.loadAction=MTLLoadActionLoad; rp.stencilAttachment.storeAction=MTLStoreActionStore;
        encoder=[command renderCommandEncoderWithDescriptor:rp]; [encoder retain];
        viewport={0,0,(double)w,(double)h,0,1}; scissor={0,0,w,h};
        applyTrackedEncoderState();
    }

    void endFrame()
    {
        if(!command) return;
        if(encoder){[encoder endEncoding];[encoder release];encoder=nil;}
        [command presentDrawable:drawable]; [command commit]; [command release]; command=nil; drawable=nil;
    }

    void clear(bool color,float r,float g,float b,float a,bool depth,float dv,bool stencil,int sv)
    {
        if(encoder){[encoder endEncoding];[encoder release];encoder=nil; [command commit];[command waitUntilCompleted];[command release];command=nil;drawable=nil;}
        drawable=[layer nextDrawable]; if(!drawable) return;
        NSUInteger w=drawable.texture.width,h=drawable.texture.height;
        if(!depthTexture || depthTexture.width!=w || depthTexture.height!=h){
            [depthTexture release]; MTLTextureDescriptor* dd=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:w height:h mipmapped:NO];
            dd.storageMode=MTLStorageModePrivate; dd.usage=MTLTextureUsageRenderTarget; depthTexture=[device newTextureWithDescriptor:dd];
        }
        command=[queue commandBuffer]; [command retain];
        MTLRenderPassDescriptor* rp=[MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture=drawable.texture; rp.colorAttachments[0].loadAction=color?MTLLoadActionClear:MTLLoadActionLoad; rp.colorAttachments[0].storeAction=MTLStoreActionStore; rp.colorAttachments[0].clearColor=MTLClearColorMake(r,g,b,a);
        rp.depthAttachment.texture=depthTexture; rp.depthAttachment.loadAction=depth?MTLLoadActionClear:MTLLoadActionLoad; rp.depthAttachment.storeAction=MTLStoreActionStore; rp.depthAttachment.clearDepth=dv;
        rp.stencilAttachment.texture=depthTexture; rp.stencilAttachment.loadAction=stencil?MTLLoadActionClear:MTLLoadActionLoad; rp.stencilAttachment.storeAction=MTLStoreActionStore; rp.stencilAttachment.clearStencil=sv;
        encoder=[command renderCommandEncoderWithDescriptor:rp]; [encoder retain];
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
            case PipelineKind::NormalTex32:      vs=@"cna_v3d_normaltex";fs=@"cna_f3d_texture"; stride=32; break;
            case PipelineKind::DualTex20:        vs=@"cna_v3d_tex";      fs=@"cna_f3d_dualtex"; stride=20; break;
            case PipelineKind::DualTex24Colored: vs=@"cna_v3d_colortex"; fs=@"cna_f3d_dualtex"; stride=24; break;
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
};

class MetalSpriteBatch final : public ISpriteBatchBackend
{
public:
    explicit MetalSpriteBatch(MetalGraphicsBackend& b):b_(b){}
    void Begin() override { begun_=true; }
    void End() override { begun_=false; }
    void SetSamplerFilter(int f) override { filter_=f; }
    void SetSamplerAddressMode(int addressU,int addressV) override { addressU_=addressU; addressV_=addressV; }
    void Draw(const ITextureBackend& t,float x,float y) override { Rectangle d((int)x,(int)y,t.GetWidth(),t.GetHeight()); Rectangle s(0,0,t.GetWidth(),t.GetHeight()); Draw(t,d,s,Color::White); }
    void Draw(const ITextureBackend& t,const Rectangle& d,const Rectangle& s,const Color& c) override { Draw(t,d,s,c,0,Vector2::Zero,SpriteEffects::None,0); }
    void Draw(const ITextureBackend& t,const Rectangle& d,const Rectangle& s,const Color& c,float rotation,const Vector2& origin,SpriteEffects effects,float) override
    {
        if(!begun_) throw std::runtime_error("Metal SpriteBatch.Draw called outside Begin/End");
        auto* mt=dynamic_cast<const MetalTexture*>(&t); if(!mt) throw std::runtime_error("Metal: foreign texture backend");
        auto& p=b_.impl(); p.ensureFrame();
        struct V{float x,y,u,v,r,g,b,a;}; V q[6];
        float x0=(float)d.getXProperty(), y0=(float)d.getYProperty(), x1=x0+d.getWidthProperty(), y1=y0+d.getHeightProperty();
        float u0=(float)s.getXProperty()/t.GetWidth(), v0=(float)s.getYProperty()/t.GetHeight();
        float u1=(float)(s.getXProperty()+s.getWidthProperty())/t.GetWidth(), v1=(float)(s.getYProperty()+s.getHeightProperty())/t.GetHeight();
        if((int)effects & 1) std::swap(u0,u1); if((int)effects & 2) std::swap(v0,v1);
        const float cr=c.getRProperty()/255.f,cg=c.getGProperty()/255.f,cb=c.getBProperty()/255.f,ca=c.getAProperty()/255.f;
        auto xf=[&](float x,float y){ float px=x-(x0+origin.getXProperty()), py=y-(y0+origin.getYProperty()); float cs=std::cos(rotation),sn=std::sin(rotation); return std::array<float,2>{x0+origin.getXProperty()+px*cs-py*sn,y0+origin.getYProperty()+px*sn+py*cs};};
        auto a=xf(x0,y0),bb=xf(x1,y0),cc=xf(x1,y1),dd=xf(x0,y1);
        V vs[6]={{a[0],a[1],u0,v0,cr,cg,cb,ca},{bb[0],bb[1],u1,v0,cr,cg,cb,ca},{cc[0],cc[1],u1,v1,cr,cg,cb,ca},{a[0],a[1],u0,v0,cr,cg,cb,ca},{cc[0],cc[1],u1,v1,cr,cg,cb,ca},{dd[0],dd[1],u0,v1,cr,cg,cb,ca}};
        struct U{float w,h;} u{(float)p.drawable.texture.width,(float)p.drawable.texture.height};
        [p.encoder setRenderPipelineState:p.getOrCreatePipeline(PipelineKind::Sprite2D)]; [p.encoder setVertexBytes:vs length:sizeof(vs) atIndex:0]; [p.encoder setVertexBytes:&u length:sizeof(u) atIndex:1];
        [p.encoder setFragmentTexture:mt->native() atIndex:0]; [p.encoder setFragmentSamplerState:p.samplerFor(filter_,addressU_,addressV_,1) atIndex:0]; [p.encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    }
private: MetalGraphicsBackend& b_; bool begun_=false; int filter_=0; int addressU_=1; int addressV_=1;
};

MetalGraphicsBackend::MetalGraphicsBackend(const GraphicsBackendCreateArgs& args):impl_(std::make_unique<Impl>())
{
    auto& p=*impl_; p.window=args.window; p.virtualW=args.virtualWidth; p.virtualH=args.virtualHeight; p.swapInterval=args.swapInterval;
    if(!p.window) throw std::runtime_error("Metal backend requires an SDL_Window");
    p.device=MTLCreateSystemDefaultDevice(); if(!p.device) throw std::runtime_error("Metal: MTLCreateSystemDefaultDevice failed"); [p.device retain];
    p.view=SDL_Metal_CreateView(p.window); if(!p.view) throw std::runtime_error(std::string("Metal: SDL_Metal_CreateView failed: ")+SDL_GetError());
    p.layer=(CAMetalLayer*)SDL_Metal_GetLayer(p.view); [p.layer retain]; p.layer.device=p.device; p.layer.pixelFormat=MTLPixelFormatBGRA8Unorm; p.layer.framebufferOnly=NO;
    p.queue=[p.device newCommandQueue];
    NSError* err=nil; NSString* src=[NSString stringWithUTF8String:kMetalShaderSource]; p.library=[p.device newLibraryWithSource:src options:nil error:&err];
    if(!p.library) throw std::runtime_error(std::string("Metal shader compile failed: ")+([[err localizedDescription] UTF8String]?:"unknown"));
    // plan_metal.md METAL-23: pipelines are no longer built eagerly here -- getOrCreatePipeline()
    // lazily builds+caches each (PipelineKind, BlendKey) combination on first use instead.
    MTLSamplerDescriptor* sd=[[MTLSamplerDescriptor alloc]init];sd.minFilter=MTLSamplerMinMagFilterLinear;sd.magFilter=MTLSamplerMinMagFilterLinear;sd.sAddressMode=MTLSamplerAddressModeClampToEdge;sd.tAddressMode=MTLSamplerAddressModeClampToEdge;p.sampler=[p.device newSamplerStateWithDescriptor:sd];[sd release];
    p.rebuildDepthState();
}
MetalGraphicsBackend::~MetalGraphicsBackend(){auto&p=*impl_;p.endFrame();for(auto& kv:p.samplerCache)[kv.second release];p.samplerCache.clear();for(auto& kv:p.pipelineCache)[kv.second release];p.pipelineCache.clear();[p.depthTexture release];[p.depthState release];[p.sampler release];[p.library release];[p.queue release];[p.layer release];if(p.view)SDL_Metal_DestroyView(p.view);[p.device release];}
MetalGraphicsBackend::Impl& MetalGraphicsBackend::impl(){return *impl_;} const MetalGraphicsBackend::Impl& MetalGraphicsBackend::impl()const{return *impl_;}
void MetalGraphicsBackend::Clear(float r,float g,float b,float a){impl_->clear(true,r,g,b,a,false,1,false,0);} void MetalGraphicsBackend::Present(){impl_->endFrame();}
void MetalGraphicsBackend::GetViewportSize(int&w,int&h){int pw=0,ph=0;SDL_GetWindowSizeInPixels(impl_->window,&pw,&ph);w=impl_->virtualW>0?impl_->virtualW:pw;h=impl_->virtualH>0?impl_->virtualH:ph;}
void MetalGraphicsBackend::SetVirtualResolution(int w,int h){impl_->virtualW=w;impl_->virtualH=h;} void MetalGraphicsBackend::SetPresentationMode(int m){impl_->presentationMode=m;} void MetalGraphicsBackend::SetSwapInterval(int i){impl_->swapInterval=i;}
SDL_Window* MetalGraphicsBackend::GetWindowInternal()const{return impl_->window;} SDL_Renderer* MetalGraphicsBackend::GetRendererInternal()const{return nullptr;}
std::unique_ptr<ITextureBackend> MetalGraphicsBackend::CreateTexture(const ImageData& d){return std::make_unique<MetalTexture>(impl_->device,d);} std::unique_ptr<ISpriteBatchBackend> MetalGraphicsBackend::CreateSpriteBatch(){return std::make_unique<MetalSpriteBatch>(*this);}
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
void MetalGraphicsBackend::ApplyRasterizerState(int c,int f,bool se,float db,float sb){impl_->cull=c==1?MTLCullModeFront:(c==2?MTLCullModeBack:MTLCullModeNone);impl_->fill=f==1?MTLTriangleFillModeLines:MTLTriangleFillModeFill;impl_->scissorEnabled=se;impl_->depthBias=db;impl_->slopeBias=sb;if(impl_->encoder){[impl_->encoder setCullMode:impl_->cull];[impl_->encoder setTriangleFillMode:impl_->fill];[impl_->encoder setDepthBias:db slopeScale:sb clamp:0];}}
void MetalGraphicsBackend::ApplySamplerState(int slot,int filter,int addressU,int addressV,int maxAnisotropy){if(slot<0||slot>=16)return;impl_->samplerSlots[slot]=impl_->samplerFor(filter,addressU,addressV,maxAnisotropy);}
void MetalGraphicsBackend::SetBlendFactor(float r,float g,float b,float a){impl_->blendColor[0]=r;impl_->blendColor[1]=g;impl_->blendColor[2]=b;impl_->blendColor[3]=a;if(impl_->encoder)[impl_->encoder setBlendColorRed:r green:g blue:b alpha:a];}
void MetalGraphicsBackend::SetReferenceStencil(int v){impl_->refStencil=v;if(impl_->encoder)[impl_->encoder setStencilReferenceValue:v];}
void MetalGraphicsBackend::SetScissorRect(int x,int y,int w,int h){impl_->scissor={(NSUInteger)std::max(0,x),(NSUInteger)std::max(0,y),(NSUInteger)std::max(0,w),(NSUInteger)std::max(0,h)};if(impl_->encoder)[impl_->encoder setScissorRect:impl_->scissor];}
void MetalGraphicsBackend::SetViewport(int x,int y,int w,int h,float mn,float mx){impl_->viewport={(double)x,(double)y,(double)w,(double)h,mn,mx};if(impl_->encoder)[impl_->encoder setViewport:impl_->viewport];}
std::unique_ptr<IVertexBufferBackend> MetalGraphicsBackend::CreateVertexBuffer(int c){return std::make_unique<MetalVertexBuffer>(impl_->device,c);} std::unique_ptr<IIndexBufferBackend> MetalGraphicsBackend::CreateIndexBuffer16(int){return std::make_unique<MetalIndexBuffer>(impl_->device,false);} std::unique_ptr<IIndexBufferBackend> MetalGraphicsBackend::CreateIndexBuffer32(int){return std::make_unique<MetalIndexBuffer>(impl_->device,true);}

// plan_metal.md METAL-29/55/61: dispatch precedence deliberately mirrors
// EasyGLGraphicsBackend::SelectProgram()'s own top-of-function order (dualTexture checked before
// the plain stride switch) -- envMapping/skinned/pbr are still ahead of this in EasyGL's real
// precedence and are simply not reachable yet on Metal (their GpuDrawParams flags fall through to
// this function unconsumed until Phases 6-8 land), not a knowingly-wrong ordering.
static PipelineKind selectPipelineKind(std::size_t stride, const GpuDrawParams* params)
{
    const bool textured = params && params->texture0;
    const bool dual = params && params->dualTexture;
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
            case 32: return PipelineKind::NormalTex32;
            default: throw std::runtime_error("Metal: textured 3D requires stride 20, 24, or 32 until generic VertexDeclaration pipeline cache is implemented");
        }
    }
    if (stride != 16) throw std::runtime_error("Metal: colored 3D currently requires VertexPositionColor stride 16");
    return PipelineKind::Colored16;
}

static void drawMetal3D(MetalGraphicsBackend::Impl& p,const MetalVertexBuffer& vb,const MetalIndexBuffer* ib,const Matrix&w,const Matrix&v,const Matrix&pr,PrimitiveType pt,int pc,const GpuDrawParams* params)
{
    p.ensureFrame(); Mat4 wvp=transpose(multiply(multiply(fromXna(w),fromXna(v)),fromXna(pr)));
    const bool textured = params && params->texture0;
    const bool dual = params && params->dualTexture;
    const PipelineKind kind = selectPipelineKind(vb.stride(), params);
    id<MTLRenderPipelineState> pipeline = p.getOrCreatePipeline(kind);

    // plan_metal.md METAL-35/36/37/51-63: DiffuseColor/VertexColorEnabled/AlphaTest now actually
    // reach the shader (previously silently ignored for every draw). Defaults below exactly
    // reproduce this function's own prior hardcoded behavior for the non-Ex (params==nullptr)
    // path: diffuseColor=white, alphaTest=always-pass, vertexColorEnabled=true.
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

    [p.encoder setRenderPipelineState:pipeline]; [p.encoder setVertexBuffer:vb.native() offset:0 atIndex:0]; [p.encoder setVertexBytes:&wvp length:sizeof(wvp) atIndex:1]; [p.encoder setDepthStencilState:p.depthState]; [p.encoder setCullMode:p.cull]; [p.encoder setTriangleFillMode:p.fill];
    [p.encoder setFragmentBytes:&mp length:sizeof(mp) atIndex:2];
    if(textured){
        auto* mt=dynamic_cast<const MetalTexture*>(params->texture0);
        if(mt)[p.encoder setFragmentTexture:mt->native() atIndex:0];
        [p.encoder setFragmentSamplerState:(p.samplerSlots[0]?p.samplerSlots[0]:p.sampler) atIndex:0];
        if(dual){
            // plan_metal.md: EasyGL/Vulkan/Bgfx fall back to a 1x1 opaque white texture when
            // Texture2 is left null (docs/dualtextureeffect-support.md Task 386/387); Metal does
            // not yet have that fallback mechanism (a real, tracked follow-up gap, not silently
            // dropped), so a null Texture2 here leaves texture unit 1 holding whatever a prior
            // draw last bound there -- matches this same function's own pre-existing texture0
            // null-handling pattern exactly, not a new class of gap introduced by DualTexture.
            auto* mt1=dynamic_cast<const MetalTexture*>(params->texture1);
            if(mt1)[p.encoder setFragmentTexture:mt1->native() atIndex:1];
            [p.encoder setFragmentSamplerState:(p.samplerSlots[1]?p.samplerSlots[1]:p.sampler) atIndex:1];
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
    // a false positive for these 3 -- CreateOcclusionQuery()/CreateEffectBackend() both still
    // return nullptr and SetRenderTargets() still only binds rts[0], so a caller that checks this
    // capability before relying on the feature would otherwise get a wrong answer today. Revert
    // each case to the inherited default (remove the explicit `false`) as its own phase lands.
    switch (capability) {
        case CNA::GraphicsCapability::MultipleRenderTargets: return false; // plan_metal.md METAL-112
        case CNA::GraphicsCapability::OcclusionQuery:         return false; // plan_metal.md METAL-136
        case CNA::GraphicsCapability::CustomEffects:          return false; // plan_metal.md METAL-144
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
