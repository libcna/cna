#include "CNA/Internal/Backends/Skia/SkiaGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaBlendMapping.hpp"
#include "CNA/Internal/Backends/Skia/SkiaEffectBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaGeneratedBlender.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRenderTargetBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRenderTargetCubeBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSpriteBatchBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaStateTrace.hpp"
#include "CNA/Internal/Backends/Skia/SkiaTextureBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaTextureStorageBackends.hpp"
#include "CNA/Internal/Backends/Skia/SkiaUnsupported3D.hpp"
#include "System/NotSupportedException.hpp"

#include "include/core/SkData.h"
#include "include/effects/SkRuntimeEffect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Backends::Skia
{
    namespace
    {
        class SkiaUnsupportedOcclusionQuery final : public IOcclusionQueryBackend
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

    SkiaGraphicsBackend::SkiaGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                                             CnaPresentationMode presentationMode, int swapInterval,
                                             std::function<void(BackendDeviceEvent)> deviceEventCallback,
                                             SkiaInitializationFailurePointEXT failurePoint)
        : window_(window)
        , deviceEventCallback_(std::move(deviceEventCallback))
        , presentationMode_(presentationMode)
        , preferredVirtualWidth_(virtualWidth)
        , preferredVirtualHeight_(virtualHeight)
    {
        if (!window_)
            throw std::runtime_error("SkiaGraphicsBackend initialized with null window.");

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

            IGraphicsBackend::RegisterForWindow(window_, this);
            registered = true;
            failAt(SkiaInitializationFailurePointEXT::AfterRegistration, "backend registration");
            std::cout << kSkiaStartupDiagnostic << std::endl;
        }
        catch (...)
        {
            // A throwing constructor never reaches ~SkiaGraphicsBackend(). Unwind every acquired
            // SDL/registry resource here so the caller-owned window can host a succeeding backend.
            if (registered)
                IGraphicsBackend::UnregisterForWindow(window_);
            DestroyPresentationTexture();
            if (renderer_)
            {
                SDL_DestroyRenderer(renderer_);
                renderer_ = nullptr;
            }
            throw;
        }
    }

    SkiaGraphicsBackend::~SkiaGraphicsBackend()
    {
        IGraphicsBackend::UnregisterForWindow(window_);
        DestroyPresentationTexture();
        if (renderer_) SDL_DestroyRenderer(renderer_);
    }

    void SkiaGraphicsBackend::AssertOwnership(const char* operation) const
    {
        ownership_->AssertOwnerThread(operation);
        if (!window_ || !renderer_ || SDL_GetRenderer(window_) != renderer_)
        {
            throw std::runtime_error(
                std::string("Skia presenter ownership violation during ") + operation + ".");
        }
    }

    SkiaSurface& SkiaGraphicsBackend::ActiveSurface()
    {
        AssertOwnership("active-surface access");
        targetBinding_->AssertConsistent(&surface_);
        SkiaSurface* activeSurface = targetBinding_->ActiveSurface();
        SkiaRasterTarget* activeTarget = targetBinding_->ActiveTarget();
        if (activeTarget && activeSurface != activeTarget->BoundSurfaceEXT())
            throw std::runtime_error("Skia active target does not own the selected raster surface.");
        return *activeSurface;
    }

    const SkiaSurface& SkiaGraphicsBackend::ActiveSurface() const
    {
        AssertOwnership("active-surface access");
        targetBinding_->AssertConsistent(&surface_);
        SkiaSurface* activeSurface = targetBinding_->ActiveSurface();
        const SkiaRasterTarget* activeTarget = targetBinding_->ActiveTarget();
        if (activeTarget && activeSurface != activeTarget->BoundSurfaceEXT())
            throw std::runtime_error("Skia active target does not own the selected raster surface.");
        return *activeSurface;
    }

    void SkiaGraphicsBackend::GetPresentationOutputSize(int& width, int& height) const
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

    void SkiaGraphicsBackend::RecreateBackbuffer(int requestedWidth, int requestedHeight)
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

    void SkiaGraphicsBackend::RefreshDynamicBackbufferIfNeeded()
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

    void SkiaGraphicsBackend::RecreatePresentationTexture()
    {
        presentTexture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_STREAMING, LogicalWidth(), LogicalHeight());
        if (!presentTexture_)
            throw std::runtime_error(std::string("Skia SDL_CreateTexture failed: ") + SDL_GetError());
    }

    void SkiaGraphicsBackend::DestroyPresentationTexture() noexcept
    {
        if (!presentTexture_)
            return;
        SDL_DestroyTexture(presentTexture_);
        presentTexture_ = nullptr;
    }

    void SkiaGraphicsBackend::RecreatePresentationRenderer()
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
        AssertOwnership("Clear");
        RefreshDynamicBackbufferIfNeeded();
        if (SkiaRasterTarget* target = targetBinding_->ActiveTarget())
            target->BeforeWriteEXT();
        ActiveSurface().Clear(r, g, b, a);
    }

    void SkiaGraphicsBackend::Present()
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

    void SkiaGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        AssertOwnership("GetViewportSize");
        RefreshDynamicBackbufferIfNeeded();
        width = ActiveSurface().Width();
        height = ActiveSurface().Height();
    }

    void SkiaGraphicsBackend::SetVirtualResolution(int width, int height)
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

    void SkiaGraphicsBackend::SetPresentationMode(int mode)
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

    void SkiaGraphicsBackend::SetSwapInterval(int interval)
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

    void SkiaGraphicsBackend::DebugSetPresentationOutputSizeEXT(int width, int height)
    {
        AssertOwnership("DebugSetPresentationOutputSizeEXT");
        if (width < 0 || height < 0)
            throw std::out_of_range("Skia debug presentation output size must not be negative.");
        debugOutputSizeOverride_ = true;
        debugOutputWidth_ = width;
        debugOutputHeight_ = height;
    }

    void SkiaGraphicsBackend::DebugClearPresentationOutputSizeEXT()
    {
        AssertOwnership("DebugClearPresentationOutputSizeEXT");
        debugOutputSizeOverride_ = false;
        debugOutputWidth_ = 0;
        debugOutputHeight_ = 0;
    }

    void SkiaGraphicsBackend::DebugSimulateContextLoss()
    {
        AssertOwnership("DebugSimulateContextLoss");
        // A CPU-raster Skia surface cannot incur a real GPU context loss. The useful equivalent
        // is loss of the SDL presenter: reconstruct its renderer/streaming texture while leaving
        // CPU-owned Texture2D and RenderTarget2D data live. Report only a reset pair, rather than
        // fabricating DeviceLost for a device whose raster resources never became unavailable.
        if (deviceEventCallback_)
            deviceEventCallback_(BackendDeviceEvent::Resetting);
        RecreatePresentationRenderer();
        if (deviceEventCallback_)
            deviceEventCallback_(BackendDeviceEvent::Reset);
    }

    void SkiaGraphicsBackend::DebugRestoreContext()
    {
        AssertOwnership("DebugRestoreContext");
        // Desktop recovery is immediate, exactly like EasyGL's existing debug restore path.
        DebugSimulateContextLoss();
    }

    bool SkiaGraphicsBackend::TransformWindowToLogical(float windowX, float windowY,
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

    bool SkiaGraphicsBackend::TransformLogicalToWindow(float logX, float logY,
                                                        float& windowX, float& windowY) const
    {
        AssertOwnership("TransformLogicalToWindow");
        return renderer_ && SDL_RenderCoordinatesToWindow(renderer_, logX, logY, &windowX, &windowY);
    }

    std::unique_ptr<ITextureBackend> SkiaGraphicsBackend::CreateTexture(const ImageData& data)
    {
        AssertOwnership("CreateTexture");
        return std::make_unique<SkiaTextureBackend>(data, resourceCounters_);
    }

    std::unique_ptr<ITextureCubeBackend> SkiaGraphicsBackend::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        AssertOwnership("CreateTextureCube");
        if (surfaceFormat != 0)
            throw System::NotSupportedException("Skia CPU TextureCube storage supports only RGBA8 Color.");
        return std::make_unique<SkiaTextureCubeBackend>(size, mipMap, resourceCounters_);
    }

    std::unique_ptr<ITexture3DBackend> SkiaGraphicsBackend::CreateTexture3D(
        int width, int height, int depth, bool mipMap, int surfaceFormat)
    {
        AssertOwnership("CreateTexture3D");
        if (surfaceFormat != 0)
            throw System::NotSupportedException("Skia CPU Texture3D storage supports only RGBA8 Color.");
        return std::make_unique<SkiaTexture3DBackend>(
            width, height, depth, mipMap, resourceCounters_);
    }

    std::unique_ptr<ISpriteBatchBackend> SkiaGraphicsBackend::CreateSpriteBatch()
    {
        AssertOwnership("CreateSpriteBatch");
        return std::make_unique<SkiaSpriteBatchBackend>(targetBinding_->ActiveSurfaceRef(), spriteBlendMode_,
                                                         spriteCustomBlender_, spriteSourceAlphaConvention_,
                                                         rasterState_, targetBinding_);
    }

    std::unique_ptr<IEffectBackend> SkiaGraphicsBackend::CreateEffectBackend(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        AssertOwnership("CreateEffectBackend");
        // Existing ShaderEffect payloads are untagged GLSL/HLSL or even binary SPIR-V. Preserve
        // the historical null result for those rather than asking SkSL to guess their language.
        if (vertSrc != kSkiaSkslSpriteEffectMarkerEXT)
            return nullptr;

        auto backend = std::make_unique<SkiaEffectBackend>();
        (void)backend->CompileProgram(vertSrc, fragSrc);
        return backend;
    }

    std::unique_ptr<IRenderTargetBackend> SkiaGraphicsBackend::CreateRenderTarget2D(
        int width, int height, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        return CreateRenderTarget2DEXT(
            width, height, depthFormat, preserveContents, mipMap, multiSampleCount,
            static_cast<int>(Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color));
    }

    std::unique_ptr<IRenderTargetBackend> SkiaGraphicsBackend::CreateRenderTarget2DEXT(
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
        return std::make_unique<SkiaRenderTargetBackend>(
            width, height, preserveContents, targetBinding_, resourceCounters_, mipMap,
            static_cast<Microsoft::Xna::Framework::Graphics::SurfaceFormat>(surfaceFormat));
    }

    std::unique_ptr<IRenderTargetCubeBackend> SkiaGraphicsBackend::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        AssertOwnership("CreateRenderTargetCube");
        (void)depthFormat;
        (void)multiSampleCount; // Raster applies zero samples and reports that exact clamp.
        return std::make_unique<SkiaRenderTargetCubeBackend>(
            size, preserveContents, mipMap, targetBinding_, resourceCounters_);
    }

    void SkiaGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* renderTarget)
    {
        AssertOwnership("SetRenderTarget2D");

        // Validate the requested destination before resolving the current one. A failed foreign
        // or cross-device bind is not a pass boundary and must leave both the active selection and
        // its dirty mip chain byte-for-byte unchanged.
        SkiaRenderTargetBackend* skiaTarget = nullptr;
        if (renderTarget)
        {
            skiaTarget = dynamic_cast<SkiaRenderTargetBackend*>(renderTarget);
            if (!skiaTarget || !skiaTarget->BelongsToBindingEXT(targetBinding_))
            {
                throw std::runtime_error(
                    "Skia cannot bind a render target created by a different graphics backend.");
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

    void SkiaGraphicsBackend::SetRenderTargetCubeFace(
        IRenderTargetCubeBackend* renderTarget, int face)
    {
        AssertOwnership("SetRenderTargetCubeFace");
        if (!renderTarget)
        {
            SetRenderTarget2D(nullptr);
            return;
        }
        auto* skiaTarget = dynamic_cast<SkiaRenderTargetCubeBackend*>(renderTarget);
        if (!skiaTarget)
            throw std::runtime_error("Skia cannot bind a cube target created by a different backend.");
        skiaTarget->BindAsRenderTargetFace(face);
        TraceSkiaState("surface=render-target-cube face=%d id=%llu size=%dx%d",
                       face,
                       static_cast<unsigned long long>(ActiveSurface().Identity()),
                       ActiveSurface().Width(), ActiveSurface().Height());
    }

    void SkiaGraphicsBackend::ReadBackbuffer(int x, int y, int width, int height, std::uint8_t* pixels)
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

    void SkiaGraphicsBackend::SetRenderTargets(
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
                "Skia raster backend cannot emulate multiple render targets: SkCanvas has one "
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

    void SkiaGraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
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
            throw std::runtime_error("Skia raster backend received an invalid ColorWriteChannels mask.");
        for (int target = 1; target < 4; ++target)
        {
            if (writeState.colorWriteChannels[target] != kColorWriteAll)
            {
                throw std::runtime_error(
                    "Skia raster backend has one render target and does not implement ColorWriteChannels1-3.");
            }
        }
        if (writeState.multiSampleMask != ~0u)
            throw std::runtime_error("Skia raster backend does not implement non-default MultiSampleMask values.");

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

    void SkiaGraphicsBackend::SetBlendFactor(float r, float g, float b, float a)
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

    void SkiaGraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
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
        AssertOwnership("SetScissorRect");
        rasterState_.scissorX = x;
        rasterState_.scissorY = y;
        rasterState_.scissorWidth = width;
        rasterState_.scissorHeight = height;
        TraceSkiaState("scissor x=%d y=%d width=%d height=%d", x, y, width, height);
    }

    void SkiaGraphicsBackend::SetViewport(int x, int y, int width, int height,
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

    bool SkiaGraphicsBackend::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        AssertOwnership("SupportsCapability");
        return capability == CNA::GraphicsCapability::Texture3D;
    }

    void SkiaGraphicsBackend::Ensure3DSupported(const char* operation) const
    {
        AssertOwnership(operation);
        ThrowSkiaUnsupported3D(operation);
    }

    void SkiaGraphicsBackend::ApplyDepthStencilState(
        bool depthEnable, bool depthWriteEnable, int,
        bool stencilEnable, int, int, int, int, int, int, int,
        bool, int, int, int, int)
    {
        // DepthStencilState.None is part of the normal SpriteBatch 2D contract. It describes the
        // absence of a depth/stencil operation, so accepting it does not claim an attachment.
        if (depthEnable || depthWriteEnable || stencilEnable)
            ThrowSkiaUnsupported3D("ApplyDepthStencilState");
    }

    void SkiaGraphicsBackend::SetReferenceStencil(int value)
    {
        // Zero accompanies the accepted disabled state. A nonzero reference would only have
        // meaning in the unsupported stencil pipeline.
        if (value != 0)
            ThrowSkiaUnsupported3D("SetReferenceStencil");
    }

    void SkiaGraphicsBackend::ClearColorAndDepth(float, float, float, float, float) { ThrowSkiaUnsupported3D("ClearColorAndDepth"); }
    void SkiaGraphicsBackend::ClearDepth(float) { ThrowSkiaUnsupported3D("ClearDepth"); }
    void SkiaGraphicsBackend::ClearStencil(int) { ThrowSkiaUnsupported3D("ClearStencil"); }
    void SkiaGraphicsBackend::ClearDepthAndStencil(float, int) { ThrowSkiaUnsupported3D("ClearDepthAndStencil"); }
    void SkiaGraphicsBackend::ClearColorAndStencil(float, float, float, float, int) { ThrowSkiaUnsupported3D("ClearColorAndStencil"); }
    void SkiaGraphicsBackend::ClearColorDepthAndStencil(float, float, float, float, float, int) { ThrowSkiaUnsupported3D("ClearColorDepthAndStencil"); }
    void SkiaGraphicsBackend::SetDepthTestEnabled(bool) { ThrowSkiaUnsupported3D("SetDepthTestEnabled"); }
    void SkiaGraphicsBackend::SetBlendEnabled(bool enabled)
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
    void SkiaGraphicsBackend::SetDepthWriteEnabled(bool) { ThrowSkiaUnsupported3D("SetDepthWriteEnabled"); }
    std::unique_ptr<IVertexBufferBackend> SkiaGraphicsBackend::CreateVertexBuffer(int) { ThrowSkiaUnsupported3D("CreateVertexBuffer"); }
    std::unique_ptr<IIndexBufferBackend> SkiaGraphicsBackend::CreateIndexBuffer16(int) { ThrowSkiaUnsupported3D("CreateIndexBuffer16"); }
    std::unique_ptr<IIndexBufferBackend> SkiaGraphicsBackend::CreateIndexBuffer32(int) { ThrowSkiaUnsupported3D("CreateIndexBuffer32"); }
    std::unique_ptr<IOcclusionQueryBackend> SkiaGraphicsBackend::CreateOcclusionQuery()
    {
        return std::make_unique<SkiaUnsupportedOcclusionQuery>();
    }
    void SkiaGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&, const Matrix&, const Matrix&, const Matrix&, PrimitiveType, int) { ThrowSkiaUnsupported3D("DrawColoredPrimitives"); }
    void SkiaGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&, const IIndexBufferBackend&, const Matrix&, const Matrix&, const Matrix&, PrimitiveType, int) { ThrowSkiaUnsupported3D("DrawIndexedColoredPrimitives"); }
    void SkiaGraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend&, const Matrix&, const Matrix&, const Matrix&, PrimitiveType, int, const GpuDrawParams&) { ThrowSkiaUnsupported3D("DrawPrimitivesEx"); }
    void SkiaGraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend&, const IIndexBufferBackend&, const Matrix&, const Matrix&, const Matrix&, PrimitiveType, int, const GpuDrawParams&) { ThrowSkiaUnsupported3D("DrawIndexedPrimitivesEx"); }
    void SkiaGraphicsBackend::DrawInstancedPrimitivesEx(const IVertexBufferBackend&, const IIndexBufferBackend&, const Matrix&, const Matrix&, const Matrix&, PrimitiveType, int, int, const GpuDrawParams&) { ThrowSkiaUnsupported3D("DrawInstancedPrimitivesEx"); }
} // namespace CNA::Internal::Backends::Skia

namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_SKIA
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Skia::SkiaGraphicsBackend>(args.window, args.virtualWidth, args.virtualHeight,
                                                            args.presentationMode, args.swapInterval,
                                                            args.deviceEventCallback);
    }
#endif
} // namespace CNA::Internal::Backends
