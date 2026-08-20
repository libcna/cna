#include "CNA/Internal/Renderers/Gdi/GdiRenderer.hpp"
#include "CNA/Internal/Renderers/Gdi/GdiConfiguration.hpp"
#include "CNA/Internal/Renderers/Gdi/GdiPresentation.hpp"
#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorMatrixEffect.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace CNA::Internal::Renderers::Gdi
{
    /**
     * Private composition adapter around the reusable CPU rasterizer.
     *
     * No pointer or reference to this complete Software renderer escapes GDI. The public renderer
     * derives directly from IGraphicsRenderer and forwards only its reviewed 2D operations, so a
     * future Software 3D virtual cannot silently appear in GDI's public vtable. This small adapter
     * exposes only the backbuffer needed for presentation and the raster-bounds callback needed
     * for dirty tracking.
     */
    class GdiSoftware2DCore final : public Software::SoftwareRenderer
    {
    public:
        using RasterBoundsCallback = std::function<void(int, int, int, int)>;
        using SoftwareRenderer::SoftwareRenderer;

        void SetRasterBoundsCallback(RasterBoundsCallback callback)
        {
            rasterBoundsCallback_ = std::move(callback);
        }

        [[nodiscard]] Software::SoftwareFramebuffer& Backbuffer()
        {
            return BackbufferFramebuffer();
        }

        [[nodiscard]] const Software::SoftwareFramebuffer& Backbuffer() const
        {
            return BackbufferFramebuffer();
        }

    protected:
        void OnSpriteRasterBounds(int minX, int minY, int maxX, int maxY) override
        {
            if (rasterBoundsCallback_)
                rasterBoundsCallback_(minX, minY, maxX, maxY);
        }

    private:
        RasterBoundsCallback rasterBoundsCallback_;
    };

    namespace
    {
        [[nodiscard]] CnaPresentationMode ValidatePresentationMode(int mode)
        {
            if (mode < static_cast<int>(CnaPresentationMode::Letterbox) ||
                mode > static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth))
            {
                throw System::ArgumentOutOfRangeException(
                    "mode", std::to_string(mode),
                    "GDI presentation mode must be an ordinal from 0 through 4.");
            }
            return static_cast<CnaPresentationMode>(mode);
        }

        [[nodiscard]] int ValidateGdiFramebufferDimension(const char* parameterName, int value)
        {
            if (value <= 0 || value > Software::SoftwareFramebufferMaxDimension)
            {
                throw System::ArgumentOutOfRangeException(
                    parameterName, std::to_string(value),
                    "GDI framebuffer dimensions must be positive and no greater than " +
                        std::to_string(Software::SoftwareFramebufferMaxDimension) + ".");
            }
            return value;
        }

        [[nodiscard]] std::string FormatWin32Error(DWORD error)
        {
            std::string message = "Win32 error " + std::to_string(error);
            if (error == ERROR_SUCCESS)
                return message + " (no extended error was reported)";

            char* buffer = nullptr;
            const DWORD length = FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, error, 0, reinterpret_cast<char*>(&buffer), 0, nullptr);
            if (length != 0 && buffer != nullptr)
            {
                std::string detail(buffer, length);
                LocalFree(buffer);
                while (!detail.empty() &&
                       (detail.back() == '\r' || detail.back() == '\n' || detail.back() == ' '))
                {
                    detail.pop_back();
                }
                if (!detail.empty())
                    message += ": " + detail;
            }
            return message;
        }

        [[noreturn]] void ThrowWin32Failure(const char* operation, DWORD error = GetLastError())
        {
            throw std::runtime_error(std::string("GDI ") + operation + " failed: " +
                                     FormatWin32Error(error));
        }

        /// GDI-077: result of the explicit, checked ReleaseDC attempt below.
        struct WindowDeviceContextReleaseResult
        {
            bool success = true;
            std::uint32_t win32Error = 0;
        };

        /// One GetDC/ReleaseDC pair on every return and exception path.
        class WindowDeviceContext final
        {
        public:
            explicit WindowDeviceContext(HWND window) : window_(window)
            {
                SetLastError(ERROR_SUCCESS);
                context_ = GetDC(window_);
                if (context_ == nullptr)
                    ThrowWin32Failure("GetDC");
            }

            ~WindowDeviceContext()
            {
                // GDI-077: a fallback only. The checked presentation transaction always calls
                // Release() explicitly before this destructor runs; this path exists solely so an
                // exception thrown earlier in Present() still releases the DC. It must never throw.
                if (context_ != nullptr)
                {
                    SetLastError(ERROR_SUCCESS);
                    (void)ReleaseDC(window_, context_);
                    context_ = nullptr;
                }
            }

            WindowDeviceContext(const WindowDeviceContext&) = delete;
            WindowDeviceContext& operator=(const WindowDeviceContext&) = delete;

            [[nodiscard]] HDC Get() const { return context_; }

            /// Explicit, checked release. Exactly one real (or, under test injection, simulated)
            /// ReleaseDC attempt is made per instance: after this returns, the destructor is a
            /// no-op, so a failed release can never be retried or double-released.
            [[nodiscard]] WindowDeviceContextReleaseResult Release(bool forceFailure = false)
            {
                if (context_ == nullptr)
                    return {true, 0};
                const HDC context = context_;
                context_ = nullptr;
                if (forceFailure)
                    return {false, static_cast<std::uint32_t>(ERROR_INVALID_HANDLE)};
                SetLastError(ERROR_SUCCESS);
                if (ReleaseDC(window_, context) == 0)
                    return {false, static_cast<std::uint32_t>(GetLastError())};
                return {true, 0};
            }

        private:
            HWND window_ = nullptr;
            HDC context_ = nullptr;
        };

        /// Selects only the final GDI blit's resampling mode. Texture sampling remains entirely
        /// in the shared CPU SpriteBatch rasterizer, so this must never change SamplerState
        /// semantics. COLORONCOLOR is deliberately the default because it preserves crisp
        /// pixel-art edges. HALFTONE is opt-in for applications that prefer a smoother window
        /// resize: `CNA_GDI_PRESENT_FILTER=halftone`.
        [[nodiscard]] int GetPresentationStretchMode(GdiPresentationFilter filter)
        {
            return filter == GdiPresentationFilter::Halftone ? HALFTONE : COLORONCOLOR;
        }

        /// DwmFlush is a compositor pacing hint, not a swap interval. Keep it strictly opt-in:
        /// it can block until DWM accepts the next composition batch, which is useful for modest
        /// compatibility applications but is an unwanted latency policy by default. Loading it at
        /// runtime preserves the GDI renderer's ability to run on systems where DWM is absent or
        /// disabled, with an intentionally silent non-blocking fallback.
        void ApplyOptionalDwmPacing(bool requested)
        {
            if (!requested)
                return;

            using DwmFlushFunction = HRESULT(WINAPI*)();
            static DwmFlushFunction dwmFlush = []() -> DwmFlushFunction
            {
                HMODULE dwmApi = LoadLibraryW(L"dwmapi.dll");
                if (dwmApi == nullptr)
                    return nullptr;
                return reinterpret_cast<DwmFlushFunction>(GetProcAddress(dwmApi, "DwmFlush"));
            }();

            if (dwmFlush != nullptr)
                (void)dwmFlush(); // A disabled compositor/failure is an intentional safe fallback.
        }

        [[noreturn]] void ThrowUnsupportedFeature(const char* operation)
        {
            throw System::NotSupportedException(
                std::string("GDI (Win32 2D) does not support ") + operation + ".");
        }

        /// Delegates the shared CPU rasterizer SpriteBatch implementation while accepting exactly
        /// GDI-022's fixed CPU ColorMatrixEffect. Every other custom Effect is rejected, rather
        /// than accepted and ignored as if GDI had a programmable shader path.
        class GdiSpriteBatchRenderer final : public ISpriteBatchRenderer
        {
        public:
            explicit GdiSpriteBatchRenderer(std::unique_ptr<ISpriteBatchRenderer> inner,
                                           std::function<void()> synchronizeBackbuffer)
                : inner_(std::move(inner))
                , synchronizeBackbuffer_(std::move(synchronizeBackbuffer))
            {
                if (!inner_)
                    throw std::runtime_error("GDI failed to create its CPU SpriteBatch renderer.");
            }

            void Begin() override
            {
                synchronizeBackbuffer_();
                inner_->Begin();
            }
            void End() override { inner_->End(); }
            void SetTransformMatrix(const Matrix& matrix) override
            {
                inner_->SetTransformMatrix(matrix);
            }
            void SetCustomEffect(Effect* effect) override
            {
                if (effect != nullptr &&
                    dynamic_cast<Microsoft::Xna::Framework::Graphics::ColorMatrixEffect*>(effect) == nullptr)
                    throw std::runtime_error(
                        "GDI (Win32 2D) supports only the fixed ColorMatrixEffect for SpriteBatch; "
                        "custom shader effects are unsupported.");
                inner_->SetCustomEffect(effect);
            }
            void SetSamplerFilter(int textureFilter) override { inner_->SetSamplerFilter(textureFilter); }
            void SetSamplerAddressMode(int addressU, int addressV) override
            {
                inner_->SetSamplerAddressMode(addressU, addressV);
            }
            void Draw(const ITextureRenderer& texture, float x, float y) override
            {
                synchronizeBackbuffer_();
                inner_->Draw(texture, x, y);
            }
            void Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                      const Rectangle& sourceRectangle, const Color& color) override
            {
                synchronizeBackbuffer_();
                inner_->Draw(texture, destinationRectangle, sourceRectangle, color);
            }
            void Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                      const Rectangle& sourceRectangle, const Color& color, float rotation,
                      const Vector2& origin, SpriteEffects effects, float layerDepth) override
            {
                synchronizeBackbuffer_();
                inner_->Draw(texture, destinationRectangle, sourceRectangle, color, rotation,
                             origin, effects, layerDepth);
            }

        private:
            std::unique_ptr<ISpriteBatchRenderer> inner_;
            std::function<void()> synchronizeBackbuffer_;
        };
    } // namespace

    GdiRenderer::GdiRenderer(const GraphicsRendererCreateArgs& args)
        : GdiRenderer(args, CaptureGdiConfigurationFromEnvironment())
    {
    }

    GdiRenderer::GdiRenderer(const GraphicsRendererCreateArgs& args,
                             GdiConfiguration configuration)
        : software2D_(std::make_unique<GdiSoftware2DCore>(
              ValidateGdiFramebufferDimension("virtualWidth", args.virtualWidth),
              ValidateGdiFramebufferDimension("virtualHeight", args.virtualHeight), false, true))
        , surface_(args.surface, "GdiRenderer")
        , requestedVirtualWidth_(args.virtualWidth)
        , requestedVirtualHeight_(args.virtualHeight)
        , presentationMode_(ValidatePresentationMode(static_cast<int>(args.presentationMode)))
        , configuration_(configuration)
    {
        software2D_->SetRasterBoundsCallback(
            [this](int minX, int minY, int maxX, int maxY)
            {
                OnSpriteRasterBounds(minX, minY, maxX, maxY);
            });

        // GDI owns no depth storage. Establish that invariant even for direct renderer users;
        // GraphicsDevice later applies its public default state, but focused/internal callers can
        // create a SpriteBatch immediately after construction.
        software2D_->SetDepthTestEnabled(false);
        software2D_->SetDepthWriteEnabled(false);

        CNA::Platform::Win32NativeWindow nativeWindow;
        if (!CNA::Platform::TryGetWin32(surface_.GetNativeHandle(), nativeWindow))
            throw std::runtime_error("GDI graphics renderer requires a Win32 native window.");
        nativeWindow_ = nativeWindow.hwnd;

        SynchronizeBackbufferSize();
        IGraphicsRenderer::RegisterForWindow(surface_.GetWindowId(), this);
    }

    GdiRenderer::~GdiRenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(surface_.GetWindowId());
    }

    void GdiRenderer::RecordNativeClientInvalidation()
    {
        // Event watches may run on another thread. A generation (rather than one bool) prevents a
        // repaint event arriving during Present() from being cleared by that older present.
        nativeInvalidationGeneration_.fetch_add(1, std::memory_order_release);
    }

    bool GdiRenderer::GetDrawablePixelSize(int& width, int& height) const
    {
        // The platform snapshot is the single size authority for CPU storage and native
        // destination geometry. Win32 can retain a non-zero last client size while iconic, so
        // reject that state before consulting the snapshot.
        if (nativeWindow_ == nullptr || IsIconic(static_cast<HWND>(nativeWindow_)))
        {
            width = 0;
            height = 0;
            return false;
        }
        const auto drawableSize = surface_.GetDrawableSize();
        width = drawableSize.width;
        height = drawableSize.height;
        return width > 0 && height > 0;
    }

    bool GdiRenderer::GetWindowCoordinateSize(int& width, int& height) const
    {
        const auto drawableSize = surface_.GetDrawableSize();
        const float displayScale = surface_.GetDisplayScale();
        if (drawableSize.width <= 0 || drawableSize.height <= 0 || !(displayScale > 0.0f))
        {
            width = 0;
            height = 0;
            return false;
        }
        width = static_cast<int>(std::lround(drawableSize.width / displayScale));
        height = static_cast<int>(std::lround(drawableSize.height / displayScale));
        return width > 0 && height > 0;
    }

    void GdiRenderer::GetLogicalSize(int& width, int& height) const
    {
        int drawableWidth = 0;
        int drawableHeight = 0;
        (void)GetDrawablePixelSize(drawableWidth, drawableHeight);
        const Software::SoftwareFramebuffer& backbuffer = software2D_->Backbuffer();
        const GdiPresentationSize size = ResolveGdiLogicalSize(
            presentationMode_, drawableWidth, drawableHeight,
            requestedVirtualWidth_, requestedVirtualHeight_,
            backbuffer.width, backbuffer.height);
        width = size.width;
        height = size.height;
    }

    void GdiRenderer::SynchronizeBackbufferSize()
    {
        int logicalWidth = 0;
        int logicalHeight = 0;
        GetLogicalSize(logicalWidth, logicalHeight);
        const Software::SoftwareFramebuffer& backbuffer = software2D_->Backbuffer();
        if (logicalWidth > 0 && logicalHeight > 0 &&
            (backbuffer.width != logicalWidth || backbuffer.height != logicalHeight))
        {
            software2D_->SetVirtualResolution(logicalWidth, logicalHeight);
            MarkBackbufferFullyDirty();
        }
    }

    void GdiRenderer::Clear(float r, float g, float b, float a)
    {
        SynchronizeBackbufferSize();
        software2D_->Clear(r, g, b, a);
        if (renderingToBackbuffer_)
            MarkBackbufferFullyDirty();
    }

    void GdiRenderer::Present()
    {
        SynchronizeBackbufferSize();

        Software::SoftwareFramebuffer& writableBackbuffer = software2D_->Backbuffer();
        writableBackbuffer.ResolveColor();
        const Software::SoftwareFramebuffer& backbuffer = writableBackbuffer;
        if (backbuffer.width <= 0 || backbuffer.height <= 0 || backbuffer.color.empty())
            return;

        int clientWidth = 0;
        int clientHeight = 0;
        if (!GetDrawablePixelSize(clientWidth, clientHeight))
            return; // A minimized window has no drawable client area.

        const std::uint64_t invalidationSnapshot =
            nativeInvalidationGeneration_.load(std::memory_order_acquire);

        // The explicit platform-invalidation generation is authoritative; GetUpdateRect remains
        // only an additional native hint because the event pump may already have validated
        // WM_PAINT before CNA receives the corresponding exposure event.
        const bool clientNeedsRepair =
            invalidationSnapshot != presentedNativeInvalidationGeneration_ ||
            GetUpdateRect(static_cast<HWND>(nativeWindow_), nullptr, FALSE) != FALSE;
        const GdiPresentationRect dirtyRectangle{
            backbufferDirtyX_, backbufferDirtyY_,
            backbufferDirtyWidth_, backbufferDirtyHeight_};
        const GdiPresentationPlan plan = BuildGdiPresentationPlan(
            presentationMode_, clientWidth, clientHeight, backbuffer.width, backbuffer.height,
            configuration_.dirtyPresentation, clientNeedsRepair,
            backbufferFullyDirty_, backbufferDirtyValid_, dirtyRectangle);
        const int stretchMode = GetPresentationStretchMode(configuration_.presentationFilter);
        lastPresentationTelemetry_ = {
            true, plan, stretchMode, {true, 0, 0, "none"}};
        if (plan.path == GdiBlitPath::None)
            return; // No damage: do not acquire a window DC.

        WindowDeviceContext windowDc(static_cast<HWND>(nativeWindow_));
        const HDC deviceContext = windowDc.Get();
        const bool forceFailure = debugForceNextDibBlitFailure_;
        debugForceNextDibBlitFailure_ = false;
        const GdiBlitResult blitResult = BlitGdiRgbaToDeviceContext(
            deviceContext, clientWidth, clientHeight,
            backbuffer.width, backbuffer.height,
            backbuffer.color.data(), backbuffer.color.size(),
            plan, stretchMode, forceFailure);
        lastPresentationTelemetry_.result = blitResult;
        if (!blitResult.success)
            ThrowWin32Failure(blitResult.operation, blitResult.win32Error);

        SetLastError(ERROR_SUCCESS);
        if (!GdiFlush())
        {
            const DWORD error = GetLastError();
            lastPresentationTelemetry_.result = {
                false, 0, static_cast<std::uint32_t>(error), "GdiFlush"};
            ThrowWin32Failure("GdiFlush", error);
        }
        ApplyOptionalDwmPacing(configuration_.dwmFlush);

        // GDI-077: include the DC release itself in the checked transaction rather than leaving
        // it to WindowDeviceContext's non-throwing destructor, whose result was previously
        // discarded. A failed release must be visible in telemetry and must not let damage/the
        // invalidation generation be acknowledged as if the frame were fully committed.
        const bool forceReleaseFailure = debugForceNextReleaseDcFailure_;
        debugForceNextReleaseDcFailure_ = false;
        const WindowDeviceContextReleaseResult releaseResult = windowDc.Release(forceReleaseFailure);
        if (!releaseResult.success)
        {
            lastPresentationTelemetry_.result = {
                false, blitResult.copiedLines, releaseResult.win32Error, "ReleaseDC"};
            ThrowWin32Failure("ReleaseDC", releaseResult.win32Error);
        }

        // Commit only after every correctness-relevant native operation succeeds. Any exception
        // above leaves both CPU damage and the watched invalidation generation pending.
        ResetBackbufferDamage();
        // Only acknowledge the generation captured before this blit. An event watch that ran
        // during the transaction has a newer value and therefore forces the next full repaint.
        presentedNativeInvalidationGeneration_ = invalidationSnapshot;
    }

    void GdiRenderer::GetViewportSize(int& width, int& height)
    {
        SynchronizeBackbufferSize();
        software2D_->GetViewportSize(width, height);
    }

    void GdiRenderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        surface_.Update(surface);
        RecordNativeClientInvalidation();
    }

    void GdiRenderer::OnSurfaceInvalidated(const CNA::Platform::WindowId window)
    {
        if (window == 0 || window == surface_.GetWindowId())
            RecordNativeClientInvalidation();
    }

    void GdiRenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        SynchronizeBackbufferSize();
        software2D_->ReadBackbuffer(x, y, w, h, pixels);
    }

    void GdiRenderer::SetVirtualResolution(int width, int height)
    {
        (void)ValidateGdiFramebufferDimension("width", width);
        (void)ValidateGdiFramebufferDimension("height", height);
        const int previousWidth = requestedVirtualWidth_;
        const int previousHeight = requestedVirtualHeight_;
        requestedVirtualWidth_ = width;
        requestedVirtualHeight_ = height;
        try
        {
            SynchronizeBackbufferSize();
        }
        catch (...)
        {
            requestedVirtualWidth_ = previousWidth;
            requestedVirtualHeight_ = previousHeight;
            throw;
        }
        MarkBackbufferFullyDirty();
    }

    void GdiRenderer::SetPresentationMode(int mode)
    {
        // Validate before mutating any state: a failed request leaves the previous presentation
        // geometry active and cannot flow into switch statements as an impossible enum value.
        const CnaPresentationMode requestedMode = ValidatePresentationMode(mode);
        const CnaPresentationMode previousMode = presentationMode_;
        presentationMode_ = requestedMode;
        try
        {
            SynchronizeBackbufferSize();
        }
        catch (...)
        {
            // GDI-075: an ordinal-valid mode can still derive a logical size that exceeds the
            // framebuffer's axis or byte budget (e.g. FixedHeightDynamicWidth against an extreme
            // drawable aspect). SynchronizeBackbufferSize()'s own resize is already transactional,
            // so restoring only presentationMode_ fully rolls the whole request back: the retained
            // framebuffer's dimensions, pixels, and damage are already untouched.
            presentationMode_ = previousMode;
            throw;
        }
        MarkBackbufferFullyDirty();
    }

    void GdiRenderer::SetSwapInterval(int /*interval*/)
    {
        // GDI has no swap-chain or vertical-retrace present control.
    }

    int GdiRenderer::ApplyMultiSampleCount(int requestedMultiSampleCount)
    {
        // CPU MSAA is intentionally opt-in and deliberately supports one honest configuration:
        // four 2x2-grid samples per pixel. A request for any other count stays single-sampled;
        // reporting a requested 2x/8x count while allocating four samples would be misleading.
        SynchronizeBackbufferSize();
        software2D_->Backbuffer().SetMultiSampleCount(requestedMultiSampleCount == 4 ? 4 : 0);
        MarkBackbufferFullyDirty();
        return software2D_->Backbuffer().multiSampleCount;
    }

    int GdiRenderer::GetMultiSampleCount() const
    {
        return software2D_->Backbuffer().multiSampleCount;
    }

    void GdiRenderer::ApplyDepthStencilState(bool, bool, int, bool stencilEnable,
                                                     int stencilFunc, int stencilPass,
                                                     int stencilFail, int stencilDepthFail,
                                                     int stencilMask, int stencilWriteMask,
                                                     int referenceStencil, bool /*twoSidedStencilMode*/,
                                                     int /*ccwStencilFunc*/, int /*ccwStencilPass*/,
                                                     int /*ccwStencilFail*/,
                                                     int /*ccwStencilDepthFail*/)
    {
        // GDI has no 3D depth contract: retaining a caller's depth state here would change
        // SpriteBatch's documented draw ordering.  Its separate 8-bit CPU stencil plane is real,
        // however, and is useful for ordinary 2D clipping/masking.  Front/back face selection has
        // no meaning for a 2D quad, so the clockwise state is deliberately the one applied.
        software2D_->ApplyDepthStencilState(
            /*depthEnable*/ false, /*depthWriteEnable*/ false, /*LessEqual*/ 3,
            stencilEnable, stencilFunc, stencilPass, stencilFail, stencilDepthFail,
            stencilMask, stencilWriteMask, referenceStencil, false, 0, 0, 0, 0);
    }

    bool GdiRenderer::TransformWindowToLogical(float windowX, float windowY,
                                                       float& logX, float& logY) const
    {
        int clientWidth = 0;
        int clientHeight = 0;
        int windowCoordinateWidth = 0;
        int windowCoordinateHeight = 0;
        int logicalWidth = 0;
        int logicalHeight = 0;
        if (!GetDrawablePixelSize(clientWidth, clientHeight) ||
            !GetWindowCoordinateSize(windowCoordinateWidth, windowCoordinateHeight))
            return false;
        GetLogicalSize(logicalWidth, logicalHeight);
        return MapGdiSdlWindowToLogical(
            presentationMode_, windowCoordinateWidth, windowCoordinateHeight,
            clientWidth, clientHeight, logicalWidth, logicalHeight,
            windowX, windowY, logX, logY);
    }

    bool GdiRenderer::TransformLogicalToWindow(float logX, float logY,
                                                       float& windowX, float& windowY) const
    {
        int clientWidth = 0;
        int clientHeight = 0;
        int windowCoordinateWidth = 0;
        int windowCoordinateHeight = 0;
        int logicalWidth = 0;
        int logicalHeight = 0;
        if (!GetDrawablePixelSize(clientWidth, clientHeight) ||
            !GetWindowCoordinateSize(windowCoordinateWidth, windowCoordinateHeight))
            return false;
        GetLogicalSize(logicalWidth, logicalHeight);
        return MapGdiLogicalToSdlWindow(
            presentationMode_, windowCoordinateWidth, windowCoordinateHeight,
            clientWidth, clientHeight, logicalWidth, logicalHeight,
            logX, logY, windowX, windowY);
    }

    std::unique_ptr<ITextureRenderer> GdiRenderer::CreateTexture(const ImageData& data)
    {
        return software2D_->CreateTexture(data);
    }

    std::unique_ptr<ISpriteBatchRenderer> GdiRenderer::CreateSpriteBatch()
    {
        return std::make_unique<GdiSpriteBatchRenderer>(
            software2D_->CreateSpriteBatch(),
            [this] { SynchronizeBackbufferSize(); });
    }

    void GdiRenderer::SetRenderTarget2D(IRenderTargetRenderer* target)
    {
        if (target != nullptr &&
            dynamic_cast<Software::SoftwareRenderTargetRenderer*>(target) == nullptr)
        {
            throw std::invalid_argument(
                "GDI SetRenderTarget2D requires a compatible CPU render target.");
        }
        software2D_->SetRenderTarget2D(target);
        renderingToBackbuffer_ = target == nullptr;
    }

    void GdiRenderer::SetRenderTargetCubeFace(IRenderTargetCubeRenderer* target, int)
    {
        if (target != nullptr)
            ThrowUnsupportedFeature("RenderTargetCube face bindings");
        SetRenderTarget2D(nullptr);
    }

    void GdiRenderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (renderTargets == nullptr || count <= 0)
        {
            SetRenderTarget2D(nullptr);
            return;
        }
        if (count != 1)
            ThrowUnsupportedFeature("multiple simultaneous render targets");
        if (renderTargets[0].IsRenderTargetCubeFace())
            ThrowUnsupportedFeature("RenderTargetCube face bindings");
        if (renderTargets[0].GetArraySlice() != 0)
            ThrowUnsupportedFeature("RenderTarget2D array slices");
        IRenderTargetRenderer* target = renderTargets[0].GetRenderTarget2D();
        if (target == nullptr)
            throw std::invalid_argument("GDI SetRenderTargets received a null 2D target.");
        SetRenderTarget2D(target);
    }

    void GdiRenderer::MarkBackbufferDirty(const Rectangle& rectangle)
    {
        if (!renderingToBackbuffer_ || backbufferFullyDirty_ ||
            rectangle.Width <= 0 || rectangle.Height <= 0)
            return;

        const Software::SoftwareFramebuffer& backbuffer = software2D_->Backbuffer();
        const auto clampEdge = [](long long value, int limit) {
            return static_cast<int>(std::clamp<long long>(value, 0, limit));
        };
        const int left = clampEdge(rectangle.X, backbuffer.width);
        const int top = clampEdge(rectangle.Y, backbuffer.height);
        const int right = clampEdge(static_cast<long long>(rectangle.X) + rectangle.Width,
                                    backbuffer.width);
        const int bottom = clampEdge(static_cast<long long>(rectangle.Y) + rectangle.Height,
                                     backbuffer.height);
        if (right <= left || bottom <= top)
            return;

        if (!backbufferDirtyValid_)
        {
            backbufferDirtyX_ = left;
            backbufferDirtyY_ = top;
            backbufferDirtyWidth_ = right - left;
            backbufferDirtyHeight_ = bottom - top;
            backbufferDirtyValid_ = true;
            return;
        }

        const int unionRight = std::max(backbufferDirtyX_ + backbufferDirtyWidth_, right);
        const int unionBottom = std::max(backbufferDirtyY_ + backbufferDirtyHeight_, bottom);
        backbufferDirtyX_ = std::min(backbufferDirtyX_, left);
        backbufferDirtyY_ = std::min(backbufferDirtyY_, top);
        backbufferDirtyWidth_ = unionRight - backbufferDirtyX_;
        backbufferDirtyHeight_ = unionBottom - backbufferDirtyY_;
    }

    void GdiRenderer::OnSpriteRasterBounds(int minX, int minY, int maxX, int maxY)
    {
        const long long width = static_cast<long long>(maxX) - minX + 1;
        const long long height = static_cast<long long>(maxY) - minY + 1;
        if (width <= 0 || height <= 0 ||
            width > std::numeric_limits<int>::max() ||
            height > std::numeric_limits<int>::max())
        {
            MarkBackbufferFullyDirty();
            return;
        }
        MarkBackbufferDirty(Rectangle(minX, minY, static_cast<int>(width),
                                      static_cast<int>(height)));
    }

    void GdiRenderer::MarkBackbufferFullyDirty()
    {
        if (!renderingToBackbuffer_)
            return;
        backbufferFullyDirty_ = true;
        backbufferDirtyValid_ = true;
    }

    void GdiRenderer::ResetBackbufferDamage()
    {
        backbufferFullyDirty_ = false;
        backbufferDirtyValid_ = false;
        backbufferDirtyX_ = 0;
        backbufferDirtyY_ = 0;
        backbufferDirtyWidth_ = 0;
        backbufferDirtyHeight_ = 0;
    }

    bool GdiRenderer::DebugGetBackbufferDamage(Rectangle& rectangle,
                                                       bool& fullyDirty) const
    {
        fullyDirty = backbufferFullyDirty_;
        rectangle = Rectangle(backbufferDirtyX_, backbufferDirtyY_,
                              backbufferDirtyWidth_, backbufferDirtyHeight_);
        return backbufferFullyDirty_ || backbufferDirtyValid_;
    }

    void GdiRenderer::DebugResetBackbufferDamage()
    {
        ResetBackbufferDamage();
    }

    bool GdiRenderer::DebugIsNativeClientInvalidated() const
    {
        return nativeInvalidationGeneration_.load(std::memory_order_acquire) !=
               presentedNativeInvalidationGeneration_;
    }

    bool GdiRenderer::DebugGetLastPresentationTelemetry(
        GdiPresentationTelemetry& telemetry) const
    {
        telemetry = lastPresentationTelemetry_;
        return lastPresentationTelemetry_.valid;
    }

    GdiFramebufferStorageTelemetry GdiRenderer::DebugGetBackbufferStorage() const
    {
        const Software::SoftwareFramebuffer& framebuffer = software2D_->Backbuffer();
        return {
            framebuffer.color.size(),
            framebuffer.depthBuffer.size() * sizeof(float),
            framebuffer.stencilBuffer.size(),
            framebuffer.multiSampleColor.size(),
        };
    }

    std::unique_ptr<IRenderTargetRenderer> GdiRenderer::CreateRenderTarget2D(
        int width, int height, int /*depthFormat*/, bool /*preserveContents*/, bool mipMap,
        int /*multiSampleCount*/)
    {
        // A 2D CPU surface is a useful SpriteBatch target. It can generate an RGBA8 mip chain on
        // unbind and owns the shared CPU stencil plane, but GDI still has no depth attachment or
        // MSAA resource. This explicit construction prevents the reusable Software target from
        // reporting a requested depth attachment as if GDI had one.
        return std::make_unique<Software::SoftwareRenderTargetRenderer>(
            width, height, 0, mipMap, 0, false, true);
    }

    std::unique_ptr<ITextureCubeRenderer> GdiRenderer::CreateTextureCube(int, bool, int)
    {
        ThrowUnsupportedFeature("TextureCube resources");
    }

    std::unique_ptr<ITexture3DRenderer> GdiRenderer::CreateTexture3D(int, int, int, bool, int)
    {
        ThrowUnsupportedFeature("Texture3D resources");
    }

    std::unique_ptr<IRenderTargetCubeRenderer> GdiRenderer::CreateRenderTargetCube(
        int, int, bool, bool, int)
    {
        ThrowUnsupportedFeature("RenderTargetCube resources");
    }

    std::unique_ptr<IEffectRenderer> GdiRenderer::CreateEffectRenderer(const std::string&,
                                                                              const std::string&)
    {
        ThrowUnsupportedFeature("ShaderEffect programs");
    }

    std::unique_ptr<IOcclusionQueryRenderer> GdiRenderer::CreateOcclusionQuery()
    {
        ThrowUnsupportedFeature("occlusion queries");
    }

    void GdiRenderer::ApplyBlendState(
        int colorSrcBlend, int alphaSrcBlend, int colorDstBlend, int alphaDstBlend,
        int colorBlendFunc, int alphaBlendFunc, const BlendWriteState& writeState)
    {
        software2D_->ApplyBlendState(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend,
            colorBlendFunc, alphaBlendFunc, writeState);
    }

    void GdiRenderer::ApplyRasterizerState(
        int cullMode, int fillMode, bool scissorTestEnable,
        float depthBias, float slopeScaleDepthBias)
    {
        software2D_->ApplyRasterizerState(
            cullMode, fillMode, scissorTestEnable, depthBias, slopeScaleDepthBias);
    }

    void GdiRenderer::ApplySamplerState(
        int slot, int filter, int addressU, int addressV, int maxAnisotropy)
    {
        software2D_->ApplySamplerState(slot, filter, addressU, addressV, maxAnisotropy);
    }

    void GdiRenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        software2D_->SetBlendFactor(r, g, b, a);
    }

    void GdiRenderer::SetReferenceStencil(int value)
    {
        software2D_->SetReferenceStencil(value);
    }

    void GdiRenderer::SetScissorRect(int x, int y, int width, int height)
    {
        software2D_->SetScissorRect(x, y, width, height);
    }

    void GdiRenderer::SetViewport(
        int x, int y, int width, int height, float minDepth, float maxDepth)
    {
        software2D_->SetViewport(x, y, width, height, minDepth, maxDepth);
    }

    bool GdiRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        // Wireframe SpriteBatch quads are genuinely rasterized by the shared CPU 2D path. GDI
        // also has an opt-in, real 4x CPU-MSAA backbuffer path and an always-present stencil-only
        // plane for 2D masks. GraphicsCapability::DepthStencilBuffer means a complete
        // depth+stencil attachment and remains false because GDI has no depth.
        return capability == CNA::GraphicsCapability::StencilBuffer ||
               capability == CNA::GraphicsCapability::WireFrame ||
               capability == CNA::GraphicsCapability::MultiSampleAntiAliasing ||
               capability == CNA::GraphicsCapability::AdditiveBlending;
    }

    int GdiRenderer::GetMaxTextureDimension() const
    {
        return Software::SoftwareFramebufferMaxDimension;
    }

    void GdiRenderer::ClearColorAndDepth(float, float, float, float, float)
    { ThrowUnsupportedFeature("ClearColorAndDepth"); }
    void GdiRenderer::ClearDepth(float) { ThrowUnsupportedFeature("ClearDepth"); }
    void GdiRenderer::ClearStencil(int stencil)
    {
        SynchronizeBackbufferSize();
        software2D_->ClearStencil(stencil);
    }
    void GdiRenderer::ClearDepthAndStencil(float, int)
    { ThrowUnsupportedFeature("ClearDepthAndStencil"); }
    void GdiRenderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        SynchronizeBackbufferSize();
        software2D_->ClearColorAndStencil(r, g, b, a, stencil);
        if (renderingToBackbuffer_)
            MarkBackbufferFullyDirty();
    }
    void GdiRenderer::ClearColorDepthAndStencil(float, float, float, float, float, int)
    { ThrowUnsupportedFeature("ClearColorDepthAndStencil"); }
    void GdiRenderer::SetDepthTestEnabled(bool)
    { ThrowUnsupportedFeature("SetDepthTestEnabled"); }
    void GdiRenderer::SetBlendEnabled(bool)
    { ThrowUnsupportedFeature("SetBlendEnabled"); }
    void GdiRenderer::SetDepthWriteEnabled(bool)
    { ThrowUnsupportedFeature("SetDepthWriteEnabled"); }

    std::unique_ptr<IVertexBufferRenderer> GdiRenderer::CreateVertexBuffer(int)
    {
        ThrowUnsupportedFeature("vertex buffers");
    }

    std::unique_ptr<IIndexBufferRenderer> GdiRenderer::CreateIndexBuffer16(int)
    {
        ThrowUnsupportedFeature("16-bit index buffers");
    }

    std::unique_ptr<IIndexBufferRenderer> GdiRenderer::CreateIndexBuffer32(int)
    {
        ThrowUnsupportedFeature("32-bit index buffers");
    }

    void GdiRenderer::DrawColoredPrimitives(const IVertexBufferRenderer&, const Matrix&,
                                                    const Matrix&, const Matrix&, PrimitiveType, int)
    { ThrowUnsupportedFeature("DrawColoredPrimitives"); }

    void GdiRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer&,
                                                           const IIndexBufferRenderer&, const Matrix&,
                                                           const Matrix&, const Matrix&, PrimitiveType, int)
    { ThrowUnsupportedFeature("DrawIndexedColoredPrimitives"); }

    void GdiRenderer::DrawPrimitivesEx(const IVertexBufferRenderer&, const Matrix&,
                                               const Matrix&, const Matrix&, PrimitiveType, int,
                                               const GpuDrawParams&)
    { ThrowUnsupportedFeature("DrawPrimitivesEx"); }

    void GdiRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer&,
                                                      const IIndexBufferRenderer&, const Matrix&,
                                                      const Matrix&, const Matrix&, PrimitiveType, int,
                                                      const GpuDrawParams&)
    { ThrowUnsupportedFeature("DrawIndexedPrimitivesEx"); }

    void GdiRenderer::DrawInstancedPrimitivesEx(const IVertexBufferRenderer&,
                                                        const IIndexBufferRenderer&, const Matrix&,
                                                        const Matrix&, const Matrix&, PrimitiveType, int,
                                                        int, const GpuDrawParams&)
    { ThrowUnsupportedFeature("DrawInstancedPrimitivesEx"); }
} // namespace CNA::Internal::Renderers::Gdi

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_GDI
    // plans/plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace Gdi { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> Gdi::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        auto renderer = std::make_unique<Gdi::GdiRenderer>(args);
        renderer->ApplyMultiSampleCount(args.multiSampleCount);
        return renderer;
    }
#endif
} // namespace CNA::Internal::Renderers
