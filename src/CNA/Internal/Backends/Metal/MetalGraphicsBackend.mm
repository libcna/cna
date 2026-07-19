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
fragment float4 cna_f3d_color(V3Out in [[stage_in]]) { return in.color; }
fragment float4 cna_f3d_texture(V3Out in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {
    return tex.sample(smp, in.uv) * in.color;
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
            default: return MTLPrimitiveTypeTriangle;
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
    id<MTLRenderPipelineState> pipe3Color=nil, pipe3Tex20=nil, pipe3ColorTex24=nil, pipe3NormalTex32=nil, pipe2=nil;
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
        [encoder setViewport:viewport]; [encoder setCullMode:cull]; [encoder setTriangleFillMode:fill];
        [encoder setDepthBias:depthBias slopeScale:slopeBias clamp:0]; [encoder setDepthStencilState:depthState];
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
        viewport={0,0,(double)w,(double)h,0,1}; scissor={0,0,w,h}; [encoder setViewport:viewport]; [encoder setDepthStencilState:depthState];
    }

    void rebuildDepthState()
    {
        MTLDepthStencilDescriptor* d=[[MTLDepthStencilDescriptor alloc] init];
        d.depthCompareFunction=depthEnabled?MTLCompareFunctionLessEqual:MTLCompareFunctionAlways;
        d.depthWriteEnabled=depthWrite;
        id<MTLDepthStencilState> s=[device newDepthStencilStateWithDescriptor:d]; [d release]; [depthState release]; depthState=s;
        if(encoder) [encoder setDepthStencilState:depthState];
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
        [p.encoder setRenderPipelineState:p.pipe2]; [p.encoder setVertexBytes:vs length:sizeof(vs) atIndex:0]; [p.encoder setVertexBytes:&u length:sizeof(u) atIndex:1];
        [p.encoder setFragmentTexture:mt->native() atIndex:0]; [p.encoder setFragmentSamplerState:p.samplerFor(filter_,addressU_,addressV_,1) atIndex:0]; [p.encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    }
private: MetalGraphicsBackend& b_; bool begun_=false; int filter_=0; int addressU_=1; int addressV_=1;
};

static id<MTLRenderPipelineState> makePipeline(id<MTLDevice> dev,id<MTLLibrary> lib,NSString* vs,NSString* fs,MTLVertexDescriptor* vd)
{
    MTLRenderPipelineDescriptor* d=[[MTLRenderPipelineDescriptor alloc] init]; d.vertexFunction=[lib newFunctionWithName:vs]; d.fragmentFunction=[lib newFunctionWithName:fs]; d.vertexDescriptor=vd;
    d.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm; d.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8; d.stencilAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8;
    d.colorAttachments[0].blendingEnabled=YES; d.colorAttachments[0].sourceRGBBlendFactor=MTLBlendFactorSourceAlpha; d.colorAttachments[0].destinationRGBBlendFactor=MTLBlendFactorOneMinusSourceAlpha; d.colorAttachments[0].sourceAlphaBlendFactor=MTLBlendFactorOne; d.colorAttachments[0].destinationAlphaBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
    NSError* err=nil; id<MTLRenderPipelineState> p=[dev newRenderPipelineStateWithDescriptor:d error:&err]; [d.vertexFunction release]; [d.fragmentFunction release]; [d release];
    if(!p) throw std::runtime_error(std::string("Metal pipeline compile failed: ")+([[err localizedDescription] UTF8String]?:"unknown")); return p;
}

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
    MTLVertexDescriptor* vd16=[MTLVertexDescriptor vertexDescriptor]; vd16.attributes[0].format=MTLVertexFormatFloat3;vd16.attributes[0].offset=0;vd16.attributes[0].bufferIndex=0;vd16.attributes[1].format=MTLVertexFormatUChar4Normalized;vd16.attributes[1].offset=12;vd16.attributes[1].bufferIndex=0;vd16.layouts[0].stride=16;
    MTLVertexDescriptor* vd20=[MTLVertexDescriptor vertexDescriptor]; vd20.attributes[0].format=MTLVertexFormatFloat3;vd20.attributes[0].offset=0;vd20.attributes[0].bufferIndex=0;vd20.attributes[1].format=MTLVertexFormatFloat2;vd20.attributes[1].offset=12;vd20.attributes[1].bufferIndex=0;vd20.layouts[0].stride=20;
    MTLVertexDescriptor* vd24=[MTLVertexDescriptor vertexDescriptor]; vd24.attributes[0].format=MTLVertexFormatFloat3;vd24.attributes[0].offset=0;vd24.attributes[0].bufferIndex=0;vd24.attributes[1].format=MTLVertexFormatUChar4Normalized;vd24.attributes[1].offset=12;vd24.attributes[1].bufferIndex=0;vd24.attributes[2].format=MTLVertexFormatFloat2;vd24.attributes[2].offset=16;vd24.attributes[2].bufferIndex=0;vd24.layouts[0].stride=24;
    MTLVertexDescriptor* vd32=[MTLVertexDescriptor vertexDescriptor]; vd32.attributes[0].format=MTLVertexFormatFloat3;vd32.attributes[0].offset=0;vd32.attributes[0].bufferIndex=0;vd32.attributes[1].format=MTLVertexFormatFloat3;vd32.attributes[1].offset=12;vd32.attributes[1].bufferIndex=0;vd32.attributes[2].format=MTLVertexFormatFloat2;vd32.attributes[2].offset=24;vd32.attributes[2].bufferIndex=0;vd32.layouts[0].stride=32;
    p.pipe3Color=makePipeline(p.device,p.library,@"cna_v3d_color",@"cna_f3d_color",vd16); p.pipe3Tex20=makePipeline(p.device,p.library,@"cna_v3d_tex",@"cna_f3d_texture",vd20); p.pipe3ColorTex24=makePipeline(p.device,p.library,@"cna_v3d_colortex",@"cna_f3d_texture",vd24); p.pipe3NormalTex32=makePipeline(p.device,p.library,@"cna_v3d_normaltex",@"cna_f3d_texture",vd32); p.pipe2=makePipeline(p.device,p.library,@"cna_v2d",@"cna_f2d",nil);
    MTLSamplerDescriptor* sd=[[MTLSamplerDescriptor alloc]init];sd.minFilter=MTLSamplerMinMagFilterLinear;sd.magFilter=MTLSamplerMinMagFilterLinear;sd.sAddressMode=MTLSamplerAddressModeClampToEdge;sd.tAddressMode=MTLSamplerAddressModeClampToEdge;p.sampler=[p.device newSamplerStateWithDescriptor:sd];[sd release];
    p.rebuildDepthState();
}
MetalGraphicsBackend::~MetalGraphicsBackend(){auto&p=*impl_;p.endFrame();for(auto& kv:p.samplerCache)[kv.second release];p.samplerCache.clear();[p.depthTexture release];[p.depthState release];[p.sampler release];[p.pipe2 release];[p.pipe3NormalTex32 release];[p.pipe3ColorTex24 release];[p.pipe3Tex20 release];[p.pipe3Color release];[p.library release];[p.queue release];[p.layer release];if(p.view)SDL_Metal_DestroyView(p.view);[p.device release];}
MetalGraphicsBackend::Impl& MetalGraphicsBackend::impl(){return *impl_;} const MetalGraphicsBackend::Impl& MetalGraphicsBackend::impl()const{return *impl_;}
void MetalGraphicsBackend::Clear(float r,float g,float b,float a){impl_->clear(true,r,g,b,a,false,1,false,0);} void MetalGraphicsBackend::Present(){impl_->endFrame();}
void MetalGraphicsBackend::GetViewportSize(int&w,int&h){int pw=0,ph=0;SDL_GetWindowSizeInPixels(impl_->window,&pw,&ph);w=impl_->virtualW>0?impl_->virtualW:pw;h=impl_->virtualH>0?impl_->virtualH:ph;}
void MetalGraphicsBackend::SetVirtualResolution(int w,int h){impl_->virtualW=w;impl_->virtualH=h;} void MetalGraphicsBackend::SetPresentationMode(int m){impl_->presentationMode=m;} void MetalGraphicsBackend::SetSwapInterval(int i){impl_->swapInterval=i;}
SDL_Window* MetalGraphicsBackend::GetWindowInternal()const{return impl_->window;} SDL_Renderer* MetalGraphicsBackend::GetRendererInternal()const{return nullptr;}
std::unique_ptr<ITextureBackend> MetalGraphicsBackend::CreateTexture(const ImageData& d){return std::make_unique<MetalTexture>(impl_->device,d);} std::unique_ptr<ISpriteBatchBackend> MetalGraphicsBackend::CreateSpriteBatch(){return std::make_unique<MetalSpriteBatch>(*this);}
void MetalGraphicsBackend::ClearColorAndDepth(float r,float g,float b,float a,float d){impl_->clear(true,r,g,b,a,true,d,false,0);} void MetalGraphicsBackend::ClearDepth(float d){impl_->clear(false,0,0,0,0,true,d,false,0);} void MetalGraphicsBackend::ClearStencil(int s){impl_->clear(false,0,0,0,0,false,1,true,s);} void MetalGraphicsBackend::ClearDepthAndStencil(float d,int s){impl_->clear(false,0,0,0,0,true,d,true,s);} void MetalGraphicsBackend::ClearColorAndStencil(float r,float g,float b,float a,int s){impl_->clear(true,r,g,b,a,false,1,true,s);} void MetalGraphicsBackend::ClearColorDepthAndStencil(float r,float g,float b,float a,float d,int s){impl_->clear(true,r,g,b,a,true,d,true,s);}
void MetalGraphicsBackend::SetDepthTestEnabled(bool e){impl_->depthEnabled=e;impl_->rebuildDepthState();} void MetalGraphicsBackend::SetBlendEnabled(bool e){impl_->blendEnabled=e;} void MetalGraphicsBackend::SetDepthWriteEnabled(bool e){impl_->depthWrite=e;impl_->rebuildDepthState();}
void MetalGraphicsBackend::ApplyBlendState(int,int,int,int,int,int){}
void MetalGraphicsBackend::ApplyDepthStencilState(bool de,bool dw,int,bool,int,int,int,int,int,int,int ref,bool,int,int,int,int){impl_->depthEnabled=de;impl_->depthWrite=dw;impl_->refStencil=ref;impl_->rebuildDepthState();if(impl_->encoder)[impl_->encoder setStencilReferenceValue:ref];}
void MetalGraphicsBackend::ApplyRasterizerState(int c,int f,bool se,float db,float sb){impl_->cull=c==1?MTLCullModeFront:(c==2?MTLCullModeBack:MTLCullModeNone);impl_->fill=f==1?MTLTriangleFillModeLines:MTLTriangleFillModeFill;impl_->scissorEnabled=se;impl_->depthBias=db;impl_->slopeBias=sb;if(impl_->encoder){[impl_->encoder setCullMode:impl_->cull];[impl_->encoder setTriangleFillMode:impl_->fill];[impl_->encoder setDepthBias:db slopeScale:sb clamp:0];}}
void MetalGraphicsBackend::ApplySamplerState(int slot,int filter,int addressU,int addressV,int maxAnisotropy){if(slot<0||slot>=16)return;impl_->samplerSlots[slot]=impl_->samplerFor(filter,addressU,addressV,maxAnisotropy);}
void MetalGraphicsBackend::SetReferenceStencil(int v){impl_->refStencil=v;if(impl_->encoder)[impl_->encoder setStencilReferenceValue:v];}
void MetalGraphicsBackend::SetScissorRect(int x,int y,int w,int h){impl_->scissor={(NSUInteger)std::max(0,x),(NSUInteger)std::max(0,y),(NSUInteger)std::max(0,w),(NSUInteger)std::max(0,h)};if(impl_->encoder)[impl_->encoder setScissorRect:impl_->scissor];}
void MetalGraphicsBackend::SetViewport(int x,int y,int w,int h,float mn,float mx){impl_->viewport={(double)x,(double)y,(double)w,(double)h,mn,mx};if(impl_->encoder)[impl_->encoder setViewport:impl_->viewport];}
std::unique_ptr<IVertexBufferBackend> MetalGraphicsBackend::CreateVertexBuffer(int c){return std::make_unique<MetalVertexBuffer>(impl_->device,c);} std::unique_ptr<IIndexBufferBackend> MetalGraphicsBackend::CreateIndexBuffer16(int){return std::make_unique<MetalIndexBuffer>(impl_->device,false);} std::unique_ptr<IIndexBufferBackend> MetalGraphicsBackend::CreateIndexBuffer32(int){return std::make_unique<MetalIndexBuffer>(impl_->device,true);}

static void drawMetal3D(MetalGraphicsBackend::Impl& p,const MetalVertexBuffer& vb,const MetalIndexBuffer* ib,const Matrix&w,const Matrix&v,const Matrix&pr,PrimitiveType pt,int pc,const GpuDrawParams* params)
{
    p.ensureFrame(); Mat4 wvp=transpose(multiply(multiply(fromXna(w),fromXna(v)),fromXna(pr)));
    bool textured=params && params->texture0; id<MTLRenderPipelineState> pipeline=p.pipe3Color;
    if(textured){ if(vb.stride()==20) pipeline=p.pipe3Tex20; else if(vb.stride()==24) pipeline=p.pipe3ColorTex24; else if(vb.stride()==32) pipeline=p.pipe3NormalTex32; else throw std::runtime_error("Metal: textured 3D requires stride 20, 24, or 32 until generic VertexDeclaration pipeline cache is implemented"); }
    else if(vb.stride()!=16) throw std::runtime_error("Metal: colored 3D currently requires VertexPositionColor stride 16");
    [p.encoder setRenderPipelineState:pipeline]; [p.encoder setVertexBuffer:vb.native() offset:0 atIndex:0]; [p.encoder setVertexBytes:&wvp length:sizeof(wvp) atIndex:1]; [p.encoder setDepthStencilState:p.depthState]; [p.encoder setCullMode:p.cull]; [p.encoder setTriangleFillMode:p.fill];
    if(textured){auto* mt=dynamic_cast<const MetalTexture*>(params->texture0);if(mt)[p.encoder setFragmentTexture:mt->native() atIndex:0];[p.encoder setFragmentSamplerState:(p.samplerSlots[0]?p.samplerSlots[0]:p.sampler) atIndex:0];}
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
