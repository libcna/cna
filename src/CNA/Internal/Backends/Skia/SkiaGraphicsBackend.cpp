#include "CNA/Internal/Backends/Skia/SkiaGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaBlendMapping.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRenderTargetBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSpriteBatchBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaStateTrace.hpp"
#include "CNA/Internal/Backends/Skia/SkiaTextureBackend.hpp"
#include "System/NotSupportedException.hpp"

#include "include/effects/SkRuntimeEffect.h"

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

        [[nodiscard]] bool IsDestinationColorPrototype(
            int colorSrcBlend, int alphaSrcBlend,
            int colorDstBlend, int alphaDstBlend,
            int colorBlendFunc, int alphaBlendFunc) noexcept
        {
            // Blend::DestinationColor / Blend::One / Blend::Zero / Blend::Zero, with Add for
            // both branches.  This has a genuine independent alpha branch and no blend constant,
            // making it the bounded public SKIA-54 probe for the runtime-blender route.
            return colorSrcBlend == 6 && alphaSrcBlend == 0
                && colorDstBlend == 1 && alphaDstBlend == 1
                && colorBlendFunc == 0 && alphaBlendFunc == 0;
        }

        [[nodiscard]] const sk_sp<SkRuntimeEffect>& DestinationColorPrototypeEffect()
        {
            static const sk_sp<SkRuntimeEffect> effect = []
            {
                const auto result = SkRuntimeEffect::MakeForBlender(SkString(R"(
                    half4 main(half4 src, half4 dst) {
                        // XNA: RGB = src * DestinationColor, alpha = src * One + dst * Zero.
                        return half4(src.rgb * dst.rgb, src.a);
                    }
                )"));
                if (!result.effect)
                {
                    throw std::runtime_error(
                        std::string("Skia failed to compile the SKIA-54 runtime blender: ")
                        + result.errorText.c_str());
                }
                return result.effect;
            }();
            return effect;
        }

        [[nodiscard]] sk_sp<SkBlender> MakeDestinationColorPrototypeBlender()
        {
            return DestinationColorPrototypeEffect()->makeBlender(nullptr);
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
        targetBinding_->SetBackbuffer(&surface_);
        // A newly allocated raster backbuffer has no previous-frame contents to preserve.  Make
        // the zero-draw Present contract deterministic instead of exposing allocator bytes.
        surface_.Clear(0.0f, 0.0f, 0.0f, 0.0f);
        TraceSkiaState("surface=backbuffer size=%dx%d", width, height);
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
        return std::make_unique<SkiaSpriteBatchBackend>(targetBinding_->ActiveSurfaceRef(), spriteBlendMode_,
                                                        spriteCustomBlender_, spriteSourceAlphaConvention_,
                                                        rasterState_);
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
        return std::make_unique<SkiaRenderTargetBackend>(width, height, preserveContents, targetBinding_);
    }

    void SkiaGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* renderTarget)
    {
        if (!renderTarget)
        {
            targetBinding_->UnbindToBackbuffer();
            TraceSkiaState("surface=backbuffer size=%dx%d", surface_.Width(), surface_.Height());
            return;
        }

        auto* skiaTarget = dynamic_cast<SkiaRenderTargetBackend*>(renderTarget);
        if (!skiaTarget)
            throw std::runtime_error("Skia cannot bind a render target created by a different backend.");
        skiaTarget->PrepareForBind();
        targetBinding_->Bind(skiaTarget, &skiaTarget->Surface());
        TraceSkiaState("surface=render-target size=%dx%d", ActiveSurface().Width(), ActiveSurface().Height());
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
        // The table records only public XNA BlendState presets whose source-alpha convention is
        // known. SKIA-54 adds one separately proven runtime-blender tuple below; it is deliberately
        // narrow until the full factor/function generator has public target/readback evidence.
        constexpr int kColorWriteAll = 15;

        for (int mask : writeState.colorWriteChannels)
        {
            if (mask != kColorWriteAll)
                throw std::runtime_error("Skia raster backend does not implement ColorWriteChannels masks yet.");
        }
        if (writeState.multiSampleMask != ~0u)
            throw std::runtime_error("Skia raster backend does not implement non-default MultiSampleMask values.");
        const SkiaBlendMapping* mapping = FindSkiaDirectBlendMapping(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend, colorBlendFunc, alphaBlendFunc);
        const bool destinationColorPrototype = !mapping && IsDestinationColorPrototype(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend, colorBlendFunc, alphaBlendFunc);
        if (!mapping && !destinationColorPrototype)
            throw std::runtime_error(DescribeUnsupportedSkiaBlendState(
                colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend,
                colorBlendFunc, alphaBlendFunc));

        if (mapping)
        {
            configuredSpriteBlendMode_ = mapping->mode;
            configuredSpriteCustomBlender_.reset();
            configuredSpriteSourceAlphaConvention_ = mapping->sourceAlphaConvention;
        }
        else
        {
            configuredSpriteBlendMode_ = SkBlendMode::kSrcOver;
            configuredSpriteCustomBlender_ = MakeDestinationColorPrototypeBlender();
            // This first public runtime-blender state is pixel-proven only for opaque source
            // content. Retain CNA's established premultiplied label instead of guessing how a
            // general custom BlendState intends its Texture2D bytes to be interpreted.
            configuredSpriteSourceAlphaConvention_ = SkiaSourceAlphaConvention::Premultiplied;
        }
        if (blendEnabled_)
        {
            spriteBlendMode_ = configuredSpriteBlendMode_;
            spriteCustomBlender_ = configuredSpriteCustomBlender_;
            spriteSourceAlphaConvention_ = configuredSpriteSourceAlphaConvention_;
        }
        TraceSkiaState("blend mapping=%s mode=%d source-alpha=%s enabled=%s",
                       mapping ? mapping->name : "DestinationColorPrototype",
                       static_cast<int>(configuredSpriteBlendMode_),
                       configuredSpriteSourceAlphaConvention_ == SkiaSourceAlphaConvention::Premultiplied
                           ? "premultiplied" : "straight",
                       blendEnabled_ ? "true" : "false");
    }

    void SkiaGraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                                   float depthBias, float slopeScaleDepthBias)
    {
        constexpr int kFillModeSolid = 0;
        if (fillMode != kFillModeSolid)
        {
            throw std::runtime_error(
                "Skia raster backend does not implement RasterizerState::FillMode::WireFrame.");
        }

        // Skia's SpriteBatch route is intrinsically filled 2D canvas geometry, without a depth
        // buffer or face winding.  CullMode, depth biases, and multisample policy only affect a
        // 3D pipeline, which this backend does not advertise and whose draw entry points reject.
        // ScissorTestEnable is the one RasterizerState member with a direct 2D effect and is
        // applied by SkiaSpriteBatchBackend for every draw.  WireFrame is rejected above rather
        // than silently approximating a textured sprite with filled canvas geometry.
        (void)cullMode;
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
        TraceSkiaState("scissor x=%d y=%d width=%d height=%d", x, y, width, height);
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
            spriteCustomBlender_ = configuredSpriteCustomBlender_;
            spriteSourceAlphaConvention_ = configuredSpriteSourceAlphaConvention_;
            TraceSkiaState("blend enabled=true mode=%d runtime-blender=%s",
                           static_cast<int>(spriteBlendMode_), spriteCustomBlender_ ? "true" : "false");
            return;
        }

        // This is the 2D equivalent of disabling the output-merger blend stage: source replaces
        // destination. Keep the same source labelling as BlendState::Opaque so tint/alpha obey the
        // already-tested opaque path rather than a new alpha convention.
        spriteBlendMode_ = SkBlendMode::kSrc;
        spriteCustomBlender_.reset();
        spriteSourceAlphaConvention_ = SkiaSourceAlphaConvention::Premultiplied;
        TraceSkiaState("blend enabled=false mode=%d", static_cast<int>(spriteBlendMode_));
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
