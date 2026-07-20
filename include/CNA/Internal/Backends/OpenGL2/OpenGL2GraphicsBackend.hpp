#pragma once

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"

#include <SDL3/SDL.h>

#include <memory>

namespace CNA::Internal::Backends::OpenGL2
{
    // plan_opengl2.md (context-loss recovery): mirrors EasyGL's own RecoverableResource/
    // ResourceRegistry design exactly (see easy-gl's ResourceRegistry.hpp/RecoverableResource.hpp),
    // deliberately reimplemented locally rather than depending on EasyGL/easy-gl -- consistent with
    // this backend's own "deliberately independent of EasyGL" scope. Defined fully in the .cpp;
    // only forward-declared here so OpenGL2GraphicsBackend can hold a registry of them.
    class RecoverableResource;

    // Native desktop OpenGL 2.1 (compatibility profile) backend. See plan_opengl2.md for scope
    // and known follow-up work. Deliberately independent of EasyGL/easy-gl.
    class OpenGL2GraphicsBackend final : public IGraphicsBackend
    {
    public:
        OpenGL2GraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                               CnaPresentationMode presentationMode, bool contextRecoveryEnabled,
                               int swapInterval);
        ~OpenGL2GraphicsBackend() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void GetDefaultViewportRect(int& x, int& y, int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        SDL_Window* GetWindowInternal() const override { return window_; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITexture3DBackend> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IEffectBackend> CreateEffectBackend(const std::string& vertSrc, const std::string& fragSrc) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        std::unique_ptr<IRenderTargetCubeBackend> CreateRenderTargetCube(int size, int depthFormat,
                                                                          bool mipMap = false,
                                                                          int multiSampleCount = 0) override;
        void SetRenderTargetCubeFace(IRenderTargetCubeBackend* rt, int face) override;
        void SetRenderTargets(IRenderTargetBackend* const* rts, int count) override;

        // NOXNA: backend-internal helper (not part of IGraphicsBackend) mirroring
        // EasyGLGraphicsBackend::GetCurrentRenderTarget2DSize -- lets Sprite::Draw size its
        // screen->clip mapping to the bound render target instead of the window/virtual
        // resolution when one is active.
        bool GetCurrentRenderTarget2DSize(int& width, int& height) const;

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;

        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;

        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int index_capacity) override;

        void DrawColoredPrimitives(const IVertexBufferBackend& vb, const Matrix& world, const Matrix& view,
                                   const Matrix& projection, PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        void DrawPrimitivesEx(const IVertexBufferBackend& vb, const Matrix& world, const Matrix& view,
                              const Matrix& projection, PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params) override;
        void DrawInstancedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount, int instanceCount,
                                       const GpuDrawParams& params) override;

        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend, int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc) override;
        void SetBlendFactor(float r, float g, float b, float a) override;
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias = 0.0f, float slopeScaleDepthBias = 0.0f) override;
        void ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy) override;

        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;
        bool TransformWindowToLogical(float windowX, float windowY, float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY, float& windowX, float& windowY) const override;

        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        // plan_opengl2.md (context-loss recovery): scope mirrors EasyGLGraphicsBackend's own real
        // limitation exactly -- VertexBuffer/IndexBuffer/Texture2D content is restored from a CPU
        // shadow; RenderTarget2D/RenderTargetCube/OcclusionQuery recreate but their GPU-only
        // content is genuinely lost (not shadow-able); TextureCube/Texture3D/custom ShaderEffect
        // programs are NOT recoverable at all (EasyGL itself doesn't register these with its own
        // ResourceRegistry either -- this is a real, established boundary, not a gap introduced
        // here). Desktop loss+restore is atomic (matches EasyGL): DebugRestoreContext() delegates
        // to DebugSimulateContextLoss().
        void DebugSimulateContextLoss() override;
        void DebugRestoreContext() override;
        void SetContextRecoveryEnabled(bool enabled) override;

        // NOXNA: called by every recoverable resource's constructor/destructor (VB/IB/Tex/
        // RenderTarget/RenderTargetCubeBackend/OcclusionQuery) -- not part of IGraphicsBackend.
        void RegisterRecoverable(RecoverableResource* resource);
        void UnregisterRecoverable(RecoverableResource* resource);
        [[nodiscard]] bool IsContextRecoveryEnabled() const { return contextRecoveryEnabled_; }

        // NOXNA: mirrors SdlGpuGraphicsBackend::LogicalViewport/ComputeLogicalViewport exactly
        // (see that backend's own implementation -- the established reference for real
        // Letterbox/Overscan/Stretch semantics in this codebase; EasyGL's own equivalent is a
        // documented no-op fallback, not a model to follow here). `x`/`y`/`width`/`height` are
        // the PHYSICAL window sub-rectangle the logical content maps into (identical to the full
        // window for FixedHeightDynamicWidth/NativeBackBuffer/Stretch -- only Letterbox/Overscan
        // actually offset/shrink it); `logicalWidth`/`logicalHeight` are what
        // GetViewportSize()/SpriteBatch coordinate math treat as the game's own resolution.
        struct LogicalViewport
        {
            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            float logicalWidth = 0.0f;
            float logicalHeight = 0.0f;
        };
        [[nodiscard]] LogicalViewport ComputeLogicalViewport() const;

    private:
        SDL_Window* window_{};
        SDL_GLContext context_{};
        // plan_opengl2.md (context-loss recovery): SDL_GL_SetSwapInterval is per-CONTEXT state --
        // remembered here so DebugSimulateContextLoss() can reapply the game's actual current
        // setting to the freshly-created context instead of silently reverting to SDL's own
        // context-creation default.
        int swapInterval_{};
        int virtualWidth_{};
        int virtualHeight_{};
        CnaPresentationMode presentationMode_{};
        unsigned colorProgram_{};
        unsigned texturedProgram_{};
        unsigned dualTextureProgram_{};
        unsigned litProgram_{};
        unsigned envMapProgram_{};
        unsigned skinnedProgram_{};
        unsigned pbrProgram_{};
        unsigned pbrSkinnedProgram_{};
        IRenderTargetBackend* currentRt_{};
        IRenderTargetCubeBackend* currentRtCube_{};
        int currentRtWidth_{};
        int currentRtHeight_{};
        // MRT (SetRenderTargets, count > 1): one shared FBO, re-attached (glFramebufferTexture2D
        // per target + glDrawBuffers) on every call rather than cached per render-target-set --
        // mirrors EasyGLGraphicsBackend::SetRenderTargets's own mrtFbo_ precedent, including its
        // documented gap (MRT targets are never tracked as currentRt_/currentRtCube_, so mip
        // regeneration on switching away from MRT mode is not supported).
        unsigned mrtFbo_{};
        bool mrtFboReady_{};

        // plan_opengl2.md (context-loss recovery): every currently-live recoverable resource
        // (VB/IB/Tex/RenderTarget/RenderTargetCubeBackend/OcclusionQuery), registered/unregistered
        // by its own constructor/destructor -- mirrors EasyGL's own ResourceRegistry exactly,
        // reimplemented locally (see RecoverableResource's own forward-declaration comment).
        std::vector<RecoverableResource*> recoverableResources_;
        // SetContextRecoveryEnabled(false): gates future registrations only (matches
        // EasyGLGraphicsBackend::SetContextRecoveryEnabled's own doc comment -- "safe to call
        // after backend creation when no resources have been loaded yet"); resources already
        // registered before the call stay tracked.
        bool contextRecoveryEnabled_ = true;

        // Calls UnbindAsRenderTarget() on whichever render target (2D or cube) is currently
        // active, if any, and clears both tracking pointers -- shared by SetRenderTarget2D() and
        // SetRenderTargetCubeFace() so switching between (or away from) either kind always runs
        // the outgoing target's resolve/mipmap-regeneration step exactly once.
        void unbindCurrentRenderTarget();
        // Per-slot cached SamplerState (GL 2.1 has no sampler objects -- state is applied via
        // glTexParameteri on whichever texture drawInternal() actually binds to that unit, at
        // bind time, not here). Defaults match SamplerStateCollection's own SamplerState::LinearWrap.
        static constexpr int kMaxSamplerSlots = 16;
        int samplerFilter_[kMaxSamplerSlots] = {};
        int samplerAddressU_[kMaxSamplerSlots] = {};
        int samplerAddressV_[kMaxSamplerSlots] = {};
        int samplerMaxAnisotropy_[kMaxSamplerSlots] = {};

        // Lazily-created 1x1 white fallbacks for effects that always sample a texture regardless
        // of whether the XNA-level Texture property was actually set (unlike BasicEffect/
        // AlphaTestEffect/DualTextureEffect, which only select a texturing program when texture0
        // is actually non-null -- see drawInternal()'s dispatch). Used by
        // EnvironmentMapEffect (Texture + EnvironmentMap), SkinnedEffect (Texture), and
        // PbrEffect/SkinnedPbrEffect (Texture + MetallicRoughness/Emissive/OcclusionMap -- all
        // four are "value 1.0 when absent", matching a flat white texture's sampled value).
        unsigned defaultWhiteTexture2D_{};
        unsigned defaultWhiteTextureCube_{};
        // Lazily-created 1x1 flat-normal (128,128,255,255 -> decodes to tangent-space (0,0,1),
        // i.e. "no perturbation") fallback for PbrEffect/SkinnedPbrEffect's NormalMap, which --
        // unlike the other three PBR maps above -- is not simply "1.0 when absent".
        unsigned defaultFlatNormalTexture2D_{};
        void ensureDefaultWhiteTextures();

        void ensurePrograms();
        void drawInternal(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                          const Matrix& world, const Matrix& view, const Matrix& projection,
                          PrimitiveType primitive, int primitiveCount, const GpuDrawParams* params);
    };
}
