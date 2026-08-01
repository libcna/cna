// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/Direct2D/Direct2DGraphicsBackend.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Backends::Direct2D
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        [[nodiscard]] std::string FormatHr(HRESULT hr)
        {
            std::ostringstream stream;
            stream << "0x" << std::uppercase << std::hex
                   << static_cast<unsigned long>(hr);
            return stream.str();
        }

        void ThrowIfFailed(HRESULT hr, const char* operation)
        {
            if (SUCCEEDED(hr)) return;
            if (hr == D2DERR_RECREATE_TARGET)
            {
                throw std::runtime_error(
                    std::string("Direct2D device resources were lost during ") + operation +
                    "; recreate the GraphicsDevice and its textures/render targets.");
            }
            throw std::runtime_error(std::string("Direct2D: ") + operation + " failed, hr=" + FormatHr(hr));
        }

        [[noreturn]] void ThrowNo3D(const char* operation)
        {
            throw std::runtime_error(std::string("Direct2D does not support 3D: ") + operation);
        }

        [[nodiscard]] D2D1_PRIMITIVE_BLEND ToPrimitiveBlend(Direct2DBlendMode blendMode)
        {
            switch (blendMode)
            {
                case Direct2DBlendMode::Copy: return D2D1_PRIMITIVE_BLEND_COPY;
                case Direct2DBlendMode::Add: return D2D1_PRIMITIVE_BLEND_ADD;
                case Direct2DBlendMode::SourceOver: return D2D1_PRIMITIVE_BLEND_SOURCE_OVER;
            }
            return D2D1_PRIMITIVE_BLEND_SOURCE_OVER;
        }

        [[nodiscard]] bool IsWhite(const Color& color)
        {
            return color.getRProperty() == 255 && color.getGProperty() == 255 &&
                   color.getBProperty() == 255 && color.getAProperty() == 255;
        }

        [[nodiscard]] bool IsFlipped(SpriteEffects effects)
        {
            return effects != SpriteEffects::None;
        }

        [[nodiscard]] int PositiveModulo(int value, int divisor)
        {
            const int remainder = value % divisor;
            return remainder < 0 ? remainder + divisor : remainder;
        }

        [[nodiscard]] int MirrorCoordinate(int value, int size)
        {
            const int period = size * 2;
            const int coordinate = PositiveModulo(value, period);
            return coordinate < size ? coordinate : period - 1 - coordinate;
        }
    }

    Direct2DBlendMode BlendStateToDirect2DBlendMode(
        int colorSrcBlend, int alphaSrcBlend, int colorDstBlend, int alphaDstBlend,
        int colorBlendFunc, int alphaBlendFunc)
    {
        // Microsoft::Xna::Framework::Graphics::Blend: One=0, Zero=1, SourceAlpha=4,
        // InverseSourceAlpha=5. BlendFunction::Add=0. Direct2D can express precisely the four
        // standard SpriteBatch presets, except that AlphaBlend and NonPremultiplied both select
        // SourceOver; the caller records their source-alpha convention separately.
        const bool additiveFunction = colorBlendFunc == 0 && alphaBlendFunc == 0;
        const bool symmetricFactors = colorSrcBlend == alphaSrcBlend && colorDstBlend == alphaDstBlend;
        if (!additiveFunction || !symmetricFactors)
        {
            throw std::runtime_error(
                "Direct2D supports only symmetric Add BlendState factors; its primitive blend API "
                "does not expose general source/destination factor or blend-equation state.");
        }
        if (colorSrcBlend == 0 && colorDstBlend == 1) return Direct2DBlendMode::Copy;       // Opaque
        if (colorSrcBlend == 0 && colorDstBlend == 5) return Direct2DBlendMode::SourceOver; // AlphaBlend
        if (colorSrcBlend == 4 && colorDstBlend == 5) return Direct2DBlendMode::SourceOver; // NonPremultiplied
        if (colorSrcBlend == 4 && colorDstBlend == 0) return Direct2DBlendMode::Add;         // Additive
        throw std::runtime_error(
            "Direct2D supports only BlendState::Opaque, AlphaBlend, NonPremultiplied, and Additive.");
    }

    Direct2DTextureBackend::Direct2DTextureBackend(Direct2DGraphicsBackend& owner, const ImageData& data)
        : owner_(&owner), width_(data.width), height_(data.height)
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::runtime_error("Direct2D texture dimensions must be positive.");
        const std::size_t bytes = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u;
        if (data.pixels.size() < bytes)
            throw std::runtime_error("Direct2D texture ImageData has fewer than width*height*4 bytes.");
        rgbaPixels_.assign(data.pixels.begin(), data.pixels.begin() + static_cast<std::ptrdiff_t>(bytes));
        RecreateBitmap();
    }

    void Direct2DTextureBackend::RecreateBitmap()
    {
        bitmap_.Attach(owner_->CreateBitmapFromRgba(rgbaPixels_.data(), width_, height_));
    }

    void Direct2DTextureBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (!rgba) throw std::runtime_error("Direct2DTextureBackend::UpdatePixels received null pixels.");
        if (stride < width_ * 4)
            throw std::runtime_error("Direct2DTextureBackend::UpdatePixels stride is smaller than one RGBA row.");
        for (int y = 0; y < height_; ++y)
        {
            std::memcpy(rgbaPixels_.data() + static_cast<std::size_t>(y) * width_ * 4u,
                        rgba + static_cast<std::size_t>(y) * stride,
                        static_cast<std::size_t>(width_) * 4u);
        }
        RecreateBitmap();
    }

    void Direct2DTextureBackend::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (level != 0)
            throw System::NotSupportedException(
                "Direct2D textures have no native mip chain; only mip level 0 can be updated.");
        if (levelW != width_ || levelH != height_)
            throw System::ArgumentOutOfRangeException(
                "level dimensions", std::to_string(levelW) + "x" + std::to_string(levelH),
                "Level-0 dimensions must equal the Direct2D texture dimensions.");
        UpdatePixels(rgba, width_ * 4);
    }

    Direct2DRenderTargetBackend::Direct2DRenderTargetBackend(
        Direct2DGraphicsBackend& owner, int width, int height)
        : owner_(&owner), width_(width), height_(height)
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::runtime_error("Direct2D render-target dimensions must be positive.");
        D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);
        ThrowIfFailed(owner_->d2dContext_->CreateBitmap(
                          D2D1::SizeU(static_cast<UINT32>(width_), static_cast<UINT32>(height_)),
                          nullptr, 0, &properties, &bitmap_),
                      "CreateBitmap(render target)");
    }

    Direct2DRenderTargetBackend::~Direct2DRenderTargetBackend()
    {
        if (owner_) owner_->ReleaseRenderTarget(this);
    }

    void Direct2DRenderTargetBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (!rgba) throw std::runtime_error("Direct2DRenderTargetBackend::UpdatePixels received null pixels.");
        if (stride < width_ * 4)
            throw std::runtime_error("Direct2DRenderTargetBackend::UpdatePixels stride is smaller than one RGBA row.");
        std::vector<uint8_t> bgra(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u);
        for (int y = 0; y < height_; ++y)
        {
            for (int x = 0; x < width_; ++x)
            {
                const uint8_t* source = rgba + static_cast<std::size_t>(y) * stride + x * 4;
                uint8_t* destination = bgra.data() + (static_cast<std::size_t>(y) * width_ + x) * 4u;
                destination[0] = source[2];
                destination[1] = source[1];
                destination[2] = source[0];
                destination[3] = source[3];
            }
        }
        ThrowIfFailed(bitmap_->CopyFromMemory(nullptr, bgra.data(), static_cast<UINT32>(width_ * 4)),
                      "ID2D1Bitmap::CopyFromMemory(render target)");
    }

    bool Direct2DRenderTargetBackend::GetData(int level, int x, int y, int w, int h,
                                               void* data, int dataLength) const
    {
        if (level < 0)
            throw System::ArgumentOutOfRangeException("level", std::to_string(level),
                                                       "level must not be negative.");
        if (level > 0)
            throw System::NotSupportedException("Direct2D render targets have no mip chain.");
        const std::int64_t right = static_cast<std::int64_t>(x) + w;
        const std::int64_t bottom = static_cast<std::int64_t>(y) + h;
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || right > width_ || bottom > height_)
            throw System::ArgumentOutOfRangeException("rect", "invalid", "The requested rectangle leaves the render target.");
        if (!data || static_cast<std::int64_t>(dataLength) < static_cast<std::int64_t>(w) * h * 4)
            throw System::ArgumentOutOfRangeException("dataLength", std::to_string(dataLength),
                                                       "The destination is too small for the requested RGBA pixels.");
        // A bitmap that is both a Direct2D target and a shader input has no CPU map/readback mode.
        // Returning false makes Texture2D::GetData report that limitation rather than manufacture
        // a transparent frame from an untouched staging buffer.
        return false;
    }

    void Direct2DRenderTargetBackend::BindAsRenderTarget() { owner_->BindRenderTarget(this); }
    void Direct2DRenderTargetBackend::UnbindAsRenderTarget() { owner_->BindRenderTarget(nullptr); }

    void Direct2DSpriteBatchBackend::Begin()
    {
        if (begun_) throw std::runtime_error("Direct2D SpriteBatch::Begin called while already begun.");
        owner_->EnsureDrawing();
        begun_ = true;
    }

    void Direct2DSpriteBatchBackend::End()
    {
        if (!begun_) throw std::runtime_error("Direct2D SpriteBatch::End called before Begin.");
        begun_ = false;
    }

    void Direct2DSpriteBatchBackend::SetCustomEffect(Effect* effect)
    {
        if (effect)
            throw std::runtime_error(
                "Direct2D does not support custom SpriteBatch Effects: it has no CNA shader-effect pipeline.");
    }

    void Direct2DSpriteBatchBackend::SetSamplerFilter(int textureFilter)
    {
        // Direct2D exposes a single bitmap interpolation choice.  Preserve the same
        // magnification partition as the other 2D backends: Linear-like values stay linear.
        switch (textureFilter)
        {
            case 0: case 2: case 3: case 7: case 8: linearFilter_ = true; break;
            default: linearFilter_ = false; break;
        }
    }

    void Direct2DSpriteBatchBackend::SetSamplerAddressMode(int addressU, int addressV)
    {
        if (addressU < 0 || addressU > 2 || addressV < 0 || addressV > 2)
            throw std::runtime_error("Direct2D received an unknown TextureAddressMode value.");
        addressU_ = addressU;
        addressV_ = addressV;
    }

    void Direct2DSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        if (!begun_) throw std::runtime_error("Direct2D SpriteBatch::Draw called before Begin.");
        owner_->DrawSprite(texture,
                           Rectangle(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()),
                           Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()), Color(255, 255, 255, 255),
                           0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, transform_, linearFilter_, addressU_, addressV_);
    }

    void Direct2DSpriteBatchBackend::Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color)
    {
        if (!begun_) throw std::runtime_error("Direct2D SpriteBatch::Draw called before Begin.");
        owner_->DrawSprite(texture, destinationRectangle, sourceRectangle, color, 0.0f,
                           Vector2(0.0f, 0.0f), SpriteEffects::None, transform_, linearFilter_, addressU_, addressV_);
    }

    void Direct2DSpriteBatchBackend::Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color, float rotation,
                                          const Vector2& origin, SpriteEffects effects, float layerDepth)
    {
        (void)layerDepth;
        if (!begun_) throw std::runtime_error("Direct2D SpriteBatch::Draw called before Begin.");
        owner_->DrawSprite(texture, destinationRectangle, sourceRectangle, color, rotation, origin, effects,
                           transform_, linearFilter_, addressU_, addressV_);
    }

    Direct2DGraphicsBackend::Direct2DGraphicsBackend(
        SDL_Window* window, int virtualWidth, int virtualHeight,
        CnaPresentationMode presentationMode, int swapInterval)
        : window_(window), virtualWidth_(virtualWidth), virtualHeight_(virtualHeight),
          presentationMode_(presentationMode), swapInterval_(std::clamp(swapInterval, 0, 4))
    {
        if (!window_) throw std::runtime_error("Direct2DGraphicsBackend initialized with null SDL_Window.");
        CreateDeviceResources();
        IGraphicsBackend::RegisterForWindow(window_, this);
    }

    Direct2DGraphicsBackend::~Direct2DGraphicsBackend()
    {
        if (drawing_)
        {
            // Destructors must not throw. EndDraw still ensures Direct2D releases any outstanding
            // resource references before the device context is destroyed.
            ClearScissorClip();
            d2dContext_->EndDraw();
        }
        IGraphicsBackend::UnregisterForWindow(window_);
    }

    void Direct2DGraphicsBackend::CreateDeviceResources()
    {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        constexpr D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        D3D_FEATURE_LEVEL createdFeatureLevel{};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                       featureLevels, static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
                                       &d3dDevice_, &createdFeatureLevel, &d3dContext_);
        if (FAILED(hr))
        {
            // WARP retains a functional Direct2D path on VMs/RDP sessions without a hardware D3D
            // adapter; this is an intentional Direct2D fallback, not an SDL renderer fallback.
            ThrowIfFailed(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                                             featureLevels, static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
                                             &d3dDevice_, &createdFeatureLevel, &d3dContext_),
                          "D3D11CreateDevice (hardware and WARP)");
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        ThrowIfFailed(d3dDevice_.As(&dxgiDevice), "QueryInterface(IDXGIDevice)");
        ComPtr<IDXGIAdapter> adapter;
        ThrowIfFailed(dxgiDevice->GetAdapter(&adapter), "IDXGIDevice::GetAdapter");
        ComPtr<IDXGIFactory2> dxgiFactory;
        ThrowIfFailed(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory)), "IDXGIAdapter::GetParent(IDXGIFactory2)");

        ThrowIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&d2dFactory_)),
                      "D2D1CreateFactory");
        ComPtr<ID2D1Device> d2dDevice;
        ThrowIfFailed(d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice), "ID2D1Factory1::CreateDevice");
        ThrowIfFailed(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext_),
                      "ID2D1Device::CreateDeviceContext");
        d2dContext_->SetDpi(96.0f, 96.0f); // CNA's public 2D coordinates are pixels, not DIPs.

        const HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!hwnd) throw std::runtime_error("Direct2DGraphicsBackend: SDL did not expose a Win32 HWND.");
        RECT clientRect{};
        if (!GetClientRect(hwnd, &clientRect))
            throw std::runtime_error("Direct2DGraphicsBackend: GetClientRect failed.");
        const UINT width = static_cast<UINT>(std::max<LONG>(1, clientRect.right - clientRect.left));
        const UINT height = static_cast<UINT>(std::max<LONG>(1, clientRect.bottom - clientRect.top));

        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = width;
        description.Height = height;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(
                          d3dDevice_.Get(), hwnd, &description, nullptr, nullptr, &swapChain_),
                      "IDXGIFactory2::CreateSwapChainForHwnd");
        dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        CreateBackBufferTarget();
    }

    void Direct2DGraphicsBackend::CreateBackBufferTarget()
    {
        backBufferTexture_.Reset();
        backBufferTarget_.Reset();
        ThrowIfFailed(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBufferTexture_)), "IDXGISwapChain::GetBuffer");
        D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
        ComPtr<IDXGISurface> surface;
        ThrowIfFailed(backBufferTexture_.As(&surface), "QueryInterface(IDXGISurface back buffer)");
        ThrowIfFailed(d2dContext_->CreateBitmapFromDxgiSurface(surface.Get(), &properties,
                                                                backBufferTarget_.GetAddressOf()),
                      "ID2D1DeviceContext::CreateBitmapFromDxgiSurface");
        d2dContext_->SetTarget(backBufferTarget_.Get());
    }

    void Direct2DGraphicsBackend::EnsureMainTargetSize()
    {
        if (activeRenderTarget_) return;
        const HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!hwnd) throw std::runtime_error("Direct2DGraphicsBackend: SDL did not expose a Win32 HWND.");
        RECT clientRect{};
        if (!GetClientRect(hwnd, &clientRect)) throw std::runtime_error("Direct2DGraphicsBackend: GetClientRect failed.");
        const UINT width = static_cast<UINT>(std::max<LONG>(1, clientRect.right - clientRect.left));
        const UINT height = static_cast<UINT>(std::max<LONG>(1, clientRect.bottom - clientRect.top));
        const D2D1_SIZE_U current = backBufferTarget_->GetPixelSize();
        if (current.width == width && current.height == height) return;

        if (drawing_) EndDrawing("resize");
        d2dContext_->SetTarget(nullptr);
        backBufferTarget_.Reset();
        backBufferTexture_.Reset();
        ThrowIfFailed(swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0),
                      "IDXGISwapChain::ResizeBuffers");
        CreateBackBufferTarget();
    }

    void Direct2DGraphicsBackend::EnsureDrawing()
    {
        if (drawing_) return;
        EnsureMainTargetSize();
        d2dContext_->BeginDraw();
        drawing_ = true;
        d2dContext_->SetPrimitiveBlend(ToPrimitiveBlend(blendMode_));
        ApplyScissorClip();
    }

    void Direct2DGraphicsBackend::EndDrawing(const char* operation)
    {
        if (!drawing_) return;
        ClearScissorClip();
        const HRESULT hr = d2dContext_->EndDraw();
        drawing_ = false;
        transientBitmaps_.clear();
        ThrowIfFailed(hr, operation);
    }

    Direct2DGraphicsBackend::PresentationTransform Direct2DGraphicsBackend::GetPresentationTransform() const
    {
        PresentationTransform result{};
        if (activeRenderTarget_)
        {
            result.logicalWidth = activeRenderTarget_->GetWidth();
            result.logicalHeight = activeRenderTarget_->GetHeight();
            return result;
        }
        const D2D1_SIZE_U size = backBufferTarget_->GetPixelSize();
        const int physicalWidth = static_cast<int>(size.width);
        const int physicalHeight = static_cast<int>(size.height);
        if (virtualWidth_ <= 0 || virtualHeight_ <= 0)
        {
            result.logicalWidth = physicalWidth;
            result.logicalHeight = physicalHeight;
            return result;
        }
        result.logicalWidth = virtualWidth_;
        result.logicalHeight = virtualHeight_;
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth && physicalHeight > 0)
        {
            result.logicalWidth = static_cast<int>(std::lround(
                static_cast<double>(physicalWidth) * virtualHeight_ / physicalHeight));
            result.scaleX = result.scaleY = static_cast<float>(physicalHeight) / virtualHeight_;
            return result;
        }
        const float sx = static_cast<float>(physicalWidth) / virtualWidth_;
        const float sy = static_cast<float>(physicalHeight) / virtualHeight_;
        switch (presentationMode_)
        {
            case CnaPresentationMode::Letterbox:
            {
                result.scaleX = result.scaleY = std::min(sx, sy);
                result.offsetX = (physicalWidth - virtualWidth_ * result.scaleX) * 0.5f;
                result.offsetY = (physicalHeight - virtualHeight_ * result.scaleY) * 0.5f;
                break;
            }
            case CnaPresentationMode::Overscan:
            {
                result.scaleX = result.scaleY = std::max(sx, sy);
                result.offsetX = (physicalWidth - virtualWidth_ * result.scaleX) * 0.5f;
                result.offsetY = (physicalHeight - virtualHeight_ * result.scaleY) * 0.5f;
                break;
            }
            case CnaPresentationMode::Stretch:
                result.scaleX = sx;
                result.scaleY = sy;
                break;
            case CnaPresentationMode::NativeBackBuffer:
                break;
            case CnaPresentationMode::FixedHeightDynamicWidth:
                break;
        }
        return result;
    }

    D2D1_MATRIX_3X2_F Direct2DGraphicsBackend::PresentationMatrix() const
    {
        const PresentationTransform transform = GetPresentationTransform();
        return D2D1::Matrix3x2F(transform.scaleX, 0.0f, 0.0f, transform.scaleY,
                                 transform.offsetX, transform.offsetY);
    }

    void Direct2DGraphicsBackend::ApplyScissorClip()
    {
        if (!scissorActive_ || scissorPushed_) return;
        const D2D1_MATRIX_3X2_F presentation = PresentationMatrix();
        d2dContext_->SetTransform(presentation);
        d2dContext_->PushAxisAlignedClip(
            D2D1::RectF(static_cast<float>(scissorRect_.X), static_cast<float>(scissorRect_.Y),
                         static_cast<float>(scissorRect_.X + scissorRect_.Width),
                         static_cast<float>(scissorRect_.Y + scissorRect_.Height)),
            D2D1_ANTIALIAS_MODE_ALIASED);
        d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
        scissorPushed_ = true;
    }

    void Direct2DGraphicsBackend::ClearScissorClip()
    {
        if (!scissorPushed_) return;
        d2dContext_->PopAxisAlignedClip();
        scissorPushed_ = false;
    }

    void Direct2DGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        EnsureDrawing();
        // XNA's GraphicsDevice.Clear always affects the entire active target; it must not inherit
        // a RasterizerState scissor that happened to be active for preceding SpriteBatch work.
        ClearScissorClip();
        d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
        d2dContext_->Clear(D2D1::ColorF(r, g, b, a));
        ApplyScissorClip();
    }

    void Direct2DGraphicsBackend::Present()
    {
        EndDrawing("ID2D1DeviceContext::EndDraw");
        ThrowIfFailed(swapChain_->Present(static_cast<UINT>(swapInterval_), 0), "IDXGISwapChain::Present");
    }

    void Direct2DGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        const PresentationTransform transform = GetPresentationTransform();
        width = transform.logicalWidth;
        height = transform.logicalHeight;
    }

    void Direct2DGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void Direct2DGraphicsBackend::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void Direct2DGraphicsBackend::SetSwapInterval(int interval)
    {
        swapInterval_ = std::clamp(interval, 0, 4);
    }

    bool Direct2DGraphicsBackend::TransformWindowToLogical(float windowX, float windowY,
                                                            float& logicalX, float& logicalY) const
    {
        if (activeRenderTarget_) return false;
        const PresentationTransform transform = GetPresentationTransform();
        if (transform.scaleX == 0.0f || transform.scaleY == 0.0f) return false;
        logicalX = (windowX - transform.offsetX) / transform.scaleX;
        logicalY = (windowY - transform.offsetY) / transform.scaleY;
        return true;
    }

    bool Direct2DGraphicsBackend::TransformLogicalToWindow(float logicalX, float logicalY,
                                                            float& windowX, float& windowY) const
    {
        if (activeRenderTarget_) return false;
        const PresentationTransform transform = GetPresentationTransform();
        windowX = logicalX * transform.scaleX + transform.offsetX;
        windowY = logicalY * transform.scaleY + transform.offsetY;
        return true;
    }

    void Direct2DGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (!pixels) throw std::runtime_error("Direct2D ReadBackbuffer received null pixels.");
        if (activeRenderTarget_)
            throw System::NotSupportedException(
                "Direct2D ReadBackbuffer cannot read an active RenderTarget2D; unbind it and use the back buffer.");
        const PresentationTransform presentation = GetPresentationTransform();
        if (presentation.scaleX != 1.0f || presentation.scaleY != 1.0f ||
            presentation.offsetX != 0.0f || presentation.offsetY != 0.0f)
        {
            throw System::NotSupportedException(
                "Direct2D exact back-buffer readback requires NativeBackBuffer/1:1 presentation.");
        }
        if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
            x + w > static_cast<int>(backBufferTarget_->GetPixelSize().width) ||
            y + h > static_cast<int>(backBufferTarget_->GetPixelSize().height))
            throw System::ArgumentOutOfRangeException("rect", "invalid", "The requested back-buffer rectangle is outside bounds.");

        if (drawing_)
            ThrowIfFailed(d2dContext_->Flush(), "ID2D1DeviceContext::Flush before readback");
        d3dContext_->Flush();

        D3D11_TEXTURE2D_DESC sourceDescription{};
        backBufferTexture_->GetDesc(&sourceDescription);
        D3D11_TEXTURE2D_DESC stagingDescription = sourceDescription;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.BindFlags = 0;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDescription.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        ThrowIfFailed(d3dDevice_->CreateTexture2D(&stagingDescription, nullptr, &staging),
                      "ID3D11Device::CreateTexture2D(readback staging)");
        d3dContext_->CopyResource(staging.Get(), backBufferTexture_.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        ThrowIfFailed(d3dContext_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped),
                      "ID3D11DeviceContext::Map(readback staging)");
        for (int row = 0; row < h; ++row)
        {
            const uint8_t* source = static_cast<const uint8_t*>(mapped.pData) +
                                    static_cast<std::size_t>(y + row) * mapped.RowPitch + x * 4u;
            uint8_t* destination = pixels + static_cast<std::size_t>(row) * w * 4u;
            for (int column = 0; column < w; ++column)
            {
                destination[column * 4 + 0] = source[column * 4 + 2];
                destination[column * 4 + 1] = source[column * 4 + 1];
                destination[column * 4 + 2] = source[column * 4 + 0];
                destination[column * 4 + 3] = source[column * 4 + 3];
            }
        }
        d3dContext_->Unmap(staging.Get(), 0);
    }

    std::unique_ptr<ITextureBackend> Direct2DGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<Direct2DTextureBackend>(*this, data);
    }

    std::unique_ptr<ISpriteBatchBackend> Direct2DGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<Direct2DSpriteBatchBackend>(*this);
    }

    std::unique_ptr<IRenderTargetBackend> Direct2DGraphicsBackend::CreateRenderTarget2D(
        int width, int height, int /*depthFormat*/, bool /*preserveContents*/, bool /*mipMap*/, int /*multiSampleCount*/)
    {
        return std::make_unique<Direct2DRenderTargetBackend>(*this, width, height);
    }

    void Direct2DGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* renderTarget)
    {
        if (renderTarget && !dynamic_cast<Direct2DRenderTargetBackend*>(renderTarget))
            throw std::runtime_error("Direct2D cannot bind a render target created by another graphics backend.");
        BindRenderTarget(static_cast<Direct2DRenderTargetBackend*>(renderTarget));
    }

    void Direct2DGraphicsBackend::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (count > 1)
            throw std::runtime_error("Direct2D supports exactly one active 2D render target; MRT is unsupported.");
        if (count > 0 && renderTargets[0].IsRenderTargetCubeFace())
            throw std::runtime_error("Direct2D does not support RenderTargetCube bindings.");
        SetRenderTarget2D(count > 0 ? renderTargets[0].GetRenderTarget2D() : nullptr);
    }

    void Direct2DGraphicsBackend::BindRenderTarget(Direct2DRenderTargetBackend* renderTarget)
    {
        if (activeRenderTarget_ == renderTarget) return;
        EndDrawing("render-target switch");
        activeRenderTarget_ = renderTarget;
        d2dContext_->SetTarget(renderTarget ? renderTarget->Bitmap() : backBufferTarget_.Get());
    }

    void Direct2DGraphicsBackend::ReleaseRenderTarget(Direct2DRenderTargetBackend* renderTarget)
    {
        if (activeRenderTarget_ == renderTarget) BindRenderTarget(nullptr);
    }

    void Direct2DGraphicsBackend::SetScissorRect(int x, int y, int width, int height)
    {
        if (drawing_) ClearScissorClip();
        scissorActive_ = width > 0 && height > 0;
        scissorRect_ = Rectangle(x, y, width, height);
        if (drawing_) ApplyScissorClip();
    }

    void Direct2DGraphicsBackend::ApplyBlendState(
        int colorSrcBlend, int alphaSrcBlend, int colorDstBlend, int alphaDstBlend,
        int colorBlendFunc, int alphaBlendFunc, const BlendWriteState& /*writeState*/)
    {
        blendMode_ = BlendStateToDirect2DBlendMode(colorSrcBlend, alphaSrcBlend, colorDstBlend,
                                                   alphaDstBlend, colorBlendFunc, alphaBlendFunc);
        // NonPremultiplied differs from AlphaBlend only in the source pixel convention.  A
        // transient CPU-generated premultiplied bitmap is used for it on each affected draw.
        nonPremultipliedSource_ = colorSrcBlend == 4 && colorDstBlend == 5;
        if (drawing_) d2dContext_->SetPrimitiveBlend(ToPrimitiveBlend(blendMode_));
    }

    int Direct2DGraphicsBackend::ApplyMultiSampleCount(int requestedMultiSampleCount)
    {
        if (requestedMultiSampleCount > 1)
            SDL_Log("[Direct2D] MultiSampleCount=%d requested but Direct2D's swap chain uses one sample.",
                    requestedMultiSampleCount);
        return 0;
    }

    ID2D1Bitmap1* Direct2DGraphicsBackend::CreateBitmapFromRgba(const uint8_t* rgba, int width, int height,
                                                                 bool ignoreAlpha) const
    {
        if (!rgba || width <= 0 || height <= 0)
            throw std::runtime_error("Direct2D CreateBitmapFromRgba requires non-empty RGBA pixels.");
        std::vector<uint8_t> bgra(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const uint8_t* source = rgba + (static_cast<std::size_t>(y) * width + x) * 4u;
                uint8_t* destination = bgra.data() + (static_cast<std::size_t>(y) * width + x) * 4u;
                destination[0] = source[2];
                destination[1] = source[1];
                destination[2] = source[0];
                destination[3] = source[3];
            }
        }
        D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                               ignoreAlpha ? D2D1_ALPHA_MODE_IGNORE : D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);
        ComPtr<ID2D1Bitmap1> bitmap;
        ThrowIfFailed(d2dContext_->CreateBitmap(
                          D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)),
                          bgra.data(), static_cast<UINT32>(width * 4), &properties, &bitmap),
                      "ID2D1DeviceContext::CreateBitmap");
        return bitmap.Detach();
    }

    D2D1_MATRIX_3X2_F Direct2DGraphicsBackend::ToD2DMatrix(const Matrix& matrix)
    {
        // Both XNA Matrix and Direct2D use row-vector affine form for 2D points.
        return D2D1::Matrix3x2F(matrix.M11, matrix.M12, matrix.M21, matrix.M22, matrix.M41, matrix.M42);
    }

    D2D1_MATRIX_3X2_F Direct2DGraphicsBackend::Multiply(const D2D1_MATRIX_3X2_F& left,
                                                         const D2D1_MATRIX_3X2_F& right)
    {
        return D2D1::Matrix3x2F(
            left._11 * right._11 + left._12 * right._21,
            left._11 * right._12 + left._12 * right._22,
            left._21 * right._11 + left._22 * right._21,
            left._21 * right._12 + left._22 * right._22,
            left._31 * right._11 + left._32 * right._21 + right._31,
            left._31 * right._12 + left._32 * right._22 + right._32);
    }

    std::vector<uint8_t> Direct2DGraphicsBackend::MakeSpritePixels(
        const Direct2DTextureBackend& texture, const Rectangle& sourceRectangle,
        const Color& color, SpriteEffects effects, int addressU, int addressV) const
    {
        const int sourceWidth = sourceRectangle.Width;
        const int sourceHeight = sourceRectangle.Height;
        std::vector<uint8_t> result(static_cast<std::size_t>(sourceWidth) * sourceHeight * 4u, 0);
        const bool flipH = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0;
        const bool flipV = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0;
        const auto sampleCoordinate = [](int coordinate, int size, int addressMode) -> int
        {
            if (coordinate >= 0 && coordinate < size) return coordinate;
            if (addressMode == 0) return PositiveModulo(coordinate, size); // Wrap
            if (addressMode == 2) return MirrorCoordinate(coordinate, size); // Mirror
            return -1; // Clamp here means no source pixel outside the requested source rectangle.
        };
        const std::vector<uint8_t>& pixels = texture.RgbaPixels();
        for (int dy = 0; dy < sourceHeight; ++dy)
        {
            for (int dx = 0; dx < sourceWidth; ++dx)
            {
                const int unflippedX = flipH ? sourceWidth - 1 - dx : dx;
                const int unflippedY = flipV ? sourceHeight - 1 - dy : dy;
                const int sx = sampleCoordinate(sourceRectangle.X + unflippedX, texture.GetWidth(), addressU);
                const int sy = sampleCoordinate(sourceRectangle.Y + unflippedY, texture.GetHeight(), addressV);
                if (sx < 0 || sy < 0) continue;
                const uint8_t* input = pixels.data() + (static_cast<std::size_t>(sy) * texture.GetWidth() + sx) * 4u;
                const int alpha = input[3];
                const int sourceR = nonPremultipliedSource_ ? input[0] * alpha / 255 : input[0];
                const int sourceG = nonPremultipliedSource_ ? input[1] * alpha / 255 : input[1];
                const int sourceB = nonPremultipliedSource_ ? input[2] * alpha / 255 : input[2];
                uint8_t* output = result.data() + (static_cast<std::size_t>(dy) * sourceWidth + dx) * 4u;
                // Direct2D source-over/add pixels are premultiplied, so Color.A scales both source
                // alpha and RGB. Copy/Opaque instead uses an alpha-ignore bitmap: XNA's source
                // factor is One there, therefore its RGB must not be attenuated by Color.A.
                const int colourAlpha = blendMode_ == Direct2DBlendMode::Copy ? 255 : color.getAProperty();
                output[0] = static_cast<uint8_t>(sourceR * color.getRProperty() * colourAlpha / (255 * 255));
                output[1] = static_cast<uint8_t>(sourceG * color.getGProperty() * colourAlpha / (255 * 255));
                output[2] = static_cast<uint8_t>(sourceB * color.getBProperty() * colourAlpha / (255 * 255));
                output[3] = static_cast<uint8_t>(alpha * color.getAProperty() / 255);
            }
        }
        return result;
    }

    void Direct2DGraphicsBackend::DrawSprite(
        const ITextureBackend& texture, const Rectangle& destinationRectangle,
        const Rectangle& sourceRectangle, const Color& color, float rotation, const Vector2& origin,
        SpriteEffects effects, const Matrix& batchTransform, bool linearFilter, int addressU, int addressV)
    {
        if (sourceRectangle.Width <= 0 || sourceRectangle.Height <= 0 ||
            destinationRectangle.Width == 0 || destinationRectangle.Height == 0)
            return;
        EnsureDrawing();

        const auto* ordinaryTexture = dynamic_cast<const Direct2DTextureBackend*>(&texture);
        const auto* renderTargetTexture = dynamic_cast<const Direct2DRenderTargetBackend*>(&texture);
        if (!ordinaryTexture && !renderTargetTexture)
            throw std::runtime_error("Direct2D SpriteBatch received a texture created by another graphics backend.");
        const bool sourceOutside = sourceRectangle.X < 0 || sourceRectangle.Y < 0 ||
            sourceRectangle.X + sourceRectangle.Width > texture.GetWidth() ||
            sourceRectangle.Y + sourceRectangle.Height > texture.GetHeight();
        const bool requiresCpuBitmap = ordinaryTexture &&
            (!IsWhite(color) || IsFlipped(effects) || nonPremultipliedSource_ || sourceOutside ||
             addressU != 1 || addressV != 1);
        if (!ordinaryTexture && (!IsWhite(color) || IsFlipped(effects) || nonPremultipliedSource_ || sourceOutside ||
                                 addressU != 1 || addressV != 1))
        {
            throw System::NotSupportedException(
                "Direct2D cannot tint, flip, reinterpret alpha, or wrap/mirror a RenderTarget2D source; "
                "those operations require a CPU texture shadow which rendered targets intentionally do not have.");
        }

        ComPtr<ID2D1Bitmap1> transient;
        ID2D1Bitmap1* bitmap = ordinaryTexture ? ordinaryTexture->Bitmap() : renderTargetTexture->Bitmap();
        Rectangle localSource = sourceRectangle;
        if (requiresCpuBitmap)
        {
            const std::vector<uint8_t> pixels = MakeSpritePixels(*ordinaryTexture, sourceRectangle, color,
                                                                  effects, addressU, addressV);
            transient.Attach(CreateBitmapFromRgba(pixels.data(), sourceRectangle.Width, sourceRectangle.Height,
                                                   blendMode_ == Direct2DBlendMode::Copy));
            bitmap = transient.Get();
            localSource = Rectangle(0, 0, sourceRectangle.Width, sourceRectangle.Height);
            transientBitmaps_.push_back(transient);
        }

        const float scaleX = static_cast<float>(destinationRectangle.Width) / sourceRectangle.Width;
        const float scaleY = static_cast<float>(destinationRectangle.Height) / sourceRectangle.Height;
        const float cosine = std::cos(rotation);
        const float sine = std::sin(rotation);
        const D2D1_MATRIX_3X2_F scale = D2D1::Matrix3x2F(scaleX, 0.0f, 0.0f, scaleY, 0.0f, 0.0f);
        const D2D1_MATRIX_3X2_F rotate = D2D1::Matrix3x2F(cosine, sine, -sine, cosine, 0.0f, 0.0f);
        const D2D1_MATRIX_3X2_F translate = D2D1::Matrix3x2F::Translation(
            static_cast<float>(destinationRectangle.X), static_cast<float>(destinationRectangle.Y));
        const D2D1_MATRIX_3X2_F spriteTransform = Multiply(Multiply(scale, rotate), translate);
        const D2D1_MATRIX_3X2_F batch = ToD2DMatrix(batchTransform);
        const D2D1_MATRIX_3X2_F finalTransform = Multiply(Multiply(spriteTransform, batch), PresentationMatrix());
        d2dContext_->SetTransform(finalTransform);
        const D2D1_RECT_F destination = D2D1::RectF(-origin.X, -origin.Y,
                                                     static_cast<float>(sourceRectangle.Width) - origin.X,
                                                     static_cast<float>(sourceRectangle.Height) - origin.Y);
        const D2D1_RECT_F source = D2D1::RectF(static_cast<float>(localSource.X), static_cast<float>(localSource.Y),
                                                static_cast<float>(localSource.X + localSource.Width),
                                                static_cast<float>(localSource.Y + localSource.Height));
        d2dContext_->DrawBitmap(bitmap, &destination, 1.0f,
                                linearFilter ? D2D1_INTERPOLATION_MODE_LINEAR : D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                                &source);
        d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
    }

    void Direct2DGraphicsBackend::ClearColorAndDepth(float, float, float, float, float) { ThrowNo3D("ClearColorAndDepth"); }
    void Direct2DGraphicsBackend::ClearDepth(float) { ThrowNo3D("ClearDepth"); }
    void Direct2DGraphicsBackend::ClearStencil(int) { ThrowNo3D("ClearStencil"); }
    void Direct2DGraphicsBackend::ClearDepthAndStencil(float, int) { ThrowNo3D("ClearDepthAndStencil"); }
    void Direct2DGraphicsBackend::ClearColorAndStencil(float, float, float, float, int) { ThrowNo3D("ClearColorAndStencil"); }
    void Direct2DGraphicsBackend::ClearColorDepthAndStencil(float, float, float, float, float, int) { ThrowNo3D("ClearColorDepthAndStencil"); }
    void Direct2DGraphicsBackend::SetDepthTestEnabled(bool) { ThrowNo3D("SetDepthTestEnabled"); }
    void Direct2DGraphicsBackend::SetBlendEnabled(bool) { ThrowNo3D("SetBlendEnabled"); }
    void Direct2DGraphicsBackend::SetDepthWriteEnabled(bool) { ThrowNo3D("SetDepthWriteEnabled"); }
    std::unique_ptr<IVertexBufferBackend> Direct2DGraphicsBackend::CreateVertexBuffer(int) { ThrowNo3D("CreateVertexBuffer"); }
    std::unique_ptr<IIndexBufferBackend> Direct2DGraphicsBackend::CreateIndexBuffer16(int) { ThrowNo3D("CreateIndexBuffer16"); }
    std::unique_ptr<IOcclusionQueryBackend> Direct2DGraphicsBackend::CreateOcclusionQuery() { ThrowNo3D("CreateOcclusionQuery"); }
    void Direct2DGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&, const Matrix&, const Matrix&,
                                                        const Matrix&, PrimitiveType, int) { ThrowNo3D("DrawColoredPrimitives"); }
    void Direct2DGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&, const IIndexBufferBackend&,
                                                               const Matrix&, const Matrix&, const Matrix&,
                                                               PrimitiveType, int) { ThrowNo3D("DrawIndexedColoredPrimitives"); }
}

namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_DIRECT2D
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Direct2D::Direct2DGraphicsBackend>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode, args.swapInterval);
    }
#endif
}
