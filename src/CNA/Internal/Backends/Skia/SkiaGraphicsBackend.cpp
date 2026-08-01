#include "CNA/Internal/Backends/Skia/SkiaGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRenderTargetBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSpriteBatchBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaTextureBackend.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Backends::Skia
{
    namespace
    {
        [[noreturn]] void ThrowUnavailable(const char* method)
        {
            throw std::runtime_error(std::string("Skia backend does not implement this path yet: ") + method);
        }

        [[noreturn]] void ThrowNo3D(const char* method)
        {
            throw std::runtime_error(std::string("Skia (raster 2D) does not support 3D: ") + method);
        }

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
    }

    SkiaGraphicsBackend::SkiaGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                                             CnaPresentationMode presentationMode, int swapInterval)
        : window_(window)
        , presentationMode_(presentationMode)
        , preferredVirtualHeight_(virtualHeight)
    {
        if (!window_)
            throw std::runtime_error("SkiaGraphicsBackend initialized with null window.");

        renderer_ = SDL_CreateRenderer(window_, nullptr);
        if (!renderer_)
            throw std::runtime_error(std::string("Skia SDL_CreateRenderer failed: ") + SDL_GetError());

        SetSwapInterval(swapInterval);
        RecreateBackbuffer(virtualWidth, virtualHeight);
        IGraphicsBackend::RegisterForWindow(window_, this);
    }

    SkiaGraphicsBackend::~SkiaGraphicsBackend()
    {
        IGraphicsBackend::UnregisterForWindow(window_);
        if (presentTexture_) SDL_DestroyTexture(presentTexture_);
        if (renderer_) SDL_DestroyRenderer(renderer_);
    }

    void SkiaGraphicsBackend::RecreateBackbuffer(int requestedWidth, int requestedHeight)
    {
        int outputWidth = 0;
        int outputHeight = 0;
        SDL_GetRenderOutputSize(renderer_, &outputWidth, &outputHeight);
        if (outputWidth <= 0 || outputHeight <= 0)
            SDL_GetWindowSize(window_, &outputWidth, &outputHeight);

        const int height = requestedHeight > 0 ? requestedHeight : std::max(outputHeight, 1);
        int width = requestedWidth;
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth && requestedHeight > 0 && outputHeight > 0)
            width = static_cast<int>(static_cast<double>(outputWidth) * requestedHeight / outputHeight + 0.5);
        if (width <= 0) width = std::max(outputWidth, 1);

        surface_.Resize(width, height);
        if (presentTexture_)
        {
            SDL_DestroyTexture(presentTexture_);
            presentTexture_ = nullptr;
        }
        presentTexture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!presentTexture_)
            throw std::runtime_error(std::string("Skia SDL_CreateTexture failed: ") + SDL_GetError());

        ApplyLogicalPresentation();
    }

    void SkiaGraphicsBackend::ApplyLogicalPresentation()
    {
        if (!SDL_SetRenderLogicalPresentation(renderer_, LogicalWidth(), LogicalHeight(),
                                              ToSdlPresentation(presentationMode_)))
        {
            throw std::runtime_error(std::string("Skia SDL_SetRenderLogicalPresentation failed: ") + SDL_GetError());
        }
    }

    void SkiaGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        ActiveSurface().Clear(r, g, b, a);
    }

    void SkiaGraphicsBackend::Present()
    {
        surface_.Flush();
        const auto pixels = surface_.SnapshotRgba();
        if (!SDL_UpdateTexture(presentTexture_, nullptr, pixels.data(), LogicalWidth() * 4))
            throw std::runtime_error(std::string("Skia SDL_UpdateTexture failed: ") + SDL_GetError());
        if (!SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255) || !SDL_RenderClear(renderer_)
            || !SDL_RenderTexture(renderer_, presentTexture_, nullptr, nullptr) || !SDL_RenderPresent(renderer_))
        {
            throw std::runtime_error(std::string("Skia SDL presentation failed: ") + SDL_GetError());
        }
    }

    void SkiaGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        width = ActiveSurface().Width();
        height = ActiveSurface().Height();
    }

    void SkiaGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        preferredVirtualHeight_ = height;
        RecreateBackbuffer(width, height);
    }

    void SkiaGraphicsBackend::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
        RecreateBackbuffer(LogicalWidth(), preferredVirtualHeight_ > 0 ? preferredVirtualHeight_ : LogicalHeight());
    }

    void SkiaGraphicsBackend::SetSwapInterval(int interval)
    {
        if (!SDL_SetRenderVSync(renderer_, interval))
        {
            if (interval > 1 && SDL_SetRenderVSync(renderer_, 1)) return;
            throw std::runtime_error(std::string("Skia SDL_SetRenderVSync failed: ") + SDL_GetError());
        }
    }

    bool SkiaGraphicsBackend::TransformWindowToLogical(float windowX, float windowY,
                                                        float& logX, float& logY) const
    {
        int outputWidth = 0;
        int outputHeight = 0;
        SDL_GetRenderOutputSize(renderer_, &outputWidth, &outputHeight);
        if (outputWidth <= 0 || outputHeight <= 0) return false;
        logX = windowX * static_cast<float>(LogicalWidth()) / outputWidth;
        logY = windowY * static_cast<float>(LogicalHeight()) / outputHeight;
        return true;
    }

    bool SkiaGraphicsBackend::TransformLogicalToWindow(float logX, float logY,
                                                        float& windowX, float& windowY) const
    {
        int outputWidth = 0;
        int outputHeight = 0;
        SDL_GetRenderOutputSize(renderer_, &outputWidth, &outputHeight);
        if (outputWidth <= 0 || outputHeight <= 0) return false;
        windowX = logX * static_cast<float>(outputWidth) / LogicalWidth();
        windowY = logY * static_cast<float>(outputHeight) / LogicalHeight();
        return true;
    }

    std::unique_ptr<ITextureBackend> SkiaGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<SkiaTextureBackend>(data);
    }

    std::unique_ptr<ISpriteBatchBackend> SkiaGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<SkiaSpriteBatchBackend>(activeSurface_, spriteBlendMode_,
                                                        spriteSourceAlphaConvention_, rasterState_);
    }

    std::unique_ptr<IRenderTargetBackend> SkiaGraphicsBackend::CreateRenderTarget2D(
        int width, int height, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        (void)depthFormat;
        if (width <= 0 || height <= 0)
            throw std::runtime_error("Skia RenderTarget2D dimensions must be positive.");
        if (mipMap)
            throw System::NotSupportedException(
                "Skia raster RenderTarget2D does not implement public mip chains; mipMap=true is rejected.");
        if (multiSampleCount != 0)
            throw std::runtime_error("Skia raster RenderTarget2D multisampling is not implemented yet.");
        return std::make_unique<SkiaRenderTargetBackend>(width, height, preserveContents);
    }

    void SkiaGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* renderTarget)
    {
        if (!renderTarget)
        {
            activeSurface_ = &surface_;
            return;
        }

        auto* skiaTarget = dynamic_cast<SkiaRenderTargetBackend*>(renderTarget);
        if (!skiaTarget)
            throw std::runtime_error("Skia cannot bind a render target created by a different backend.");
        skiaTarget->PrepareForBind();
        activeSurface_ = &skiaTarget->Surface();
    }

    void SkiaGraphicsBackend::ReadBackbuffer(int x, int y, int width, int height, std::uint8_t* pixels)
    {
        ActiveSurface().Flush();
        if (!ActiveSurface().ReadPixels(x, y, width, height, pixels, width * 4))
            throw std::runtime_error("Skia ReadBackbuffer request is outside the raster backbuffer.");
    }

    void SkiaGraphicsBackend::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (count < 0)
            throw std::runtime_error("Skia SetRenderTargets count must not be negative.");
        if (count == 0)
        {
            SetRenderTarget2D(nullptr);
            return;
        }
        if (!renderTargets)
            throw std::runtime_error("Skia SetRenderTargets received null descriptors with a positive count.");
        if (count != 1)
            throw std::runtime_error("Skia raster backend does not implement multiple render targets.");
        if (renderTargets[0].IsRenderTargetCubeFace())
            throw std::runtime_error("Skia raster backend does not implement RenderTargetCube faces.");
        SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
    }

    void SkiaGraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                               int colorDstBlend, int alphaDstBlend,
                                               int colorBlendFunc, int alphaBlendFunc,
                                               const BlendWriteState& writeState)
    {
        // SkCanvas supports one compositing mode per paint. Map only the standard XNA formulas
        // whose premultiplied Skia counterpart is exact; other factor/function combinations are
        // deliberately rejected until SKIA-51--57 establish an equivalent path.
        constexpr int kBlendOne = 0;
        constexpr int kBlendZero = 1;
        constexpr int kBlendSourceAlpha = 4;
        constexpr int kBlendInverseSourceAlpha = 5;
        constexpr int kBlendFunctionAdd = 0;
        constexpr int kColorWriteAll = 15;

        for (int mask : writeState.colorWriteChannels)
        {
            if (mask != kColorWriteAll)
                throw std::runtime_error("Skia raster backend does not implement ColorWriteChannels masks yet.");
        }
        if (writeState.multiSampleMask != ~0u)
            throw std::runtime_error("Skia raster backend does not implement non-default MultiSampleMask values.");
        if (colorBlendFunc != kBlendFunctionAdd || alphaBlendFunc != kBlendFunctionAdd)
            throw std::runtime_error("Skia raster backend does not implement non-additive BlendState functions yet.");

        const bool opaque = colorSrcBlend == kBlendOne && alphaSrcBlend == kBlendOne
            && colorDstBlend == kBlendZero && alphaDstBlend == kBlendZero;
        const bool alphaBlend = colorSrcBlend == kBlendOne && alphaSrcBlend == kBlendOne
            && colorDstBlend == kBlendInverseSourceAlpha && alphaDstBlend == kBlendInverseSourceAlpha;
        const bool nonPremultiplied = colorSrcBlend == kBlendSourceAlpha && alphaSrcBlend == kBlendSourceAlpha
            && colorDstBlend == kBlendInverseSourceAlpha && alphaDstBlend == kBlendInverseSourceAlpha;
        const bool additive = colorSrcBlend == kBlendSourceAlpha && alphaSrcBlend == kBlendSourceAlpha
            && colorDstBlend == kBlendOne && alphaDstBlend == kBlendOne;

        SkBlendMode mappedMode;
        SkiaSourceAlphaConvention mappedSourceConvention;
        if (opaque)
        {
            mappedMode = SkBlendMode::kSrc;
            mappedSourceConvention = SkiaSourceAlphaConvention::Premultiplied;
        }
        else if (alphaBlend)
        {
            mappedMode = SkBlendMode::kSrcOver;
            mappedSourceConvention = SkiaSourceAlphaConvention::Premultiplied;
        }
        else if (nonPremultiplied)
        {
            // Skia converts the straight-alpha image into its native premultiplied canvas pipeline
            // before applying SourceOver.  AlphaBlend selects the separately labelled premultiplied
            // image above, preventing an accidental second premultiplication.
            mappedMode = SkBlendMode::kSrcOver;
            mappedSourceConvention = SkiaSourceAlphaConvention::Straight;
        }
        else if (additive)
        {
            // kPlus adds premultiplied source and destination.  Selecting the straight-alpha
            // texture representation performs the XNA SourceAlpha source factor first.
            mappedMode = SkBlendMode::kPlus;
            mappedSourceConvention = SkiaSourceAlphaConvention::Straight;
        }
        else
        {
            throw std::runtime_error("Skia raster backend does not implement this BlendState yet.");
        }

        configuredSpriteBlendMode_ = mappedMode;
        configuredSpriteSourceAlphaConvention_ = mappedSourceConvention;
        if (blendEnabled_)
        {
            spriteBlendMode_ = mappedMode;
            spriteSourceAlphaConvention_ = mappedSourceConvention;
        }
    }

    void SkiaGraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                                   float depthBias, float slopeScaleDepthBias)
    {
        // Skia's SpriteBatch route is intrinsically filled 2D canvas geometry, without a depth
        // buffer or face winding. ScissorTestEnable is the one RasterizerState member that has
        // a direct 2D effect and is applied by SkiaSpriteBatchBackend for every draw.
        (void)cullMode;
        (void)fillMode;
        (void)depthBias;
        (void)slopeScaleDepthBias;
        rasterState_.scissorTestEnabled = scissorTestEnable;
    }

    void SkiaGraphicsBackend::SetScissorRect(int x, int y, int width, int height)
    {
        rasterState_.scissorX = x;
        rasterState_.scissorY = y;
        rasterState_.scissorWidth = width;
        rasterState_.scissorHeight = height;
    }

    void SkiaGraphicsBackend::SetViewport(int x, int y, int width, int height,
                                          float minDepth, float maxDepth)
    {
        // SpriteBatch is a top-left, 2D path. Its coordinates and Begin transform are viewport
        // local; the viewport then positions and clips the resulting canvas geometry. Depth range
        // has no observable meaning without a Skia depth buffer, but retaining this call's spatial
        // state avoids the base class's former silent no-op.
        (void)minDepth;
        (void)maxDepth;
        rasterState_.viewportSet = true;
        rasterState_.viewportX = x;
        rasterState_.viewportY = y;
        rasterState_.viewportWidth = width;
        rasterState_.viewportHeight = height;
    }

    bool SkiaGraphicsBackend::SupportsCapability(CNA::GraphicsCapability) const
    {
        return false;
    }

    void SkiaGraphicsBackend::ClearColorAndDepth(float, float, float, float, float) { ThrowNo3D("ClearColorAndDepth"); }
    void SkiaGraphicsBackend::ClearDepth(float) { ThrowNo3D("ClearDepth"); }
    void SkiaGraphicsBackend::ClearStencil(int) { ThrowNo3D("ClearStencil"); }
    void SkiaGraphicsBackend::ClearDepthAndStencil(float, int) { ThrowNo3D("ClearDepthAndStencil"); }
    void SkiaGraphicsBackend::ClearColorAndStencil(float, float, float, float, int) { ThrowNo3D("ClearColorAndStencil"); }
    void SkiaGraphicsBackend::ClearColorDepthAndStencil(float, float, float, float, float, int) { ThrowNo3D("ClearColorDepthAndStencil"); }
    void SkiaGraphicsBackend::SetDepthTestEnabled(bool) { ThrowNo3D("SetDepthTestEnabled"); }
    void SkiaGraphicsBackend::SetBlendEnabled(bool enabled)
    {
        blendEnabled_ = enabled;
        if (enabled)
        {
            spriteBlendMode_ = configuredSpriteBlendMode_;
            spriteSourceAlphaConvention_ = configuredSpriteSourceAlphaConvention_;
            return;
        }

        // This is the 2D equivalent of disabling the output-merger blend stage: source replaces
        // destination. Keep the same source labelling as BlendState::Opaque so tint/alpha obey the
        // already-tested opaque path rather than a new alpha convention.
        spriteBlendMode_ = SkBlendMode::kSrc;
        spriteSourceAlphaConvention_ = SkiaSourceAlphaConvention::Premultiplied;
    }
    void SkiaGraphicsBackend::SetDepthWriteEnabled(bool) { ThrowNo3D("SetDepthWriteEnabled"); }
    std::unique_ptr<IVertexBufferBackend> SkiaGraphicsBackend::CreateVertexBuffer(int) { ThrowNo3D("CreateVertexBuffer"); }
    std::unique_ptr<IIndexBufferBackend> SkiaGraphicsBackend::CreateIndexBuffer16(int) { ThrowNo3D("CreateIndexBuffer16"); }
    void SkiaGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&, const Matrix&, const Matrix&, const Matrix&, PrimitiveType, int) { ThrowNo3D("DrawColoredPrimitives"); }
    void SkiaGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&, const IIndexBufferBackend&, const Matrix&, const Matrix&, const Matrix&, PrimitiveType, int) { ThrowNo3D("DrawIndexedColoredPrimitives"); }
} // namespace CNA::Internal::Backends::Skia

namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_SKIA
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Skia::SkiaGraphicsBackend>(args.window, args.virtualWidth, args.virtualHeight,
                                                            args.presentationMode, args.swapInterval);
    }
#endif
} // namespace CNA::Internal::Backends
