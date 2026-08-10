#include "CNA/Internal/Renderers/Blend2D/Blend2DRenderer.hpp"
#include "CNA/Internal/Renderers/Blend2D/Blend2DPixelConvert.hpp"

#include <algorithm>
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
                case CnaPresentationMode::FixedHeightDynamicWidth: return SDL_LOGICAL_PRESENTATION_LETTERBOX;
            }
            return SDL_LOGICAL_PRESENTATION_LETTERBOX;
        }
    } // namespace

    Blend2DRenderer::Blend2DRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                                     CnaPresentationMode presentationMode, int swapInterval)
        : window_(window)
        , virtualWidth_(virtualWidth > 0 ? virtualWidth : 1)
        , virtualHeight_(virtualHeight > 0 ? virtualHeight : 1)
        , presentationMode_(presentationMode)
        , swapInterval_(swapInterval)
        , backbuffer_(virtualWidth_, virtualHeight_)
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
            RecreatePresentationTexture();
            if (!SDL_SetRenderLogicalPresentation(presentRenderer_, virtualWidth_, virtualHeight_,
                                                  ToSdlPresentation(presentationMode_)))
            {
                throw std::runtime_error(
                    std::string("Blend2D SDL_SetRenderLogicalPresentation failed: ") + SDL_GetError());
            }

            IGraphicsRenderer::RegisterForWindow(window_, this);
            registered = true;
        }
        catch (...)
        {
            if (registered)
                IGraphicsRenderer::UnregisterForWindow(window_);
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
        IGraphicsRenderer::UnregisterForWindow(window_);
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
    }

    void Blend2DRenderer::GetViewportSize(int& width, int& height)
    {
        width = virtualWidth_;
        height = virtualHeight_;
    }

    void Blend2DRenderer::SetVirtualResolution(int width, int height)
    {
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

    void Blend2DRenderer::SetPresentationMode(int mode)
    {
        if (mode < static_cast<int>(CnaPresentationMode::Letterbox) ||
            mode > static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth))
        {
            throw std::out_of_range("Blend2D received an invalid presentation mode.");
        }
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
        if (!SDL_SetRenderLogicalPresentation(presentRenderer_, virtualWidth_, virtualHeight_,
                                              ToSdlPresentation(presentationMode_)))
        {
            throw std::runtime_error(
                std::string("Blend2D SDL_SetRenderLogicalPresentation failed: ") + SDL_GetError());
        }
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
        BLContext& ctx = ActiveSurface().Context();
        ctx.restore_clipping();
        ctx.clip_to_rect(BLRectI(x, y, w, h));
    }

    void Blend2DRenderer::ApplyBlendState(int colorSrcBlend, int /*alphaSrcBlend*/, int colorDstBlend,
                                          int /*alphaDstBlend*/, int colorBlendFunc,
                                          int /*alphaBlendFunc*/, const BlendWriteState& /*writeState*/)
    {
        // Raw Microsoft::Xna::Framework::Graphics::Blend/BlendFunction ordinals (see those
        // headers): One=0, Zero=1, SourceAlpha=4, InverseSourceAlpha=5; BlendFunction::Add=0.
        // Blend2D genuinely supports SRC_COPY (Opaque) and PLUS (Additive) as exact composition
        // operators; every other tuple (AlphaBlend, NonPremultiplied, and any custom BlendState)
        // renders through the same premultiplied SRC_OVER composite path -- a truthful, documented
        // v1 boundary (docs/blend2d-renderer.md), not a silent divergence: AlphaBlend is XNA
        // SpriteBatch's default and renders correctly; NonPremultiplied composites correctly too
        // since every CNA-owned Blend2D texture/target is stored premultiplied regardless of the
        // caller's blend-state choice.
        const bool add = colorBlendFunc == 0;
        if (add && colorSrcBlend == 0 && colorDstBlend == 1)
            appliedCompOp_ = BL_COMP_OP_SRC_COPY;
        else if (add && colorSrcBlend == 4 && colorDstBlend == 0)
            appliedCompOp_ = BL_COMP_OP_PLUS;
        else
            appliedCompOp_ = BL_COMP_OP_SRC_OVER;
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
            // Genuinely honored: ApplyBlendState maps the Additive preset to BL_COMP_OP_PLUS, an
            // exact Blend2D composition operator, not an approximation.
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
