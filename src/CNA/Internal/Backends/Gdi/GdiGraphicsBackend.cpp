#include "CNA/Internal/Backends/Gdi/GdiGraphicsBackend.hpp"
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
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace CNA::Internal::Backends::Gdi
{
    namespace
    {
        struct BlitRect
        {
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
        };

        struct RgbaBitmapInfo
        {
            BITMAPINFOHEADER header{};
            DWORD channelMasks[3] = {
                0x000000FFu, // R occupies the first byte of CNA's RGBA8 CPU pixels.
                0x0000FF00u, // G
                0x00FF0000u  // B
            };
        };

        [[nodiscard]] BlitRect CalculateBlitRect(CnaPresentationMode presentationMode,
                                                  int clientWidth, int clientHeight,
                                                  int sourceWidth, int sourceHeight)
        {
            if (clientWidth <= 0 || clientHeight <= 0 || sourceWidth <= 0 || sourceHeight <= 0)
                return {};

            if (presentationMode == CnaPresentationMode::NativeBackBuffer)
                return { 0, 0, sourceWidth, sourceHeight };
            if (presentationMode == CnaPresentationMode::Stretch ||
                presentationMode == CnaPresentationMode::FixedHeightDynamicWidth)
                return { 0, 0, clientWidth, clientHeight };

            const double sx = static_cast<double>(clientWidth) / sourceWidth;
            const double sy = static_cast<double>(clientHeight) / sourceHeight;
            const double scale = presentationMode == CnaPresentationMode::Overscan
                ? std::max(sx, sy)
                : std::min(sx, sy);
            const int width = std::max(1, static_cast<int>(std::lround(sourceWidth * scale)));
            const int height = std::max(1, static_cast<int>(std::lround(sourceHeight * scale)));
            return { (clientWidth - width) / 2, (clientHeight - height) / 2, width, height };
        }

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
            void SetTransformMatrix(const Matrix& matrix) override { inner_->SetTransformMatrix(matrix); }
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

        IGraphicsBackend::RegisterForWindow(window_, this);
        SynchronizeBackbufferSize();
    }

    GdiGraphicsBackend::~GdiGraphicsBackend()
    {
        IGraphicsBackend::UnregisterForWindow(window_);
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
            requestedVirtualHeight_ > 0 && hasClientSize)
        {
            height = requestedVirtualHeight_;
            width = std::max(1, static_cast<int>(std::lround(
                static_cast<double>(clientWidth) * height / clientHeight)));
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
        }
    }

    void GdiGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        SynchronizeBackbufferSize();
        Software::SoftwareGraphicsBackend::Clear(r, g, b, a);
    }

    void GdiGraphicsBackend::Present()
    {
        SynchronizeBackbufferSize();

        const Software::SoftwareFramebuffer& backbuffer = BackbufferFramebuffer();
        if (backbuffer.width <= 0 || backbuffer.height <= 0 || backbuffer.color.empty())
            return;

        int clientWidth = 0;
        int clientHeight = 0;
        if (!GetClientSize(clientWidth, clientHeight))
            return; // A minimized window has no drawable client area.

        HDC deviceContext = GetDC(static_cast<HWND>(nativeWindow_));
        if (deviceContext == nullptr)
            throw std::runtime_error("GDI graphics backend failed to acquire the window device context.");

        try
        {
            const BlitRect destination = CalculateBlitRect(
                presentationMode_, clientWidth, clientHeight, backbuffer.width, backbuffer.height);
            if (destination.width <= 0 || destination.height <= 0)
            {
                ReleaseDC(static_cast<HWND>(nativeWindow_), deviceContext);
                return;
            }

            // Make letterbox bars and unused NativeBackBuffer space deterministic.  Overscan and
            // Stretch cover the full client area, so this is just an inexpensive full-window fill.
            const RECT clientRect{ 0, 0, clientWidth, clientHeight };
            FillRect(deviceContext, &clientRect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

            RgbaBitmapInfo bitmapInfo{};
            bitmapInfo.header.biSize = sizeof(BITMAPINFOHEADER);
            bitmapInfo.header.biWidth = backbuffer.width;
            bitmapInfo.header.biHeight = -backbuffer.height; // CNA rows are top-first.
            bitmapInfo.header.biPlanes = 1;
            bitmapInfo.header.biBitCount = 32;
            bitmapInfo.header.biCompression = BI_BITFIELDS;
            bitmapInfo.header.biSizeImage = static_cast<DWORD>(
                static_cast<std::size_t>(backbuffer.width) * backbuffer.height * 4u);

            // A native-size blit needs no GDI scaling. SetDIBitsToDevice avoids the generic
            // StretchDIBits path in this common case, while retaining exactly the same top-down
            // RGBA BI_BITFIELDS layout. It also covers aspect-correct letterboxing where the
            // calculated destination happens to be 1:1.
            if (destination.width == backbuffer.width && destination.height == backbuffer.height)
            {
                const int copiedLines = SetDIBitsToDevice(
                    deviceContext, destination.x, destination.y, backbuffer.width, backbuffer.height,
                    0, 0, 0, static_cast<UINT>(backbuffer.height), backbuffer.color.data(),
                    reinterpret_cast<const BITMAPINFO*>(&bitmapInfo), DIB_RGB_COLORS);
                if (copiedLines != backbuffer.height)
                    throw std::runtime_error(
                        "GDI SetDIBitsToDevice failed while presenting the 2D framebuffer.");
            }
            else
            {
                const int stretchMode = GetPresentationStretchMode();
                const int oldStretchMode = SetStretchBltMode(deviceContext, stretchMode);
                if (stretchMode == HALFTONE)
                    SetBrushOrgEx(deviceContext, 0, 0, nullptr);
                const int copiedLines = StretchDIBits(
                    deviceContext, destination.x, destination.y, destination.width, destination.height,
                    0, 0, backbuffer.width, backbuffer.height, backbuffer.color.data(),
                    reinterpret_cast<const BITMAPINFO*>(&bitmapInfo), DIB_RGB_COLORS, SRCCOPY);
                if (oldStretchMode != 0)
                    SetStretchBltMode(deviceContext, oldStretchMode);
                if (copiedLines == static_cast<int>(GDI_ERROR))
                    throw std::runtime_error(
                        "GDI StretchDIBits failed while presenting the scaled 2D framebuffer.");
            }

            GdiFlush();
            ReleaseDC(static_cast<HWND>(nativeWindow_), deviceContext);
        }
        catch (...)
        {
            ReleaseDC(static_cast<HWND>(nativeWindow_), deviceContext);
            throw;
        }
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
    }

    void GdiGraphicsBackend::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
        SynchronizeBackbufferSize();
    }

    void GdiGraphicsBackend::SetSwapInterval(int /*interval*/)
    {
        // GDI has no swap-chain or vertical-retrace present control.
    }

    int GdiGraphicsBackend::ApplyMultiSampleCount(int /*requestedMultiSampleCount*/)
    {
        // GDI's CPU framebuffer is single-sampled. Sprite edges are rasterized exactly as stored.
        return 0;
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
        const BlitRect destination = CalculateBlitRect(
            presentationMode_, clientWidth, clientHeight, logicalWidth, logicalHeight);
        if (destination.width <= 0 || destination.height <= 0 ||
            windowX < destination.x || windowY < destination.y ||
            windowX >= destination.x + destination.width ||
            windowY >= destination.y + destination.height)
            return false;

        logX = (windowX - destination.x) * logicalWidth / destination.width;
        logY = (windowY - destination.y) * logicalHeight / destination.height;
        return true;
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
        if (logicalWidth <= 0 || logicalHeight <= 0 || logX < 0.0f || logY < 0.0f ||
            logX >= logicalWidth || logY >= logicalHeight)
            return false;

        const BlitRect destination = CalculateBlitRect(
            presentationMode_, clientWidth, clientHeight, logicalWidth, logicalHeight);
        if (destination.width <= 0 || destination.height <= 0)
            return false;
        windowX = destination.x + logX * destination.width / logicalWidth;
        windowY = destination.y + logY * destination.height / logicalHeight;
        return true;
    }

    std::unique_ptr<ISpriteBatchBackend> GdiGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<GdiSpriteBatchBackend>(
            Software::SoftwareGraphicsBackend::CreateSpriteBatch(),
            [this] { SynchronizeBackbufferSize(); });
    }

    std::unique_ptr<IRenderTargetBackend> GdiGraphicsBackend::CreateRenderTarget2D(
        int width, int height, int /*depthFormat*/, bool /*preserveContents*/, bool mipMap,
        int /*multiSampleCount*/)
    {
        // A 2D CPU surface is a useful SpriteBatch target. It can generate an RGBA8 mip chain on
        // unbind, but GDI still has no depth/stencil or MSAA resource. This explicit construction
        // prevents the reusable Software target from reporting a requested depth attachment as if
        // GDI had one.
        return std::make_unique<Software::SoftwareRenderTargetBackend>(
            width, height, 0, mipMap, 0, false);
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

    bool GdiGraphicsBackend::SupportsCapability(CNA::GraphicsCapability /*capability*/) const
    {
        return false;
    }

    void GdiGraphicsBackend::ClearColorAndDepth(float, float, float, float, float)
    { ThrowNo3D("ClearColorAndDepth"); }
    void GdiGraphicsBackend::ClearDepth(float) { ThrowNo3D("ClearDepth"); }
    void GdiGraphicsBackend::ClearStencil(int) { ThrowNo3D("ClearStencil"); }
    void GdiGraphicsBackend::ClearDepthAndStencil(float, int) { ThrowNo3D("ClearDepthAndStencil"); }
    void GdiGraphicsBackend::ClearColorAndStencil(float, float, float, float, int)
    { ThrowNo3D("ClearColorAndStencil"); }
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
        return std::make_unique<Gdi::GdiGraphicsBackend>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode);
    }
#endif
} // namespace CNA::Internal::Backends
