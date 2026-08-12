#pragma once
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/OpenGL1/OpenGL1Capabilities.hpp"
#include "CNA/Internal/Renderers/OpenGL1/OpenGL1ContextRecovery.hpp"
#include "CNA/Internal/Renderers/OpenGL1/OpenGL1OcclusionQueryRenderer.hpp"
#include "CNA/Internal/Renderers/OpenGL1/OpenGL1RenderTargetRenderer.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include <SDL3/SDL.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>


namespace CNA::Internal::Renderers::OpenGL1 {
class OpenGL1VertexBufferRenderer final : public IVertexBufferRenderer {
public:
 explicit OpenGL1VertexBufferRenderer(int capacity):capacity_(capacity){}
 void SetData(const void*,int,std::size_t) override;
 void SetDataWithOptions(const void* d,int c,std::size_t s,SetDataOptions) override {SetData(d,c,s);}
 // REMED-GFX-DECL-GUARD: this renderer selects its immediate-mode emit layout from the vertex
 // stride alone (16/20/24/32 -- DrawInternal's own dispatch), so the declaration is remembered
 // rather than discarded and a draw can refuse one the stride table would silently reinterpret.
 void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override{declaration_.Remember(vertexDeclaration);}
 int GetVertexCount() const override{return count_;}
 const std::vector<std::uint8_t>& Data() const{return data_;} std::size_t Stride()const{return stride_;}
 // CNAEXT. The declaration this buffer carries, for REMED-GFX-DECL-GUARD's fidelity check.
 [[nodiscard]] const CNA::Internal::Graphics::DeclaredVertexLayout& GetDeclarationEXT()const{return declaration_;}
private:int capacity_=0,count_=0;std::size_t stride_=0;std::vector<std::uint8_t> data_;
 CNA::Internal::Graphics::DeclaredVertexLayout declaration_;
};
// REMED-GFX-DECL-GUARD: refuses a draw whose VertexDeclaration this renderer cannot bind
// faithfully. Asymmetric -- only what the caller actually declared is verified, never equality
// against the renderer's own template. An unlisted stride is emitted as position-at-offset-0 with
// constant white (DrawInternal's measured default arm), i.e. the PositionOnlyFallback layout.
// Header-only, as the renderer static library cannot see src/CNA/**.cpp.
inline void RequireFaithfulDeclarationEXT(const IVertexBufferRenderer& vb, const char* route)
{
    const auto& glVb = static_cast<const OpenGL1VertexBufferRenderer&>(vb);
    CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
        glVb.GetDeclarationEXT(), static_cast<int>(glVb.Stride()),
        CNA::Internal::Graphics::UnlistedStrideLayout::PositionOnlyFallback, "OpenGL1", route);
}
class OpenGL1IndexBufferRenderer final : public IIndexBufferRenderer {
public: explicit OpenGL1IndexBufferRenderer(bool i32):i32_(i32){}
 void SetData16(const void*,int) override; void SetData32(const void*,int) override;
 int GetIndexCount()const override{return count_;} bool IsThirtyTwoBit()const override{return i32_;}
 const std::vector<std::uint8_t>& Data()const{return data_;}
private:bool i32_=false;int count_=0;std::vector<std::uint8_t> data_;
};
// plan_opengl1.md phase 8: implements IOpenGL1Recoverable so Texture2D content survives a
// simulated/real GL context loss -- ShareCpuPixels() retains the SAME shared_ptr<vector<uint8_t>>
// Texture2D itself keeps (no duplicate copy; Texture2D's own SetData mutations are visible
// through it automatically), which RecreateGLResource() re-uploads from after the context is
// recreated.
// plan_opengl1.md phase 6: mipMap_ tracks whether the ImageData that created/last-recreated this
// texture requested a mip chain (ImageData::mipLevels>1); hasMips_ tracks whether generation
// actually succeeded (false only if mipMap_ is true but the driver has neither glGenerateMipmap
// nor the older GL_GENERATE_MIPMAP/SGIS_generate_mipmap mechanism AND no CPU pixel data was
// available yet to fall back on) -- consulted by ApplySamplerState so a mip-aware TextureFilter
// is only ever requested against a texture that genuinely has more than one level.
class OpenGL1TextureRenderer final : public ITextureRenderer, public IOpenGL1Recoverable {
public: OpenGL1TextureRenderer(const ImageData&,OpenGL1ResourceRegistry*,bool generateMipmapCap); ~OpenGL1TextureRenderer() override;
 int GetWidth()const override{return width_;} int GetHeight()const override{return height_;}
 void UpdatePixels(const uint8_t*,int) override; void UpdatePixelsLevel(int,const uint8_t*,int,int) override; void BindGL(int /*unit*/)const override;
 void ShareCpuPixels(std::shared_ptr<std::vector<uint8_t>> pixels) override{cpuPixels_=std::move(pixels);}
 void ReleaseGLHandleOnly() override{id_=0;}
 void RecreateGLResource() override;
 unsigned int Id()const{return id_;}
 bool HasMips()const{return hasMips_;}
private:void RegenerateMips(const uint8_t*level0);
 unsigned int id_=0;int width_=0,height_=0;OpenGL1ResourceRegistry* registry_=nullptr;std::shared_ptr<std::vector<uint8_t>> cpuPixels_;
 bool mipMap_=false;bool hasMips_=false;bool generateMipmapCap_=false;
};
// plan_opengl1.md phase 8 follow-up: implements IOpenGL1Recoverable so TextureCube content
// survives a simulated/real GL context loss the same way OpenGL1TextureRenderer does --
// ShareCpuPixels(face,...) retains the SAME shared_ptr<vector<uint8_t>> TextureCube itself keeps
// per face (no duplicate copy). Closes the gap plan_opengl1.md previously documented as
// intentional (ITextureCubeRenderer had no ShareCpuPixels()-equivalent hook -- now added to the
// shared IGraphicsRenderer.hpp interface with a default no-op, so every other renderer stays
// source-compatible unchanged).
class OpenGL1TextureCubeRenderer final : public ITextureCubeRenderer, public IOpenGL1Recoverable {
public: OpenGL1TextureCubeRenderer(int size,bool mipMap,int surfaceFormat,OpenGL1ResourceRegistry*registry,bool generateMipmapCap); ~OpenGL1TextureCubeRenderer() override;
 [[nodiscard]] bool SetData(int face,int level,int x,int y,int w,int h,const void*data,int dataLength) override;
 [[nodiscard]] bool GetData(int face,int level,int x,int y,int w,int h,void*data,int dataLength) const override;
 void BindGL(int /*unit*/)const override;
 void ShareCpuPixels(int face,std::shared_ptr<std::vector<uint8_t>> pixels) override{if(face>=0&&face<6)cpuPixels_[face]=std::move(pixels);}
 void ReleaseGLHandleOnly() override{id_=0;}
 void RecreateGLResource() override;
private:void Build();void RegenerateMips();
 unsigned int id_=0;int size_=0;bool mipMap_=false;bool generateMipmapCap_=false;
 OpenGL1ResourceRegistry* registry_=nullptr;
 std::shared_ptr<std::vector<uint8_t>> cpuPixels_[6];
};
class OpenGL1SpriteBatchRenderer final : public ISpriteBatchRenderer {
public: explicit OpenGL1SpriteBatchRenderer(class OpenGL1Renderer& o):owner_(o){}
 void Begin()override;void End()override;void SetTransformMatrix(const Matrix& m)override{transform_=m;}
 void SetSamplerFilter(int f)override{filter_=f;} void SetSamplerAddressMode(int u,int v)override{u_=u;v_=v;}
 void Draw(const ITextureRenderer&,float,float)override;
 void Draw(const ITextureRenderer&,const Rectangle&,const Rectangle&,const Color&)override;
 void Draw(const ITextureRenderer&,const Rectangle&,const Rectangle&,const Color&,float,const Vector2&,SpriteEffects,float)override;
private:OpenGL1Renderer& owner_;bool begun_=false;Matrix transform_=Matrix::getIdentityProperty();int filter_=0,u_=1,v_=1;
};
class OpenGL1Renderer final : public IGraphicsRenderer {
public: explicit OpenGL1Renderer(const GraphicsRendererCreateArgs&);~OpenGL1Renderer()override;
 void Clear(float,float,float,float)override;void Present()override;void GetViewportSize(int&,int&)override;void SetVirtualResolution(int,int)override;
 // plan_opengl1.md item 13 (EasyGL parity): was a no-op -- now stores the mode, consulted by
 // EffectiveWidth()/EffectiveHeight() (FixedHeightDynamicWidth only; see their own doc comment).
 void SetPresentationMode(int)override;
 // plan_opengl1.md item 13: uniform logical<->physical scale for FixedHeightDynamicWidth (no
 // offset needed -- the logical canvas fills the window exactly, by construction, so there are no
 // letterbox bars to account for). Default (unimplemented, "window==logical") behavior stays for
 // every other presentation mode. Registered for GetForWindow() lookup already, in the
 // constructor (IGraphicsRenderer::RegisterForWindow) -- Mouse::logical_to_window's own consumer.
 bool TransformWindowToLogical(float windowX,float windowY,float&logX,float&logY)const override;
 bool TransformLogicalToWindow(float logX,float logY,float&windowX,float&windowY)const override;
 // plan_opengl1.md item 20 (EasyGL parity): only the constructor's swapInterval ever reached SDL
 // before this -- a runtime GraphicsDevice.PresentationParameters/vsync change silently did
 // nothing.
 void SetSwapInterval(int)override;
 // plan_opengl1.md item 22 (EasyGL parity): backbuffer MSAA. SDL_GL_MULTISAMPLEBUFFERS/SAMPLES
 // are requested before SDL_CreateWindow() (GraphicsDevice.cpp, same GLX-visual-fixed-at-window-
 // creation-time constraint as depth/stencil) -- the constructor here only reads back whatever
 // the driver actually granted, since GLX can silently clamp/refuse the request. Cannot be
 // reconfigured post-construction (same reason), so ApplyMultiSampleCount() is intentionally not
 // overridden -- the IGraphicsRenderer default (echo GetMultiSampleCount() back unchanged) is
 // already the honest answer, matching EasyGL's own established behavior for the same reason.
 [[nodiscard]] int GetMultiSampleCount()const override{return multiSampleCount_;}
 void ReadBackbuffer(int,int,int,int,uint8_t*)override;
 SDL_Window* GetWindowInternal()const override{return window_;} SDL_Renderer* GetRendererInternal()const override{return nullptr;}
 std::unique_ptr<ITextureRenderer>CreateTexture(const ImageData&)override;std::unique_ptr<ISpriteBatchRenderer>CreateSpriteBatch()override;
 std::unique_ptr<IRenderTargetRenderer>CreateRenderTarget2D(int,int,int,bool,bool,int)override;void SetRenderTarget2D(IRenderTargetRenderer*)override;
 std::unique_ptr<ITextureCubeRenderer>CreateTextureCube(int,bool,int)override;
 // plan_opengl1.md item 24 (EasyGL parity): real cube-map render targets, combining the existing
 // FBO (CreateRenderTarget2D) and cube-map (CreateTextureCube) machinery. Returns nullptr (the
 // documented IGraphicsRenderer contract) when either the FBO or cube-map capability is absent.
 // multiSampleCount is accepted but ignored -- out of scope for this item (item 25 addresses
 // RenderTarget2D MSAA specifically, not six separate cube-face resolve targets).
 // preserveContents needs no action here: the colour attachment IS the cube texture itself, so
 // face contents survive every unbind by construction (same answer as OPENGLES1's).
 std::unique_ptr<IRenderTargetCubeRenderer>CreateRenderTargetCube(int,int,bool,bool,int)override;
 // Unlike SetRenderTarget2D()'s default single-target tracking, a cube-face target needs its OWN
 // tracked pointer+size (currentCubeRt_) so EffectiveWidth()/EffectiveHeight()/SetViewport()/
 // SetScissorRect() report the ACTIVE face's real size, not the window's -- SetRenderTarget2D()
 // and this override each clear the other's stale pointer when switching between the two kinds.
 void SetRenderTargetCubeFace(IRenderTargetCubeRenderer*,int)override;
 // Post-audit descriptor route. This fixed-function renderer has one colour attachment, so a
 // count above one is refused with System::NotSupportedException rather than silently reduced to
 // the first target; a cube-face descriptor routes through SetRenderTargetCubeFace() above;
 // nullptr / count 0 restores the default back buffer.
 void SetRenderTargets(const RenderTargetBindingDescriptor*,int)override;
 // plan_opengl1.md item 23 (EasyGL parity): real ARB_occlusion_query/core-1.5 occlusion queries --
 // returns nullptr (the documented IGraphicsRenderer contract, matching CreateRenderTarget2D's own
 // capability-gated fallback) when the driver genuinely lacks it.
 std::unique_ptr<IOcclusionQueryRenderer>CreateOcclusionQuery()override;
 void ApplyBlendState(int,int,int,int,int,int,const BlendWriteState&)override;void ApplyDepthStencilState(bool,bool,int,bool,int,int,int,int,int,int,int,bool,int,int,int,int)override;
 void ApplyRasterizerState(int,int,bool,float,float)override;void ApplySamplerState(int,int,int,int,int)override;void SetBlendFactor(float,float,float,float)override;void SetReferenceStencil(int)override;
 void SetScissorRect(int,int,int,int)override;void SetViewport(int,int,int,int,float,float)override;
 void ClearColorAndDepth(float,float,float,float,float)override;void ClearDepth(float)override;void ClearStencil(int)override;void ClearDepthAndStencil(float,int)override;void ClearColorAndStencil(float,float,float,float,int)override;void ClearColorDepthAndStencil(float,float,float,float,float,int)override;
 void SetDepthTestEnabled(bool)override;void SetBlendEnabled(bool)override;void SetDepthWriteEnabled(bool)override;
 std::unique_ptr<IVertexBufferRenderer>CreateVertexBuffer(int)override;std::unique_ptr<IIndexBufferRenderer>CreateIndexBuffer16(int)override;std::unique_ptr<IIndexBufferRenderer>CreateIndexBuffer32(int)override;
 void DrawColoredPrimitives(const IVertexBufferRenderer&,const Matrix&,const Matrix&,const Matrix&,PrimitiveType,int)override;
 void DrawIndexedColoredPrimitives(const IVertexBufferRenderer&,const IIndexBufferRenderer&,const Matrix&,const Matrix&,const Matrix&,PrimitiveType,int)override;
 void DrawPrimitivesEx(const IVertexBufferRenderer&,const Matrix&,const Matrix&,const Matrix&,PrimitiveType,int,const GpuDrawParams&)override;
 void DrawIndexedPrimitivesEx(const IVertexBufferRenderer&,const IIndexBufferRenderer&,const Matrix&,const Matrix&,const Matrix&,PrimitiveType,int,const GpuDrawParams&)override;
 bool SupportsCapability(CNA::GraphicsCapability capability)const override;
 int VirtualWidth()const{return virtualWidth_;}int VirtualHeight()const{return virtualHeight_;}
 const OpenGL1Capabilities& Capabilities()const{return caps_;}
 // plan_opengl1.md item 13 (EasyGL parity): used to return the raw, never-recomputed
 // virtualWidth_/virtualHeight_ regardless of the window's actual current size or
 // presentationMode_ -- now applies the FixedHeightDynamicWidth aspect-correct recomputation
 // (logicalW = round(physicalW * preferredH / physicalH), preferredH fixed) when that mode is
 // active, matching EasyGL's own getLogicalSize(). Every other mode keeps today's original
 // behavior (see ComputeLogicalSize()'s own doc comment for why that's a real, not merely
 // deferred, answer for Stretch/NativeBackBuffer specifically).
 // plan_opengl1.md item 24: also checks currentCubeRt_ (a cube-face render target) before
 // falling back to the window-relative logical size -- see SetRenderTargetCubeFace()'s own doc
 // comment for why a cube-face target needs its own tracked pointer, separate from currentRt_.
 int EffectiveWidth()const{if(currentRt_)return currentRt_->GetWidth();if(currentCubeRt_)return currentCubeRt_->GetSize();int w,h;ComputeLogicalSize(w,h);return w;}
 int EffectiveHeight()const{if(currentRt_)return currentRt_->GetHeight();if(currentCubeRt_)return currentCubeRt_->GetSize();int w,h;ComputeLogicalSize(w,h);return h;}
 // plan_opengl1.md phase 8: context-loss resource recreation registry, independent of EasyGL's
 // own (::easygl::ResourceRegistry). SetContextRecoveryEnabled(false) stops future Create* calls
 // from registering (matches the documented IGraphicsRenderer contract: "safe to call ... when no
 // resources have been loaded yet"); DebugSimulateContextLoss()/DebugRestoreContext() perform one
 // atomic destroy+recreate cycle, same as every other desktop renderer that implements this.
 void SetContextRecoveryEnabled(bool)override;
 void DebugSimulateContextLoss()override;
 void DebugRestoreContext()override;
 OpenGL1ResourceRegistry* RegistryIfEnabled(){return contextRecoveryEnabled_?&registry_:nullptr;}
private:void SetupMatrices(const Matrix&,const Matrix&,const Matrix&);void DrawInternal(const OpenGL1VertexBufferRenderer&,const OpenGL1IndexBufferRenderer*,PrimitiveType,int,const GpuDrawParams*);
 // plan_opengl1.md item 22: reads back the ACTUAL SDL_GL_MULTISAMPLEBUFFERS/SAMPLES the driver
 // granted for the current GL context (not just echoing back what was requested -- GLX can
 // silently clamp/refuse), enables GL_MULTISAMPLE when genuinely present. Shared by the
 // constructor and DebugSimulateContextLoss() (same window, same fixed visual, but re-detected
 // for consistency rather than assumed unchanged).
 void DetectMultiSampleCount();
 int multiSampleCount_=0;
 // plan_opengl1.md item 13 (EasyGL parity): the actual FixedHeightDynamicWidth recomputation --
 // see EffectiveWidth()/EffectiveHeight()'s own doc comment above for the formula and rationale.
 // Every mode besides FixedHeightDynamicWidth (Letterbox/Overscan/Stretch/NativeBackBuffer)
 // returns virtualWidth_/virtualHeight_ unchanged: Stretch and NativeBackBuffer are honestly
 // already correct that way (Stretch means "don't preserve aspect", which is exactly what an
 // unscaled logical size mapped onto a differently-shaped physical viewport already produces;
 // NativeBackBuffer means "no scaling" by definition) -- Letterbox/Overscan (real bars/cropping)
 // would need the actual glViewport sub-rectangle adjusted, not just this SpriteBatch-facing
 // logical size, and are a documented, intentional gap (plan_opengl1.md).
 void ComputeLogicalSize(int&outW,int&outH)const;
 int presentationMode_=4; // CnaPresentationMode::FixedHeightDynamicWidth, matches the shared interface's own default.
 // Fixes a real, pre-existing bug found while implementing phase 6's mip-aware filtering:
 // ApplySamplerState() used to write glTexParameteri directly, once per draw for every sampler
 // slot, called (via GraphicsDevice::applySamplerStatesToRenderer()) BEFORE DrawInternal actually
 // binds the corresponding texture -- so its writes landed on whichever texture happened to be
 // bound from the PREVIOUS draw call, not the one about to be sampled, and its own `slot`
 // parameter was ignored entirely (every slot wrote to whichever unit was currently active).
 // ApplySamplerState now only caches per-slot params here; ApplySamplerFilterAndWrap() applies
 // them to whatever texture is actually bound, called from DrawInternal right after each
 // BindGL() call, on the correct active unit, with the SPECIFIC texture's own HasMips().
 struct GL1SamplerParams{int filter=0,addrU=0,addrV=0,maxAniso=4;};
 void ApplySamplerFilterAndWrap(int slot,bool hasMips);
 SDL_Window* window_=nullptr;SDL_GLContext glContext_=nullptr;int virtualWidth_=0,virtualHeight_=0;int stencilRef_=0;OpenGL1Capabilities caps_;IRenderTargetRenderer* currentRt_=nullptr;
 // plan_opengl1.md item 24: currentCubeFace_ is only meaningful while currentCubeRt_ is
 // non-null -- used to re-bind the correct face after a context loss, the same "rebind whatever
 // was ACTIVE at loss time" fix phase 8 already established for currentRt_.
 IRenderTargetCubeRenderer* currentCubeRt_=nullptr;int currentCubeFace_=-1;
 OpenGL1ResourceRegistry registry_;bool contextRecoveryEnabled_=true;
 GL1SamplerParams samplerSlot_[2];
};
}
