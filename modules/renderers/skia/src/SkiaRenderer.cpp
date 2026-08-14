#include "CNA/Internal/Renderers/Skia/SkiaRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaBlendMapping.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaEffectRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaGeneratedBlender.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaRenderTargetRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaRenderTargetCubeRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaStateTrace.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaTextureRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaTextureStorageRenderers.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaUnsupported3D.hpp"
#include "System/NotSupportedException.hpp"

#include "include/core/SkData.h"
#include "include/effects/SkRuntimeEffect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::Skia
{
    namespace
    {
        class SkiaUnsupportedOcclusionQuery final : public IOcclusionQueryRenderer
        {
        public:
            void Begin() override { ThrowSkiaUnsupported3D("OcclusionQuery::Begin"); }
            void End() override { ThrowSkiaUnsupported3D("OcclusionQuery::End"); }
            [[nodiscard]] bool IsComplete() const override { return false; }
            [[nodiscard]] int PixelCount() const override { return 0; }
        };

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

        struct MaskedBlendUniform
        {
            // source-over, XNA NonPremultiplied, XNA Additive, destination-colour runtime route
            float route[4];
            float writeMask[4];
        };
        static_assert(sizeof(MaskedBlendUniform) == sizeof(float) * 8);

        [[nodiscard]] const sk_sp<SkRuntimeEffect>& MaskedBlendEffect()
        {
            static const sk_sp<SkRuntimeEffect> effect = []
            {
                const auto result = SkRuntimeEffect::MakeForBlender(SkString(R"(
                    uniform float4 route;
                    uniform float4 writeMask;
                    half4 main(half4 src, half4 dst) {
                        // Skia supplies premultiplied src. For straight-labelled XNA input,
                        // src.rgb therefore already includes the colour SourceAlpha factor, but
                        // XNA's independent alpha equation still needs src.a * src.a. Recover
                        // the logical destination RGB channels before applying XNA factors, then
                        // premultiply the independently computed result for SkSurface storage so
                        // public unpremultiplied readback returns those same logical channels.
                        half3 dstColor = dst.a > 0.0 ? dst.rgb / dst.a : half3(0.0);
                        half nonPremulAlpha = saturate(src.a * src.a + dst.a * (1.0 - src.a));
                        half3 nonPremulColor = saturate(src.rgb + dstColor * (1.0 - src.a));
                        half additiveAlpha = saturate(src.a * src.a + dst.a);
                        half3 additiveColor = saturate(src.rgb + dstColor);
                        half4 blended = src;
                        blended = mix(blended, src + dst * (1.0 - src.a), route.x);
                        blended = mix(blended,
                            half4(nonPremulColor * nonPremulAlpha, nonPremulAlpha), route.y);
                        blended = mix(blended,
                            half4(additiveColor * additiveAlpha, additiveAlpha), route.z);
                        blended = mix(blended, half4(src.rgb * dst.rgb, src.a), route.w);
                        // XNA ColorWriteChannels is an output-merger write mask: choose after
                        // calculating the blend result, retaining disabled destination channels.
                        return half4(mix(dst, blended, writeMask));
                    }
                )"));
                if (!result.effect)
                {
                    throw std::runtime_error(
                        std::string("Skia failed to compile the masked runtime blender: ")
                        + result.errorText.c_str());
                }
                return result.effect;
            }();
            return effect;
        }

        [[nodiscard]] sk_sp<SkBlender> MakeMaskedBlender(const SkiaBlendMapping& mapping, int writeMask)
        {
            MaskedBlendUniform uniform{};
            switch (mapping.route)
            {
                case SkiaBlendMappingRoute::DirectBlendMode:
                    if (mapping.mode == SkBlendMode::kSrcOver)
                        uniform.route[0] = 1.0f;
                    else if (mapping.mode != SkBlendMode::kSrc)
                        throw std::runtime_error("Skia has no masked runtime formula for this blend mapping.");
                    break;
                case SkiaBlendMappingRoute::RuntimeNonPremultiplied:
                    uniform.route[1] = 1.0f;
                    break;
                case SkiaBlendMappingRoute::RuntimeAdditive:
                    uniform.route[2] = 1.0f;
                    break;
                case SkiaBlendMappingRoute::RuntimeDestinationColorPrototype:
                    uniform.route[3] = 1.0f;
                    break;
            }

            for (int channel = 0; channel < 4; ++channel)
                uniform.writeMask[channel] = (writeMask & (1 << channel)) ? 1.0f : 0.0f;
            const sk_sp<SkBlender> blender = MaskedBlendEffect()->makeBlender(
                SkData::MakeWithCopy(&uniform, sizeof(uniform)));
            if (!blender)
                throw std::runtime_error("Skia failed to create a masked runtime blender.");
            return blender;
        }
    }

    SkiaRenderer::SkiaRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                                             CnaPresentationMode presentationMode, int swapInterval,
                                             std::function<void(RendererDeviceEvent)> deviceEventCallback,
                                             SkiaInitializationFailurePointEXT failurePoint)
        : window_(window)
        , deviceEventCallback_(std::move(deviceEventCallback))
        , presentationMode_(presentationMode)
        , preferredVirtualWidth_(virtualWidth)
        , preferredVirtualHeight_(virtualHeight)
    {
        if (!window_)
            throw std::runtime_error("SkiaRenderer initialized with null window.");

        bool registered = false;
        try
        {
            const auto failAt = [failurePoint](SkiaInitializationFailurePointEXT point,
                                                const char* stage)
            {
                if (failurePoint == point)
                    throw std::runtime_error(std::string("Skia injected initialization failure after ") + stage);
            };

            renderer_ = SDL_CreateRenderer(window_, nullptr);
            if (!renderer_)
                throw std::runtime_error(std::string("Skia SDL_CreateRenderer failed: ") + SDL_GetError());
            failAt(SkiaInitializationFailurePointEXT::AfterRenderer, "renderer creation");

            SetSwapInterval(swapInterval);
            RecreateBackbuffer(virtualWidth, virtualHeight);
            failAt(SkiaInitializationFailurePointEXT::AfterBackbuffer, "backbuffer creation");

            IGraphicsRenderer::RegisterForWindow(window_, this);
            registered = true;
            failAt(SkiaInitializationFailurePointEXT::AfterRegistration, "renderer registration");
            std::cout << kSkiaStartupDiagnostic << std::endl;
        }
        catch (...)
        {
            // A throwing constructor never reaches ~SkiaRenderer(). Unwind every acquired
            // SDL/registry resource here so the caller-owned window can host a succeeding renderer.
            if (registered)
                IGraphicsRenderer::UnregisterForWindow(window_);
            DestroyPresentationTexture();
            if (renderer_)
            {
                SDL_DestroyRenderer(renderer_);
                renderer_ = nullptr;
            }
            throw;
        }
    }

    SkiaRenderer::~SkiaRenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(window_);
        DestroyPresentationTexture();
        if (renderer_) SDL_DestroyRenderer(renderer_);
    }

    void SkiaRenderer::AssertOwnership(const char* operation) const
    {
        ownership_->AssertOwnerThread(operation);
        if (!window_ || !renderer_ || SDL_GetRenderer(window_) != renderer_)
        {
            throw std::runtime_error(
                std::string("Skia presenter ownership violation during ") + operation + ".");
        }
    }

    SkiaSurface& SkiaRenderer::ActiveSurface()
    {
        AssertOwnership("active-surface access");
        targetBinding_->AssertConsistent(&surface_);
        SkiaSurface* activeSurface = targetBinding_->ActiveSurface();
        SkiaRasterTarget* activeTarget = targetBinding_->ActiveTarget();
        if (activeTarget && activeSurface != activeTarget->BoundSurfaceEXT())
            throw std::runtime_error("Skia active target does not own the selected raster surface.");
        return *activeSurface;
    }

    const SkiaSurface& SkiaRenderer::ActiveSurface() const
    {
        AssertOwnership("active-surface access");
        targetBinding_->AssertConsistent(&surface_);
        SkiaSurface* activeSurface = targetBinding_->ActiveSurface();
        const SkiaRasterTarget* activeTarget = targetBinding_->ActiveTarget();
        if (activeTarget && activeSurface != activeTarget->BoundSurfaceEXT())
            throw std::runtime_error("Skia active target does not own the selected raster surface.");
        return *activeSurface;
    }

    void SkiaRenderer::GetPresentationOutputSize(int& width, int& height) const
    {
        if (debugOutputSizeOverride_)
        {
            width = debugOutputWidth_;
            height = debugOutputHeight_;
            return;
        }
        width = 0;
        height = 0;
        (void)SDL_GetRenderOutputSize(renderer_, &width, &height);
    }

    void SkiaRenderer::RecreateBackbuffer(int requestedWidth, int requestedHeight)
    {
        int outputWidth = 0;
        int outputHeight = 0;
        GetPresentationOutputSize(outputWidth, outputHeight);

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
        TraceSkiaState("surface=backbuffer id=%llu size=%dx%d",
                       static_cast<unsigned long long>(surface_.Identity()), width, height);
        DestroyPresentationTexture();
        RecreatePresentationTexture();

        ApplyLogicalPresentation();
    }

    void SkiaRenderer::RefreshDynamicBackbufferIfNeeded()
    {
        if (presentationMode_ != CnaPresentationMode::FixedHeightDynamicWidth
            || targetBinding_->ActiveTarget() != nullptr || preferredVirtualHeight_ <= 0)
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
            && (desiredWidth != LogicalWidth() || preferredVirtualHeight_ != LogicalHeight()))
        {
            RecreateBackbuffer(preferredVirtualWidth_, preferredVirtualHeight_);
        }
    }

    void SkiaRenderer::RecreatePresentationTexture()
    {
        presentTexture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_STREAMING, LogicalWidth(), LogicalHeight());
        if (!presentTexture_)
            throw std::runtime_error(std::string("Skia SDL_CreateTexture failed: ") + SDL_GetError());
    }

    void SkiaRenderer::DestroyPresentationTexture() noexcept
    {
        if (!presentTexture_)
            return;
        SDL_DestroyTexture(presentTexture_);
        presentTexture_ = nullptr;
    }

    void SkiaRenderer::RecreatePresentationRenderer()
    {
        // Skia's selected raster surface and every raster image are CPU-owned, so they have no
        // GPU/context handle to recreate. The SDL renderer and its streaming texture are the
        // presentation-only device objects. Rebuild those without calling RecreateBackbuffer(),
        // which would incorrectly clear the live raster surface and invalidate app resources.
        DestroyPresentationTexture();
        if (renderer_)
        {
            SDL_DestroyRenderer(renderer_);
            renderer_ = nullptr;
        }
        renderer_ = SDL_CreateRenderer(window_, nullptr);
        if (!renderer_)
            throw std::runtime_error(std::string("Skia SDL_CreateRenderer failed during presentation recovery: ")
                                     + SDL_GetError());

        try
        {
            SetSwapInterval(swapInterval_);
            RecreatePresentationTexture();
            ApplyLogicalPresentation();
        }
        catch (...)
        {
            DestroyPresentationTexture();
            SDL_DestroyRenderer(renderer_);
            renderer_ = nullptr;
            throw;
        }
    }

    void SkiaRenderer::ApplyLogicalPresentation()
    {
        if (!SDL_SetRenderLogicalPresentation(renderer_, LogicalWidth(), LogicalHeight(),
                                              ToSdlPresentation(presentationMode_)))
        {
            throw std::runtime_error(std::string("Skia SDL_SetRenderLogicalPresentation failed: ") + SDL_GetError());
        }
    }

    void SkiaRenderer::Clear(float r, float g, float b, float a)
    {
        AssertOwnership("Clear");
        RefreshDynamicBackbufferIfNeeded();
        if (SkiaRasterTarget* target = targetBinding_->ActiveTarget())
            target->BeforeWriteEXT();
        ActiveSurface().Clear(r, g, b, a);
    }

    void SkiaRenderer::Present()
    {
        AssertOwnership("Present");
        surface_.Flush();
        const auto pixels = surface_.SnapshotRgba();
        if (!SDL_UpdateTexture(presentTexture_, nullptr, pixels.data(), LogicalWidth() * 4))
            throw std::runtime_error(std::string("Skia SDL_UpdateTexture failed: ") + SDL_GetError());
        SDL_FRect nativeDestination{
            0.0f, 0.0f, static_cast<float>(LogicalWidth()), static_cast<float>(LogicalHeight())};
        const SDL_FRect* destination = presentationMode_ == CnaPresentationMode::NativeBackBuffer
            ? &nativeDestination : nullptr;
        if (!SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255) || !SDL_RenderClear(renderer_)
            || !SDL_RenderTexture(renderer_, presentTexture_, nullptr, destination)
            || !SDL_RenderPresent(renderer_))
        {
            throw std::runtime_error(std::string("Skia SDL presentation failed: ") + SDL_GetError());
        }

        // A resize can arrive between draws without a ClientSizeChanged notification. Preserve
        // the just-completed frame, then make the CPU raster surface match the new dynamic width
        // for the next frame. GraphicsDevice::Present immediately queries GetViewportSize(), so
        // its public viewport observes this replacement in the same Present call.
        RefreshDynamicBackbufferIfNeeded();
    }

    void SkiaRenderer::GetViewportSize(int& width, int& height)
    {
        AssertOwnership("GetViewportSize");
        RefreshDynamicBackbufferIfNeeded();
        width = ActiveSurface().Width();
        height = ActiveSurface().Height();
    }

    void SkiaRenderer::SetVirtualResolution(int width, int height)
    {
        AssertOwnership("SetVirtualResolution");
        preferredVirtualWidth_ = width;
        preferredVirtualHeight_ = height;

        // GraphicsDevice::Reset() requests the matching SDL window size immediately before this
        // call. SDL documents that request as asynchronous on some window systems (notably X11),
        // while FixedHeightDynamicWidth derives the raster width from the renderer's live output.
        // Synchronize the pending request so a shrinking window cannot recreate a transiently
        // narrower backbuffer from the old aspect ratio. A timeout is deliberately non-fatal: the
        // existing dynamic refresh path will still converge when SDL reports the new output size.
        if (!SDL_SyncWindow(window_))
            SDL_ClearError();

        RecreateBackbuffer(width, height);
    }

    void SkiaRenderer::SetPresentationMode(int mode)
    {
        AssertOwnership("SetPresentationMode");
        if (mode < static_cast<int>(CnaPresentationMode::Letterbox)
            || mode > static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth))
        {
            throw std::out_of_range("Skia received an invalid presentation mode.");
        }
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
        RecreateBackbuffer(preferredVirtualWidth_, preferredVirtualHeight_);
    }

    void SkiaRenderer::SetSwapInterval(int interval)
    {
        AssertOwnership("SetSwapInterval");
        if (!SDL_SetRenderVSync(renderer_, interval))
        {
            if (interval > 1 && SDL_SetRenderVSync(renderer_, 1))
            {
                swapInterval_ = 1;
                return;
            }
            throw std::runtime_error(std::string("Skia SDL_SetRenderVSync failed: ") + SDL_GetError());
        }
        swapInterval_ = interval;
    }

    void SkiaRenderer::DebugSetPresentationOutputSizeEXT(int width, int height)
    {
        AssertOwnership("DebugSetPresentationOutputSizeEXT");
        if (width < 0 || height < 0)
            throw std::out_of_range("Skia debug presentation output size must not be negative.");
        debugOutputSizeOverride_ = true;
        debugOutputWidth_ = width;
        debugOutputHeight_ = height;
    }

    void SkiaRenderer::DebugClearPresentationOutputSizeEXT()
    {
        AssertOwnership("DebugClearPresentationOutputSizeEXT");
        debugOutputSizeOverride_ = false;
        debugOutputWidth_ = 0;
        debugOutputHeight_ = 0;
    }

    void SkiaRenderer::DebugSimulateContextLoss()
    {
        AssertOwnership("DebugSimulateContextLoss");
        // A CPU-raster Skia surface cannot incur a real GPU context loss. The useful equivalent
        // is loss of the SDL presenter: reconstruct its renderer/streaming texture while leaving
        // CPU-owned Texture2D and RenderTarget2D data live. Report only a reset pair, rather than
        // fabricating DeviceLost for a device whose raster resources never became unavailable.
        if (deviceEventCallback_)
            deviceEventCallback_(RendererDeviceEvent::Resetting);
        RecreatePresentationRenderer();
        if (deviceEventCallback_)
            deviceEventCallback_(RendererDeviceEvent::Reset);
    }

    void SkiaRenderer::DebugRestoreContext()
    {
        AssertOwnership("DebugRestoreContext");
        // Desktop recovery is immediate, exactly like EasyGL's existing debug restore path.
        DebugSimulateContextLoss();
    }

    bool SkiaRenderer::TransformWindowToLogical(float windowX, float windowY,
                                                        float& logX, float& logY) const
    {
        AssertOwnership("TransformWindowToLogical");
        // Window coordinates are SDL window-space points, whereas GetRenderOutputSize() reports
        // physical renderer pixels. Scaling against the latter halves a coordinate on a 2x HiDPI
        // display and also loses Letterbox/Overscan offsets. SDL owns both conversions for its
        // active logical-presentation mode, so use the exact same offset- and DPI-aware mapping
        // as the input bridge and Mouse::SetPosition path.
        return renderer_ && SDL_RenderCoordinatesFromWindow(renderer_, windowX, windowY, &logX, &logY);
    }

    bool SkiaRenderer::TransformLogicalToWindow(float logX, float logY,
                                                        float& windowX, float& windowY) const
    {
        AssertOwnership("TransformLogicalToWindow");
        return renderer_ && SDL_RenderCoordinatesToWindow(renderer_, logX, logY, &windowX, &windowY);
    }

    std::unique_ptr<ITextureRenderer> SkiaRenderer::CreateTexture(const ImageData& data)
    {
        AssertOwnership("CreateTexture");
        return std::make_unique<SkiaTextureRenderer>(data, resourceCounters_);
    }

    std::unique_ptr<ITextureCubeRenderer> SkiaRenderer::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        AssertOwnership("CreateTextureCube");
        if (surfaceFormat != 0)
            throw System::NotSupportedException("Skia CPU TextureCube storage supports only RGBA8 Color.");
        return std::make_unique<SkiaTextureCubeRenderer>(size, mipMap, resourceCounters_);
    }

    std::unique_ptr<ITexture3DRenderer> SkiaRenderer::CreateTexture3D(
        int width, int height, int depth, bool mipMap, int surfaceFormat)
    {
        AssertOwnership("CreateTexture3D");
        if (surfaceFormat != 0)
            throw System::NotSupportedException("Skia CPU Texture3D storage supports only RGBA8 Color.");
        return std::make_unique<SkiaTexture3DRenderer>(
            width, height, depth, mipMap, resourceCounters_);
    }

    std::unique_ptr<ISpriteBatchRenderer> SkiaRenderer::CreateSpriteBatch()
    {
        AssertOwnership("CreateSpriteBatch");
        return std::make_unique<SkiaSpriteBatchRenderer>(targetBinding_->ActiveSurfaceRef(), spriteBlendMode_,
                                                         spriteCustomBlender_, spriteSourceAlphaConvention_,
                                                         rasterState_, targetBinding_);
    }

    std::unique_ptr<IEffectRenderer> SkiaRenderer::CreateEffectRenderer(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        AssertOwnership("CreateEffectRenderer");
        // SKIA-157: the mesh ABI (docs/skia-vertices-2d-effect-contract.md) uses its own, distinct
        // marker so a mesh-mode program can never be silently accepted down the sprite path.
        if (vertSrc == kSkiaSkslMeshEffectMarkerEXT)
        {
            auto meshEffect = std::make_unique<SkiaMeshEffectAdapterEXT>(meshEffectCache_);
            (void)meshEffect->CompileProgram(vertSrc, fragSrc);
            return meshEffect;
        }
        // Existing ShaderEffect payloads are untagged GLSL/HLSL or even binary SPIR-V. Preserve
        // the historical null result for those rather than asking SkSL to guess their language.
        if (vertSrc != kSkiaSkslSpriteEffectMarkerEXT)
            return nullptr;

        auto renderer = std::make_unique<SkiaEffectRenderer>();
        (void)renderer->CompileProgram(vertSrc, fragSrc);
        return renderer;
    }

    std::unique_ptr<IRenderTargetRenderer> SkiaRenderer::CreateRenderTarget2D(
        int width, int height, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        return CreateRenderTarget2DEXT(
            width, height, depthFormat, preserveContents, mipMap, multiSampleCount,
            static_cast<int>(Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color));
    }

    std::unique_ptr<IRenderTargetRenderer> SkiaRenderer::CreateRenderTarget2DEXT(
        int width, int height, int depthFormat, bool preserveContents, bool mipMap,
        int multiSampleCount, int surfaceFormat)
    {
        AssertOwnership("CreateRenderTarget2DEXT");
        (void)depthFormat;
        if (width <= 0 || height <= 0)
            throw std::runtime_error("Skia RenderTarget2D dimensions must be positive.");
        if (multiSampleCount != 0)
            throw std::runtime_error(
                "Skia raster RenderTarget2D does not support multisampling; real sample counts are rejected.");
        return std::make_unique<SkiaRenderTargetRenderer>(
            width, height, preserveContents, targetBinding_, resourceCounters_, mipMap,
            static_cast<Microsoft::Xna::Framework::Graphics::SurfaceFormat>(surfaceFormat));
    }

    std::unique_ptr<IRenderTargetCubeRenderer> SkiaRenderer::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        AssertOwnership("CreateRenderTargetCube");
        (void)depthFormat;
        (void)multiSampleCount; // Raster applies zero samples and reports that exact clamp.
        return std::make_unique<SkiaRenderTargetCubeRenderer>(
            size, preserveContents, mipMap, targetBinding_, resourceCounters_);
    }

    void SkiaRenderer::SetRenderTarget2D(IRenderTargetRenderer* renderTarget)
    {
        AssertOwnership("SetRenderTarget2D");

        // Validate the requested destination before resolving the current one. A failed foreign
        // or cross-device bind is not a pass boundary and must leave both the active selection and
        // its dirty mip chain byte-for-byte unchanged.
        SkiaRenderTargetRenderer* skiaTarget = nullptr;
        if (renderTarget)
        {
            skiaTarget = dynamic_cast<SkiaRenderTargetRenderer*>(renderTarget);
            if (!skiaTarget || !skiaTarget->BelongsToBindingEXT(targetBinding_))
            {
                throw std::runtime_error(
                    "Skia cannot bind a render target created by a different graphics renderer.");
            }
        }

        if (SkiaRasterTarget* activeTarget = targetBinding_->ActiveTarget())
            activeTarget->FinalizeWriteEXT();
        if (!renderTarget)
        {
            targetBinding_->UnbindToBackbuffer();
            RefreshDynamicBackbufferIfNeeded();
            TraceSkiaState("surface=backbuffer id=%llu size=%dx%d",
                           static_cast<unsigned long long>(surface_.Identity()),
                           surface_.Width(), surface_.Height());
            return;
        }

        skiaTarget->PrepareForBind();
        targetBinding_->Bind(skiaTarget, &skiaTarget->Surface());
        TraceSkiaState("surface=render-target id=%llu size=%dx%d",
                       static_cast<unsigned long long>(ActiveSurface().Identity()),
                       ActiveSurface().Width(), ActiveSurface().Height());
    }

    void SkiaRenderer::SetRenderTargetCubeFace(
        IRenderTargetCubeRenderer* renderTarget, int face)
    {
        AssertOwnership("SetRenderTargetCubeFace");
        if (!renderTarget)
        {
            SetRenderTarget2D(nullptr);
            return;
        }
        auto* skiaTarget = dynamic_cast<SkiaRenderTargetCubeRenderer*>(renderTarget);
        if (!skiaTarget)
            throw std::runtime_error("Skia cannot bind a cube target created by a different renderer.");
        skiaTarget->BindAsRenderTargetFace(face);
        TraceSkiaState("surface=render-target-cube face=%d id=%llu size=%dx%d",
                       face,
                       static_cast<unsigned long long>(ActiveSurface().Identity()),
                       ActiveSurface().Width(), ActiveSurface().Height());
    }

    void SkiaRenderer::ReadBackbuffer(int x, int y, int width, int height, std::uint8_t* pixels)
    {
        AssertOwnership("ReadBackbuffer");
        // SKIA-68 (established, tested contract -- see Skia_GetBackBufferData_ActiveTarget):
        // GetBackBufferData deliberately follows Skia's active canvas while a RenderTarget2D is
        // bound, reverting to the preserved default backbuffer after unbind. SKIA-142 makes that
        // active canvas capable of a native pixel layout other than RGBA8 for the first time; this
        // hardcoded width*4 raw read would silently reinterpret those bytes as straight Color
        // instead of converting them, so refuse clearly instead of returning corrupted pixels.
        SkiaSurface& active = ActiveSurface();
        if (active.NativeBytesPerPixelEXT() != 4u)
        {
            throw System::NotSupportedException(
                "Skia GetBackBufferData does not yet support a non-Color-format RenderTarget2D "
                "as the active render target; unbind it (or read it directly) first.");
        }
        active.Flush();
        if (!active.ReadPixels(x, y, width, height, pixels, width * 4))
            throw std::runtime_error("Skia ReadBackbuffer request is outside the raster backbuffer.");
    }

    void SkiaRenderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        AssertOwnership("SetRenderTargets");
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
        {
            throw std::runtime_error(
                "Skia raster renderer cannot emulate multiple render targets: SkCanvas has one "
                "color output, so the active target set was not changed.");
        }
        if (renderTargets[0].IsRenderTargetCubeFace())
        {
            SetRenderTargetCubeFace(renderTargets[0].GetRenderTargetCube(),
                                    renderTargets[0].GetCubeFace());
            return;
        }
        SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
    }

    void SkiaRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                               int colorDstBlend, int alphaDstBlend,
                                               int colorBlendFunc, int alphaBlendFunc,
                                               const BlendWriteState& writeState)
    {
        AssertOwnership("ApplyBlendState");
        // Preserve the five existing pixel-proven routes exactly. Every other valid tuple uses
        // the bounded generic program; invalid raw ordinals still fail before state mutation.
        constexpr int kColorWriteAll = 15;
        const int colorWriteMask = writeState.colorWriteChannels[0];
        if (colorWriteMask < 0 || colorWriteMask > kColorWriteAll)
            throw std::runtime_error("Skia raster renderer received an invalid ColorWriteChannels mask.");
        for (int target = 1; target < 4; ++target)
        {
            if (writeState.colorWriteChannels[target] != kColorWriteAll)
            {
                throw std::runtime_error(
                    "Skia raster renderer has one render target and does not implement ColorWriteChannels1-3.");
            }
        }
        if (writeState.multiSampleMask != ~0u)
            throw std::runtime_error("Skia raster renderer does not implement non-default MultiSampleMask values.");

        const SkiaGeneratedBlendSelectors generatedSelectors{
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend,
            colorBlendFunc, alphaBlendFunc,
        };
        const SkiaBlendSelectorDisposition disposition = ClassifySkiaBlendSelectors(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend,
            colorBlendFunc, alphaBlendFunc);
        const SkiaBlendMapping* mapping
            = disposition == SkiaBlendSelectorDisposition::EstablishedMapping
                ? FindSkiaBlendMapping(colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend,
                                       colorBlendFunc, alphaBlendFunc)
                : nullptr;

        SkBlendMode configuredMode = SkBlendMode::kSrcOver;
        sk_sp<SkBlender> configuredBlender;
        SkiaSourceAlphaConvention configuredConvention = kSkiaGeneratedBlendSourceAlphaConvention;
        const bool usesGeneratedBlender
            = disposition == SkiaBlendSelectorDisposition::Generated;
        const char* mappingName = "Generated";
        if (!mapping)
        {
            std::string error;
            configuredBlender = TryMakeSkiaGeneratedBlender(
                generatedSelectors, blendFactor_, colorWriteMask, error);
            if (!configuredBlender)
                throw std::runtime_error(error);
        }
        else if (colorWriteMask == kColorWriteAll
                 && mapping->route == SkiaBlendMappingRoute::DirectBlendMode)
        {
            configuredMode = mapping->mode;
            configuredConvention = mapping->sourceAlphaConvention;
            mappingName = mapping->name;
        }
        else
        {
            configuredBlender = MakeMaskedBlender(*mapping, colorWriteMask);
            configuredConvention = mapping->sourceAlphaConvention;
            mappingName = mapping->name;
        }

        sk_sp<SkBlender> disabledBlender;
        if (!blendEnabled_ && colorWriteMask != kColorWriteAll)
        {
            const SkiaBlendMapping* opaque = FindSkiaBlendMapping(0, 0, 1, 1, 0, 0);
            if (!opaque)
                throw std::runtime_error("Skia internal Opaque blend mapping is unavailable.");
            disabledBlender = MakeMaskedBlender(*opaque, colorWriteMask);
        }

        // Commit only after every validation/SkBlender construction succeeds.
        configuredSpriteBlendMode_ = configuredMode;
        configuredSpriteCustomBlender_ = std::move(configuredBlender);
        configuredSpriteSourceAlphaConvention_ = configuredConvention;
        configuredGeneratedBlendSelectors_ = generatedSelectors;
        configuredColorWriteMask_ = colorWriteMask;
        configuredUsesGeneratedBlender_ = usesGeneratedBlender;
        if (blendEnabled_)
        {
            spriteBlendMode_ = configuredSpriteBlendMode_;
            spriteCustomBlender_ = configuredSpriteCustomBlender_;
            spriteSourceAlphaConvention_ = configuredSpriteSourceAlphaConvention_;
        }
        else
        {
            spriteBlendMode_ = SkBlendMode::kSrc;
            spriteCustomBlender_ = std::move(disabledBlender);
            spriteSourceAlphaConvention_ = SkiaSourceAlphaConvention::Premultiplied;
        }
        TraceSkiaState("blend mapping=%s mode=%d write-mask=%d source-alpha=%s enabled=%s",
                       mappingName,
                       static_cast<int>(configuredSpriteBlendMode_),
                       colorWriteMask,
                       configuredSpriteSourceAlphaConvention_ == SkiaSourceAlphaConvention::Premultiplied
                           ? "premultiplied" : "straight",
                       blendEnabled_ ? "true" : "false");
    }

    void SkiaRenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        AssertOwnership("SetBlendFactor");
        const std::array<float, 4> candidate{r, g, b, a};
        for (std::size_t channel = 0; channel < candidate.size(); ++channel)
        {
            if (!std::isfinite(candidate[channel]) || candidate[channel] < 0.0f
                || candidate[channel] > 1.0f)
            {
                throw std::runtime_error(
                    "Skia BlendFactor channel " + std::to_string(channel)
                    + " must be finite and within [0, 1].");
            }
        }

        sk_sp<SkBlender> rebuilt;
        if (configuredUsesGeneratedBlender_)
        {
            std::string error;
            rebuilt = TryMakeSkiaGeneratedBlender(
                configuredGeneratedBlendSelectors_, candidate, configuredColorWriteMask_, error);
            if (!rebuilt)
                throw std::runtime_error(error);
        }

        blendFactor_ = candidate;
        if (configuredUsesGeneratedBlender_)
        {
            configuredSpriteCustomBlender_ = std::move(rebuilt);
            if (blendEnabled_)
                spriteCustomBlender_ = configuredSpriteCustomBlender_;
        }
        TraceSkiaState("blend factor=(%.6f,%.6f,%.6f,%.6f) generated=%s",
                       r, g, b, a, configuredUsesGeneratedBlender_ ? "true" : "false");
    }

    void SkiaRenderer::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                                   float depthBias, float slopeScaleDepthBias)
    {
        AssertOwnership("ApplyRasterizerState");
        constexpr int kFillModeSolid = 0;
        if (fillMode != kFillModeSolid)
        {
            ThrowSkiaUnsupported3D("RasterizerState::FillMode::WireFrame");
        }

        // Skia's SpriteBatch route is intrinsically filled 2D canvas geometry, without a depth
        // buffer or face winding.  CullMode, depth biases, and multisample policy only affect a
        // 3D pipeline, which this renderer does not advertise and whose draw entry points reject.
        // ScissorTestEnable is the one RasterizerState member with a direct 2D effect and is
        // applied by SkiaSpriteBatchRenderer for every draw.  WireFrame is rejected above rather
        // than silently approximating a textured sprite with filled canvas geometry.
        (void)cullMode;
        (void)depthBias;
        (void)slopeScaleDepthBias;
        rasterState_.scissorTestEnabled = scissorTestEnable;
    }

    void SkiaRenderer::SetScissorRect(int x, int y, int width, int height)
    {
        AssertOwnership("SetScissorRect");
        rasterState_.scissorX = x;
        rasterState_.scissorY = y;
        rasterState_.scissorWidth = width;
        rasterState_.scissorHeight = height;
        TraceSkiaState("scissor x=%d y=%d width=%d height=%d", x, y, width, height);
    }

    void SkiaRenderer::SetViewport(int x, int y, int width, int height,
                                          float minDepth, float maxDepth)
    {
        AssertOwnership("SetViewport");
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

    bool SkiaRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        AssertOwnership("SupportsCapability");
        // REMED-GFX-201/202: exhaustive over every member, with no `default` arm, so a capability
        // added after this renderer was written is a compile-time -Wswitch diagnostic here instead
        // of an inherited wrong answer. The equality test this replaces was already truthful for
        // the nine members that existed then; MultiStreamVertexInput and Instancing are new, and
        // both are false because this renderer has no vertex-stream or draw pipeline at all --
        // every 3D route goes through Ensure3DSupported()'s refusal before any binding is read.
        switch (capability)
        {
            // Bounded CPU transfer/readback storage only, per docs/skia-texture-storage.md. This
            // flag describes storage and never promises shader sampling.
            case CNA::GraphicsCapability::Texture3D:
                return true;

            case CNA::GraphicsCapability::AdditiveBlending:
                return true;

            case CNA::GraphicsCapability::ThreeD:
            case CNA::GraphicsCapability::DepthStencilBuffer:
            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
            case CNA::GraphicsCapability::MultipleRenderTargets:
            case CNA::GraphicsCapability::AnisotropicFiltering:
            case CNA::GraphicsCapability::WireFrame:
            case CNA::GraphicsCapability::OcclusionQuery:
            // Deliberately false rather than true: the accepted route is the narrow, opt-in
            // CNA_SKIA_SKSL_V1/CNA_SKIA_SKSL_MESH_V1 ABI, not the arbitrary-Effect support a true
            // would promise. Under-reporting a bounded extension is honest; claiming general
            // custom-effect support and then throwing on ordinary GLSL would not be.
            case CNA::GraphicsCapability::CustomEffects:
            case CNA::GraphicsCapability::MultiStreamVertexInput:
            case CNA::GraphicsCapability::Instancing:
            case CNA::GraphicsCapability::StencilBuffer:
                return false;
        }
        return false;
    }

    void SkiaRenderer::Ensure3DSupported(const char* operation) const
    {
        AssertOwnership(operation);
        ThrowSkiaUnsupported3D(operation);
    }

    void SkiaRenderer::ApplyDepthStencilState(
        bool depthEnable, bool depthWriteEnable, int,
        bool stencilEnable, int, int, int, int, int, int, int,
        bool, int, int, int, int)
    {
        // DepthStencilState.None is part of the normal SpriteBatch 2D contract. It describes the
        // absence of a depth/stencil operation, so accepting it does not claim an attachment.
        if (depthEnable || depthWriteEnable || stencilEnable)
            ThrowSkiaUnsupported3D("ApplyDepthStencilState");
    }

    void SkiaRenderer::SetReferenceStencil(int value)
    {
        // Zero accompanies the accepted disabled state. A nonzero reference would only have
        // meaning in the unsupported stencil pipeline.
        if (value != 0)
            ThrowSkiaUnsupported3D("SetReferenceStencil");
    }

    void SkiaRenderer::ClearColorAndDepth(float, float, float, float, float) { ThrowSkiaUnsupported3D("ClearColorAndDepth"); }
    void SkiaRenderer::ClearDepth(float) { ThrowSkiaUnsupported3D("ClearDepth"); }
    void SkiaRenderer::ClearStencil(int) { ThrowSkiaUnsupported3D("ClearStencil"); }
    void SkiaRenderer::ClearDepthAndStencil(float, int) { ThrowSkiaUnsupported3D("ClearDepthAndStencil"); }
    void SkiaRenderer::ClearColorAndStencil(float, float, float, float, int) { ThrowSkiaUnsupported3D("ClearColorAndStencil"); }
    void SkiaRenderer::ClearColorDepthAndStencil(float, float, float, float, float, int) { ThrowSkiaUnsupported3D("ClearColorDepthAndStencil"); }
    void SkiaRenderer::SetDepthTestEnabled(bool) { ThrowSkiaUnsupported3D("SetDepthTestEnabled"); }
    void SkiaRenderer::SetBlendEnabled(bool enabled)
    {
        AssertOwnership("SetBlendEnabled");
        if (enabled)
        {
            blendEnabled_ = true;
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
        sk_sp<SkBlender> replacementBlender;
        if (configuredColorWriteMask_ != 15)
        {
            const SkiaBlendMapping* opaque = FindSkiaBlendMapping(0, 0, 1, 1, 0, 0);
            if (!opaque)
                throw std::runtime_error("Skia internal Opaque blend mapping is unavailable.");
            replacementBlender = MakeMaskedBlender(*opaque, configuredColorWriteMask_);
        }
        blendEnabled_ = false;
        spriteBlendMode_ = SkBlendMode::kSrc;
        spriteCustomBlender_ = std::move(replacementBlender);
        spriteSourceAlphaConvention_ = SkiaSourceAlphaConvention::Premultiplied;
        TraceSkiaState("blend enabled=false mode=%d write-mask=%d",
                       static_cast<int>(spriteBlendMode_), configuredColorWriteMask_);
    }
    void SkiaRenderer::SetDepthWriteEnabled(bool) { ThrowSkiaUnsupported3D("SetDepthWriteEnabled"); }
    std::unique_ptr<IVertexBufferRenderer> SkiaRenderer::CreateVertexBuffer(int) { ThrowSkiaUnsupported3D("CreateVertexBuffer"); }
    std::unique_ptr<IIndexBufferRenderer> SkiaRenderer::CreateIndexBuffer16(int) { ThrowSkiaUnsupported3D("CreateIndexBuffer16"); }
    std::unique_ptr<IIndexBufferRenderer> SkiaRenderer::CreateIndexBuffer32(int) { ThrowSkiaUnsupported3D("CreateIndexBuffer32"); }
    std::unique_ptr<IOcclusionQueryRenderer> SkiaRenderer::CreateOcclusionQuery()
    {
        return std::make_unique<SkiaUnsupportedOcclusionQuery>();
    }
    void SkiaRenderer::DrawColoredPrimitives(const IVertexBufferRenderer&, const Matrix&, const Matrix&, const Matrix&, PrimitiveType, int) { ThrowSkiaUnsupported3D("DrawColoredPrimitives"); }
    void SkiaRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer&, const IIndexBufferRenderer&, const Matrix&, const Matrix&, const Matrix&, PrimitiveType, int) { ThrowSkiaUnsupported3D("DrawIndexedColoredPrimitives"); }
    void SkiaRenderer::DrawPrimitivesEx(const IVertexBufferRenderer&, const Matrix&, const Matrix&, const Matrix&, PrimitiveType, int, const GpuDrawParams&) { ThrowSkiaUnsupported3D("DrawPrimitivesEx"); }
    void SkiaRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer&, const IIndexBufferRenderer&, const Matrix&, const Matrix&, const Matrix&, PrimitiveType, int, const GpuDrawParams&) { ThrowSkiaUnsupported3D("DrawIndexedPrimitivesEx"); }
    void SkiaRenderer::DrawInstancedPrimitivesEx(const IVertexBufferRenderer&, const IIndexBufferRenderer&, const Matrix&, const Matrix&, const Matrix&, PrimitiveType, int, int, const GpuDrawParams&) { ThrowSkiaUnsupported3D("DrawInstancedPrimitivesEx"); }

    // --- Format boundaries (plan_runtimerenderer.md design decision 9) ---------------------
    //
    // These three tables used to live as #ifdef CNA_RENDERER_SKIA blocks inside Texture2D.cpp and
    // RenderTarget2D.cpp. They are reproduced here unchanged; the only difference is that the XNA
    // layer now asks the renderer instead of asking the preprocessor.

    RendererFormatVerdict SkiaRenderer::ClassifySurfaceFormatEXT(int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            case SurfaceFormat::Color:
            case SurfaceFormat::Bgr565:
            case SurfaceFormat::Bgra5551:
            case SurfaceFormat::Bgra4444:
            case SurfaceFormat::Rgba1010102:
            case SurfaceFormat::Rg32:
            case SurfaceFormat::Rgba64:
            case SurfaceFormat::Alpha8:
            case SurfaceFormat::ColorBgraEXT:
            case SurfaceFormat::ColorSrgbEXT:
            case SurfaceFormat::ByteEXT:
            case SurfaceFormat::UShortEXT:
            case SurfaceFormat::Single:
            case SurfaceFormat::Vector2:
            case SurfaceFormat::Vector4:
            case SurfaceFormat::HalfSingle:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::NormalizedByte4:
            case SurfaceFormat::HdrBlendable:
            case SurfaceFormat::Dxt1:
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
            case SurfaceFormat::Bc7EXT:
            case SurfaceFormat::Bc7SrgbEXT:
                return RendererFormatVerdict::Supported;
            default:
                return RendererFormatVerdict::Unsupported;
        }
    }

    RendererFormatVerdict SkiaRenderer::ClassifyColorTransferFormatEXT(int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        // Skia stores each promoted format in its own native layout, so a Color* transfer is only
        // meaningful for the three that are genuinely 32-bit RGBA-shaped. Every other promoted
        // format has a typed overload that reads its real bits instead.
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            case SurfaceFormat::Color:
            case SurfaceFormat::ColorBgraEXT:
            case SurfaceFormat::ColorSrgbEXT:
                return RendererFormatVerdict::Supported;
            default:
                return RendererFormatVerdict::Unsupported;
        }
    }

    RendererFormatVerdict SkiaRenderer::ClassifyRenderTargetFormatEXT(int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        // SKIA-142: the formats FNA itself reports renderable (see the surface-format matrix's
        // "FNA/Skia RT decision" column) that this raster renderer can genuinely render into.
        // Skia's raster SkSurface has no hardware format restriction, but promoting only these
        // keeps parity with real XNA/FNA renderability rather than "whatever Skia happens to
        // allow". Every other format (packed 16-bit colours, all compressed formats, SNORM,
        // Alpha8, ColorBgraEXT) is a real non-renderable format on actual XNA/FNA hardware and
        // stays refused regardless.
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            case SurfaceFormat::Color:
            case SurfaceFormat::Rgba1010102:
            case SurfaceFormat::Rg32:
            case SurfaceFormat::Rgba64:
            case SurfaceFormat::Single:
            case SurfaceFormat::Vector2:
            case SurfaceFormat::Vector4:
            case SurfaceFormat::HalfSingle:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
            case SurfaceFormat::ColorSrgbEXT:
            case SurfaceFormat::ByteEXT:
            case SurfaceFormat::UShortEXT:
                return RendererFormatVerdict::Supported;
            default:
                return RendererFormatVerdict::Unsupported;
        }
    }

    bool SkiaRenderer::IsCompressedTransferFormatEXT(int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        // SKIA-140/141: these transfer raw compressed blocks through the same CNAEXT byte-array
        // overloads ByteEXT uses, matching how real XNA/FNA upload compressed content.
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            case SurfaceFormat::Dxt1:
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
            case SurfaceFormat::Bc7EXT:
            case SurfaceFormat::Bc7SrgbEXT:
                return true;
            default:
                return false;
        }
    }

} // namespace CNA::Internal::Renderers::Skia

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_SKIA
    // plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace Skia { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> Skia::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<Skia::SkiaRenderer>(args.window, args.virtualWidth, args.virtualHeight,
                                                            args.presentationMode, args.swapInterval,
                                                            args.deviceEventCallback);
    }
#endif
} // namespace CNA::Internal::Renderers
