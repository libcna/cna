#pragma once

// plan_dx9.md Phase D9-3 (D9-30/D9-31): real Direct3DCreate9/CreateDevice + Clear/Present/
// ReadBackbuffer. Windows-only (see CMakeLists.txt's FATAL_ERROR guard for non-Windows
// CNA_GRAPHICS_BACKEND=D3D9).
//
// Unlike D3D11/D3D12, this backend does not use D3DCommon (plan_dx9.md design decision 12 --
// D3DFORMAT is a different enum space from DXGI_FORMAT, and D3D9 has no state objects at all).
// Unlike D3D11, D3D9 has no separate device/swap-chain split -- CreateDevice() creates the
// implicit swap chain (back buffer + optional depth-stencil) in the same call.

#include "../Common/IGraphicsBackend.hpp"

#include <d3d9.h>
#include <wrl/client.h>

namespace CNA::Internal::Backends::D3D9
{
    using Microsoft::WRL::ComPtr;

    /**
     * D3D9 graphics backend (plan_dx9.md). Implements IGraphicsBackend on top of plain Direct3D 9
     * (Direct3DCreate9, not D3D9Ex -- design decision 2), targeting Microsoft's own XNA 4.0 Stock
     * Effects HLSL compiled to real vs_2_0/ps_2_0 bytecode (design decision 3/5), with the
     * project's stated goal of pixel-for-pixel indistinguishability from the original XNA 4.0
     * runtime, not mere feature parity.
     *
     * Phase D9-3 (this revision): real device creation using the game's actual requested
     * back-buffer/depth-stencil format, fullscreen flag, and swap interval (via the project
     * owner-approved additive GraphicsBackendCreateArgs extension -- see plan_dx9.md's
     * "IGraphicsBackend boundary problem" section) -- not D3D11's own hardcoded-format precedent,
     * which is fine for D3D11 (parity, not authenticity) but would be a direct fidelity violation
     * here. Clear()/all 6 Clear* combos/Present()/ReadBackbuffer() are real. Everything else
     * (buffers, textures, draws, SpriteBatch) still throws NotYetImplemented() naming its own
     * follow-up task.
     */
    class D3D9GraphicsBackend final : public IGraphicsBackend
    {
    public:
        explicit D3D9GraphicsBackend(const GraphicsBackendCreateArgs& args);
        ~D3D9GraphicsBackend() override;

        D3D9GraphicsBackend(const D3D9GraphicsBackend&) = delete;
        D3D9GraphicsBackend& operator=(const D3D9GraphicsBackend&) = delete;

        // ---- IGraphicsBackend: pure virtual, real (trivial bookkeeping; no device needed) ----
        SDL_Window* GetWindowInternal() const override { return window_; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        /// D9-30/D9-33 (found empirically): GraphicsDevice::Reset() calls this so a
        /// GraphicsDeviceManager preference set AFTER this backend's initial construction (the
        /// common case) still reaches a real, honored format -- see this method's own base-class
        /// doc comment (IGraphicsBackend::UpdatePresentationFormatEXT) for the full rationale.
        /// Only updates the tracked fields; the actual device Reset() happens lazily from the next
        /// Present() via EnsureDeviceSize(), same timing EasyGL/D3D11 already use for resize.
        void UpdatePresentationFormatEXT(int backBufferFormat, int depthStencilFormat, bool isFullScreen) override;

        // ---- IGraphicsBackend: real (Phase D9-3, D9-31) ----
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        // ---- IGraphicsBackend: pure virtual, NotYetImplemented until later D3D9 tasks land ----
        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        // ---- IGraphicsBackend: real (D9-30/D9-6 -- GraphicsDevice's own constructor unconditionally
        // pushes BlendState::Opaque/DepthStencilState::Default/RasterizerState::CullCounterClockwise
        // and the viewport right after construction (Task 896/955, UpdateViewportFromWindow()), so
        // none of these can stay throwing stubs once a real device exists -- found empirically: the
        // basic Game/GraphicsDeviceManager construction path does not even complete without them.
        // design decision 11: render state, not state objects -- plain SetRenderState() sequences,
        // using the D9-21 D3D9StateMapping tables; nothing to cache. ) ----
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                              int colorDstBlend, int alphaDstBlend,
                              int colorBlendFunc, int alphaBlendFunc) override;
        void SetBlendFactor(float r, float g, float b, float a) override;
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                     int depthFunc,
                                     bool stencilEnable, int stencilFunc,
                                     int stencilPass, int stencilFail, int stencilDepthFail,
                                     int stencilMask, int stencilWriteMask, int referenceStencil,
                                     bool twoSidedStencilMode,
                                     int ccwStencilFunc, int ccwStencilPass,
                                     int ccwStencilFail, int ccwStencilDepthFail) override;
        void SetReferenceStencil(int value) override;
        void ApplyRasterizerState(int cullMode, int fillMode,
                                  bool scissorTestEnable,
                                  float depthBias = 0.0f,
                                  float slopeScaleDepthBias = 0.0f) override;
        void SetScissorRect(int x, int y, int w, int h) override;

        // ---- IGraphicsBackend: silently-empty-default virtuals, explicitly loud until real ----
        // (D9-11's own distinction: these have a `{}` default on IGraphicsBackend itself, so an
        // un-overridden call would silently no-op instead of failing -- override each so a missing
        // capability is a build-time-enforced, loud runtime failure instead. NOTE: this list
        // originally missed ApplyBlendState/ApplyDepthStencilState/ApplyRasterizerState/
        // ApplySamplerState -- their `{}` default spans multiple lines, so a single-line `grep
        // 'virtual.*{}$'` (D9-11's own suggested method) does not find them. The first 3 are real
        // above (forced by this same discovery); ApplySamplerState is listed here since no texture/
        // sampler work exists yet to make it real.)
        void SetSwapInterval(int interval) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        void ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy) override;
        void SetContextRecoveryEnabled(bool enabled) override;
        void SetStringMarkerEXT(const char* marker) override;
        void DebugSimulateContextLoss() override;
        void DebugRestoreContext() override;

        // Every other IGraphicsBackend virtual (e.g. CreateOcclusionQuery, CreateTexture3D)
        // already has a throwing or harmlessly-inert default on the base interface -- left
        // un-overridden here on purpose (plan_dx9.md D9-11): inheriting "throws" is fine, only
        // inheriting silence is the trap.

        /// Exposes the real IDirect3DDevice9 for tests/diagnostics and later D3D9 backend files
        /// (buffers/textures/draws) that need it without duplicating this backend's own device-
        /// creation path (NOXNA, mirrors D3D11GraphicsBackend::GetDeviceEXT()).
        [[nodiscard]] IDirect3DDevice9* GetDeviceEXT() const { return device_.Get(); }
        /// Exposes the real D3DCAPS9 queried at device-creation time (NOXNA, D9-32 profile
        /// enforcement and diagnostics/tests both need this without re-querying).
        [[nodiscard]] const D3DCAPS9& GetCapsEXT() const { return caps_; }
        /// Exposes whether this backend currently considers its device lost (NOXNA, D9-34
        /// diagnostics/tests). Mirrors GraphicsDevice::GraphicsDeviceStatus at the backend level.
        [[nodiscard]] bool IsDeviceLostEXT() const { return deviceLost_; }

    private:
        void CreateDeviceResources(const GraphicsBackendCreateArgs& args);
        /// D9-33/D9-34: (re)builds currentPresentParams_ from the tracked width_/height_/format/
        /// fullscreen/swap-interval fields -- shared by construction, resize, and device-lost
        /// recovery, all of which need the identical D3DPRESENT_PARAMETERS shape.
        D3DPRESENT_PARAMETERS BuildPresentParameters() const;
        /// D9-33: D3D9 has no ResizeBuffers -- a window resize (or a presentation-format change
        /// reported via UpdatePresentationFormatEXT()) is a device Reset() with updated
        /// width_/height_/format fields. Called lazily from Present() (mirrors D3D11's own
        /// EnsureSwapChainSize(), checked every Present() rather than reacting to an OS resize
        /// event) when the SDL window's actual pixel size no longer matches what the device was
        /// created/last reset at, OR presentationDirty_ was set.
        void EnsureDeviceSize();
        /// D9-34: real XNA device-lost lifecycle. Called every Present() while deviceLost_ is true.
        /// Polls TestCooperativeLevel(): D3DERR_DEVICELOST -> still lost, do nothing more this
        /// frame; D3DERR_DEVICENOTRESET -> fire DeviceResetting, Reset(), restore the viewport,
        /// fire DeviceReset, clear deviceLost_. D3DPOOL_DEFAULT resources would need to be released
        /// before Reset() and recreated after -- none exist yet beyond the implicit swap chain/back
        /// buffer/depth-stencil surface, which Reset() itself recreates; a real resource type
        /// landing later (D9-4/D9-5) must hook into this method, not reinvent its own recovery path.
        void PollDeviceLost();
        /// D9-34: the actual Resetting->Reset()->Reset event sequence, shared by the real
        /// TestCooperativeLevel()-driven path (PollDeviceLost()) and DebugRestoreContext()'s
        /// simulated one (which skips the TestCooperativeLevel wait since nothing really lost the
        /// device -- Reset() itself is real either way).
        void PerformResetRecovery();
        /// D9-34: throws Microsoft::Xna::Framework::Graphics::DeviceLostException if the device is
        /// currently lost -- called at the top of every rendering entry point (Clear/ClearX/
        /// ReadBackbuffer), matching real XNA's behavior of refusing to render to a lost device.
        void ThrowIfDeviceLost() const;

        SDL_Window* window_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        int presentationMode_ = 0;

        // D9-30: the actual requested presentation parameters, tracked so D9-33 (resize) and D9-34
        // (device-lost recovery) can rebuild an identical D3DPRESENT_PARAMETERS via
        // BuildPresentParameters() without needing the original GraphicsBackendCreateArgs again.
        int backBufferFormatOrdinal_ = 0;   // Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color
        int depthStencilFormatOrdinal_ = 0; // Microsoft::Xna::Framework::Graphics::DepthFormat::None
        bool isFullScreen_ = false;
        int swapInterval_ = 1;
        /// D9-32: the game's requested Microsoft::Xna::Framework::Graphics::GraphicsProfile
        /// ordinal (Reach=0, HiDef=1), checked against the real device's D3DCAPS9 at construction.
        int graphicsProfileOrdinal_ = 0;
        /// Set by UpdatePresentationFormatEXT() when the tracked format/fullscreen fields
        /// genuinely changed -- tells EnsureDeviceSize() to Reset() even if the window's pixel
        /// size did not also change (a format-only change would otherwise never be noticed).
        bool presentationDirty_ = false;

        // NOXNA (D9-34): forwards a real device-lost/reset event to GraphicsDevice's own public
        // XNA events. May be empty (default-constructed std::function) if the caller (a direct
        // spike/test, not GraphicsDevice) never set one -- call sites must check before invoking.
        std::function<void(BackendDeviceEvent)> deviceEventCallback_;
        /// D9-34: true from the moment Present() (or DebugSimulateContextLoss()) first detects/
        /// simulates a lost device, until PerformResetRecovery() completes successfully.
        bool deviceLost_ = false;

        ComPtr<IDirect3D9> d3d9_;
        ComPtr<IDirect3DDevice9> device_;
        D3DCAPS9 caps_{};
    };
}
