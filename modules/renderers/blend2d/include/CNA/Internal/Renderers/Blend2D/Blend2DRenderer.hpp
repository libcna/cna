#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Blend2D/Blend2DSurface.hpp"

#include <blend2d/blend2d.h>
#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>

namespace CNA::Internal::Renderers::Blend2D
{
    /// Vertex/index buffer handles: Blend2D has no 3D vertex pipeline at all (it is a 2D-only
    /// vector rasterizer), so these keep only the bookkeeping IVertexBufferRenderer/
    /// IIndexBufferRenderer require -- the same deliberate no-backing-storage shape as the Stub
    /// renderer's handles (StubRenderer.hpp). Never reached by a real draw: Blend2DRenderer's
    /// DrawColoredPrimitives/DrawIndexedColoredPrimitives reject before touching them.
    class Blend2DVertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        explicit Blend2DVertexBufferRenderer(int vertexCapacity) : vertexCount_(vertexCapacity) {}

        void SetData(const void* /*data*/, int vertex_count, std::size_t /*stride_in_bytes*/) override
        {
            vertexCount_ = vertex_count;
        }

        /// Discarded deliberately, the explicit decision IVertexBufferRenderer requires: this
        /// renderer keeps no vertex storage and binds no native layout, so there is nothing a
        /// declaration could describe unfaithfully (same reasoning as StubRenderer.hpp).
        void SetVertexDeclaration(const VertexDeclaration& /*vertexDeclaration*/) override {}

        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

    private:
        int vertexCount_;
    };

    class Blend2DIndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        explicit Blend2DIndexBufferRenderer(int indexCapacity) : indexCount_(indexCapacity) {}

        void SetData16(const void* /*data*/, int index_count) override
        {
            indexCount_ = index_count;
            isThirtyTwoBit_ = false;
        }

        void SetData32(const void* /*data*/, int index_count) override
        {
            indexCount_ = index_count;
            isThirtyTwoBit_ = true;
        }

        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return isThirtyTwoBit_; }

    private:
        int indexCount_;
        bool isThirtyTwoBit_ = false;
    };

    /**
     * @brief Internal mixin exposing the concrete BLImage backing a texture-shaped renderer
     * handle (plain texture or bindable render target).
     *
     * ISpriteBatchRenderer::Draw() receives only the abstract ITextureRenderer&, and a checked
     * `dynamic_cast<const Blend2DNativeImageSourceEXT*>` (Blend2DSpriteBatchRenderer.cpp's
     * RequireNativeImageEXT) is the safe way to reach the real image for blit_image() -- both
     * concrete classes below multiply-inherit it. The cast can only fail if a *different*
     * renderer's texture handle reaches this renderer, a caller bug that RequireNativeImageEXT
     * turns into a deliberate std::invalid_argument with an actionable message, thrown before any
     * native BLImage memory is touched -- not an accidental, undiagnosable std::bad_cast (the same
     * UB concern SdlRenderer.cpp's Draw() overloads document for their own sibling-class
     * RenderTarget/Texture handles, resolved there through an SDL-only sibling interface too).
     */
    class Blend2DNativeImageSourceEXT
    {
    public:
        virtual ~Blend2DNativeImageSourceEXT() = default;
        [[nodiscard]] virtual const BLImage& NativeImageEXT() const = 0;
    };

    /**
     * @brief CNAEXT. Which stock XNA BlendState preset ApplyBlendState most recently accepted.
     *
     * Blend2D's BLCompOp alone cannot distinguish AlphaBlend from NonPremultiplied: both compose
     * through the identical native BL_COMP_OP_SRC_OVER call (the colour channels are byte-
     * identical either way -- see ApplyBlendState's doc comment for the derivation), but their
     * PUBLIC ALPHA results differ, because NonPremultiplied's AlphaSourceBlend is also
     * SourceAlpha (giving an independent Sa*Sa term) where AlphaBlend's is One. The same split
     * applies to Additive vs a plain PLUS. Blend2DSpriteBatchRenderer::DrawQuad needs to know
     * which preset is active (not just which BLCompOp) to decide whether the bounded CPU alpha
     * correction pass (ApplyIndependentAlphaCorrectionEXT) is required.
     */
    enum class Blend2DBlendPresetEXT
    {
        Opaque,
        AlphaBlend,
        NonPremultiplied,
        Additive,
    };

    class Blend2DRenderer;

    /// Plain Texture2D handle: an owned premultiplied BLImage plus straight-RGBA reads/writes
    /// through Blend2DPixelConvert.hpp (Blend2D's own storage is premultiplied and channel-
    /// swapped -- see that header -- so every transfer converts, never a raw byte copy).
    class Blend2DTextureRenderer final : public ITextureRenderer, public Blend2DNativeImageSourceEXT
    {
    public:
        Blend2DTextureRenderer(int width, int height, const std::uint8_t* rgba);

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const std::uint8_t* rgba, int stride) override;
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h, void* data,
                                   int dataLength) const override;
        [[nodiscard]] const BLImage& NativeImageEXT() const override { return image_; }

    private:
        int width_;
        int height_;
        BLImage image_;
    };

    /// RenderTarget2D handle: owns its own Blend2DSurface (context-bound BLImage) so it can be
    /// drawn into directly, then sampled as an ordinary texture once unbound -- the established
    /// "render, unbind, sample" render-target contract this codebase's other renderers use.
    ///
    /// Sampling a render target WHILE it is still the active target (drawing it onto itself) is
    /// handled by Blend2DSpriteBatchRenderer::DrawQuad's self-sampling check, which snapshots an
    /// independent copy before drawing rather than assuming Blend2D permits reading a BLImage
    /// through blit_image() while that same image is the live target of its own attached
    /// BLContext -- the pinned v0.21.2 source establishes only that a synchronous context's
    /// completed draws are visible to get_data()/make_mutable() (Blend2DSurface's own doc
    /// comment), not that self-referential source/destination blits are well-defined.
    ///
    /// Lifetime: owner_ is a raw reference, safe under CNA's established GraphicsResource
    /// contract -- GraphicsDevice::Dispose() (GraphicsDevice.cpp) disposes every tracked
    /// GraphicsResource (which includes every live RenderTarget2D/Texture2D) BEFORE it tears down
    /// its IGraphicsRenderer, so a Blend2DRenderTargetRenderer is always destroyed while owner_ is
    /// still valid under normal (documented) disposal order -- the same raw-backpointer contract
    /// GraphicsResource itself uses for its own GraphicsDevice* and SdlRenderTargetRenderer uses
    /// for its native handle. A leaked RenderTarget2D that outlives an explicitly-disposed
    /// GraphicsDevice is a pre-existing, codebase-wide caller error, not a BLEND2D-specific gap.
    class Blend2DRenderTargetRenderer final : public IRenderTargetRenderer,
                                               public Blend2DNativeImageSourceEXT
    {
    public:
        Blend2DRenderTargetRenderer(Blend2DRenderer& owner, int width, int height,
                                    bool preserveContents);
        ~Blend2DRenderTargetRenderer() override;

        [[nodiscard]] int GetWidth() const override { return surface_.Width(); }
        [[nodiscard]] int GetHeight() const override { return surface_.Height(); }

        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h, void* data,
                                   int dataLength) const override;
        [[nodiscard]] const BLImage& NativeImageEXT() const override { return surface_.Image(); }

        /// Activates this target on the owning renderer -- mirrors SdlRenderTargetRenderer's own
        /// captured-SDL_Renderer* binding shape, just against Blend2DRenderer's single tracked
        /// "active surface" pointer instead of a native single-target API call.
        void BindAsRenderTarget() override;
        /// Restores the backbuffer, but only if this target is still the active one (a stale
        /// unbind after a different target was already bound must not clobber it).
        void UnbindAsRenderTarget() override;

        /// Blend2D's 2D raster targets never allocate a real depth/stencil plane, regardless of
        /// the requested DepthFormat -- the same truthful override SDL_Renderer's own 2D-only
        /// render targets use (IGraphicsRenderer.hpp's HasRealDepthBuffer doc comment, Task 708).
        [[nodiscard]] bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override
        {
            return false;
        }
        /// Truthful counterpart to HasRealDepthBuffer() above, for the PUBLIC
        /// RenderTarget2D.DepthStencilFormat property: RenderTarget2D.cpp populates that property
        /// directly from this method's return value (not from HasRealDepthBuffer(), which only
        /// gates ClearOptions handling), so without this override a caller requesting Depth24/
        /// Depth24Stencil8 would receive a RenderTarget2D that PUBLICLY CLAIMS that attachment
        /// exists even though Blend2D allocated no depth/stencil storage at all -- the same "one
        /// property source of truth" bug HasRealDepthBuffer already fixed for the boolean queries,
        /// now fixed for the format property too. `0` is DepthFormat::None's raw ordinal (this
        /// header must not depend on the XNA namespace, matching CreateRenderTarget2D's own
        /// `depthFormat` int convention).
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(int /*requestedDepthStencilFormat*/) const override
        {
            return 0;
        }

        [[nodiscard]] Blend2DSurface& Surface() noexcept { return surface_; }
        [[nodiscard]] bool PreservesContents() const noexcept { return preserveContents_; }

    private:
        Blend2DRenderer& owner_;
        Blend2DSurface surface_;
        bool preserveContents_;
    };

    /// Real Blend2D-backed 2D sprite batch: every Draw() overload composes a BLContext transform
    /// (transform matrix, translate to destination, rotate, mirror-about-center flip, offset by
    /// the scaled origin) and blits the source sub-rectangle through blit_image() -- genuine
    /// per-draw rasterization, not a recorded or no-op placeholder. A non-white tint, or a source
    /// rectangle/address mode that requires sampling outside the texture's own bounds, resolves a
    /// temporary premultiplied sub-image (ResolveSourceImageEXT) since Blend2D's stock image blit
    /// has no per-draw colour modulation and its blit_image() rejects an out-of-bounds source area
    /// outright rather than extending it.
    class Blend2DSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        explicit Blend2DSpriteBatchRenderer(Blend2DRenderer& renderer) : renderer_(renderer) {}

        void Begin() override;
        void End() override;

        void SetTransformMatrix(const Matrix& m) override;
        void SetCustomEffect(Effect* effect) override;
        void SetSamplerFilter(int textureFilter) override;
        void SetSamplerAddressMode(int addressU, int addressV) override;
        void SetImmediateMode(bool immediate) override;

        void Draw(const ITextureRenderer& texture, float x, float y) override;
        void Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color) override;
        void Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color, float rotation,
                  const Vector2& origin, SpriteEffects effects, float layerDepth) override;

    private:
        void DrawQuad(const ITextureRenderer& texture, double destX, double destY, double destW,
                      double destH, int srcX, int srcY, int srcW, int srcH, const Color& color,
                      double rotation, double originX, double originY, bool flipH, bool flipV);

        Blend2DRenderer& renderer_;
        bool begun_ = false;
        /// Raw Microsoft::Xna::Framework::Graphics::TextureFilter ordinal from the most recent
        /// SetSamplerFilter(); see SamplerFilterToPatternQualityEXT() in the .cpp for the mapping.
        int textureFilter_ = 0;
        /// Raw TextureAddressMode ordinals (0=Wrap,1=Clamp,2=Mirror) from the most recent
        /// SetSamplerAddressMode(); Clamp matches SpriteBatch.Begin's SamplerState.LinearClamp
        /// default (Microsoft::Xna::Framework::Graphics::SpriteBatch.cpp always calls
        /// SetSamplerAddressMode before Begin(), so this default is never actually observed).
        int addressU_ = 1;
        int addressV_ = 1;
        /// Outer transform applied after each sprite's own local translate/rotate/flip, matching
        /// SoftwareSpriteBatchRenderer's placeCorner()/SkiaSpriteBatchRenderer's canvas->concat()
        /// ordering: SetTransformMatrix() composes on top of the fully-placed sprite quad.
        Matrix transformMatrix_ = Matrix::getIdentityProperty();
    };

    /**
     * @brief CNAEXT. The Blend2D graphics renderer -- a genuine 2D-only `IGraphicsRenderer`
     * implementation backed by the Blend2D vector rasterizer (https://github.com/blend2d/blend2d).
     *
     * Every draw is real Blend2D CPU rasterization into an owned BLImage backbuffer (or a bound
     * RenderTarget2D's own BLImage); presentation uploads the completed frame to an SDL streaming
     * texture -- the same "CPU raster + SDL presentation" shape already established by the SKIA
     * renderer (docs/skia-renderer.md). SDL never executes a Blend2D draw command, it only
     * displays the finished image. The 3D pipeline (vertex/index buffers, DrawColoredPrimitives,
     * depth/stencil) has no Blend2D equivalent and is truthfully refused via
     * Ensure3DSupported/HandleUnsupported3DCall.
     */
    class Blend2DRenderer final : public IGraphicsRenderer
    {
    public:
        Blend2DRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                        CnaPresentationMode presentationMode, int swapInterval);
        ~Blend2DRenderer() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;


        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(
            int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false,
            int multiSampleCount = 0) override;
        /// Blend2D supports exactly one active render target at a time -- the shared
        /// IGraphicsRenderer::SetRenderTargets default would otherwise silently bind only rts[0]
        /// and ignore the rest (same Task 709 reasoning SdlRenderer.cpp documents). Cube-face
        /// targets are rejected: Blend2D has no cube-map concept at all.
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;
        /// GraphicsDevice::SetRenderTarget(RenderTarget2D*) -- the single-target legacy entry
        /// point -- calls this directly rather than SetRenderTargets(); the shared
        /// IGraphicsRenderer default is a no-op, which would silently leave the backbuffer active
        /// while the caller believes a target is bound. Delegates to the same
        /// BindAsRenderTarget()/activeRenderTarget_ mechanism SetRenderTargets() uses.
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;

        void ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels) override;

        void ClearColorAndDepth(float r, float g, float b, float a, float /*depth*/) override
        {
            Clear(r, g, b, a);
        }
        void ClearDepth(float /*depth*/) override {}
        void ClearStencil(int /*stencil*/) override {}
        void ClearDepthAndStencil(float /*depth*/, int /*stencil*/) override {}
        void ClearColorAndStencil(float r, float g, float b, float a, int /*stencil*/) override
        {
            Clear(r, g, b, a);
        }
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float /*depth*/,
                                       int /*stencil*/) override
        {
            Clear(r, g, b, a);
        }

        void SetDepthTestEnabled(bool /*enabled*/) override {}
        void SetBlendEnabled(bool enabled) override { blendEnabled_ = enabled; }
        void SetDepthWriteEnabled(bool /*enabled*/) override {}

        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;

        void SetScissorRect(int x, int y, int w, int h) override;
        /// Records the XNA Viewport rectangle. SpriteBatch coordinates are viewport-local (Task
        /// 880, matching every other CNA renderer's own SpriteBatch contract -- see
        /// SoftwareSpriteBatchRenderer::Draw's REMED-GFX-073 comment): a non-default
        /// (non-full-backbuffer) viewport, e.g. split-screen, must clip to and offset by this
        /// rectangle. The shared IGraphicsRenderer default is a silent no-op, which would
        /// otherwise leave every BLEND2D sprite positioned at true backbuffer coordinates
        /// regardless of the active Viewport. Depth range has no meaning for this 2D-only
        /// renderer.
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /// CNAEXT. Current viewport state for Blend2DSpriteBatchRenderer to re-apply on every
        /// draw -- see SetViewport's own doc comment.
        void GetViewportStateEXT(bool& set, int& x, int& y, int& w, int& h) const noexcept
        {
            set = viewportSet_;
            x = viewportX_;
            y = viewportY_;
            w = viewportW_;
            h = viewportH_;
        }
        /// Records RasterizerState.ScissorTestEnable (the shared IGraphicsRenderer default is a
        /// silent no-op, which previously meant SetScissorRect's rectangle stayed clipping forever
        /// once set, even after the caller disabled the scissor test). FillMode::WireFrame is
        /// rejected; CullMode/depth-bias fields are meaningless for this 2D-only renderer.
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias = 0.0f, float slopeScaleDepthBias = 0.0f) override;
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend, int colorDstBlend,
                             int alphaDstBlend, int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        /// CNAEXT. Current scissor state for Blend2DSpriteBatchRenderer to re-apply, gated by
        /// enabled, on whatever BLContext is active at each draw -- see SetScissorRect's own doc
        /// comment for why the clip is not applied here.
        void GetScissorStateEXT(bool& enabled, int& x, int& y, int& w, int& h) const noexcept
        {
            enabled = scissorTestEnabled_;
            x = scissorX_;
            y = scissorY_;
            w = scissorW_;
            h = scissorH_;
        }

        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /// The shared IGraphicsRenderer::ApplyDepthStencilState/SetReferenceStencil defaults are
        /// silent no-ops, which would let a caller set a real depth-test/write/stencil state (or
        /// a nonzero reference stencil) on a renderer that can never actually apply it -- SupportsDepthStencil()/
        /// SupportsCapability(StencilBuffer) both correctly report false, but nothing enforced
        /// that before this override. DepthStencilState.None (depthEnable=depthWriteEnable=
        /// stencilEnable=false, the state every SpriteBatch::Begin applies) is accepted, matching
        /// this renderer's genuine 2D contract; anything else routes through the same
        /// HandleUnsupported3DCall policy every other unsupported-3D entry point here uses.
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc, int stencilPass,
                                    int stencilFail, int stencilDepthFail, int stencilMask,
                                    int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode, int ccwStencilFunc,
                                    int ccwStencilPass, int ccwStencilFail,
                                    int ccwStencilDepthFail) override;
        void SetReferenceStencil(int value) override;

        /// CNAEXT. The raw XNA ColorWriteChannels mask (bit0=R,1=G,2=B,3=A; 15=All) most recently
        /// applied via ApplyBlendState's BlendWriteState -- used by Blend2DSpriteBatchRenderer to
        /// decide between the fast native-blit path (mask==15, the overwhelming common case) and
        /// the bounded whole-surface snapshot/merge path a partial mask requires (Blend2D has no
        /// native per-channel colour write mask).
        [[nodiscard]] int ActiveColorWriteMaskEXT() const noexcept { return colorWriteMask_; }

        // Ensure3DSupported() deliberately keeps the shared IGraphicsRenderer no-op default
        // (not overridden here): GraphicsDevice's own DrawUserPrimitives/DrawUserIndexedPrimitives
        // overloads call it BEFORE their own argument validation (GraphicsDevice.cpp), so an
        // eager override here would make an invalid-argument call report "unsupported" instead of
        // the XNA-faithful ArgumentOutOfRangeException every renderer's argument-guard tests
        // expect (same reasoning SdlRenderer.cpp's own lack of an override embodies). Rejection
        // instead happens where the real 3D work would start: DrawColoredPrimitives/
        // DrawIndexedColoredPrimitives below, exactly like every other 2D-only renderer in this
        // codebase.
        void DrawColoredPrimitives(const IVertexBufferRenderer& /*vb*/, const Matrix& /*world*/,
                                   const Matrix& /*view*/, const Matrix& /*projection*/,
                                   PrimitiveType /*primitive*/, int /*primitiveCount*/) override
        {
            HandleUnsupported3DCall("BLEND2D", "DrawColoredPrimitives");
        }
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& /*vb*/,
                                          const IIndexBufferRenderer& /*ib*/, const Matrix& /*world*/,
                                          const Matrix& /*view*/, const Matrix& /*projection*/,
                                          PrimitiveType /*primitive*/, int /*primitiveCount*/) override
        {
            HandleUnsupported3DCall("BLEND2D", "DrawIndexedColoredPrimitives");
        }

        /// The currently active raster surface -- the bound RenderTarget2D, or the backbuffer
        /// when none is bound. Every Clear()/SpriteBatch draw/readback targets this surface,
        /// matching XNA's "operations apply to the currently set render target" contract.
        [[nodiscard]] Blend2DSurface& ActiveSurface() noexcept;
        [[nodiscard]] BLCompOp ActiveCompOp() const noexcept;
        /// CNAEXT. Which stock preset is active right now, or Opaque when blending is disabled
        /// (SetBlendEnabled(false) forces a literal SRC_COPY regardless of the last ApplyBlendState
        /// call, so no independent-alpha correction is ever needed in that state -- see
        /// Blend2DBlendPresetEXT's own doc comment).
        [[nodiscard]] Blend2DBlendPresetEXT ActiveBlendPresetEXT() const noexcept
        {
            return blendEnabled_ ? appliedPreset_ : Blend2DBlendPresetEXT::Opaque;
        }

        /// CNAEXT. Internal hook used by Blend2DRenderTargetRenderer::BindAsRenderTarget/
        /// UnbindAsRenderTarget (see that class) -- not part of the public IGraphicsRenderer
        /// contract, SetRenderTargets() is.
        void SetActiveRenderTargetEXT(Blend2DRenderTargetRenderer* target) noexcept
        {
            activeRenderTarget_ = target;
        }
        [[nodiscard]] Blend2DRenderTargetRenderer* GetActiveRenderTargetEXT() const noexcept
        {
            return activeRenderTarget_;
        }

    private:
        void RecreatePresentationTexture();
        /// Queries the real physical output size of presentRenderer_ (SDL_GetRenderOutputSize),
        /// or the debug override when DebugSetPresentationOutputSizeEXT() is active.
        void GetPresentationOutputSize(int& width, int& height) const;
        /// Rebuilds the backbuffer (and, when needed, the presentation texture/logical
        /// presentation) for @p requestedWidth/@p requestedHeight, deriving the actual logical
        /// width from the live SDL output aspect ratio when presentationMode_ is
        /// FixedHeightDynamicWidth -- mirrors SkiaRenderer::RecreateBackbuffer exactly (same
        /// formula: outputWidth * requestedHeight / outputHeight, rounded).
        void RecreateBackbuffer(int requestedWidth, int requestedHeight);
        /// Re-derives and applies the FixedHeightDynamicWidth logical width when the live SDL
        /// output aspect ratio has changed since the backbuffer was last built (a resize can
        /// arrive between draws with no explicit notification) -- mirrors
        /// SkiaRenderer::RefreshDynamicBackbufferIfNeeded. No-op for every other presentation mode
        /// and while a RenderTarget2D is bound (a render target's own fixed size is never
        /// dynamic).
        void RefreshDynamicBackbufferIfNeeded();

        SDL_Window* window_;
        SDL_Renderer* presentRenderer_ = nullptr;
        SDL_Texture* presentTexture_ = nullptr;
        int virtualWidth_;
        int virtualHeight_;
        /// CNAEXT. The caller's originally requested virtual size, preserved across
        /// FixedHeightDynamicWidth recomputations of virtualWidth_/virtualHeight_ -- mirrors
        /// SkiaRenderer's preferredVirtualWidth_/preferredVirtualHeight_ split between "what the
        /// game asked for" and "what is currently allocated".
        int preferredVirtualWidth_;
        int preferredVirtualHeight_;
        CnaPresentationMode presentationMode_;
        int swapInterval_;
        /// CNAEXT. Debug-only presentation output size override (window-independent tests have no
        /// real SDL output to query); mirrors SkiaRenderer's own DebugSetPresentationOutputSizeEXT.
        bool debugOutputSizeOverride_ = false;
        int debugOutputWidth_ = 0;
        int debugOutputHeight_ = 0;

        Blend2DSurface backbuffer_;
        Blend2DRenderTargetRenderer* activeRenderTarget_ = nullptr;

        bool blendEnabled_ = true;
        BLCompOp appliedCompOp_ = BL_COMP_OP_SRC_OVER;
        /// See Blend2DBlendPresetEXT's doc comment: tracks which stock preset ApplyBlendState most
        /// recently accepted, since appliedCompOp_ alone cannot distinguish AlphaBlend from
        /// NonPremultiplied (nor a plain PLUS from Additive).
        Blend2DBlendPresetEXT appliedPreset_ = Blend2DBlendPresetEXT::AlphaBlend;
        /// Raw XNA ColorWriteChannels mask for render-target slot 0 (Blend2D has exactly one
        /// active target), applied by every SpriteBatch draw; see ActiveColorWriteMaskEXT().
        int colorWriteMask_ = 15;

        /// RasterizerState.ScissorTestEnable, from ApplyRasterizerState(); the scissor rectangle
        /// itself, from SetScissorRect(). Neither is applied until a SpriteBatch draw re-reads
        /// both via GetScissorStateEXT() -- see SetScissorRect's doc comment.
        bool scissorTestEnabled_ = false;
        int scissorX_ = 0;
        int scissorY_ = 0;
        int scissorW_ = 0;
        int scissorH_ = 0;

        /// GraphicsDevice.Viewport, from SetViewport(); see GetViewportStateEXT().
        bool viewportSet_ = false;
        int viewportX_ = 0;
        int viewportY_ = 0;
        int viewportW_ = 0;
        int viewportH_ = 0;

    public:
        /// CNAEXT. Test-only presentation output size override so FixedHeightDynamicWidth's
        /// aspect-derived width can be exercised deterministically without a real, resizable
        /// window (mirrors SkiaRenderer::DebugSetPresentationOutputSizeEXT/
        /// DebugClearPresentationOutputSizeEXT exactly).
        void DebugSetPresentationOutputSizeEXT(int width, int height);
        void DebugClearPresentationOutputSizeEXT();
    };
} // namespace CNA::Internal::Renderers::Blend2D
