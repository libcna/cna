#pragma once

// plans/plan_dx.md Phase DIRECTX2/DIRECTX4: D3D11 renderer skeleton + device/swap-chain/back-buffer.
// Windows-only (see CMakeLists.txt's FATAL_ERROR guard for non-Windows CNA_GRAPHICS_RENDERER=D3D11).

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformRendererSurfaceState.hpp"
#include "CNA/Internal/Renderers/D3DCommon/ID3DDeviceRecoverableEXT.hpp"
#include "D3D11InputLayoutCache.hpp"
#include "D3D11SamplerCache.hpp"
#include "D3D11StateObjectCache.hpp"

#include <d3d11.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

#if defined(CNA_DIRECTX11_COMPILED_EFFECTS)
typedef struct MOJOSHADER_d3d11Context MOJOSHADER_d3d11Context;

namespace Microsoft::Xna::Framework::Graphics
{
    class TextureCollection;
}
#endif

namespace CNA::Internal::Renderers::DirectX11
{
    using Microsoft::WRL::ComPtr;

    class D3D11RenderTargetRenderer;
    class D3D11RenderTargetCubeRenderer;
    class D3D11VertexBufferRenderer;
#if defined(CNA_DIRECTX11_COMPILED_EFFECTS)
    class D3D11CompiledEffect;
#endif

    /**
     * D3D11 graphics renderer (plans/plan_dx.md). Implements IGraphicsRenderer on top of Direct3D 11 via
     * DXGI: real device/swap-chain/back-buffer/clear/present/readback (Phase DIRECTX4), vertex/index
     * buffers + input layout caching (Phase DIRECTX5), textures/render targets/MSAA/MRT/occlusion
     * queries (Phase DIRECTX6), blend/depth-stencil/rasterizer state objects (Phase DIRECTX7), all 10 stock
     * shader variants + custom ShaderEffect (Phase DIRECTX8), and SpriteBatch (Phase DX9). Phase DX10
     * (broader test coverage) and DX11 (docs) are what remains unstarted.
     *
     * Resource lifetime is split into three independent groups (plans/plan_dx.md design decision 11):
     *   - Device lifetime (device_/context_/factory_/allowTearingSupported_/featureLevel_):
     *     created once in CreateDeviceResources(), only torn down on device-removed recovery.
     *   - Swap-chain lifetime (swapChain_): created once in CreateSwapChainResources(); a plain
     *     resize reuses the same object via ResizeBuffers(), never recreates it.
     *   - Window-size lifetime (backBufferRTV_/depthStencilView_/...): recreated on every resize
     *     AND on device-removed recovery, via CreateWindowSizeDependentViews().
     */
    class DirectX11Renderer final : public IGraphicsRenderer
    {
    public:
        explicit DirectX11Renderer(const GraphicsRendererCreateArgs& args);
        ~DirectX11Renderer() override;

        DirectX11Renderer(const DirectX11Renderer&) = delete;
        DirectX11Renderer& operator=(const DirectX11Renderer&) = delete;

        // ---- IGraphicsRenderer: real (Phase DIRECTX4) ----
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        /**
         * @brief Returns the physical back-buffer rectangle used for logical presentation.
         * @param x Receives the rectangle's left edge in drawable pixels.
         * @param y Receives the rectangle's top edge in drawable pixels.
         * @param width Receives the rectangle's width in drawable pixels.
         * @param height Receives the rectangle's height in drawable pixels.
         */
        void GetDefaultViewportRect(int& x, int& y, int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        /**
         * @brief Rebuilds the default back-buffer render surface with a device-supported sample count.
         * @param requestedMultiSampleCount Preferred number of samples; zero or one disables MSAA.
         * @return The sample count actually applied, or zero when multisampling is disabled.
         */
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;
        /** @brief Returns the sample count actually used by the default render surface. */
        [[nodiscard]] int GetMultiSampleCount() const override { return appliedMultiSampleCount_; }
        /**
         * @brief Maps a requested sample count to the count currently applied by this renderer.
         * @param requestedMultiSampleCount The caller's requested count.
         * @return The current device-clamped count.
         */
        [[nodiscard]] int GetAppliedMultiSampleCountEXT(
            int requestedMultiSampleCount) const override
        {
            (void) requestedMultiSampleCount;
            return appliedMultiSampleCount_;
        }
        /**
         * @brief Reports the fixed XNA surface format of the DXGI swap chain.
         * @param requestedFormat The caller's requested SurfaceFormat ordinal.
         * @return SurfaceFormat::Color, matching the actual R8G8B8A8_UNORM resource.
         */
        [[nodiscard]] int GetAppliedBackBufferFormatEXT(int requestedFormat) const override;
        /**
         * @brief Reports the fixed XNA depth format of the default D3D11 depth resource.
         * @param requestedFormat The caller's requested DepthFormat ordinal.
         * @return DepthFormat::Depth24Stencil8, matching the actual D24S8 resource.
         */
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(int requestedFormat) const override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        /** @brief Enables registration of subsequently-created resources for device recovery. */
        void SetContextRecoveryEnabled(bool enabled) override;
        /** @brief Reports whether the renderer currently accepts drawing commands. */
        [[nodiscard]] bool CanBeginDrawEXT() const override { return !deviceLost_; }
        /** @brief Enters the deterministic lost-device state used by lifecycle tests. */
        void DebugSimulateContextLoss() override;
        /** @brief Recreates the device and registered resources after a simulated loss. */
        void DebugRestoreContext() override;
        /**
         * @brief Returns the most recently requested DXGI presentation interval.
         * @return The exact interval most recently passed to SetSwapInterval.
         */
        CNAEXT [[nodiscard]] int GetSwapIntervalEXT() const override { return swapInterval_; }
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;
        /**
         * @brief Maps a platform-window point into the current logical presentation.
         * @param windowX Window-space X coordinate.
         * @param windowY Window-space Y coordinate.
         * @param logX Receives the logical X coordinate.
         * @param logY Receives the logical Y coordinate.
         * @return true when the point lies inside the presented image.
         */
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        /**
         * @brief Maps a logical point into platform-window coordinates.
         * @param logX Logical X coordinate.
         * @param logY Logical Y coordinate.
         * @param windowX Receives the window-space X coordinate.
         * @param windowY Receives the window-space Y coordinate.
         * @return true when presentation geometry is available.
         */
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;


        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        /// plans/plan_dx.md DX-212: TRUE, because the source handed to CreateEffectRenderer() genuinely
        /// determines the pixels here. The default is FALSE, which means "this renderer ACCEPTS an
        /// effect and keeps rendering with its own fixed path" -- the SOFTWARE/HEADLESS answer, and
        /// the reason a caller is told to ask this IN ADDITION to GraphicsCapability::CustomEffects.
        /// D3D11EffectRenderer::CompileProgram() runs a real D3DCompile() on the caller's HLSL and binds the resulting shader
        /// objects, so a post-process pass that believes its shader ran is right.
        [[nodiscard]] bool ExecutesShaderEffectSourceEXT() const override { return true; }

        /**
         * @brief Reports the complete runtime-backed D3D11 capability surface.
         *
         * @param capability Capability to query.
         * @return true only when this renderer and its current device implement the capability.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;

        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;

        /// Exposes the negotiated feature level for tests/diagnostics (CNAEXT).
        [[nodiscard]] D3D_FEATURE_LEVEL GetFeatureLevelEXT() const { return featureLevel_; }
        /// Exposes whether the debug layer actually ended up enabled (CNAEXT, DX-21 diagnostics).
        [[nodiscard]] bool IsDebugLayerEnabledEXT() const { return debugLayerEnabled_; }
        /// Exposes whether the swap chain was created tearing-capable (CNAEXT, DX-23 diagnostics).
        [[nodiscard]] bool IsTearingCapableEXT() const { return allowTearingSupported_ && allowTearingRequested_; }
        /// Exposes the raw device pointer for tests/diagnostics and for D3DCommon helpers (e.g.
        /// D3DShaderCache, DX-15-embed) that need a real ID3D11Device* without duplicating this
        /// renderer's own device-creation path (CNAEXT).
        [[nodiscard]] ID3D11Device* GetDeviceEXT() const { return device_.Get(); }
        /// Exposes the real device context for D3DCommon/tests that need to issue Map/Unmap or
        /// draw calls without duplicating this renderer's own context-creation path (CNAEXT,
        /// DX-30/DX-31's buffer renderers both need this).
        [[nodiscard]] ID3D11DeviceContext* GetContextEXT() const { return context_.Get(); }
        /** @brief Registers a live resource for D3D11 device recreation. */
        void RegisterRecoverableResourceEXT(D3DCommon::ID3DDeviceRecoverableEXT* resource);
        /** @brief Removes a live resource from the D3D11 recovery registry. */
        void UnregisterRecoverableResourceEXT(D3DCommon::ID3DDeviceRecoverableEXT* resource) noexcept;
        /** @brief Returns the number of resources currently tracked for recovery. */
        [[nodiscard]] std::size_t GetRecoverableResourceCountEXT() const noexcept
        {
            return recoverableResources_.size();
        }
        /** @brief Recreates the D3D11 device domain and every registered resource. */
        void RecreateDeviceEXT();
        /**
         * @brief Returns the active SpriteBatch projection size in logical viewport units.
         * @param width Receives the logical viewport width.
         * @param height Receives the logical viewport height.
         */
        CNAEXT void GetSpriteViewportSizeEXT(float& width, float& height) const;
        /// Exposes the per-(shader,stride) ID3D11InputLayout cache (CNAEXT, DX-32) -- shared by
        /// tests now and by Phase DIRECTX8's draw-call wiring later.
        [[nodiscard]] D3D11InputLayoutCache& GetInputLayoutCacheEXT() { return inputLayoutCache_; }

        // ---- IGraphicsRenderer: real (Phase DIRECTX5) ----
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;

        // ---- IGraphicsRenderer: real (Phase DIRECTX6) ----
        /** @brief Classifies core XNA surface formats backed by native D3D11 storage. */
        [[nodiscard]] RendererFormatVerdict ClassifySurfaceFormatEXT(int surfaceFormat) const override;
        /**
         * @brief Classifies XNA render-target formats using actual D3D11 device support.
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return Supported only when the format is an XNA render-target format that this device
         *         can render to and sample.
         */
        [[nodiscard]] RendererFormatVerdict ClassifyRenderTargetFormatEXT(int surfaceFormat) const override;
        /** @brief Restricts Color-shaped transfers to actual Color storage. */
        [[nodiscard]] RendererFormatVerdict ClassifyColorTransferFormatEXT(int surfaceFormat) const override;
        /**
         * @brief Reports whether D3D11 transfers the specified surface format as compressed blocks.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return true for the core XNA Dxt1, Dxt3, and Dxt5 formats.
         */
        [[nodiscard]] bool IsCompressedTransferFormatEXT(int surfaceFormat) const override;
        /**
         * @brief Reports whether D3D11 accepts compressed block transfers for cube faces.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return true for the core XNA Dxt1, Dxt3, and Dxt5 formats.
         */
        [[nodiscard]] bool IsCompressedCubeTransferFormatEXT(int surfaceFormat) const override;
        /**
         * @brief Keeps supported compressed content in its native block-compressed form.
         *
         * @return true because D3D11 uploads XNA DXT blocks without CPU decompression.
         */
        [[nodiscard]] bool LoadsCompressedContentNativelyEXT() const override;
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        /**
         * @brief Creates a D3D11 2D render target in the requested XNA surface format.
         * @param w Width in pixels.
         * @param h Height in pixels.
         * @param depthFormat DepthFormat ordinal.
         * @param preserveContents Whether previous contents should be preserved.
         * @param mipMap Whether to allocate a full mip chain.
         * @param multiSampleCount Requested sample count.
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return The native D3D11 render target.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2DEXT(
            int w, int h, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(int size, int depthFormat,
                                                                          bool preserveContents = false,
                                                                          bool mipMap = false,
                                                                          int multiSampleCount = 0) override;
        /**
         * @brief Creates a D3D11 cube render target in the requested XNA surface format.
         * @param size Face width and height in pixels.
         * @param depthFormat DepthFormat ordinal.
         * @param preserveContents Whether previous contents should be preserved.
         * @param mipMap Whether to allocate a full mip chain.
         * @param multiSampleCount Requested sample count.
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return The native D3D11 cube render target.
         */
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCubeEXT(
            int size, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;
        /// REMED-GFX-134: overrides IGraphicsRenderer's default so a bound cube face is TRACKED and
        /// therefore finalized (MSAA resolve + mip regeneration) when the binding changes.
        void SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face) override;
        void ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy) override;
        /// plans/plan_dx.md DX-216: SamplerState.MaxMipLevel (XNA's MOST DETAILED level index, i.e. D3D's
        /// MinLOD) and MipMapLevelOfDetailBias (MipLODBias). Tracked per slot and applied by
        /// rebuilding that slot's ID3D11SamplerState, which is the only granularity D3D11 offers.
        void ApplySamplerMipState(int slot, int maxMipLevel, float lodBias) override;
        /// DX-216: SamplerState.AddressW. The cache used to mirror AddressV, which the interface
        /// documentation names as the one thing a renderer that has not implemented W must not do.
        void ApplySamplerAddressW(int slot, int addressW) override;
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;

        // ---- IGraphicsRenderer: real (Phase DIRECTX8, DX-58 -- custom ShaderEffect) ----
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;

        // ---- IGraphicsRenderer: real (Phase DIRECTX7) ----
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                    int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        void ApplyRasterizerState(int cullMode, int fillMode,
                                  bool scissorTestEnable,
                                  float depthBias = 0.0f,
                                  float slopeScaleDepthBias = 0.0f) override;
        void SetBlendFactor(float r, float g, float b, float a) override;
        void SetReferenceStencil(int value) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /// Exposes the blend/depth-stencil/rasterizer state caches (CNAEXT) -- Phase DIRECTX8's draw-call
        /// wiring reuses these directly rather than re-implementing the same caching.
        [[nodiscard]] D3D11BlendStateCache& GetBlendStateCacheEXT() { return blendStateCache_; }
        [[nodiscard]] D3D11DepthStencilStateCache& GetDepthStencilStateCacheEXT() { return depthStencilStateCache_; }
        [[nodiscard]] D3D11RasterizerStateCache& GetRasterizerStateCacheEXT() { return rasterizerStateCache_; }

        /// CNAEXT (Phase DIRECTX6): updates Clear()'s target-RTV/DSV tracking to point at a custom
        /// render target's own views. Called by D3D11RenderTargetRenderer::BindAsRenderTarget()/
        /// D3D11RenderTargetCubeRenderer::BindAsRenderTargetFace() -- those own the real
        /// OMSetRenderTargets()/viewport call, this only updates what Clear() (and friends) target
        /// next, since D3D11 has no single "currently bound FBO" the renderer can query back.
        void TrackCurrentRenderTargetEXT(ID3D11RenderTargetView* const* rtvs, int count, ID3D11DepthStencilView* dsv);
        /// CNAEXT (Phase DIRECTX6): restores the real back-buffer OM binding + viewport, and Clear()'s
        /// tracking to match. Called by D3D11RenderTargetRenderer/D3D11RenderTargetCubeRenderer's
        /// own UnbindAsRenderTarget() (after any MSAA resolve / mip regeneration they still need
        /// to do), and internally whenever SetRenderTarget2D(nullptr)/SetRenderTargets(nullptr, 0)
        /// is used to go straight back to the back buffer.
        void RestoreBackBufferRenderTargetEXT();
        /** @brief Detaches a dying 2D target from every non-owning current-binding slot. */
        void NotifyRenderTargetDestroyedEXT(D3D11RenderTargetRenderer* target) noexcept;
        /** @brief Detaches a dying cube target from the non-owning current cube binding. */
        void NotifyRenderTargetCubeDestroyedEXT(D3D11RenderTargetCubeRenderer* target) noexcept;
        /** @brief Reports whether a 2D target occupies the active single-target or MRT binding. */
        [[nodiscard]] bool IsRenderTargetActiveEXT(
            const D3D11RenderTargetRenderer* target) const noexcept;
        /** @brief Returns a weak token that expires before this renderer can be dereferenced. */
        [[nodiscard]] std::weak_ptr<void> GetLifetimeTokenEXT() const noexcept
        {
            return lifetimeToken_;
        }

        // ---- IGraphicsRenderer: real (Phase DIRECTX8, DX-61) ----
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

        // ---- IGraphicsRenderer: real (Phase DIRECTX8, DX-62/DX-63/DX-64/DX-69-partial) ----
        // Covers stride 16/20/24 (colored3d/textured3d/colored_textured3d, sharing D3DPerDrawConstants/
        // D3DFogConstants), stride 32 (lit_textured3d, DX-63) and alpha-test (DX-64, any of those 3
        // strides -- alpha_test3d's HLSL is stride-agnostic). DualTexture/EnvMap/Skinned (DX-65/66/67)
        // still throw a named "not yet implemented" error -- explicitly out of this task's scope.
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;

        // ---- IGraphicsRenderer: real (Phase DIRECTX8, DX-68 -- instanced3d) ----
        void DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount, int instanceCount,
                                       const GpuDrawParams& params) override;

        // ---- IGraphicsRenderer: real (Phase DX9, DX-70/DX-71/DX-72) ----
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;

#if defined(CNA_DIRECTX11_COMPILED_EFFECTS)
        /** @brief Creates a MojoShader-backed compiled XNA effect for this D3D11 device. */
        std::unique_ptr<ICompiledEffectRuntime> CreateCompiledEffect(
            const std::uint8_t* effectCode, std::size_t effectCodeBytes) override;

        /** @brief Reports compiled effects only in the fully conformance-gated opt-in build. */
        [[nodiscard]] bool SupportsCompiledEffects() const override { return true; }

        /** @brief Returns the one MojoShader D3D11 context associated with this native device. */
        CNAEXT MOJOSHADER_d3d11Context* GetMojoShaderContextEXT();

        /**
         * @brief Binds an applied compiled effect for an immediate D3D11 draw.
         * @param fallback Primary vertex buffer used when @p params has no explicit stream list.
         * @param params Complete public draw parameters and vertex stream bindings.
         * @param runtime Applied compiled-effect runtime.
         * @param spriteBatchSlotZeroTexture Optional SpriteBatch source that must win pixel slot 0.
         * @param spriteBatchTextures Optional public texture collection used for unassigned slots.
         */
        CNAEXT void BindCompiledEffectForDrawEXT(
            const D3D11VertexBufferRenderer& fallback,
            const GpuDrawParams& params,
            ICompiledEffectRuntime& runtime,
            const ITextureRenderer* spriteBatchSlotZeroTexture = nullptr,
            const Microsoft::Xna::Framework::Graphics::TextureCollection*
                spriteBatchTextures = nullptr);
#endif

    private:
        std::shared_ptr<void> lifetimeToken_ = std::make_shared<int>(0);

        void CreateDeviceResources();
        void CreateSwapChainResources();
        void CreateWindowSizeDependentViews();
        void RecreateDefaultRenderSurfaces(int requestedMultiSampleCount);
        void ResolveBackBufferMsaa();
        [[nodiscard]] ID3D11RenderTargetView* GetBackBufferDrawRtv() const;
        void ReleaseWindowSizeDependentViews();
        /// DX-29: resize handling -- touches ONLY the window-size group + ResizeBuffers() on the
        /// existing swap chain. Called lazily from Present() when the platform surface size no longer
        /// matches the swap chain's own cached size.
        void EnsureSwapChainSize();
        /// Routes a native device-removed result into the shared lost-device state.
        void CheckDeviceRemoved(HRESULT hr);

        /// DX-62/DX-63/DX-64: shared implementation for DrawPrimitivesEx/DrawIndexedPrimitivesEx --
        /// @p ib is nullptr for the non-indexed path (context_->Draw), non-null for the indexed path
        /// (context_->DrawIndexed), avoiding duplicating the whole variant-selection/constant-buffer
        /// block between the two public overrides (they differ only in index-buffer binding + which
        /// of Draw/DrawIndexed is finally called).
        void DrawPrimitivesExImpl(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                  PrimitiveType primitive, int primitiveCount,
                                  const GpuDrawParams& params);

        [[nodiscard]] Matrix ApplyXnaPixelCenterEXT(const Matrix& transform) const;

        PlatformRendererSurfaceState surface_;
        HWND hwnd_ = nullptr;
        int width_ = 0;
        int height_ = 0;

        // Device lifetime (plans/plan_dx.md design decision 11).
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        ComPtr<IDXGIFactory2> factory_;
        bool allowTearingSupported_ = false;
        bool debugLayerEnabled_ = false;
        D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_11_0;
        bool contextRecoveryEnabled_ = true;
        bool deviceLost_ = false;
        std::function<void(RendererDeviceEvent)> deviceEventCallback_;
        std::vector<D3DCommon::ID3DDeviceRecoverableEXT*> recoverableResources_;
#if defined(CNA_DIRECTX11_COMPILED_EFFECTS)
        MOJOSHADER_d3d11Context* mojoShaderContext_ = nullptr;
#endif

        // Swap-chain lifetime.
        ComPtr<IDXGISwapChain1> swapChain_;

        // Window-size lifetime.
        ComPtr<ID3D11Texture2D> backBufferTexture_;
        ComPtr<ID3D11RenderTargetView> backBufferRTV_;
        ComPtr<ID3D11Texture2D> backBufferMsaaTexture_;
        ComPtr<ID3D11RenderTargetView> backBufferMsaaRTV_;
        ComPtr<ID3D11Texture2D> depthStencilTexture_;
        ComPtr<ID3D11DepthStencilView> depthStencilView_;

        // Phase DIRECTX6: tracks whatever Clear()/ClearX should actually target -- the back buffer by
        // default, or a custom render target's own views once SetRenderTarget2D/SetRenderTargets
        // binds one (D3D11 has no globally-queryable "current FBO" the way GL does, so this
        // renderer must track it explicitly; see TrackCurrentRenderTargetEXT/
        // RestoreBackBufferRenderTargetEXT). Raw, non-owning pointers -- lifetime is owned by
        // backBufferRTV_/depthStencilView_ or by whichever D3D11RenderTargetRenderer is currently
        // bound (which outlives the binding, owned by the caller's RenderTarget2D/Cube).
        ID3D11RenderTargetView* currentColorRTVs_[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        int currentRTVCount_ = 0;
        ID3D11DepthStencilView* currentDSV_ = nullptr;
        /// Non-owning; nullptr means the back buffer is the active render target. Used so
        /// SetRenderTarget2D/SetRenderTargets can finalize (MSAA resolve / mip regen) whatever was
        /// previously bound before switching to something else, matching how GraphicsDevice only
        /// ever calls SetRenderTarget2D(nullptr) to signal "go back to the back buffer" rather
        /// than calling the old target's UnbindAsRenderTarget() itself.
        D3D11RenderTargetRenderer* currentCustomRT_ = nullptr;

        /// REMED-GFX-134: the same "finalize whatever was previously bound" need as
        /// `currentCustomRT_`, for a cube face. Nothing tracked a bound RenderTargetCube before, so
        /// `D3D11RenderTargetCubeRenderer::UnbindAsRenderTarget()` -- which is where this renderer's
        /// per-face `ResolveSubresource()` and `GenerateMips()` live -- was never reached from the
        /// SetRenderTarget/SetRenderTargets path at all: a multisampled cube target's resolve
        /// texture stayed empty and a mipMap=true cube target's levels above 0 were never
        /// regenerated. Non-owning, same lifetime reasoning as `currentCustomRT_`.
        D3D11RenderTargetCubeRenderer* currentCubeRT_ = nullptr;
        /// REMED-GFX-134: finalizes and forgets the currently tracked cube target, if any.
        void FlushPendingCubeResolveEXT();

        // DX-143: same "finalize whatever was previously bound before switching away" need as
        // currentCustomRT_ above, but for the MRT (N>1) path -- SetRenderTargets() deliberately
        // doesn't set currentCustomRT_ for an MRT bind (a single pointer can't represent N
        // targets), so without this, none of an MRT set's individual MSAA-resolve/mip-regen ever
        // ran when the set was replaced/unbound (the real gap this task closes). Non-owning, same
        // lifetime reasoning as currentCustomRT_.
        D3D11RenderTargetRenderer* currentMRTTargets_[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        int currentMRTCount_ = 0;
        /// DX-143: if an MRT set is currently tracked, finalizes each of its targets for real
        /// (MSAA resolve + mip regeneration, via D3D11RenderTargetRenderer::ResolveAndGenerateMipsEXT())
        /// then clears the tracking -- called at the very start of SetRenderTarget2D()/
        /// SetRenderTargets() so every path through either function finalizes a prior MRT bind
        /// before doing anything else. No-op if no MRT set is currently tracked.
        void FlushPendingMRTResolveEXT();

        // Phase DIRECTX6 (DX-44): sampler-state cache shared by ApplySamplerState().
        D3D11SamplerCache samplerCache_;
        /// DX-216: the XNA-level SamplerState this renderer last applied per slot. D3D11 bakes the
        /// whole state into one immutable sampler object, so a change to any single field means
        /// rebuilding that slot's sampler from all of them -- which needs all of them remembered.
        /// Defaults match SamplerState::LinearWrap plus XNA's own MaxMipLevel = 0 / bias = 0.
        static constexpr int kMaxSamplerSlotsEXT = 16;
        int samplerFilter_[kMaxSamplerSlotsEXT] = {};
        int samplerAddressU_[kMaxSamplerSlotsEXT] = {};
        int samplerAddressV_[kMaxSamplerSlotsEXT] = {};
        int samplerAddressW_[kMaxSamplerSlotsEXT] = {};
        int samplerMaxAnisotropy_[kMaxSamplerSlotsEXT] = {4, 4, 4, 4, 4, 4, 4, 4,
                                                         4, 4, 4, 4, 4, 4, 4, 4};
        int samplerMaxMipLevel_[kMaxSamplerSlotsEXT] = {};
        float samplerLodBias_[kMaxSamplerSlotsEXT] = {};
        /// Rebuilds and binds slot @p slot's sampler from the tracked fields above.
        void RebindSamplerEXT(int slot);

        // Phase DIRECTX7 (DX-50/DX-51/DX-52): blend/depth-stencil/rasterizer state caches, plus the
        // currently-bound state objects and the two standalone-settable values (blend factor,
        // reference stencil) that OMSetBlendState()/OMSetDepthStencilState() need re-supplied on
        // every bind but which XNA allows changing independently of a full Apply*State() call
        // (GraphicsDevice.BlendFactor, GraphicsDevice.ReferenceStencil -- Task 870/319).
        D3D11BlendStateCache blendStateCache_;
        D3D11DepthStencilStateCache depthStencilStateCache_;
        D3D11RasterizerStateCache rasterizerStateCache_;
        ComPtr<ID3D11BlendState> currentBlendState_;
        ComPtr<ID3D11DepthStencilState> currentDepthStencilState_;
        float currentBlendFactor_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        /// REMED-GFX-077: BlendState.MultiSampleMask — the dynamic SampleMask argument to
        /// OMSetBlendState (not part of the ID3D11BlendState object, so not cached/keyed). Defaults
        /// to 0xFFFFFFFF (all samples), matching XNA's default MultiSampleMask (-1).
        UINT currentSampleMask_ = 0xFFFFFFFFu;
        int currentReferenceStencil_ = 0;

        // The full depth-stencil parameter set currently applied. Tracked field-by-field (not just
        // as the finished ID3D11DepthStencilState above) so SetDepthTestEnabled()/
        // SetDepthWriteEnabled() -- which each carry only ONE bool -- can rebuild the state with
        // just that field changed instead of silently doing nothing. Mirrors D3D12's own
        // current*_ tracking. Defaults match XNA's DepthStencilState.Default.
        bool dsDepthEnable_ = true;
        bool dsDepthWriteEnable_ = true;
        int dsDepthFunc_ = 3;            // CompareFunction::LessEqual
        bool dsStencilEnable_ = false;
        int dsStencilFunc_ = 0;          // CompareFunction::Always
        int dsStencilPass_ = 0;          // StencilOperation::Keep
        int dsStencilFail_ = 0;
        int dsStencilDepthFail_ = 0;
        int dsStencilMask_ = 0xFF;
        int dsStencilWriteMask_ = 0xFF;
        bool dsTwoSidedStencilMode_ = false;
        int dsCcwStencilFunc_ = 0;
        int dsCcwStencilPass_ = 0;
        int dsCcwStencilFail_ = 0;
        int dsCcwStencilDepthFail_ = 0;

        /// Rebuilds + binds the depth-stencil state from the tracked ds*_ fields above. Shared by
        /// ApplyDepthStencilState(), SetDepthTestEnabled() and SetDepthWriteEnabled().
        void RebindDepthStencilState();

        // XNA DepthBias is normalized, while D3D11 stores depth-format-dependent integer units.
        // Keep the XNA state so a D16/D24 target switch can rebuild the native rasterizer state.
        int rsCullMode_ = 2;
        int rsFillMode_ = 0;
        bool rsScissorTestEnable_ = false;
        float rsDepthBias_ = 0.0f;
        float rsSlopeScaleDepthBias_ = 0.0f;
        bool rasterizerStateApplied_ = false;
        void RebindRasterizerState();

        // Presentation policy (plans/plan_dx.md design decision 13: capability vs. policy, kept separate).
        bool vsyncEnabled_ = true;
        bool allowTearingRequested_ = true;
        bool exclusiveFullscreen_ = false;
        int swapInterval_ = 1;

        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        int requestedMultiSampleCount_ = 0;
        int appliedMultiSampleCount_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;

        // Phase DIRECTX5 (DX-32): per-(shader,stride) ID3D11InputLayout cache.
        D3D11InputLayoutCache inputLayoutCache_;

        // Phase DIRECTX8 (DX-60/DX-61): persistent, lazily-created dynamic constant buffers for the
        // colored3d pipeline's PerDraw (b0) / FogParams (b1) cbuffers -- created once, updated via
        // Map(WRITE_DISCARD)/Unmap on every DrawColoredPrimitives() call rather than recreated per
        // draw (mirrors D3D11Buffers.hpp's own "grow, never recreate" discipline).
        ComPtr<ID3D11Buffer> perDrawConstantBuffer_;
        ComPtr<ID3D11Buffer> fogConstantBuffer_;
        ID3D11Buffer* GetOrCreatePerDrawConstantBufferEXT();
        ID3D11Buffer* GetOrCreateFogConstantBufferEXT();
        void UpdateDynamicConstantBufferEXT(ID3D11Buffer* buffer, const void* data, std::size_t byteCount);

        // Phase DIRECTX8 (DX-63/DX-64): same "grow, never recreate" persistent dynamic constant buffers
        // as above, one each for lit_textured3d's LitLightParams (b1) and alpha_test3d's single
        // combined PerDraw (b0) cbuffer.
        ComPtr<ID3D11Buffer> lightingConstantBuffer_;
        ComPtr<ID3D11Buffer> alphaTestConstantBuffer_;
        ID3D11Buffer* GetOrCreateLightingConstantBufferEXT();
        ID3D11Buffer* GetOrCreateAlphaTestConstantBufferEXT();

        // Phase DIRECTX8 (DX-65/DX-66/DX-67): same "grow, never recreate" persistent dynamic constant
        // buffers, one each for dual_texture3d's own FogParams (b2) instance (kept separate from
        // fogConstantBuffer_ even though the struct shape is identical, D3DFogConstants -- purely
        // to avoid coupling an unrelated variant's bind-slot lifetime to this one), env_map3d's
        // PerDraw (b0)/EnvMapParams (b2), skinned3d's BoneBlock (b1)/FogParams (b2, D3DSkinnedExtraConstants).
        ComPtr<ID3D11Buffer> dualTexFogConstantBuffer_;
        ComPtr<ID3D11Buffer> envMapPerDrawConstantBuffer_;
        ComPtr<ID3D11Buffer> envMapConstantBuffer_;
        ComPtr<ID3D11Buffer> boneConstantBuffer_;
        ComPtr<ID3D11Buffer> skinnedExtraConstantBuffer_;
        ID3D11Buffer* GetOrCreateDualTexFogConstantBufferEXT();
        ID3D11Buffer* GetOrCreateEnvMapPerDrawConstantBufferEXT();
        ID3D11Buffer* GetOrCreateEnvMapConstantBufferEXT();
        ID3D11Buffer* GetOrCreateBoneConstantBufferEXT();
        ID3D11Buffer* GetOrCreateSkinnedExtraConstantBufferEXT();

        // plans/plan_cnj.md CNB-58 follow-up: same "grow, never recreate" persistent dynamic constant
        // buffers as above, for pbr3d/pbr_skinned3d's own PerDraw (b0, D3DPbrPerDrawConstants) and
        // PbrLights (b1 unskinned / b2 skinned, D3DPbrLightConstants) cbuffers. PbrSkinned3d's own
        // BoneBlock (b1) reuses boneConstantBuffer_/GetOrCreateBoneConstantBufferEXT() above
        // unchanged -- D3DBoneConstants is shape-identical to skinned3d's own BoneBlock.
        ComPtr<ID3D11Buffer> pbrPerDrawConstantBuffer_;
        ComPtr<ID3D11Buffer> pbrLightsConstantBuffer_;
        ID3D11Buffer* GetOrCreatePbrPerDrawConstantBufferEXT();
        ID3D11Buffer* GetOrCreatePbrLightsConstantBufferEXT();

        // plans/plan_cnj.md CNB-58 follow-up: lazily-created 1x1 fallback SRVs for PbrEffect's optional
        // normal/metallic-roughness/emissive/occlusion maps when GpuDrawParams leaves the
        // corresponding pointer null -- mirrors EasyGLRenderer's own
        // EnsureDefaultWhiteTexture()/EnsureDefaultFlatNormalTexture() fallback textures so "map
        // absent" reads as the correct neutral BRDF input on this renderer too (flat tangent-space
        // normal (128,128,255,255); factor-only/no-emissive/fully-lit (255,255,255,255) for the
        // other three maps, matching EasyGL's own documented mapping one-for-one).
        ComPtr<ID3D11Texture2D> defaultWhiteTexture_;
        ComPtr<ID3D11ShaderResourceView> defaultWhiteSrv_;
        ComPtr<ID3D11Texture2D> defaultFlatNormalTexture_;
        ComPtr<ID3D11ShaderResourceView> defaultFlatNormalSrv_;
        ID3D11ShaderResourceView* GetOrCreateDefaultWhiteSrvEXT();
        ID3D11ShaderResourceView* GetOrCreateDefaultFlatNormalSrvEXT();

    };
}
