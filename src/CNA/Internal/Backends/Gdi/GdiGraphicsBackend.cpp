#include "CNA/Internal/Backends/Gdi/GdiGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Gdi/GdiPresentation.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorMatrixEffect.hpp"

#include <SDL3/SDL.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace CNA::Internal::Backends::Gdi
{
    namespace
    {
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
                if (context_ != nullptr)
                    (void)ReleaseDC(window_, context_);
            }

            WindowDeviceContext(const WindowDeviceContext&) = delete;
            WindowDeviceContext& operator=(const WindowDeviceContext&) = delete;

            [[nodiscard]] HDC Get() const { return context_; }

        private:
            HWND window_ = nullptr;
            HDC context_ = nullptr;
        };

        /// Selects only the final GDI blit's resampling mode. Texture sampling remains entirely
        /// in the shared CPU SpriteBatch rasterizer, so this must never change SamplerState
        /// semantics. COLORONCOLOR is deliberately the default because it preserves crisp
        /// pixel-art edges. HALFTONE is opt-in for applications that prefer a smoother window
        /// resize: `CNA_GDI_PRESENT_FILTER=halftone`.
        [[nodiscard]] int GetPresentationStretchMode()
        {
            const char* value = std::getenv("CNA_GDI_PRESENT_FILTER");
            if (value != nullptr && std::string_view(value) == "halftone")
                return HALFTONE;
            return COLORONCOLOR;
        }

        [[nodiscard]] bool IsDirtyPresentationRequested()
        {
            const char* value = std::getenv("CNA_GDI_DIRTY_PRESENTATION");
            return value != nullptr && std::string_view(value) == "1";
        }

        /// DwmFlush is a compositor pacing hint, not a swap interval. Keep it strictly opt-in:
        /// it can block until DWM accepts the next composition batch, which is useful for modest
        /// compatibility applications but is an unwanted latency policy by default. Loading it at
        /// runtime preserves the GDI backend's ability to run on systems where DWM is absent or
        /// disabled, with an intentionally silent non-blocking fallback.
        void ApplyOptionalDwmPacing()
        {
            const char* requested = std::getenv("CNA_GDI_DWM_FLUSH");
            if (requested == nullptr || std::string_view(requested) != "1")
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

        [[noreturn]] void ThrowNo3D(const char* methodName)
        {
            throw std::runtime_error(std::string("GDI (Win32 2D) does not support 3D: ") + methodName);
        }

        /// Delegates the shared CPU rasterizer SpriteBatch implementation while accepting exactly
        /// GDI-022's fixed CPU ColorMatrixEffect. Every other custom Effect is rejected, rather
        /// than accepted and ignored as if GDI had a programmable shader path.
        class GdiSpriteBatchBackend final : public ISpriteBatchBackend
        {
        public:
            explicit GdiSpriteBatchBackend(std::unique_ptr<ISpriteBatchBackend> inner,
                                           std::function<void()> synchronizeBackbuffer)
                : inner_(std::move(inner))
                , synchronizeBackbuffer_(std::move(synchronizeBackbuffer))
            {
                if (!inner_)
                    throw std::runtime_error("GDI failed to create its CPU SpriteBatch backend.");
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
            void Draw(const ITextureBackend& texture, float x, float y) override
            {
                synchronizeBackbuffer_();
                inner_->Draw(texture, x, y);
            }
            void Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                      const Rectangle& sourceRectangle, const Color& color) override
            {
                synchronizeBackbuffer_();
                inner_->Draw(texture, destinationRectangle, sourceRectangle, color);
            }
            void Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                      const Rectangle& sourceRectangle, const Color& color, float rotation,
                      const Vector2& origin, SpriteEffects effects, float layerDepth) override
            {
                synchronizeBackbuffer_();
                inner_->Draw(texture, destinationRectangle, sourceRectangle, color, rotation,
                             origin, effects, layerDepth);
            }

        private:
            std::unique_ptr<ISpriteBatchBackend> inner_;
            std::function<void()> synchronizeBackbuffer_;
        };
    } // namespace

    GdiGraphicsBackend::GdiGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                                           CnaPresentationMode presentationMode)
        : Software::SoftwareGraphicsBackend(std::max(1, virtualWidth), std::max(1, virtualHeight))
        , window_(window)
        , requestedVirtualWidth_(virtualWidth)
        , requestedVirtualHeight_(virtualHeight)
        , presentationMode_(presentationMode)
    {
        if (window_ == nullptr)
            throw std::runtime_error("GDI graphics backend requires an SDL window.");

        nativeWindow_ = SDL_GetPointerProperty(
            SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        if (nativeWindow_ == nullptr)
            throw std::runtime_error("GDI graphics backend could not obtain an HWND from the SDL window.");

        windowId_ = SDL_GetWindowID(window_);
        if (windowId_ == 0)
            throw std::runtime_error("GDI graphics backend could not obtain the SDL window ID.");

        SynchronizeBackbufferSize();
        IGraphicsBackend::RegisterForWindow(window_, this);
        if (!SDL_AddEventWatch(&GdiGraphicsBackend::WindowEventWatch, this))
        {
            const char* error = SDL_GetError();
            IGraphicsBackend::UnregisterForWindow(window_);
            throw std::runtime_error(
                std::string("GDI graphics backend could not watch window repaint events: ") +
                (error != nullptr ? error : "unknown SDL error"));
        }
        eventWatchRegistered_ = true;
    }

    GdiGraphicsBackend::~GdiGraphicsBackend()
    {
        if (eventWatchRegistered_)
        {
            SDL_RemoveEventWatch(&GdiGraphicsBackend::WindowEventWatch, this);
            eventWatchRegistered_ = false;
        }
        IGraphicsBackend::UnregisterForWindow(window_);
    }

    bool SDLCALL GdiGraphicsBackend::WindowEventWatch(void* userdata, SDL_Event* event)
    {
        auto* backend = static_cast<GdiGraphicsBackend*>(userdata);
        if (backend == nullptr || event == nullptr)
            return true;

        // A foreground transition is process-wide and can invalidate every retained window.
        if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND)
        {
            backend->RecordNativeClientInvalidation();
            return true;
        }

        switch (event->type)
        {
            case SDL_EVENT_WINDOW_SHOWN:
            case SDL_EVENT_WINDOW_EXPOSED:
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_MINIMIZED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
            case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
            case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
                if (event->window.windowID == backend->windowId_)
                    backend->RecordNativeClientInvalidation();
                break;
            default:
                break;
        }
        return true;
    }

    void GdiGraphicsBackend::RecordNativeClientInvalidation()
    {
        // Event watches may run on another thread. A generation (rather than one bool) prevents a
        // repaint event arriving during Present() from being cleared by that older present.
        nativeInvalidationGeneration_.fetch_add(1, std::memory_order_release);
    }

    bool GdiGraphicsBackend::GetClientSize(int& width, int& height) const
    {
        RECT rect{};
        if (nativeWindow_ == nullptr || !GetClientRect(static_cast<HWND>(nativeWindow_), &rect))
        {
            width = 0;
            height = 0;
            return false;
        }
        width = std::max(0L, rect.right - rect.left);
        height = std::max(0L, rect.bottom - rect.top);
        return width > 0 && height > 0;
    }

    void GdiGraphicsBackend::GetLogicalSize(int& width, int& height) const
    {
        int clientWidth = 0;
        int clientHeight = 0;
        const bool hasClientSize = GetClientSize(clientWidth, clientHeight);

        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth &&
            requestedVirtualHeight_ > 0)
        {
            if (hasClientSize)
            {
                height = requestedVirtualHeight_;
                width = std::max(1, static_cast<int>(std::lround(
                    static_cast<double>(clientWidth) * height / clientHeight)));
                return;
            }

            // Minimize/transition can temporarily remove a drawable client size. Preserve the
            // last valid dynamic logical dimensions instead of reallocating to the originally
            // requested width and reallocating again on restore.
            const Software::SoftwareFramebuffer& backbuffer = BackbufferFramebuffer();
            if (backbuffer.width > 0 && backbuffer.height > 0)
            {
                width = backbuffer.width;
                height = backbuffer.height;
                return;
            }
            width = std::max(1, requestedVirtualWidth_);
            height = requestedVirtualHeight_;
            return;
        }

        if (requestedVirtualWidth_ > 0 && requestedVirtualHeight_ > 0)
        {
            width = requestedVirtualWidth_;
            height = requestedVirtualHeight_;
            return;
        }

        if (hasClientSize)
        {
            width = clientWidth;
            height = clientHeight;
            return;
        }

        const Software::SoftwareFramebuffer& backbuffer = BackbufferFramebuffer();
        width = backbuffer.width;
        height = backbuffer.height;
    }

    void GdiGraphicsBackend::SynchronizeBackbufferSize()
    {
        int logicalWidth = 0;
        int logicalHeight = 0;
        GetLogicalSize(logicalWidth, logicalHeight);
        const Software::SoftwareFramebuffer& backbuffer = BackbufferFramebuffer();
        if (logicalWidth > 0 && logicalHeight > 0 &&
            (backbuffer.width != logicalWidth || backbuffer.height != logicalHeight))
        {
            Software::SoftwareGraphicsBackend::SetVirtualResolution(logicalWidth, logicalHeight);
            MarkBackbufferFullyDirty();
        }
    }

    void GdiGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        SynchronizeBackbufferSize();
        Software::SoftwareGraphicsBackend::Clear(r, g, b, a);
        if (renderingToBackbuffer_)
            MarkBackbufferFullyDirty();
    }

    void GdiGraphicsBackend::Present()
    {
        SynchronizeBackbufferSize();

        Software::SoftwareFramebuffer& writableBackbuffer = BackbufferFramebuffer();
        writableBackbuffer.ResolveColor();
        const Software::SoftwareFramebuffer& backbuffer = writableBackbuffer;
        if (backbuffer.width <= 0 || backbuffer.height <= 0 || backbuffer.color.empty())
            return;

        int clientWidth = 0;
        int clientHeight = 0;
        if (!GetClientSize(clientWidth, clientHeight))
            return; // A minimized window has no drawable client area.

        const std::uint64_t invalidationSnapshot =
            nativeInvalidationGeneration_.load(std::memory_order_acquire);

        // The explicit SDL-watch generation is authoritative; GetUpdateRect remains only an
        // additional native hint because SDL may already have validated WM_PAINT before CNA
        // receives SDL_EVENT_WINDOW_EXPOSED.
        const bool clientNeedsRepair =
            invalidationSnapshot != presentedNativeInvalidationGeneration_ ||
            GetUpdateRect(static_cast<HWND>(nativeWindow_), nullptr, FALSE) != FALSE;
        const GdiPresentationRect dirtyRectangle{
            backbufferDirtyX_, backbufferDirtyY_,
            backbufferDirtyWidth_, backbufferDirtyHeight_};
        const GdiPresentationPlan plan = BuildGdiPresentationPlan(
            presentationMode_, clientWidth, clientHeight, backbuffer.width, backbuffer.height,
            IsDirtyPresentationRequested(), clientNeedsRepair,
            backbufferFullyDirty_, backbufferDirtyValid_, dirtyRectangle);
        const int stretchMode = GetPresentationStretchMode();
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
        ApplyOptionalDwmPacing();

        // Commit only after every correctness-relevant native operation succeeds. Any exception
        // above leaves both CPU damage and the watched invalidation generation pending.
        ResetBackbufferDamage();
        // Only acknowledge the generation captured before this blit. An event watch that ran
        // during the transaction has a newer value and therefore forces the next full repaint.
        presentedNativeInvalidationGeneration_ = invalidationSnapshot;
    }

    void GdiGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        SynchronizeBackbufferSize();
        Software::SoftwareGraphicsBackend::GetViewportSize(width, height);
    }

    void GdiGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        SynchronizeBackbufferSize();
        Software::SoftwareGraphicsBackend::ReadBackbuffer(x, y, w, h, pixels);
    }

    void GdiGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        requestedVirtualWidth_ = width;
        requestedVirtualHeight_ = height;
        SynchronizeBackbufferSize();
        MarkBackbufferFullyDirty();
    }

    void GdiGraphicsBackend::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
        SynchronizeBackbufferSize();
        MarkBackbufferFullyDirty();
    }

    void GdiGraphicsBackend::SetSwapInterval(int /*interval*/)
    {
        // GDI has no swap-chain or vertical-retrace present control.
    }

    int GdiGraphicsBackend::ApplyMultiSampleCount(int requestedMultiSampleCount)
    {
        // CPU MSAA is intentionally opt-in and deliberately supports one honest configuration:
        // four 2x2-grid samples per pixel. A request for any other count stays single-sampled;
        // reporting a requested 2x/8x count while allocating four samples would be misleading.
        SynchronizeBackbufferSize();
        BackbufferFramebuffer().SetMultiSampleCount(requestedMultiSampleCount == 4 ? 4 : 0);
        MarkBackbufferFullyDirty();
        return BackbufferFramebuffer().multiSampleCount;
    }

    int GdiGraphicsBackend::GetMultiSampleCount() const
    {
        return BackbufferFramebuffer().multiSampleCount;
    }

    void GdiGraphicsBackend::ApplyDepthStencilState(bool, bool, int, bool stencilEnable,
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
        Software::SoftwareGraphicsBackend::ApplyDepthStencilState(
            /*depthEnable*/ false, /*depthWriteEnable*/ false, /*LessEqual*/ 3,
            stencilEnable, stencilFunc, stencilPass, stencilFail, stencilDepthFail,
            stencilMask, stencilWriteMask, referenceStencil, false, 0, 0, 0, 0);
    }

    bool GdiGraphicsBackend::TransformWindowToLogical(float windowX, float windowY,
                                                       float& logX, float& logY) const
    {
        int clientWidth = 0;
        int clientHeight = 0;
        int logicalWidth = 0;
        int logicalHeight = 0;
        if (!GetClientSize(clientWidth, clientHeight))
            return false;
        GetLogicalSize(logicalWidth, logicalHeight);
        return MapGdiWindowToLogical(
            presentationMode_, clientWidth, clientHeight, logicalWidth, logicalHeight,
            windowX, windowY, logX, logY);
    }

    bool GdiGraphicsBackend::TransformLogicalToWindow(float logX, float logY,
                                                       float& windowX, float& windowY) const
    {
        int clientWidth = 0;
        int clientHeight = 0;
        int logicalWidth = 0;
        int logicalHeight = 0;
        if (!GetClientSize(clientWidth, clientHeight))
            return false;
        GetLogicalSize(logicalWidth, logicalHeight);
        return MapGdiLogicalToWindow(
            presentationMode_, clientWidth, clientHeight, logicalWidth, logicalHeight,
            logX, logY, windowX, windowY);
    }

    std::unique_ptr<ISpriteBatchBackend> GdiGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<GdiSpriteBatchBackend>(
            Software::SoftwareGraphicsBackend::CreateSpriteBatch(),
            [this] { SynchronizeBackbufferSize(); });
    }

    void GdiGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* target)
    {
        Software::SoftwareGraphicsBackend::SetRenderTarget2D(target);
        renderingToBackbuffer_ = target == nullptr;
    }

    void GdiGraphicsBackend::MarkBackbufferDirty(const Rectangle& rectangle)
    {
        if (!renderingToBackbuffer_ || backbufferFullyDirty_ ||
            rectangle.Width <= 0 || rectangle.Height <= 0)
            return;

        const Software::SoftwareFramebuffer& backbuffer = BackbufferFramebuffer();
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

    void GdiGraphicsBackend::OnSpriteRasterBounds(int minX, int minY, int maxX, int maxY)
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

    void GdiGraphicsBackend::MarkBackbufferFullyDirty()
    {
        if (!renderingToBackbuffer_)
            return;
        backbufferFullyDirty_ = true;
        backbufferDirtyValid_ = true;
    }

    void GdiGraphicsBackend::ResetBackbufferDamage()
    {
        backbufferFullyDirty_ = false;
        backbufferDirtyValid_ = false;
        backbufferDirtyX_ = 0;
        backbufferDirtyY_ = 0;
        backbufferDirtyWidth_ = 0;
        backbufferDirtyHeight_ = 0;
    }

    bool GdiGraphicsBackend::DebugGetBackbufferDamage(Rectangle& rectangle,
                                                       bool& fullyDirty) const
    {
        fullyDirty = backbufferFullyDirty_;
        rectangle = Rectangle(backbufferDirtyX_, backbufferDirtyY_,
                              backbufferDirtyWidth_, backbufferDirtyHeight_);
        return backbufferFullyDirty_ || backbufferDirtyValid_;
    }

    void GdiGraphicsBackend::DebugResetBackbufferDamage()
    {
        ResetBackbufferDamage();
    }

    bool GdiGraphicsBackend::DebugIsNativeClientInvalidated() const
    {
        return nativeInvalidationGeneration_.load(std::memory_order_acquire) !=
               presentedNativeInvalidationGeneration_;
    }

    bool GdiGraphicsBackend::DebugGetLastPresentationTelemetry(
        GdiPresentationTelemetry& telemetry) const
    {
        telemetry = lastPresentationTelemetry_;
        return lastPresentationTelemetry_.valid;
    }

    std::unique_ptr<IRenderTargetBackend> GdiGraphicsBackend::CreateRenderTarget2D(
        int width, int height, int /*depthFormat*/, bool /*preserveContents*/, bool mipMap,
        int /*multiSampleCount*/)
    {
        // A 2D CPU surface is a useful SpriteBatch target. It can generate an RGBA8 mip chain on
        // unbind and owns the shared CPU stencil plane, but GDI still has no depth attachment or
        // MSAA resource. This explicit construction prevents the reusable Software target from
        // reporting a requested depth attachment as if GDI had one.
        return std::make_unique<Software::SoftwareRenderTargetBackend>(
            width, height, 0, mipMap, 0, false, true);
    }

    std::unique_ptr<ITextureCubeBackend> GdiGraphicsBackend::CreateTextureCube(int, bool, int)
    {
        return nullptr;
    }

    std::unique_ptr<ITexture3DBackend> GdiGraphicsBackend::CreateTexture3D(int, int, int, bool, int)
    {
        return nullptr;
    }

    std::unique_ptr<IEffectBackend> GdiGraphicsBackend::CreateEffectBackend(const std::string&,
                                                                              const std::string&)
    {
        return nullptr;
    }

    std::unique_ptr<IOcclusionQueryBackend> GdiGraphicsBackend::CreateOcclusionQuery()
    {
        ThrowNo3D("CreateOcclusionQuery");
    }

    bool GdiGraphicsBackend::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        // Wireframe SpriteBatch quads are genuinely rasterized by the shared CPU 2D path. GDI
        // also has an opt-in, real 4x CPU-MSAA backbuffer path and an always-present stencil-only
        // plane for 2D masks. GraphicsCapability::DepthStencilBuffer means a complete
        // depth+stencil attachment and remains false because GDI has no depth.
        return capability == CNA::GraphicsCapability::StencilBuffer ||
               capability == CNA::GraphicsCapability::WireFrame ||
               capability == CNA::GraphicsCapability::MultiSampleAntiAliasing;
    }

    void GdiGraphicsBackend::ClearColorAndDepth(float, float, float, float, float)
    { ThrowNo3D("ClearColorAndDepth"); }
    void GdiGraphicsBackend::ClearDepth(float) { ThrowNo3D("ClearDepth"); }
    void GdiGraphicsBackend::ClearStencil(int stencil)
    {
        SynchronizeBackbufferSize();
        Software::SoftwareGraphicsBackend::ClearStencil(stencil);
    }
    void GdiGraphicsBackend::ClearDepthAndStencil(float, int) { ThrowNo3D("ClearDepthAndStencil"); }
    void GdiGraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        SynchronizeBackbufferSize();
        Software::SoftwareGraphicsBackend::ClearColorAndStencil(r, g, b, a, stencil);
        if (renderingToBackbuffer_)
            MarkBackbufferFullyDirty();
    }
    void GdiGraphicsBackend::ClearColorDepthAndStencil(float, float, float, float, float, int)
    { ThrowNo3D("ClearColorDepthAndStencil"); }
    void GdiGraphicsBackend::SetDepthTestEnabled(bool) { ThrowNo3D("SetDepthTestEnabled"); }
    void GdiGraphicsBackend::SetBlendEnabled(bool) { ThrowNo3D("SetBlendEnabled"); }
    void GdiGraphicsBackend::SetDepthWriteEnabled(bool) { ThrowNo3D("SetDepthWriteEnabled"); }

    std::unique_ptr<IVertexBufferBackend> GdiGraphicsBackend::CreateVertexBuffer(int)
    {
        ThrowNo3D("CreateVertexBuffer");
    }

    std::unique_ptr<IIndexBufferBackend> GdiGraphicsBackend::CreateIndexBuffer16(int)
    {
        ThrowNo3D("CreateIndexBuffer16");
    }

    std::unique_ptr<IIndexBufferBackend> GdiGraphicsBackend::CreateIndexBuffer32(int)
    {
        ThrowNo3D("CreateIndexBuffer32");
    }

    void GdiGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&, const Matrix&,
                                                    const Matrix&, const Matrix&, PrimitiveType, int)
    { ThrowNo3D("DrawColoredPrimitives"); }

    void GdiGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&,
                                                           const IIndexBufferBackend&, const Matrix&,
                                                           const Matrix&, const Matrix&, PrimitiveType, int)
    { ThrowNo3D("DrawIndexedColoredPrimitives"); }

    void GdiGraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend&, const Matrix&,
                                               const Matrix&, const Matrix&, PrimitiveType, int,
                                               const GpuDrawParams&)
    { ThrowNo3D("DrawPrimitivesEx"); }

    void GdiGraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend&,
                                                      const IIndexBufferBackend&, const Matrix&,
                                                      const Matrix&, const Matrix&, PrimitiveType, int,
                                                      const GpuDrawParams&)
    { ThrowNo3D("DrawIndexedPrimitivesEx"); }

    void GdiGraphicsBackend::DrawInstancedPrimitivesEx(const IVertexBufferBackend&,
                                                        const IIndexBufferBackend&, const Matrix&,
                                                        const Matrix&, const Matrix&, PrimitiveType, int,
                                                        int, const GpuDrawParams&)
    { ThrowNo3D("DrawInstancedPrimitivesEx"); }
} // namespace CNA::Internal::Backends::Gdi

namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_GDI
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        auto backend = std::make_unique<Gdi::GdiGraphicsBackend>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode);
        backend->ApplyMultiSampleCount(args.multiSampleCount);
        return backend;
    }
#endif
} // namespace CNA::Internal::Backends
