#include "CNA/Internal/Renderers/Skia/SkiaRenderer.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/PlatformException.hpp"
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

        [[nodiscard]] CNA::Platform::PresentScaleMode ToPresentScaleMode(CnaPresentationMode mode)
        {
            switch (mode)
            {
                case CnaPresentationMode::Letterbox:
                    return CNA::Platform::PresentScaleMode::Letterbox;
                case CnaPresentationMode::Overscan:
                    return CNA::Platform::PresentScaleMode::Overscan;
                case CnaPresentationMode::Stretch:
                    return CNA::Platform::PresentScaleMode::Stretch;
                case CnaPresentationMode::NativeBackBuffer:
                    return CNA::Platform::PresentScaleMode::Native;
                case CnaPresentationMode::FixedHeightDynamicWidth:
                    return CNA::Platform::PresentScaleMode::Letterbox;
            }
            return CNA::Platform::PresentScaleMode::Letterbox;
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

    SkiaRenderer::SkiaRenderer(const GraphicsRendererCreateArgs& args,
                               const SkiaInitializationFailurePointEXT failurePoint)
        : surfaceInfo_(args.surface)
        , presenter_(args.surfacePresenter)
        , deviceEventCallback_(args.deviceEventCallback)
        , presentationMode_(args.presentationMode)
        , preferredVirtualWidth_(args.virtualWidth)
        , preferredVirtualHeight_(args.virtualHeight)
    {
        if (surfaceInfo_.windowId == 0)
            throw std::runtime_error("SkiaRenderer initialized without a platform window.");
        if (presenter_ == nullptr)
        {
            throw CNA::Platform::PlatformNotSupportedException(
                CNA::Platform::PlatformCapability::SurfacePresentation, "SKIA");
        }
        if (!(surfaceInfo_.displayScale > 0.0f)) surfaceInfo_.displayScale = 1.0f;

        bool registered = false;
        try
        {
            const auto failAt = [failurePoint](SkiaInitializationFailurePointEXT point,
                                                const char* stage)
            {
                if (failurePoint == point)
                    throw std::runtime_error(std::string("Skia injected initialization failure after ") + stage);
            };

            failAt(SkiaInitializationFailurePointEXT::AfterRenderer, "renderer creation");

            SetSwapInterval(args.swapInterval);
            RecreateBackbuffer(args.virtualWidth, args.virtualHeight);
            failAt(SkiaInitializationFailurePointEXT::AfterBackbuffer, "backbuffer creation");

            IGraphicsRenderer::RegisterForWindow(surfaceInfo_.windowId, this);
            registered = true;
            failAt(SkiaInitializationFailurePointEXT::AfterRegistration, "renderer registration");
            std::cout << kSkiaStartupDiagnostic << std::endl;
        }
        catch (...)
        {
            // A throwing constructor never reaches ~SkiaRenderer(). Unwind every acquired
            // Registry state is the only owned external resource; the presenter remains owned by
            // GraphicsDevice and can host an immediately succeeding renderer.
            if (registered)
                IGraphicsRenderer::UnregisterForWindow(surfaceInfo_.windowId);
            throw;
        }
    }

    SkiaRenderer::~SkiaRenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(surfaceInfo_.windowId);
    }

    void SkiaRenderer::AssertOwnership(const char* operation) const
    {
        ownership_->AssertOwnerThread(operation);
        if (surfaceInfo_.windowId == 0 || presenter_ == nullptr)
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
        presenter_->GetTargetSize(width, height);
    }

    SkiaRenderer::PresentationViewport SkiaRenderer::ComputePresentationViewport() const
    {
        int outputWidth = 0;
        int outputHeight = 0;
        GetPresentationOutputSize(outputWidth, outputHeight);
        PresentationViewport viewport;
        if (outputWidth <= 0 || outputHeight <= 0 || LogicalWidth() <= 0 || LogicalHeight() <= 0)
            return viewport;

        if (presentationMode_ == CnaPresentationMode::NativeBackBuffer)
        {
            viewport.width = static_cast<float>(LogicalWidth());
            viewport.height = static_cast<float>(LogicalHeight());
            return viewport;
        }
        if (presentationMode_ == CnaPresentationMode::Stretch)
        {
            viewport.width = static_cast<float>(outputWidth);
            viewport.height = static_cast<float>(outputHeight);
            return viewport;
        }

        const float scaleX = static_cast<float>(outputWidth) / LogicalWidth();
        const float scaleY = static_cast<float>(outputHeight) / LogicalHeight();
        const float scale = presentationMode_ == CnaPresentationMode::Overscan
            ? std::max(scaleX, scaleY) : std::min(scaleX, scaleY);
        viewport.width = LogicalWidth() * scale;
        viewport.height = LogicalHeight() * scale;
        viewport.x = (static_cast<float>(outputWidth) - viewport.width) * 0.5f;
        viewport.y = (static_cast<float>(outputHeight) - viewport.height) * 0.5f;
        return viewport;
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

    void SkiaRenderer::RecreatePresentationRenderer()
    {
        // CPU raster resources survive a presentation reset. The platform owns the presenter, so
        // recovery re-applies its state without replacing or invalidating the live raster surface.
        SetSwapInterval(swapInterval_);
        ApplyLogicalPresentation();
    }

    void SkiaRenderer::ApplyLogicalPresentation()
    {
        presenter_->SetScaleMode(
            ToPresentScaleMode(presentationMode_), CNA::Platform::PresentFilter::Linear);
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
        presenter_->Present(CNA::Platform::SurfaceFrame{
            pixels.data(), LogicalWidth(), LogicalHeight(), LogicalWidth() * 4});

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

    void SkiaRenderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        AssertOwnership("OnSurfaceChanged");
        if (surface.windowId != surfaceInfo_.windowId)
            throw CNA::Platform::PlatformException("SkiaRenderer::OnSurfaceChanged",
                                                   "stable window id changed");
        surfaceInfo_ = surface;
        if (!(surfaceInfo_.displayScale > 0.0f)) surfaceInfo_.displayScale = 1.0f;
        RefreshDynamicBackbufferIfNeeded();
    }

    void SkiaRenderer::SetVirtualResolution(int width, int height)
    {
        AssertOwnership("SetVirtualResolution");
        preferredVirtualWidth_ = width;
        preferredVirtualHeight_ = height;

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
        const bool enabled = interval != 0;
        (void)presenter_->SetVSync(enabled);
        swapInterval_ = enabled ? 1 : 0;
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
        // is loss of the platform presenter: reapply its state while leaving
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
        const PresentationViewport viewport = ComputePresentationViewport();
        if (viewport.width <= 0.0f || viewport.height <= 0.0f)
            return false;
        const float physicalX = windowX * surfaceInfo_.displayScale;
        const float physicalY = windowY * surfaceInfo_.displayScale;
        if (physicalX < viewport.x || physicalX > viewport.x + viewport.width
            || physicalY < viewport.y || physicalY > viewport.y + viewport.height)
            return false;
        logX = (physicalX - viewport.x) * LogicalWidth() / viewport.width;
        logY = (physicalY - viewport.y) * LogicalHeight() / viewport.height;
        return true;
    }

    bool SkiaRenderer::TransformLogicalToWindow(float logX, float logY,
                                                        float& windowX, float& windowY) const
    {
        AssertOwnership("TransformLogicalToWindow");
        const PresentationViewport viewport = ComputePresentationViewport();
        if (viewport.width <= 0.0f || viewport.height <= 0.0f
            || LogicalWidth() <= 0 || LogicalHeight() <= 0)
            return false;
        const float physicalX = viewport.x + logX * viewport.width / LogicalWidth();
        const float physicalY = viewport.y + logY * viewport.height / LogicalHeight();
        windowX = physicalX / surfaceInfo_.displayScale;
        windowY = physicalY / surfaceInfo_.displayScale;
        return true;
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
} // namespace CNA::Internal::Renderers::Skia

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_SKIA
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<Skia::SkiaRenderer>(args);
    }
#endif
} // namespace CNA::Internal::Renderers
