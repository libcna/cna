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
#include "D3D9DefaultPoolResourceEXT.hpp"

#include <d3d9.h>
#include <wrl/client.h>

#include <unordered_map>
#include <vector>

namespace CNA::Internal::Backends::D3D9
{
    using Microsoft::WRL::ComPtr;

    class D3D9RenderTargetBackend;
    class D3D9RenderTargetCubeBackend;
    class D3D9ShaderCache;

    /**
     * D3D9 graphics backend (plan_dx9.md). Implements IGraphicsBackend on top of plain Direct3D 9
     * (Direct3DCreate9, not D3D9Ex -- design decision 2), targeting Microsoft's own XNA 4.0 Stock
     * Effects HLSL compiled to real vs_2_0/ps_2_0 bytecode (design decision 3/5), with the
     * project's stated goal of pixel-for-pixel indistinguishability from the original XNA 4.0
     * runtime, not mere feature parity.
     *
     * Phase D9-3 (device/present/device-lost) and D9-6 (render states) are fully closed: real
     * device creation using the game's actual requested back-buffer/depth-stencil format,
     * fullscreen flag, and swap interval (via the project owner-approved additive
     * GraphicsBackendCreateArgs extension -- see plan_dx9.md's "IGraphicsBackend boundary problem"
     * section) -- not D3D11's own hardcoded-format precedent, which is fine for D3D11 (parity, not
     * authenticity) but would be a direct fidelity violation here. Clear()/all 6 Clear*
     * combos/Present()/ReadBackbuffer()/resize/the real XNA device-lost lifecycle/render-state
     * pushes are all real. Phase D9-4 (D3D9Buffers.hpp) adds real vertex/index buffers. D9-82 adds
     * the real BasicEffect-VertexColor-only DrawColoredPrimitives/DrawIndexedColoredPrimitives
     * path; full effect-aware DrawPrimitivesEx dispatch and SpriteBatch still throw
     * NotYetImplemented() naming their own follow-up task (D9-82b/c, D9-90).
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

        // ---- IGraphicsBackend: real (Phase D9-4, D9-40/D9-41) ----
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;
        // D9-41: explicitly overridden -- the IGraphicsBackend default silently delegates to
        // CreateIndexBuffer16(), which would give a 32-bit index request a 16-bit buffer with no
        // error (D3D11's own DX-31 caught this exact silent-default trap; do not repeat it here).
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int index_capacity) override;

        // ---- IGraphicsBackend: real (Phase D9-5, D9-50/D9-51) ----
        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        /// D9-51: CubeMap creation is gated on the real D3DCAPS9 (D3DPTEXTURECAPS_CUBEMAP) queried
        /// at device-creation time -- returns nullptr (IGraphicsBackend's own "unsupported"
        /// convention) rather than assuming support, unlike CreateTexture() above.
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;
        /// D9-51: Volume-texture creation is gated on D3DCAPS9::MaxVolumeExtent (see this same
        /// plan note on D3D9Texture3DBackend's own class doc comment) -- returns nullptr when the
        /// device reports no volume-texture support.
        std::unique_ptr<ITexture3DBackend> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        /// D9-53: real D3D9RenderTargetBackend (D3DUSAGE_RENDERTARGET, D3DPOOL_DEFAULT). `mipMap`
        /// is accepted but not yet honored (see D3D9RenderTargets.hpp's own header comment) --
        /// `preserveContents` is likewise accepted (matches the interface) but not yet implemented
        /// on any CNA backend, D3D9 included.
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false, bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        /// D9-53: real D3D9RenderTargetCubeBackend. MSAA deliberately unsupported for cube render
        /// targets (matches D3D11RenderTargetCubeBackend's own precedent) -- `multiSampleCount` is
        /// accepted for signature compatibility and silently ignored.
        std::unique_ptr<IRenderTargetCubeBackend> CreateRenderTargetCube(int size, int depthFormat,
                                                                          bool mipMap = false,
                                                                          int multiSampleCount = 0) override;
        /// D9-53: explicitly overridden -- IGraphicsBackend's own default
        /// (`if (rt) rt->BindAsRenderTargetFace(face); else SetRenderTarget2D(nullptr);`) never
        /// actually calls a bound cube target's own UnbindAsRenderTarget() on the unbind path (it
        /// calls SetRenderTarget2D(nullptr) instead, which only restores the back buffer via
        /// currentCustomRT_'s own 2D-only tracking -- a cube-face bind never sets that field, so
        /// the default would silently leave the device pointed at a stale/destroyed cube face
        /// surface). Found empirically via this backend's own Check T (D3D9_Smoke); tracked here
        /// via a second, cube-specific currentCustomCubeRT_ field so unbinding genuinely restores
        /// the device's default back-buffer/depth-stencil surfaces.
        void SetRenderTargetCubeFace(IRenderTargetCubeBackend* rt, int face) override;
        /// D9-54: real MRT via SetRenderTarget(i, surface), i=0..count-1, capped and validated
        /// against the real D3DCAPS9::NumSimultaneousRTs (design decision 13: an over-request
        /// THROWS a named error rather than silently degrading to fewer targets -- the exact
        /// invisible-capability trap this project's own D3D11/D3D12 precedent accepts but this
        /// authenticity-focused backend deliberately does not). All CNA render targets are
        /// D3DFMT_A8B8G8R8 (RGBA8-storage-only, D9-50's own simplification), so design decision
        /// 13's "same bit depth" requirement is always trivially satisfied -- nothing to actively
        /// enforce. No independent per-target blending exists either (ApplyBlendState() is a
        /// single global SetRenderState() sequence) -- also trivially satisfied.
        void SetRenderTargets(IRenderTargetBackend* const* rts, int count) override;
        /// D9-55: real D3D9OcclusionQueryBackend (`IDirect3DQuery9`, `D3DQUERYTYPE_OCCLUSION`).
        /// Gated on the device genuinely supporting the query type (`CreateQuery(type, nullptr)`,
        /// the official D3D9 support-probe idiom) -- returns nullptr (the `IGraphicsBackend`-wide
        /// "unsupported" convention) rather than assuming support.
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;
        /// D9-53: real MSAA-support probe backing D3D9RenderTargetBackend's own clamp -- queries
        /// IDirect3D9::CheckDeviceMultiSampleType() for `format`/`requested`, returning 0 if
        /// `requested` <= 1 or the device does not support that exact sample count (matches
        /// D3D11RenderTargetBackend's own all-or-nothing clamp, no step-down ladder). NOXNA.
        [[nodiscard]] int ClampMultiSampleCountEXT(D3DFORMAT format, int requested) const;
        /// D9-53: restores the device's own default back-buffer/depth-stencil surfaces -- called by
        /// SetRenderTarget2D(nullptr) and by a D3D9RenderTargetBackend/D3D9RenderTargetCubeBackend's
        /// own UnbindAsRenderTarget(). NOXNA (mirrors D3D11GraphicsBackend::RestoreBackBufferRenderTargetEXT()).
        void RestoreBackBufferRenderTargetEXT();

        // ---- IGraphicsBackend: pure virtual, NotYetImplemented until later D3D9 tasks land ----
        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;
        /// D9-82: real -- BasicEffect with VertexColorEnabled=true/DiffuseColor=white/fog disabled/
        /// no texture/no lighting (matches every other backend's own DrawColoredPrimitives scope),
        /// via the real "BasicEffect_VSBasicVcNoFog"/"BasicEffect_PSBasicNoFog" shader pair
        /// (D9-80's dispatch tables) and D9-72's real register layout. Only stride 16
        /// (VertexPositionColor) so far -- other strides/effect variants are DrawPrimitivesEx's own
        /// job (D9-82b/c, not yet implemented).
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        /// D9-82: indexed counterpart of `DrawColoredPrimitives` above -- same scope.
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        /// D9-82b: real effect-aware dispatch, `BasicEffect` only so far (`AlphaTestEffect`/
        /// `DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect` throw a named
        /// not-yet-implemented naming their own follow-up row -- `D9-82c`/`d`/`e`/`f`). See
        /// `D3D9EffectDraw.cpp`'s own header comment for the full `BasicEffect` coverage/gaps.
        void DrawPrimitivesEx(const IVertexBufferBackend& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        /// D9-82b: indexed counterpart of `DrawPrimitivesEx` above -- same scope.
        void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;
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
        /// D9-53: real -- see D3D9RenderTargetBackend/D3D9RenderTargetCubeBackend's own
        /// Bind/UnbindAsRenderTarget() for what actually changes on the device. Moved out of the
        /// "silently-empty-default" stub section below now that render targets exist to bind.
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        /// D9-63: real -- plain `SetSamplerState()` sequences (design decision 11: no D3D9 sampler
        /// state objects exist to cache), using the `D9-21` `D3D9StateMapping` tables
        /// (`TextureFilterToD3D9()`'s own `D3D9FilterTriple`, `TextureAddressModeToD3D9()`). Moved
        /// out of the "silently-empty-default" stub section below now that `D9-50`'s real textures
        /// exist to sample -- previously listed there since nothing forced it in early the way
        /// `ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState` were.
        void ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy) override;

        // ---- IGraphicsBackend: silently-empty-default virtuals, explicitly loud until real ----
        // (D9-11's own distinction: these have a `{}` default on IGraphicsBackend itself, so an
        // un-overridden call would silently no-op instead of failing -- override each so a missing
        // capability is a build-time-enforced, loud runtime failure instead. NOTE: this list
        // originally missed ApplyBlendState/ApplyDepthStencilState/ApplyRasterizerState/
        // ApplySamplerState -- their `{}` default spans multiple lines, so a single-line `grep
        // 'virtual.*{}$'` (D9-11's own suggested method) does not find them. All 4 are real above now
        // (the first 3 forced in early by GraphicsDevice's own constructor; ApplySamplerState by
        // D9-63, once D9-50's real textures existed to make it meaningful).
        void SetSwapInterval(int interval) override;
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

        /// D9-56: true when this device requires power-of-2 texture dimensions UNCONDITIONALLY
        /// (`D3DPTEXTURECAPS_POW2` set, `D3DPTEXTURECAPS_NONPOW2CONDITIONAL` NOT set) -- an NPOT
        /// `CreateTexture()` call would genuinely fail on such hardware. `false` on this dev
        /// environment's DXVK device (reports full, unconditional NPOT support). NOXNA -- feeds
        /// Phase D9-10's Reach/HiDef capability table (XNA's `Reach` profile forbids NPOT textures
        /// outright on hardware that reports this).
        [[nodiscard]] bool RequiresPowerOfTwoTexturesEXT() const
        {
            return (caps_.TextureCaps & D3DPTEXTURECAPS_POW2) &&
                   !(caps_.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL);
        }
        /// D9-56: true when NPOT textures are supported only CONDITIONALLY (`D3DPTEXTURECAPS_POW2`
        /// AND `D3DPTEXTURECAPS_NONPOW2CONDITIONAL` both set) -- real D3D9 semantics: such an NPOT
        /// texture must have no mip chain and may only use `D3DTADDRESS_CLAMP` (never `WRAP`/
        /// `MIRROR`) on both axes. `false` on this dev environment's DXVK device. NOXNA -- feeds
        /// Phase D9-10 (this is exactly the cap XNA's `Reach` profile's own "no wrapping on NPOT"
        /// restriction is modeling). Enforcing this against a real `SamplerState`/draw call is
        /// `D9-10`/`D9-82`'s own job -- no draw/sampler path exists yet to enforce it against.
        [[nodiscard]] bool NonPowerOfTwoRequiresClampAddressingEXT() const
        {
            return (caps_.TextureCaps & D3DPTEXTURECAPS_POW2) &&
                   (caps_.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL);
        }

        /// D9-40: registers a live D3DPOOL_DEFAULT resource (a buffer, later a render target) so
        /// PerformResetRecovery() can release it before Reset(). Raw, non-owning -- the resource
        /// object's own lifetime is owned by its caller (VertexBuffer/IndexBuffer/...), not this
        /// backend. NOXNA.
        void RegisterDefaultPoolResourceEXT(ID3D9DefaultPoolResourceEXT* resource);
        /// D9-40: unregisters a resource (called from its destructor). NOXNA.
        void UnregisterDefaultPoolResourceEXT(ID3D9DefaultPoolResourceEXT* resource);

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
        /// fire DeviceReset, clear deviceLost_.
        void PollDeviceLost();
        /// D9-34/D9-40: the actual Resetting->Reset()->Reset event sequence, shared by the real
        /// TestCooperativeLevel()-driven path (PollDeviceLost()) and DebugRestoreContext()'s
        /// simulated one (which skips the TestCooperativeLevel wait since nothing really lost the
        /// device -- Reset() itself is real either way). Releases every registered
        /// D3DPOOL_DEFAULT resource (defaultPoolResources_) before Reset() -- each one recreates
        /// its own object lazily on next use, same real XNA/D3D9 behavior as a DYNAMIC
        /// VertexBuffer needing its content re-filled after DeviceReset.
        void PerformResetRecovery();
        /// D9-34: throws Microsoft::Xna::Framework::Graphics::DeviceLostException if the device is
        /// currently lost -- called at the top of every rendering entry point (Clear/ClearX/
        /// ReadBackbuffer), matching real XNA's behavior of refusing to render to a lost device.
        void ThrowIfDeviceLost() const;
        /// D9-53: re-queries and caches the device's own implicit default depth-stencil surface
        /// (defaultDepthStencilSurface_) -- must run immediately after device creation/Reset(),
        /// while it is still the currently-bound one (before any custom render target ever gets
        /// bound), since IDirect3DDevice9::GetDepthStencilSurface() only ever returns whichever
        /// surface is CURRENTLY bound, with no separate "get the implicit one" query. Null (cleared)
        /// when no depth format was requested, matching the back buffer having no depth (Check J).
        void CacheDefaultDepthStencilSurfaceEXT();
        /// D9-82: returns (creating + caching on first request) the real IDirect3DVertexDeclaration9
        /// for `strideInBytes`, using D3D9VertexDeclarations.hpp's own stride-keyed
        /// D3DVERTEXELEMENT9 tables. Throws if `strideInBytes` is not one of the 5 established
        /// layouts. Not a D3DPOOL_DEFAULT resource -- vertex declarations survive Reset() unaffected
        /// (real D3D9 semantics), so this cache is never invalidated/re-registered.
        IDirect3DVertexDeclaration9* GetOrCreateVertexDeclarationEXT(std::size_t strideInBytes);

        /// D9-82b: shared entry point for `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` -- `ib` is
        /// null for the non-indexed call. Selects which Stock Effect `params` indicates (mirrors
        /// `D3D11GraphicsBackend::DrawPrimitivesExImpl`'s own flag priority-cascade) and dispatches
        /// to that effect's own draw helper; throws a named not-yet-implemented for effects whose
        /// row hasn't landed yet. Defined in `D3D9EffectDraw.cpp`.
        void DrawPrimitivesExImpl(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                  PrimitiveType primitive, int primitiveCount,
                                  const GpuDrawParams& params);
        /// D9-82b: real `BasicEffect` dispatch -- computes `ShaderIndex` from `params` (with the
        /// `oneLight` derivation `D9-81`'s corrected note describes), selects/creates the matching
        /// shader pair via `D9-80`'s dispatch tables + `D9-74`'s shader cache, uploads every named
        /// constant that variant's own `D9-72` register table declares, binds textures/vertex
        /// declaration, and issues the draw. Throws for combinations with no matching CNA vertex
        /// layout (see `D3D9EffectDraw.cpp`'s own header comment for the exact supported set).
        /// Defined in `D3D9EffectDraw.cpp`.
        void DrawBasicEffectEXT(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                std::size_t stride, const Matrix& world, const Matrix& view,
                                const Matrix& projection, PrimitiveType primitive, int primitiveCount,
                                const GpuDrawParams& params);
        /// D9-82c: real `AlphaTestEffect` dispatch -- same shape as `DrawBasicEffectEXT`, simpler
        /// (no lighting, always textured). `isEqNe` sourced losslessly from
        /// `params.alphaTest[1] (tolerance) > 0` per `D9-81`'s own finding. Defined in
        /// `D3D9EffectDraw.cpp`.
        void DrawAlphaTestEffectEXT(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                    std::size_t stride, const Matrix& world, const Matrix& view,
                                    const Matrix& projection, PrimitiveType primitive, int primitiveCount,
                                    const GpuDrawParams& params);
        /// D9-82d: real `DualTextureEffect` dispatch -- two-sampler draw (`texture0`/`texture1` ->
        /// `Texture`/`Texture2`). Uses a new, D3D9-only stride-28 vertex layout
        /// (`D3D9VertexDeclarations.hpp`) since `DualTextureEffect.fx`'s real `VSInputTx2` needs two
        /// distinct texture-coordinate sets, unlike D3D11's own simplified single-UV
        /// reimplementation. Defined in `D3D9EffectDraw.cpp`.
        void DrawDualTextureEffectEXT(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                      std::size_t stride, const Matrix& world, const Matrix& view,
                                      const Matrix& projection, PrimitiveType primitive, int primitiveCount,
                                      const GpuDrawParams& params);
        /// D9-82e: real `EnvironmentMapEffect` dispatch -- cube-map sampling (`envMap` ->
        /// `EnvironmentMap`). Only the 8 non-specular `ShaderIndex` values are reachable
        /// (`specularEnabled` stays `D9-81`'s still-open gap, same category as `BasicEffect`'s
        /// `PreferPerPixelLighting`). Uses the existing stride-32 layout (`VSInputNmTx` matches it
        /// exactly, unlike `DualTextureEffect`'s case). Defined in `D3D9EffectDraw.cpp`.
        void DrawEnvironmentMapEffectEXT(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                         std::size_t stride, const Matrix& world, const Matrix& view,
                                         const Matrix& projection, PrimitiveType primitive, int primitiveCount,
                                         const GpuDrawParams& params);
        /// D9-82f: real `SkinnedEffect` dispatch -- per-vertex bone-matrix-array upload
        /// (`Bones[72]`, 216 registers). Only the 12 non-pixel-lighting `ShaderIndex` values are
        /// reachable (`PreferPerPixelLighting` stays `D9-81`'s still-open gap, same as
        /// `BasicEffect`'s case). Uses the existing stride-52 layout (`VSInputNmTxWeights` matches
        /// it exactly). Defined in `D3D9EffectDraw.cpp`.
        void DrawSkinnedEffectEXT(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                  std::size_t stride, const Matrix& world, const Matrix& view,
                                  const Matrix& projection, PrimitiveType primitive, int primitiveCount,
                                  const GpuDrawParams& params);

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
        /// D9-40: every live D3DPOOL_DEFAULT resource (dynamic vertex/index buffers so far),
        /// raw/non-owning -- see RegisterDefaultPoolResourceEXT()'s own doc comment.
        std::vector<ID3D9DefaultPoolResourceEXT*> defaultPoolResources_;

        ComPtr<IDirect3D9> d3d9_;
        ComPtr<IDirect3DDevice9> device_;
        D3DCAPS9 caps_{};
        /// D9-53: the device's own implicit default depth-stencil surface, cached by
        /// CacheDefaultDepthStencilSurfaceEXT() so RestoreBackBufferRenderTargetEXT() can restore it
        /// after a custom render target's depth-stencil surface was bound in its place. Null when no
        /// depth format was requested.
        ComPtr<IDirect3DSurface9> defaultDepthStencilSurface_;

        /// D9-53: the currently-bound custom 2D render target, or null when the back buffer is
        /// active. Raw, non-owning -- lifetime is owned by whoever holds the IRenderTargetBackend
        /// (mirrors D3D11GraphicsBackend::currentCustomRT_'s identical lifetime reasoning). MRT
        /// (D9-54) is not tracked here yet -- unimplemented, inherits IGraphicsBackend's own
        /// SetRenderTargets() default (binds only the first target via this same field).
        D3D9RenderTargetBackend* currentCustomRT_ = nullptr;
        /// D9-53: the currently-bound custom cube-map render target face, or null when none is
        /// bound. Separate from currentCustomRT_ (a cube target is a distinct type from a 2D one) --
        /// see SetRenderTargetCubeFace()'s own doc comment for why this field exists at all.
        D3D9RenderTargetCubeBackend* currentCustomCubeRT_ = nullptr;

        /// D9-82: lazily-constructed real vertex/pixel shader object cache (D9-74's
        /// D3D9ShaderCache), keyed by name -- shared across every draw call. Shader objects are not
        /// D3DPOOL_DEFAULT resources (unlike vertex/index buffers) so they survive Reset()
        /// unaffected -- no RegisterDefaultPoolResourceEXT() wiring needed.
        std::unique_ptr<D3D9ShaderCache> shaderCache_;
        /// D9-82: real IDirect3DVertexDeclaration9 objects, cached by vertex stride in bytes
        /// (mirrors D3D11GraphicsBackend::inputLayoutCache_'s shape). See
        /// GetOrCreateVertexDeclarationEXT().
        std::unordered_map<std::size_t, ComPtr<IDirect3DVertexDeclaration9>> vertexDeclCache_;
    };
}
