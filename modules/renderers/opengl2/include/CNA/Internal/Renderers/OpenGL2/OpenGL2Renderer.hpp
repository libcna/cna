#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformGlRendererState.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"

#include <memory>

namespace CNA::Internal::Renderers::OpenGL2
{
    // plans/plan_opengl2.md (context-loss recovery): mirrors EasyGL's own RecoverableResource/
    // ResourceRegistry design exactly (see easy-gl's ResourceRegistry.hpp/RecoverableResource.hpp),
    // deliberately reimplemented locally rather than depending on EasyGL/easy-gl -- consistent with
    // this renderer's own "deliberately independent of EasyGL" scope. Defined fully in the .cpp;
    // only forward-declared here so OpenGL2Renderer can hold a registry of them.
    class RecoverableResource;

    // Native desktop OpenGL 2.1 (compatibility profile) renderer. See plans/plan_opengl2.md for scope
    // and known follow-up work. Deliberately independent of EasyGL/easy-gl.
    class OpenGL2Renderer final : public IGraphicsRenderer
    {
    public:
        explicit OpenGL2Renderer(const GraphicsRendererCreateArgs& args);
        ~OpenGL2Renderer() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;
        void GetDefaultViewportRect(int& x, int& y, int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;

        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc, const std::string& fragSrc) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(int size, int depthFormat,
                                                                          bool preserveContents = false,
                                                                          bool mipMap = false,
                                                                          int multiSampleCount = 0) override;
        void SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;
        /// The pre-descriptor MRT core: binds 2..8 RenderTarget2D renderers to one shared MRT FBO.
        /// Reached only through the validating descriptor override above.
        void SetRenderTargetsArray(IRenderTargetRenderer* const* rts, int count);

        // CNAEXT: renderer-internal helper (not part of IGraphicsRenderer) mirroring
        // EasyGLRenderer::GetCurrentRenderTarget2DSize -- lets Sprite::Draw size its
        // screen->clip mapping to the bound render target instead of the window/virtual
        // resolution when one is active.
        bool GetCurrentRenderTarget2DSize(int& width, int& height) const;

        // True while a 2D render target (single or MRT set) is bound -- the state under which
        // this renderer flips Y at render time so target storage is top-down. Cube faces are
        // excluded by design. See the .cpp definition's doc comment for the full rationale.
        [[nodiscard]] bool RtFlipActive() const;

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;

        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;

        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;

        void DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
                                   const Matrix& projection, PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
                              const Matrix& projection, PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params) override;
        void DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount, int instanceCount,
                                       const GpuDrawParams& params) override;

        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend, int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
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
        // GraphicsDevice.ReferenceStencil is a standalone device property (see
        // IGraphicsRenderer.hpp's own doc comment) that must take effect without a full
        // DepthStencilState re-application -- re-applies the func/mask most recently cached by
        // ApplyDepthStencilState() alongside the new reference value.
        void SetReferenceStencil(int value) override;

        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;
        bool TransformWindowToLogical(float windowX, float windowY, float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY, float& windowX, float& windowY) const override;

        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        // plans/plan_opengl2.md (context-loss recovery): scope mirrors EasyGLRenderer's own real
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

        // CNAEXT: called by every recoverable resource's constructor/destructor (VB/IB/Tex/
        // RenderTarget/RenderTargetCubeRenderer/OcclusionQuery) -- not part of IGraphicsRenderer.
        void RegisterRecoverable(RecoverableResource* resource);
        void UnregisterRecoverable(RecoverableResource* resource);
        [[nodiscard]] bool IsContextRecoveryEnabled() const { return contextRecoveryEnabled_; }

        // CNAEXT: mirrors SdlGpuRenderer::LogicalViewport/ComputeLogicalViewport exactly
        // (see that renderer's own implementation -- the established reference for real
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
        // Declared before every GL resource-owning member so it outlives those resources.
        std::unique_ptr<PlatformGlContextOwner> platformContext_;
        PlatformGlSurfaceState surface_;
        // plans/plan_opengl2.md (context-loss recovery): the swap interval is per-context state --
        // remembered here so DebugSimulateContextLoss() can reapply the game's actual current
        // setting to the freshly-created context instead of silently reverting to the host's
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
        IRenderTargetRenderer* currentRt_{};
        IRenderTargetCubeRenderer* currentRtCube_{};
        // Set alongside currentRtCube_ by SetRenderTargetCubeFace() -- DebugSimulateContextLoss()
        // needs this to re-select the correct face after recreate_gl_resource() resets the cube
        // target's own internal boundFace back to 0 (see that resource's release_gl_handle_only()).
        int currentRtCubeFace_{};
        int currentRtWidth_{};
        int currentRtHeight_{};
        // MRT (SetRenderTargets, count > 1): one shared FBO, re-attached (glFramebufferTexture2D/
        // glFramebufferRenderbuffer per target + glDrawBuffers) on every call rather than cached
        // per render-target-set -- mirrors EasyGLRenderer::SetRenderTargets's own mrtFbo_
        // precedent. MRT targets are never tracked as currentRt_/currentRtCube_ (a single pointer
        // can't represent a whole set) -- mrtTargets_ below is the dedicated equivalent used only
        // by unbindCurrentRenderTarget()'s own MRT-specific per-target MSAA-resolve/mip-regen step.
        unsigned mrtFbo_{};
        bool mrtFboReady_{};
        // The IRenderTargetRenderer* set currently attached to mrtFbo_ (empty when not in MRT
        // mode) -- unbindCurrentRenderTarget() walks this to resolve each MSAA-enabled target's
        // own msaaColorRbo into its resolveFbo (one glBlitFramebuffer per target, since a single
        // blit can only resolve ONE selected read attachment at a time) and regenerate mips,
        // exactly mirroring RenderTarget::UnbindAsRenderTarget()'s own single-target equivalent.
        std::vector<IRenderTargetRenderer*> mrtTargets_;

        // plans/plan_opengl2.md (context-loss recovery): every currently-live recoverable resource
        // (VB/IB/Tex/RenderTarget/RenderTargetCubeRenderer/OcclusionQuery), registered/unregistered
        // by its own constructor/destructor -- mirrors EasyGL's own ResourceRegistry exactly,
        // reimplemented locally (see RecoverableResource's own forward-declaration comment).
        std::vector<RecoverableResource*> recoverableResources_;
        // SetContextRecoveryEnabled(false): gates future registrations only (matches
        // EasyGLRenderer::SetContextRecoveryEnabled's own doc comment -- "safe to call
        // after renderer creation when no resources have been loaded yet"); resources already
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

        // Cached by ApplyDepthStencilState(), re-used by SetReferenceStencil() to re-apply the
        // func/mask unchanged alongside a new reference value (glStencilFuncSeparate takes all
        // three together -- ReferenceStencil is otherwise a standalone GraphicsDevice property
        // that must take effect without a full DepthStencilState re-application).
        bool cachedStencilEnabled_ = false;
        bool cachedTwoSidedStencil_ = false;
        int cachedStencilFunc_ = 0;
        int cachedCcwStencilFunc_ = 0;
        unsigned cachedStencilMask_ = 0xFFFFFFFFu;

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
        void drawInternal(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                          const Matrix& world, const Matrix& view, const Matrix& projection,
                          PrimitiveType primitive, int primitiveCount, const GpuDrawParams* params);
    };
}
