#include "CNA/Internal/Renderers/Blend2D/Blend2DRenderer.hpp"
#include "CNA/Internal/Renderers/Blend2D/Blend2DCheckedCallEXT.hpp"
#include "CNA/Internal/Renderers/Blend2D/Blend2DPixelConvert.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Renderers::Blend2D
{
    namespace
    {
        [[nodiscard]] SDL_RendererLogicalPresentation ToSdlPresentation(CnaPresentationMode mode)
        {
            switch (mode)
            {
                case CnaPresentationMode::Letterbox: return SDL_LOGICAL_PRESENTATION_LETTERBOX;
                case CnaPresentationMode::Overscan: return SDL_LOGICAL_PRESENTATION_OVERSCAN;
                case CnaPresentationMode::Stretch: return SDL_LOGICAL_PRESENTATION_STRETCH;
                case CnaPresentationMode::NativeBackBuffer: return SDL_LOGICAL_PRESENTATION_DISABLED;
                // FixedHeightDynamicWidth's logical WIDTH is recomputed (RecreateBackbuffer) so the
                // canvas already matches the output aspect ratio; LETTERBOX with a matching aspect
                // ratio fills the surface exactly with no bars, matching XNA/Windows Phone
                // behaviour (see the CnaPresentationMode doc comment).
                case CnaPresentationMode::FixedHeightDynamicWidth: return SDL_LOGICAL_PRESENTATION_LETTERBOX;
            }
            return SDL_LOGICAL_PRESENTATION_LETTERBOX;
        }
    } // namespace

    Blend2DRenderer::Blend2DRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                                     CnaPresentationMode presentationMode, int swapInterval)
        : window_(window)
        , virtualWidth_(1)
        , virtualHeight_(1)
        , preferredVirtualWidth_(virtualWidth)
        , preferredVirtualHeight_(virtualHeight)
        , presentationMode_(presentationMode)
        , swapInterval_(swapInterval)
        , backbuffer_(1, 1)
    {
        if (!window_)
            throw std::runtime_error("Blend2DRenderer initialized with null window.");

        bool registered = false;
        try
        {
            presentRenderer_ = SDL_CreateRenderer(window_, nullptr);
            if (!presentRenderer_)
                throw std::runtime_error(std::string("Blend2D SDL_CreateRenderer failed: ") + SDL_GetError());

            SetSwapInterval(swapInterval_);
            RecreateBackbuffer(virtualWidth, virtualHeight);

            IGraphicsRenderer::RegisterForWindow(SDL_GetWindowID(window_), this);
            registered = true;
        }
        catch (...)
        {
            if (registered)
                IGraphicsRenderer::UnregisterForWindow(SDL_GetWindowID(window_));
            if (presentTexture_)
            {
                SDL_DestroyTexture(presentTexture_);
                presentTexture_ = nullptr;
            }
            if (presentRenderer_)
            {
                SDL_DestroyRenderer(presentRenderer_);
                presentRenderer_ = nullptr;
            }
            throw;
        }
    }

    Blend2DRenderer::~Blend2DRenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(SDL_GetWindowID(window_));
        if (presentTexture_) SDL_DestroyTexture(presentTexture_);
        if (presentRenderer_) SDL_DestroyRenderer(presentRenderer_);
    }

    void Blend2DRenderer::RecreatePresentationTexture()
    {
        if (presentTexture_)
        {
            SDL_DestroyTexture(presentTexture_);
            presentTexture_ = nullptr;
        }
        presentTexture_ = SDL_CreateTexture(presentRenderer_, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_STREAMING, virtualWidth_, virtualHeight_);
        if (!presentTexture_)
            throw std::runtime_error(std::string("Blend2D SDL_CreateTexture failed: ") + SDL_GetError());
    }

    void Blend2DRenderer::GetPresentationOutputSize(int& width, int& height) const
    {
        if (debugOutputSizeOverride_)
        {
            width = debugOutputWidth_;
            height = debugOutputHeight_;
            return;
        }
        width = 0;
        height = 0;
        (void)SDL_GetRenderOutputSize(presentRenderer_, &width, &height);
    }

    void Blend2DRenderer::RecreateBackbuffer(int requestedWidth, int requestedHeight)
    {
        int outputWidth = 0;
        int outputHeight = 0;
        GetPresentationOutputSize(outputWidth, outputHeight);

        const int height = requestedHeight > 0 ? requestedHeight : std::max(outputHeight, 1);
        int width = requestedWidth;
        // FixedHeightDynamicWidth derives the logical width from the live output aspect ratio:
        // logicalW = round(outputW * preferredH / outputH). See the CnaPresentationMode doc
        // comment (IGraphicsRenderer.hpp) -- matches SkiaRenderer::RecreateBackbuffer exactly.
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth && requestedHeight > 0 && outputHeight > 0)
            width = static_cast<int>(static_cast<double>(outputWidth) * requestedHeight / outputHeight + 0.5);
        if (width <= 0) width = std::max(outputWidth, 1);

        virtualWidth_ = width > 0 ? width : 1;
        virtualHeight_ = height > 0 ? height : 1;
        backbuffer_.Resize(virtualWidth_, virtualHeight_);
        RecreatePresentationTexture();
        if (!SDL_SetRenderLogicalPresentation(presentRenderer_, virtualWidth_, virtualHeight_,
                                              ToSdlPresentation(presentationMode_)))
        {
            throw std::runtime_error(
                std::string("Blend2D SDL_SetRenderLogicalPresentation failed: ") + SDL_GetError());
        }
    }

    void Blend2DRenderer::RefreshDynamicBackbufferIfNeeded()
    {
        if (presentationMode_ != CnaPresentationMode::FixedHeightDynamicWidth
            || activeRenderTarget_ != nullptr || preferredVirtualHeight_ <= 0)
        {
            return;
        }

        int outputWidth = 0;
        int outputHeight = 0;
        GetPresentationOutputSize(outputWidth, outputHeight);
        if (outputWidth <= 0 || outputHeight <= 0)
            return;

        const int desiredWidth = static_cast<int>(
            static_cast<double>(outputWidth) * preferredVirtualHeight_ / outputHeight + 0.5);
        if (desiredWidth > 0
            && (desiredWidth != virtualWidth_ || preferredVirtualHeight_ != virtualHeight_))
        {
            RecreateBackbuffer(preferredVirtualWidth_, preferredVirtualHeight_);
        }
    }

    void Blend2DRenderer::DebugSetPresentationOutputSizeEXT(int width, int height)
    {
        if (width < 0 || height < 0)
            throw std::out_of_range("Blend2D debug presentation output size must not be negative.");
        debugOutputSizeOverride_ = true;
        debugOutputWidth_ = width;
        debugOutputHeight_ = height;
    }

    void Blend2DRenderer::DebugClearPresentationOutputSizeEXT()
    {
        debugOutputSizeOverride_ = false;
        debugOutputWidth_ = 0;
        debugOutputHeight_ = 0;
    }

    Blend2DSurface& Blend2DRenderer::ActiveSurface() noexcept
    {
        return activeRenderTarget_ ? activeRenderTarget_->Surface() : backbuffer_;
    }

    BLCompOp Blend2DRenderer::ActiveCompOp() const noexcept
    {
        return blendEnabled_ ? appliedCompOp_ : BL_COMP_OP_SRC_COPY;
    }

    void Blend2DRenderer::Clear(float r, float g, float b, float a)
    {
        RefreshDynamicBackbufferIfNeeded();
        ActiveSurface().Clear(r, g, b, a);
    }

    void Blend2DRenderer::Present()
    {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(virtualWidth_) * virtualHeight_ * 4);
        if (!backbuffer_.ReadPixelsRgba(0, 0, virtualWidth_, virtualHeight_, pixels.data()))
            throw std::runtime_error("Blend2D: backbuffer readback failed during Present().");

        if (!SDL_UpdateTexture(presentTexture_, nullptr, pixels.data(), virtualWidth_ * 4))
            throw std::runtime_error(std::string("Blend2D SDL_UpdateTexture failed: ") + SDL_GetError());

        SDL_FRect nativeDestination{0.0f, 0.0f, static_cast<float>(virtualWidth_),
                                    static_cast<float>(virtualHeight_)};
        const SDL_FRect* destination = presentationMode_ == CnaPresentationMode::NativeBackBuffer
            ? &nativeDestination : nullptr;
        if (!SDL_SetRenderDrawColor(presentRenderer_, 0, 0, 0, 255) ||
            !SDL_RenderClear(presentRenderer_) ||
            !SDL_RenderTexture(presentRenderer_, presentTexture_, nullptr, destination) ||
            !SDL_RenderPresent(presentRenderer_))
        {
            throw std::runtime_error(std::string("Blend2D SDL presentation failed: ") + SDL_GetError());
        }

        // A resize can arrive between draws without a ClientSizeChanged notification. Preserve the
        // just-completed frame, then make the raster backbuffer match the new dynamic width for
        // the next frame -- GraphicsDevice::Present immediately queries GetViewportSize(), so its
        // public viewport observes this replacement in the same Present call (mirrors
        // SkiaRenderer::Present).
        RefreshDynamicBackbufferIfNeeded();
    }

    void Blend2DRenderer::GetViewportSize(int& width, int& height)
    {
        RefreshDynamicBackbufferIfNeeded();
        width = virtualWidth_;
        height = virtualHeight_;
    }

    void Blend2DRenderer::SetVirtualResolution(int width, int height)
    {
        preferredVirtualWidth_ = width;
        preferredVirtualHeight_ = height;
        RecreateBackbuffer(width, height);
    }

    void Blend2DRenderer::SetPresentationMode(int mode)
    {
        if (mode < static_cast<int>(CnaPresentationMode::Letterbox) ||
            mode > static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth))
        {
            throw std::out_of_range("Blend2D received an invalid presentation mode.");
        }
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
        // A mode change can change the derived logical width (entering/leaving
        // FixedHeightDynamicWidth), not just which SDL_RendererLogicalPresentation is applied to
        // the existing size -- recompute the whole backbuffer, mirroring SkiaRenderer.
        RecreateBackbuffer(preferredVirtualWidth_, preferredVirtualHeight_);
    }

    void Blend2DRenderer::SetSwapInterval(int interval)
    {
        if (!SDL_SetRenderVSync(presentRenderer_, interval))
        {
            if (interval > 1 && SDL_SetRenderVSync(presentRenderer_, 1))
            {
                swapInterval_ = 1;
                return;
            }
            throw std::runtime_error(std::string("Blend2D SDL_SetRenderVSync failed: ") + SDL_GetError());
        }
        swapInterval_ = interval;
    }

    std::unique_ptr<ITextureRenderer> Blend2DRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<Blend2DTextureRenderer>(data.width, data.height, data.pixels.data());
    }

    std::unique_ptr<ISpriteBatchRenderer> Blend2DRenderer::CreateSpriteBatch()
    {
        return std::make_unique<Blend2DSpriteBatchRenderer>(*this);
    }

    std::unique_ptr<IRenderTargetRenderer> Blend2DRenderer::CreateRenderTarget2D(
        int w, int h, int /*depthFormat*/, bool preserveContents, bool /*mipMap*/,
        int /*multiSampleCount*/)
    {
        return std::make_unique<Blend2DRenderTargetRenderer>(*this, w, h, preserveContents);
    }

    void Blend2DRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (rt)
            rt->BindAsRenderTarget();
        else
            activeRenderTarget_ = nullptr;
    }

    void Blend2DRenderer::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (count > 1)
        {
            throw std::runtime_error(
                "BLEND2D does not support multiple simultaneous render targets (MRT): requested " +
                std::to_string(count) + ", but this renderer's 2D raster pipeline supports exactly "
                "one active render target at a time.");
        }
        if (count > 0 && renderTargets[0].IsRenderTargetCubeFace())
            throw std::runtime_error("BLEND2D does not support RenderTargetCube face bindings.");

        if (count == 0 || !renderTargets[0].GetRenderTarget2D())
        {
            activeRenderTarget_ = nullptr;
            return;
        }
        renderTargets[0].GetRenderTarget2D()->BindAsRenderTarget();
    }

    void Blend2DRenderer::ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels)
    {
        if (!ActiveSurface().ReadPixelsRgba(x, y, w, h, pixels))
            throw std::runtime_error("Blend2D: ReadBackbuffer region out of range.");
    }

    std::unique_ptr<IVertexBufferRenderer> Blend2DRenderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<Blend2DVertexBufferRenderer>(vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> Blend2DRenderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<Blend2DIndexBufferRenderer>(index_capacity);
    }

    void Blend2DRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        // Recorded only -- NOT applied to any BLContext here. The scissor rectangle is
        // target-space state that must be re-applied fresh on every SpriteBatch draw, gated by
        // RasterizerState.ScissorTestEnable (ApplyRasterizerState below), against whichever
        // surface/BLContext is active AT DRAW TIME. Applying it eagerly here to
        // ActiveSurface().Context() (the previous implementation) had two defects: it stayed
        // permanently attached to that one BLContext even after ScissorTestEnable was set false
        // (RasterizerState carries that flag on a separate GraphicsDevice property, and the
        // previous code never read it at all), and it silently stopped applying the moment a
        // different render target (a different, separately-attached BLContext) became active.
        scissorX_ = x;
        scissorY_ = y;
        scissorW_ = w;
        scissorH_ = h;
    }

    void Blend2DRenderer::ApplyDepthStencilState(
        bool depthEnable, bool depthWriteEnable, int /*depthFunc*/,
        bool stencilEnable, int /*stencilFunc*/, int /*stencilPass*/, int /*stencilFail*/,
        int /*stencilDepthFail*/, int /*stencilMask*/, int /*stencilWriteMask*/,
        int /*referenceStencil*/, bool /*twoSidedStencilMode*/, int /*ccwStencilFunc*/,
        int /*ccwStencilPass*/, int /*ccwStencilFail*/, int /*ccwStencilDepthFail*/)
    {
        // DepthStencilState.None (every field disabled) is part of the normal SpriteBatch 2D
        // contract and describes the absence of a depth/stencil operation, so accepting it does
        // not claim an attachment this renderer does not have.
        if (depthEnable || depthWriteEnable || stencilEnable)
            HandleUnsupported3DCall("BLEND2D", "ApplyDepthStencilState");
    }

    void Blend2DRenderer::SetReferenceStencil(int value)
    {
        // Zero accompanies the accepted disabled state above; a nonzero reference only has
        // meaning together with the unsupported stencil pipeline.
        if (value != 0)
            HandleUnsupported3DCall("BLEND2D", "SetReferenceStencil");
    }

    void Blend2DRenderer::SetViewport(int x, int y, int w, int h, float /*minDepth*/, float /*maxDepth*/)
    {
        // Recorded only, applied per-draw by Blend2DSpriteBatchRenderer -- same reasoning as
        // SetScissorRect (see that method's doc comment): the active BLContext at draw time may
        // belong to a different render target than the one active when SetViewport was called.
        viewportSet_ = true;
        viewportX_ = x;
        viewportY_ = y;
        viewportW_ = w;
        viewportH_ = h;
    }

    void Blend2DRenderer::ApplyRasterizerState(int /*cullMode*/, int fillMode, bool scissorTestEnable,
                                               float /*depthBias*/, float /*slopeScaleDepthBias*/)
    {
        // FillMode::WireFrame has no meaning for a filled 2D sprite/vector rasterizer and this
        // renderer advertises GraphicsCapability::WireFrame == false; reject rather than silently
        // drawing filled geometry while claiming wireframe was honoured.
        constexpr int kFillModeSolid = 0;
        if (fillMode != kFillModeSolid)
            throw std::runtime_error("BLEND2D does not support RasterizerState::FillMode::WireFrame.");
        // CullMode/DepthBias/SlopeScaleDepthBias only affect a 3D pipeline this renderer does not
        // have; every 3D draw entry point rejects independently (DrawColoredPrimitives etc.).
        scissorTestEnabled_ = scissorTestEnable;
    }

    void Blend2DRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend, int colorDstBlend,
                                          int alphaDstBlend, int colorBlendFunc, int alphaBlendFunc,
                                          const BlendWriteState& writeState)
    {
        // Raw Microsoft::Xna::Framework::Graphics::Blend/BlendFunction ordinals (see those
        // headers): One=0, Zero=1, SourceAlpha=4, InverseSourceAlpha=5; BlendFunction::Add=0.
        //
        // Blend2D's BLCompOp set is Porter-Duff/blend-mode based, not the generic
        // (srcFactor,dstFactor,equation) pair XNA's BlendState exposes, so only tuples that
        // correspond EXACTLY to one of the four stock BlendState presets (modules/graphics/src/
        // Xna/BlendState.cpp) are accepted; every other combination is refused rather than
        // silently approximated. The four stock presets:
        //
        //   Opaque:           (One,Zero,Add)                          -> BL_COMP_OP_SRC_COPY, exact.
        //   AlphaBlend:       (One,InverseSourceAlpha,Add)             -> BL_COMP_OP_SRC_OVER, exact
        //     in BOTH channels: every CNA-owned Blend2D texture/target already stores premultiplied
        //     pixels, so native SRC_OVER's Dca'=Sca+Dca(1-Sa), Da'=Sa+Da(1-Sa) is byte-identical to
        //     AlphaBlend's own (ColorSourceBlend=One, AlphaSourceBlend=One) equation.
        //   NonPremultiplied: (SourceAlpha,InverseSourceAlpha,Add)     -> BL_COMP_OP_SRC_OVER as a
        //     STAGING draw only, plus a bounded CPU colour+alpha correction (see below). Both
        //     ColorSourceBlend and AlphaSourceBlend are SourceAlpha here, so XNA evaluates colour
        //     and alpha from the SAME straight-space equation pair: dstColor=Dca/Da (recovered
        //     straight destination colour), Cout=saturate(Sca+dstColor*(1-Sa)),
        //     Aout=saturate(Sa*Sa+Da*(1-Sa)), stored=(Cout*Aout, Aout) -- matching
        //     SkiaRenderer.cpp's MaskedBlendEffect() runtime-blender oracle exactly (its
        //     nonPremulColor/nonPremulAlpha). This is NOT the same as native SRC_OVER's colour
        //     result: native premultiplies the blended straight colour against the OLD destination
        //     alpha Da, but XNA's Cout must be premultiplied against the NEW Aout, which differs
        //     from Da whenever Sa is neither 0 nor 1 -- so both the colour AND alpha bytes native
        //     SRC_OVER wrote are overwritten by the correction, not just alpha.
        //   Additive:         (SourceAlpha,One,Add)                    -> BL_COMP_OP_PLUS as a
        //     STAGING draw only, plus the same bounded colour+alpha correction:
        //     Cout=saturate(Sca+dstColor), Aout=saturate(Sa*Sa+Da), stored=(Cout*Aout, Aout)
        //     (Skia's additiveColor/additiveAlpha is the identical derivation). Additive's colour
        //     weights (Sa + 1) do not sum to 1, so Cout can genuinely exceed 1.0 before saturation
        //     -- a real, expected additive "blow out" XNA/FNA also clip -- which is exactly why
        //     Cout is saturated BEFORE the Cout*Aout repremultiply, not after.
        //
        // For BOTH NonPremultiplied and Additive, the native blit_image call above is only a
        // staging draw whose colour AND alpha bytes are unconditionally overwritten by
        // Blend2DSpriteBatchRenderer::ApplyIndependentBlendCorrectionEXT (driven by
        // ActiveBlendPresetEXT() below, since appliedCompOp_ alone cannot distinguish AlphaBlend
        // from NonPremultiplied or a plain PLUS from Additive -- both pairs resolve to the same
        // BLCompOp).
        constexpr int kOne = 0, kZero = 1, kSourceAlpha = 4, kInverseSourceAlpha = 5, kAdd = 0;
        const bool colorAlphaMatch = colorSrcBlend == alphaSrcBlend && colorDstBlend == alphaDstBlend
            && colorBlendFunc == alphaBlendFunc;

        BLCompOp resolved;
        Blend2DBlendPresetEXT preset;
        if (colorBlendFunc == kAdd && colorAlphaMatch)
        {
            if (colorSrcBlend == kOne && colorDstBlend == kZero)
            {
                resolved = BL_COMP_OP_SRC_COPY;
                preset = Blend2DBlendPresetEXT::Opaque;
            }
            else if (colorSrcBlend == kOne && colorDstBlend == kInverseSourceAlpha)
            {
                resolved = BL_COMP_OP_SRC_OVER;
                preset = Blend2DBlendPresetEXT::AlphaBlend;
            }
            else if (colorSrcBlend == kSourceAlpha && colorDstBlend == kInverseSourceAlpha)
            {
                resolved = BL_COMP_OP_SRC_OVER;
                preset = Blend2DBlendPresetEXT::NonPremultiplied;
            }
            else if (colorSrcBlend == kSourceAlpha && colorDstBlend == kOne)
            {
                resolved = BL_COMP_OP_PLUS;
                preset = Blend2DBlendPresetEXT::Additive;
            }
            else
            {
                throw std::runtime_error(
                    "BLEND2D only implements the stock BlendState presets (Opaque, AlphaBlend, "
                    "NonPremultiplied, Additive); this colour blend factor/function combination "
                    "has no exact Blend2D composition operator.");
            }
        }
        else
        {
            throw std::runtime_error(
                "BLEND2D only implements the stock BlendState presets (Opaque, AlphaBlend, "
                "NonPremultiplied, Additive); this renderer's colour and alpha blend "
                "factors/functions must match one of those four presets exactly (independent "
                "alpha-channel blend equations have no exact Blend2D composition operator).");
        }

        // BlendWriteState: Blend2D has exactly one active render target (no MRT) and no native
        // per-sample coverage mask, so only slot 0's ColorWriteChannels can be honoured at all;
        // slots 1-3 and a non-default MultiSampleMask are refused outright rather than silently
        // discarded (mirrors SkiaRenderer::ApplyBlendState's identical one-target/no-MSAA limits).
        constexpr int kColorWriteAll = 15;
        const int colorWriteMask = writeState.colorWriteChannels[0];
        if (colorWriteMask < 0 || colorWriteMask > kColorWriteAll)
            throw std::runtime_error("BLEND2D received an invalid ColorWriteChannels mask.");
        for (int target = 1; target < 4; ++target)
        {
            if (writeState.colorWriteChannels[target] != kColorWriteAll)
            {
                throw std::runtime_error(
                    "BLEND2D has one render target and does not implement ColorWriteChannels1-3.");
            }
        }
        if (writeState.multiSampleMask != ~0u)
            throw std::runtime_error("BLEND2D does not implement non-default MultiSampleMask values (no MSAA).");

        // Commit only after every validation above succeeds.
        appliedCompOp_ = resolved;
        appliedPreset_ = preset;
        colorWriteMask_ = colorWriteMask;
    }

    bool Blend2DRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            case CNA::GraphicsCapability::ThreeD: return false;
            case CNA::GraphicsCapability::DepthStencilBuffer: return false;
            case CNA::GraphicsCapability::MultiSampleAntiAliasing: return false;
            case CNA::GraphicsCapability::MultipleRenderTargets: return false;
            case CNA::GraphicsCapability::AnisotropicFiltering: return false;
            case CNA::GraphicsCapability::WireFrame: return false;
            case CNA::GraphicsCapability::OcclusionQuery: return false;
            case CNA::GraphicsCapability::CustomEffects: return false;
            case CNA::GraphicsCapability::Texture3D: return false;
            case CNA::GraphicsCapability::MultiStreamVertexInput: return false;
            case CNA::GraphicsCapability::Instancing: return false;
            case CNA::GraphicsCapability::StencilBuffer: return false;
            // Genuinely honored: ApplyBlendState maps the Additive preset's colour equation to
            // BL_COMP_OP_PLUS (byte-identical to the native operator) and its independent alpha
            // equation (Sa*Sa+Da) to a bounded CPU correction pass -- see ApplyBlendState's own
            // doc comment and Blend2DSpriteBatchRenderer::ApplyIndependentAlphaCorrectionEXT.
            case CNA::GraphicsCapability::AdditiveBlending: return true;
        }
        return false;
    }
} // namespace CNA::Internal::Renderers::Blend2D

namespace CNA::Internal::Renderers
{
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<Blend2D::Blend2DRenderer>(args.window, args.virtualWidth,
                                                           args.virtualHeight, args.presentationMode,
                                                           args.swapInterval);
    }
} // namespace CNA::Internal::Renderers
