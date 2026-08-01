// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/Direct2D/Direct2DGraphicsBackend.hpp"

#include <d2d1effects.h>

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

        [[nodiscard]] D2D1_COMPOSITE_MODE ToImageCompositeMode(Direct2DBlendMode blendMode)
        {
            // DrawImage's explicit composite mode avoids depending on the mutable primitive-blend
            // state. It is also the only Direct2D image API that directly expresses both CNA
            // SpriteBatch operations absent from ordinary source-over: Additive and Opaque/Copy.
            switch (blendMode)
            {
                case Direct2DBlendMode::Copy: return D2D1_COMPOSITE_MODE_SOURCE_COPY;
                case Direct2DBlendMode::Add: return D2D1_COMPOSITE_MODE_PLUS;
                case Direct2DBlendMode::SourceOver: return D2D1_COMPOSITE_MODE_SOURCE_OVER;
            }
            return D2D1_COMPOSITE_MODE_SOURCE_OVER;
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

        [[nodiscard]] D2D1_EXTEND_MODE ToD2DExtendMode(int addressMode)
        {
            switch (addressMode)
            {
                case 0: return D2D1_EXTEND_MODE_WRAP;
                case 1: return D2D1_EXTEND_MODE_CLAMP;
                case 2: return D2D1_EXTEND_MODE_MIRROR;
            }
            throw std::runtime_error("Direct2D received an unknown TextureAddressMode value.");
        }

        [[nodiscard]] D2D1_MATRIX_3X2_F MakeSpriteBrushTransform(int sourceWidth, int sourceHeight,
                                                                   SpriteEffects effects)
        {
            const bool flipH = (static_cast<int>(effects) &
                                static_cast<int>(SpriteEffects::FlipHorizontally)) != 0;
            const bool flipV = (static_cast<int>(effects) &
                                static_cast<int>(SpriteEffects::FlipVertically)) != 0;
            return D2D1::Matrix3x2F(flipH ? -1.0f : 1.0f, 0.0f,
                                    0.0f, flipV ? -1.0f : 1.0f,
                                    flipH ? static_cast<float>(sourceWidth) : 0.0f,
                                    flipV ? static_cast<float>(sourceHeight) : 0.0f);
        }

        [[nodiscard]] D2D1_MATRIX_5X4_F MakeSpriteTintMatrix(const Color& color, bool ignoreColorAlpha)
        {
            const float r = static_cast<float>(color.getRProperty()) / 255.0f;
            const float g = static_cast<float>(color.getGProperty()) / 255.0f;
            const float b = static_cast<float>(color.getBProperty()) / 255.0f;
            const float a = ignoreColorAlpha ? 1.0f : static_cast<float>(color.getAProperty()) / 255.0f;
            return D2D1::Matrix5x4F(r, 0.0f, 0.0f, 0.0f,
                                    0.0f, g, 0.0f, 0.0f,
                                    0.0f, 0.0f, b, 0.0f,
                                    0.0f, 0.0f, 0.0f, a,
                                    0.0f, 0.0f, 0.0f, 0.0f);
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
        const int mipCount = std::max(1, data.mipLevels);
        mipBitmaps_.resize(static_cast<std::size_t>(mipCount - 1));
        mipRgbaPixels_.resize(static_cast<std::size_t>(mipCount - 1));
        RecreateBitmap();
        owner_->RegisterTexture(this);
    }

    Direct2DTextureBackend::~Direct2DTextureBackend()
    {
        if (owner_) owner_->UnregisterTexture(this);
    }

    void Direct2DTextureBackend::RecreateBitmap()
    {
        bitmap_.Attach(owner_->CreateBitmapFromRgba(rgbaPixels_.data(), width_, height_));
        for (std::size_t index = 0; index < mipBitmaps_.size(); ++index)
        {
            mipBitmaps_[index].Reset();
            const std::vector<uint8_t>& pixels = mipRgbaPixels_[index];
            if (pixels.empty()) continue;
            const int level = static_cast<int>(index) + 1;
            const int levelWidth = std::max(1, width_ >> level);
            const int levelHeight = std::max(1, height_ >> level);
            mipBitmaps_[index].Attach(owner_->CreateBitmapFromRgba(pixels.data(), levelWidth, levelHeight));
        }
        deviceGeneration_ = owner_->deviceGeneration_;
    }

    ID2D1Bitmap1* Direct2DTextureBackend::Bitmap() const
    {
        owner_->EnsureResourceGeneration(deviceGeneration_, "Texture2D");
        return bitmap_.Get();
    }

    ID2D1Bitmap1* Direct2DTextureBackend::BitmapForLevel(int level) const
    {
        owner_->EnsureResourceGeneration(deviceGeneration_, "Texture2D");
        if (level < 0 || level > static_cast<int>(mipBitmaps_.size()))
            throw System::ArgumentOutOfRangeException("level", std::to_string(level),
                                                       "The requested Direct2D texture mip level does not exist.");
        if (level == 0) return bitmap_.Get();
        ID2D1Bitmap1* const result = mipBitmaps_[static_cast<std::size_t>(level - 1)].Get();
        if (!result)
        {
            throw std::runtime_error(
                "Direct2D Texture2D mip level has not been initialized with Texture2D::SetData(level, ...).");
        }
        return result;
    }

    int Direct2DTextureBackend::SelectAvailableMipLevel(int preferredLevel) const
    {
        owner_->EnsureResourceGeneration(deviceGeneration_, "Texture2D");
        preferredLevel = std::clamp(preferredLevel, 0, static_cast<int>(mipBitmaps_.size()));
        while (preferredLevel > 0 && !mipBitmaps_[static_cast<std::size_t>(preferredLevel - 1)])
            --preferredLevel;
        return preferredLevel;
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
        if (!rgba) throw std::runtime_error("Direct2DTextureBackend::UpdatePixelsLevel received null pixels.");
        if (level < 0 || level > static_cast<int>(mipBitmaps_.size()))
            throw System::ArgumentOutOfRangeException("level", std::to_string(level),
                                                       "The requested Direct2D texture mip level does not exist.");
        const int expectedWidth = std::max(1, width_ >> level);
        const int expectedHeight = std::max(1, height_ >> level);
        if (levelW != expectedWidth || levelH != expectedHeight)
            throw System::ArgumentOutOfRangeException(
                "level dimensions", std::to_string(levelW) + "x" + std::to_string(levelH),
                "The Direct2D texture mip dimensions do not match the requested level.");
        if (level == 0)
        {
            UpdatePixels(rgba, expectedWidth * 4);
            return;
        }
        std::vector<uint8_t>& pixels = mipRgbaPixels_[static_cast<std::size_t>(level - 1)];
        pixels.assign(rgba, rgba + static_cast<std::size_t>(expectedWidth) * expectedHeight * 4u);
        mipBitmaps_[static_cast<std::size_t>(level - 1)].Attach(
            owner_->CreateBitmapFromRgba(pixels.data(), expectedWidth, expectedHeight));
        deviceGeneration_ = owner_->deviceGeneration_;
    }

    Direct2DRenderTargetBackend::Direct2DRenderTargetBackend(
        Direct2DGraphicsBackend& owner, int width, int height, bool mipMap)
        : owner_(&owner), width_(width), height_(height), mipMap_(mipMap)
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::runtime_error("Direct2D render-target dimensions must be positive.");
        if (mipMap_)
        {
            int levelWidth = width_;
            int levelHeight = height_;
            while (levelWidth > 1 || levelHeight > 1)
            {
                levelWidth = std::max(1, levelWidth / 2);
                levelHeight = std::max(1, levelHeight / 2);
                mipBitmaps_.emplace_back();
            }
        }
        RecreateBitmap();
        owner_->RegisterRenderTarget(this);
    }

    void Direct2DRenderTargetBackend::RecreateBitmap()
    {
        D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);
        // RenderTarget2D deliberately has no CPU shadow. A recovered target's old contents are
        // discarded; initialize its replacement (and every generated-mip destination) as
        // transparent black so the resulting chain is deterministic until the first draw.
        const auto createTargetBitmap = [&](int bitmapWidth, int bitmapHeight,
                                            Microsoft::WRL::ComPtr<ID2D1Bitmap1>& output) {
            std::vector<uint8_t> transparentPixels(
                static_cast<std::size_t>(bitmapWidth) * static_cast<std::size_t>(bitmapHeight) * 4u, 0u);
            ThrowIfFailed(owner_->d2dContext_->CreateBitmap(
                              D2D1::SizeU(static_cast<UINT32>(bitmapWidth),
                                          static_cast<UINT32>(bitmapHeight)),
                              transparentPixels.data(), static_cast<UINT32>(bitmapWidth * 4),
                              &properties, &output),
                          "CreateBitmap(render target)");
        };
        createTargetBitmap(width_, height_, bitmap_);
        int levelWidth = width_;
        int levelHeight = height_;
        for (Microsoft::WRL::ComPtr<ID2D1Bitmap1>& mipBitmap : mipBitmaps_)
        {
            levelWidth = std::max(1, levelWidth / 2);
            levelHeight = std::max(1, levelHeight / 2);
            mipBitmap.Reset();
            createTargetBitmap(levelWidth, levelHeight, mipBitmap);
        }
        mipLevelsDirty_ = false;
        deviceGeneration_ = owner_->deviceGeneration_;
    }

    Direct2DRenderTargetBackend::~Direct2DRenderTargetBackend()
    {
        if (owner_) owner_->UnregisterRenderTarget(this);
        if (owner_) owner_->ReleaseRenderTarget(this);
    }

    ID2D1Bitmap1* Direct2DRenderTargetBackend::Bitmap() const
    {
        owner_->EnsureResourceGeneration(deviceGeneration_, "RenderTarget2D");
        return bitmap_.Get();
    }

    ID2D1Bitmap1* Direct2DRenderTargetBackend::BitmapForLevel(int level) const
    {
        owner_->EnsureResourceGeneration(deviceGeneration_, "RenderTarget2D");
        if (level < 0 || level > static_cast<int>(mipBitmaps_.size()))
        {
            throw System::ArgumentOutOfRangeException(
                "level", std::to_string(level), "The requested Direct2D render-target mip level does not exist.");
        }
        if (level == 0) return bitmap_.Get();
        const_cast<Direct2DRenderTargetBackend*>(this)->EnsureMipLevelsCurrent();
        return mipBitmaps_[static_cast<std::size_t>(level - 1)].Get();
    }

    int Direct2DRenderTargetBackend::SelectAvailableMipLevel(int preferredLevel) const
    {
        owner_->EnsureResourceGeneration(deviceGeneration_, "RenderTarget2D");
        preferredLevel = std::clamp(preferredLevel, 0, static_cast<int>(mipBitmaps_.size()));
        if (preferredLevel > 0)
            const_cast<Direct2DRenderTargetBackend*>(this)->EnsureMipLevelsCurrent();
        return preferredLevel;
    }

    void Direct2DRenderTargetBackend::MarkMipLevelsDirty()
    {
        if (!mipBitmaps_.empty()) mipLevelsDirty_ = true;
    }

    void Direct2DRenderTargetBackend::EnsureMipLevelsCurrent()
    {
        if (mipLevelsDirty_) owner_->GenerateRenderTargetMipLevels(*this);
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
        ThrowIfFailed(Bitmap()->CopyFromMemory(nullptr, bgra.data(), static_cast<UINT32>(width_ * 4)),
                      "ID2D1Bitmap::CopyFromMemory(render target)");
        MarkMipLevelsDirty();
    }

    bool Direct2DRenderTargetBackend::GetData(int level, int x, int y, int w, int h,
                                               void* data, int dataLength) const
    {
        if (level < 0)
            throw System::ArgumentOutOfRangeException("level", std::to_string(level),
                                                       "level must not be negative.");
        if (level > static_cast<int>(mipBitmaps_.size()))
        {
            throw System::NotSupportedException(
                "Direct2D RenderTarget2D has " + std::to_string(mipBitmaps_.size() + 1) +
                " mip level(s); level " + std::to_string(level) + " was requested.");
        }
        const int levelWidth = std::max(1, width_ >> level);
        const int levelHeight = std::max(1, height_ >> level);
        const std::int64_t right = static_cast<std::int64_t>(x) + w;
        const std::int64_t bottom = static_cast<std::int64_t>(y) + h;
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || right > levelWidth || bottom > levelHeight)
            throw System::ArgumentOutOfRangeException("rect", "invalid", "The requested rectangle leaves the render target.");
        if (!data || static_cast<std::int64_t>(dataLength) < static_cast<std::int64_t>(w) * h * 4)
            throw System::ArgumentOutOfRangeException("dataLength", std::to_string(dataLength),
                                                       "The destination is too small for the requested RGBA pixels.");
        owner_->ReadRenderTargetPixels(*this, level, x, y, w, h, static_cast<uint8_t*>(data));
        return true;
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
        CnaPresentationMode presentationMode, int swapInterval, bool contextRecoveryEnabled)
        : window_(window), virtualWidth_(virtualWidth), virtualHeight_(virtualHeight),
          presentationMode_(presentationMode), swapInterval_(std::clamp(swapInterval, 0, 4)),
          contextRecoveryEnabled_(contextRecoveryEnabled)
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
            ClearOutputClips();
            d2dContext_->EndDraw();
        }
        IGraphicsBackend::UnregisterForWindow(window_);
    }

    void Direct2DGraphicsBackend::RegisterTexture(Direct2DTextureBackend* texture)
    {
        if (contextRecoveryEnabled_ && texture)
            recoverableTextures_.push_back(texture);
    }

    void Direct2DGraphicsBackend::UnregisterTexture(Direct2DTextureBackend* texture)
    {
        std::erase(recoverableTextures_, texture);
    }

    void Direct2DGraphicsBackend::RegisterRenderTarget(Direct2DRenderTargetBackend* renderTarget)
    {
        if (contextRecoveryEnabled_ && renderTarget)
            recoverableRenderTargets_.push_back(renderTarget);
    }

    void Direct2DGraphicsBackend::UnregisterRenderTarget(Direct2DRenderTargetBackend* renderTarget)
    {
        std::erase(recoverableRenderTargets_, renderTarget);
    }

    bool Direct2DGraphicsBackend::IsRegisteredRenderTarget(const Direct2DRenderTargetBackend* renderTarget) const
    {
        return std::find(recoverableRenderTargets_.begin(), recoverableRenderTargets_.end(), renderTarget) !=
               recoverableRenderTargets_.end();
    }

    void Direct2DGraphicsBackend::EnsureResourceGeneration(std::uint64_t generation,
                                                            const char* resourceKind) const
    {
        if (generation == deviceGeneration_) return;
        throw std::runtime_error(
            std::string("Direct2D ") + resourceKind +
            " belongs to a lost device and was not registered for context recovery; recreate it after "
            "GraphicsDevice::SetContextRecoveryEnabled(false).");
    }

    void Direct2DGraphicsBackend::ReleaseDeviceResourcesNoThrow()
    {
        if (drawing_ && d2dContext_)
        {
            ClearOutputClips();
            d2dContext_->EndDraw();
        }
        drawing_ = false;
        viewportPushed_ = false;
        scissorPushed_ = false;
        transientBitmaps_.clear();
        transientEffects_.clear();
        transientImages_.clear();
        transientImageBrushes_.clear();
        activeRenderTarget_ = nullptr;
        if (d2dContext_) d2dContext_->SetTarget(nullptr);
        backBufferTarget_.Reset();
        backBufferTexture_.Reset();
        swapChain_.Reset();
        d2dContext_.Reset();
        d2dFactory_.Reset();
        d3dContext_.Reset();
        d3dDevice_.Reset();
    }

    void Direct2DGraphicsBackend::RecreateDeviceResourcesForRecovery()
    {
        // Snapshot active-target identity and registries before releasing the old Direct2D device.
        // Only resources that existed while recovery was enabled participate; later resources must
        // not retain stale COM objects silently after the reset.
        Direct2DRenderTargetBackend* previousActive = activeRenderTarget_;
        const bool restorePreviousActive = IsRegisteredRenderTarget(previousActive);
        const auto textures = recoverableTextures_;
        const auto renderTargets = recoverableRenderTargets_;

        ReleaseDeviceResourcesNoThrow();
        ++deviceGeneration_;
        CreateDeviceResources();

        for (Direct2DTextureBackend* texture : textures)
            if (texture) texture->RecreateBitmap();
        for (Direct2DRenderTargetBackend* renderTarget : renderTargets)
            if (renderTarget) renderTarget->RecreateBitmap();

        if (restorePreviousActive && previousActive)
        {
            activeRenderTarget_ = previousActive;
            d2dContext_->SetTarget(previousActive->Bitmap());
        }
    }

    void Direct2DGraphicsBackend::DebugSimulateContextLoss()
    {
        RecreateDeviceResourcesForRecovery();
    }

    void Direct2DGraphicsBackend::DebugRestoreContext()
    {
        // Direct2D's debug channel recreates a complete D3D11/DXGI/D2D resource domain atomically,
        // just like the desktop EasyGL path; there is no separately exposed lost-only state.
        RecreateDeviceResourcesForRecovery();
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
        ApplyOutputClips();
    }

    void Direct2DGraphicsBackend::EndDrawing(const char* operation)
    {
        if (!drawing_) return;
        ClearOutputClips();
        const HRESULT hr = d2dContext_->EndDraw();
        drawing_ = false;
        transientBitmaps_.clear();
        transientEffects_.clear();
        transientImages_.clear();
        transientImageBrushes_.clear();
        if (hr == D2DERR_RECREATE_TARGET)
        {
            RecreateDeviceResourcesForRecovery();
            throw std::runtime_error(
                "Direct2D device resources were recreated after D2DERR_RECREATE_TARGET; redraw the current frame.");
        }
        ThrowIfFailed(hr, operation);
    }

    void Direct2DGraphicsBackend::GenerateRenderTargetMipLevels(Direct2DRenderTargetBackend& renderTarget)
    {
        EnsureResourceGeneration(renderTarget.deviceGeneration_, "RenderTarget2D");
        if (!renderTarget.mipLevelsDirty_ || renderTarget.mipBitmaps_.empty()) return;

        // A target bitmap can be drawn into and sampled from the same Direct2D device context,
        // but not in the same BeginDraw/EndDraw recording interval. Finish the caller's work,
        // then downsample one level at a time into independently targetable lower-level bitmaps.
        // No CPU readback/shadow is involved: this is a GPU-only Direct2D image copy.
        EndDrawing("render-target mip generation source");
        ID2D1Bitmap1* source = renderTarget.bitmap_.Get();
        int sourceWidth = renderTarget.width_;
        int sourceHeight = renderTarget.height_;
        try
        {
            for (Microsoft::WRL::ComPtr<ID2D1Bitmap1>& destination : renderTarget.mipBitmaps_)
            {
                const int destinationWidth = std::max(1, sourceWidth / 2);
                const int destinationHeight = std::max(1, sourceHeight / 2);
                d2dContext_->SetTarget(destination.Get());
                d2dContext_->BeginDraw();
                drawing_ = true;
                d2dContext_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
                d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
                d2dContext_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
                const D2D1_RECT_F destinationRect = D2D1::RectF(
                    0.0f, 0.0f, static_cast<float>(destinationWidth), static_cast<float>(destinationHeight));
                const D2D1_RECT_F sourceRect = D2D1::RectF(
                    0.0f, 0.0f, static_cast<float>(sourceWidth), static_cast<float>(sourceHeight));
                d2dContext_->DrawBitmap(source, &destinationRect, 1.0f,
                                        D2D1_INTERPOLATION_MODE_LINEAR, &sourceRect);
                EndDrawing("render-target mip generation");
                source = destination.Get();
                sourceWidth = destinationWidth;
                sourceHeight = destinationHeight;
            }
        }
        catch (...)
        {
            // EndDrawing already handles real device loss atomically. For ordinary failures the
            // caller receives the error and the chain remains dirty, so it cannot be mistaken for
            // a completed generated chain on the next use.
            throw;
        }

        renderTarget.mipLevelsDirty_ = false;
        // Preserve the logical binding. The next SpriteBatch draw starts a fresh BeginDraw and
        // reapplies clips/state through EnsureDrawing(), while BindRenderTarget may immediately
        // replace this with the next target/backbuffer as part of an unbind switch.
        d2dContext_->SetTarget(activeRenderTarget_ ? activeRenderTarget_->bitmap_.Get()
                                                    : backBufferTarget_.Get());
    }

    void Direct2DGraphicsBackend::ReadRenderTargetPixels(
        const Direct2DRenderTargetBackend& renderTarget, int level, int x, int y, int width, int height,
        uint8_t* pixels)
    {
        // ID2D1Bitmap::CopyFromRenderTarget reads the device context's CURRENT target. A public
        // RenderTarget2D::GetData call may name an unbound target or any generated mip level, so
        // select that bitmap temporarily without changing GraphicsDevice's logical RT binding.
        EndDrawing("render-target readback");
        Microsoft::WRL::ComPtr<ID2D1Image> previousTarget;
        d2dContext_->GetTarget(previousTarget.GetAddressOf());
        ID2D1Bitmap1* const requestedBitmap = renderTarget.BitmapForLevel(level);
        d2dContext_->SetTarget(requestedBitmap);
        try
        {
            ReadCurrentTargetPixels(x, y, width, height, requestedBitmap->GetPixelFormat(), pixels);
        }
        catch (...)
        {
            d2dContext_->SetTarget(previousTarget.Get());
            throw;
        }
        d2dContext_->SetTarget(previousTarget.Get());
    }

    void Direct2DGraphicsBackend::ReadCurrentTargetPixels(
        int x, int y, int width, int height, const D2D1_PIXEL_FORMAT& pixelFormat, uint8_t* pixels)
    {
        // CopyFromRenderTarget may flush work, but it may not be called with an outstanding clip
        // or layer. EndDrawing pops our scissor clip and commits every prior sprite/clear first.
        EndDrawing("readback");

        D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            pixelFormat, 96.0f, 96.0f);
        ComPtr<ID2D1Bitmap1> readableBitmap;
        ThrowIfFailed(d2dContext_->CreateBitmap(
                          D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)),
                          nullptr, 0, &properties, &readableBitmap),
                      "ID2D1DeviceContext::CreateBitmap(readback)");

        const D2D1_RECT_U source = D2D1::RectU(
            static_cast<UINT32>(x), static_cast<UINT32>(y),
            static_cast<UINT32>(x + width), static_cast<UINT32>(y + height));
        HRESULT copyResult = readableBitmap->CopyFromRenderTarget(nullptr, d2dContext_.Get(), &source);
        if (copyResult == E_NOTIMPL)
        {
            // WineD3D 10 implements the Direct2D target bitmap itself but leaves
            // CopyFromRenderTarget as E_NOTIMPL.  The current target is already the exact bitmap
            // selected by ReadRenderTargetPixels/ReadBackbuffer, so retain the same 2D-only
            // semantics with the bitmap-to-bitmap form when a runtime supports that narrower API.
            ComPtr<ID2D1Image> currentTarget;
            d2dContext_->GetTarget(&currentTarget);
            ComPtr<ID2D1Bitmap> currentTargetBitmap;
            if (currentTarget && SUCCEEDED(currentTarget.As(&currentTargetBitmap)))
                copyResult = readableBitmap->CopyFromBitmap(nullptr, currentTargetBitmap.Get(), &source);
        }
        ThrowIfFailed(copyResult, "ID2D1Bitmap::CopyFromRenderTarget/CopyFromBitmap(readback)");

        D2D1_MAPPED_RECT mapped{};
        ThrowIfFailed(readableBitmap->Map(D2D1_MAP_OPTIONS_READ, &mapped),
                      "ID2D1Bitmap1::Map(readback)");
        for (int row = 0; row < height; ++row)
        {
            const uint8_t* sourceRow = mapped.bits + static_cast<std::size_t>(row) * mapped.pitch;
            uint8_t* destinationRow = pixels + static_cast<std::size_t>(row) * width * 4u;
            for (int column = 0; column < width; ++column)
            {
                destinationRow[column * 4 + 0] = sourceRow[column * 4 + 2];
                destinationRow[column * 4 + 1] = sourceRow[column * 4 + 1];
                destinationRow[column * 4 + 2] = sourceRow[column * 4 + 0];
                destinationRow[column * 4 + 3] = sourceRow[column * 4 + 3];
            }
        }
        readableBitmap->Unmap();
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

    D2D1_MATRIX_3X2_F Direct2DGraphicsBackend::ViewportMatrix() const
    {
        return viewportSet_ ? D2D1::Matrix3x2F::Translation(
                                  static_cast<float>(viewportX_), static_cast<float>(viewportY_))
                            : D2D1::Matrix3x2F::Identity();
    }

    void Direct2DGraphicsBackend::ApplyOutputClips()
    {
        if (viewportPushed_ || scissorPushed_) return;
        const D2D1_MATRIX_3X2_F presentation = PresentationMatrix();
        d2dContext_->SetTransform(presentation);
        if (viewportSet_)
        {
            d2dContext_->PushAxisAlignedClip(
                D2D1::RectF(static_cast<float>(viewportX_), static_cast<float>(viewportY_),
                             static_cast<float>(viewportX_ + viewportWidth_),
                             static_cast<float>(viewportY_ + viewportHeight_)),
                D2D1_ANTIALIAS_MODE_ALIASED);
            viewportPushed_ = true;
        }
        if (scissorTestEnabled_ && scissorActive_)
        {
            d2dContext_->PushAxisAlignedClip(
                D2D1::RectF(static_cast<float>(scissorRect_.X), static_cast<float>(scissorRect_.Y),
                             static_cast<float>(scissorRect_.X + scissorRect_.Width),
                             static_cast<float>(scissorRect_.Y + scissorRect_.Height)),
                D2D1_ANTIALIAS_MODE_ALIASED);
            scissorPushed_ = true;
        }
        d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
    }

    void Direct2DGraphicsBackend::ClearOutputClips()
    {
        if (scissorPushed_)
        {
            d2dContext_->PopAxisAlignedClip();
            scissorPushed_ = false;
        }
        if (viewportPushed_)
        {
            d2dContext_->PopAxisAlignedClip();
            viewportPushed_ = false;
        }
    }

    void Direct2DGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        EnsureDrawing();
        // XNA's GraphicsDevice.Clear always affects the entire active target; it must not inherit
        // a RasterizerState scissor that happened to be active for preceding SpriteBatch work.
        ClearOutputClips();
        d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
        d2dContext_->Clear(D2D1::ColorF(r, g, b, a));
        ApplyOutputClips();
    }

    void Direct2DGraphicsBackend::Present()
    {
        EndDrawing("ID2D1DeviceContext::EndDraw");
        const HRESULT hr = swapChain_->Present(static_cast<UINT>(swapInterval_), 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
            RecreateDeviceResourcesForRecovery();
            throw std::runtime_error(
                "Direct2D device resources were recreated after DXGI device removal/reset; redraw the current frame.");
        }
        ThrowIfFailed(hr, "IDXGISwapChain::Present");
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
        {
            // Match EasyGL's public GetBackBufferData contract: while a 2D target is bound, the
            // current draw target is also the read source.  Do not silently return swap-chain
            // pixels while the game is rendering somewhere else.
            if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
                x + w > activeRenderTarget_->GetWidth() || y + h > activeRenderTarget_->GetHeight())
            {
                throw System::ArgumentOutOfRangeException(
                    "rect", "invalid", "The requested active render-target rectangle is outside bounds.");
            }
            ReadCurrentTargetPixels(
                x, y, w, h,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), pixels);
            return;
        }
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

        // EndDraw, rather than ID2D1DeviceContext::Flush, is the synchronization boundary here:
        // it commits Direct2D work, pops the temporary scissor clip, and propagates a real device
        // loss through ThrowIfFailed. The following D3D11 staging Map waits for the copy itself.
        // This also avoids relying on the optional Direct2D Flush implementation (WineD3D leaves
        // it as E_NOTIMPL even though EndDraw and the actual render path are available).
        EndDrawing("Direct2D readback");
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
        int width, int height, int /*depthFormat*/, bool /*preserveContents*/, bool mipMap, int /*multiSampleCount*/)
    {
        return std::make_unique<Direct2DRenderTargetBackend>(*this, width, height, mipMap);
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
        if (activeRenderTarget_) activeRenderTarget_->EnsureMipLevelsCurrent();
        activeRenderTarget_ = renderTarget;
        d2dContext_->SetTarget(renderTarget ? renderTarget->Bitmap() : backBufferTarget_.Get());
        if (renderTarget) renderTarget->MarkMipLevelsDirty();
    }

    void Direct2DGraphicsBackend::ReleaseRenderTarget(Direct2DRenderTargetBackend* renderTarget)
    {
        if (activeRenderTarget_ == renderTarget) BindRenderTarget(nullptr);
    }

    void Direct2DGraphicsBackend::SetScissorRect(int x, int y, int width, int height)
    {
        if (drawing_) ClearOutputClips();
        // A zero-size scissor is a valid empty clip, not a request to disable the scissor test.
        // The RasterizerState gate remains independent, so changing a rectangle while disabled
        // merely records it for the next enabled state.
        scissorActive_ = true;
        scissorRect_ = Rectangle(x, y, width, height);
        if (drawing_) ApplyOutputClips();
    }

    void Direct2DGraphicsBackend::SetViewport(int x, int y, int width, int height,
                                              float /*minDepth*/, float /*maxDepth*/)
    {
        if (drawing_) ClearOutputClips();
        // SpriteBatch coordinates are local to this rectangle, matching EasyGL's existing
        // orthographic viewport behavior.  The clip prevents a scaled sprite from leaking beyond
        // its output region; depth range is intentionally irrelevant to Direct2D's 2D-only API.
        viewportSet_ = true;
        viewportX_ = x;
        viewportY_ = y;
        viewportWidth_ = width;
        viewportHeight_ = height;
        if (drawing_) ApplyOutputClips();
    }

    void Direct2DGraphicsBackend::ApplyRasterizerState(int /*cullMode*/, int /*fillMode*/,
                                                        bool scissorTestEnable, float /*depthBias*/,
                                                        float /*slopeScaleDepthBias*/)
    {
        if (scissorTestEnabled_ == scissorTestEnable) return;
        if (drawing_) ClearOutputClips();
        scissorTestEnabled_ = scissorTestEnable;
        if (drawing_) ApplyOutputClips();
    }

    void Direct2DGraphicsBackend::ApplyBlendState(
        int colorSrcBlend, int alphaSrcBlend, int colorDstBlend, int alphaDstBlend,
        int colorBlendFunc, int alphaBlendFunc, const BlendWriteState& writeState)
    {
        for (int target = 0; target < 4; ++target)
        {
            if (writeState.colorWriteChannels[target] != 15)
            {
                throw std::runtime_error(
                    "Direct2D does not support ColorWriteChannels masks; every channel must remain enabled.");
            }
        }
        if (writeState.multiSampleMask != 0xFFFFFFFFu)
        {
            throw std::runtime_error(
                "Direct2D does not support BlendState.MultiSampleMask; the coverage mask must enable every sample.");
        }
        blendMode_ = BlendStateToDirect2DBlendMode(colorSrcBlend, alphaSrcBlend, colorDstBlend,
                                                   alphaDstBlend, colorBlendFunc, alphaBlendFunc);
        // NonPremultiplied differs from AlphaBlend only in the source pixel convention.  A
        // transient CPU-generated premultiplied bitmap is used for it on each affected draw.
        nonPremultipliedSource_ = colorSrcBlend == 4 && colorDstBlend == 5;
        pendingBlendStateFactorWrite_ = true;
        if (drawing_) d2dContext_->SetPrimitiveBlend(ToPrimitiveBlend(blendMode_));
    }

    void Direct2DGraphicsBackend::SetBlendFactor(float r, float g, float b, float a)
    {
        if (pendingBlendStateFactorWrite_)
        {
            pendingBlendStateFactorWrite_ = false;
            return;
        }
        constexpr float epsilon = 0.0001f;
        if (std::abs(r - 1.0f) > epsilon || std::abs(g - 1.0f) > epsilon ||
            std::abs(b - 1.0f) > epsilon || std::abs(a - 1.0f) > epsilon)
        {
            std::ostringstream message;
            message << "Direct2D does not support GraphicsDevice.BlendFactor; only Color::White is valid "
                    << "(received " << r << ", " << g << ", " << b << ", " << a << ").";
            throw std::runtime_error(message.str());
        }
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
            // EasyGL/FNA leaves SpriteBatch UVs unclamped and delegates the edge behavior to the
            // sampler. Match its TextureAddressMode::Clamp (= clamp-to-edge), rather than making
            // a source rectangle that crosses the texture boundary unexpectedly transparent.
            return std::clamp(coordinate, 0, size - 1);
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

        const auto* ordinaryTexture = dynamic_cast<const Direct2DTextureBackend*>(&texture);
        const auto* renderTargetTexture = dynamic_cast<const Direct2DRenderTargetBackend*>(&texture);
        if (!ordinaryTexture && !renderTargetTexture)
            throw std::runtime_error("Direct2D SpriteBatch received a texture created by another graphics backend.");
        const bool sourceOutside = sourceRectangle.X < 0 || sourceRectangle.Y < 0 ||
            sourceRectangle.X + sourceRectangle.Width > texture.GetWidth() ||
            sourceRectangle.Y + sourceRectangle.Height > texture.GetHeight();
        const bool requiresCpuBitmap = ordinaryTexture &&
            (!IsWhite(color) || nonPremultipliedSource_);
        Rectangle localSource = sourceRectangle;
        Vector2 bitmapOrigin = origin;
        int selectedMipLevel = 0;
        if ((ordinaryTexture && !requiresCpuBitmap) || renderTargetTexture)
        {
            // ID2D1Bitmap1 has no implicit mip selection. Pick the nearest uploaded level for
            // minification using the unrotated source-to-destination ratio; rotation and the
            // SpriteBatch matrix change orientation but not this source-density choice. Render
            // targets regenerate their fully initialized GPU chain on unbind before this lookup.
            const float minification = std::max(
                static_cast<float>(sourceRectangle.Width) / std::abs(static_cast<float>(destinationRectangle.Width)),
                static_cast<float>(sourceRectangle.Height) / std::abs(static_cast<float>(destinationRectangle.Height)));
            const int preferredMip = minification > 1.0f
                ? static_cast<int>(std::floor(std::log2(minification)))
                : 0;
            selectedMipLevel = ordinaryTexture
                ? ordinaryTexture->SelectAvailableMipLevel(preferredMip)
                : renderTargetTexture->SelectAvailableMipLevel(preferredMip);
            if (selectedMipLevel > 0)
            {
                const float mipScale = std::ldexp(1.0f, -selectedMipLevel);
                const int left = static_cast<int>(std::floor(sourceRectangle.X * mipScale));
                const int top = static_cast<int>(std::floor(sourceRectangle.Y * mipScale));
                const int right = static_cast<int>(std::ceil(
                    (sourceRectangle.X + sourceRectangle.Width) * mipScale));
                const int bottom = static_cast<int>(std::ceil(
                    (sourceRectangle.Y + sourceRectangle.Height) * mipScale));
                localSource = Rectangle(left, top, std::max(1, right - left), std::max(1, bottom - top));
                bitmapOrigin = Vector2(origin.X * mipScale, origin.Y * mipScale);
            }
        }

        // Selecting a dirty RenderTarget2D mip level finishes the prior Direct2D batch, generates
        // the chain, and restores the logical target. Begin a fresh batch afterwards so the sprite
        // below still receives the caller's blend/clip state.
        EnsureDrawing();

        ComPtr<ID2D1Bitmap1> transient;
        ID2D1Bitmap1* bitmap = ordinaryTexture
            ? ordinaryTexture->BitmapForLevel(selectedMipLevel)
            : renderTargetTexture->BitmapForLevel(selectedMipLevel);
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

        const float scaleX = static_cast<float>(destinationRectangle.Width) / localSource.Width;
        const float scaleY = static_cast<float>(destinationRectangle.Height) / localSource.Height;
        const float cosine = std::cos(rotation);
        const float sine = std::sin(rotation);
        const D2D1_MATRIX_3X2_F scale = D2D1::Matrix3x2F(scaleX, 0.0f, 0.0f, scaleY, 0.0f, 0.0f);
        const D2D1_MATRIX_3X2_F rotate = D2D1::Matrix3x2F(cosine, sine, -sine, cosine, 0.0f, 0.0f);
        const D2D1_MATRIX_3X2_F translate = D2D1::Matrix3x2F::Translation(
            static_cast<float>(destinationRectangle.X), static_cast<float>(destinationRectangle.Y));
        const D2D1_MATRIX_3X2_F spriteTransform = Multiply(Multiply(scale, rotate), translate);
        const D2D1_MATRIX_3X2_F batch = ToD2DMatrix(batchTransform);
        // XNA/FNA SpriteBatch treats sprite coordinates as local to GraphicsDevice.Viewport. The
        // viewport then moves that local output into the target, and presentation finally maps the
        // target's logical space to the physical backbuffer. The corresponding output clip is
        // installed by ApplyOutputClips().
        const D2D1_MATRIX_3X2_F finalTransform = Multiply(
            Multiply(Multiply(spriteTransform, batch), ViewportMatrix()), PresentationMatrix());
        d2dContext_->SetTransform(finalTransform);
        const D2D1_RECT_F destination = D2D1::RectF(-bitmapOrigin.X, -bitmapOrigin.Y,
                                                     static_cast<float>(localSource.Width) - bitmapOrigin.X,
                                                     static_cast<float>(localSource.Height) - bitmapOrigin.Y);
        const D2D1_RECT_F source = D2D1::RectF(static_cast<float>(localSource.X), static_cast<float>(localSource.Y),
                                                static_cast<float>(localSource.X + localSource.Width),
                                                static_cast<float>(localSource.Y + localSource.Height));

        const bool requiresImageBrush = !requiresCpuBitmap &&
            (!IsWhite(color) || IsFlipped(effects) || nonPremultipliedSource_ || sourceOutside ||
             addressU != 1 || addressV != 1);
        if (requiresImageBrush)
        {
            // ImageBrush is the shared GPU sampling path for both ordinary textures and rendered
            // targets whenever a source rectangle leaves bounds, the sampler repeats/reflects,
            // or SpriteEffects flips it. In particular, it preserves the neighboring texel a
            // linear Wrap/Mirror sample needs at the source edge; CPU pre-expansion cannot do so
            // without reimplementing Direct2D's filter kernel.
            ID2D1Image* input = bitmap;
            ComPtr<ID2D1Effect> tintEffect;
            ComPtr<ID2D1Image> tintOutput;
            const bool ignoreColorAlpha = blendMode_ == Direct2DBlendMode::Copy;
            const bool needsTintEffect = color.getRProperty() != 255 || color.getGProperty() != 255 ||
                                         color.getBProperty() != 255 ||
                                         (!ignoreColorAlpha && color.getAProperty() != 255);
            if (needsTintEffect)
            {
                ThrowIfFailed(d2dContext_->CreateEffect(CLSID_D2D1ColorMatrix, &tintEffect),
                              "ID2D1DeviceContext::CreateEffect(ColorMatrix)");
                tintEffect->SetInput(0, input);
                ThrowIfFailed(tintEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX,
                                                   MakeSpriteTintMatrix(color, ignoreColorAlpha)),
                              "ID2D1Effect::SetValue(ColorMatrix)");
                if (nonPremultipliedSource_)
                {
                    // In this mode the RT's stored premultiplied values deliberately become the
                    // straight-alpha source seen by XNA's NonPremultiplied blend.  Apply tint
                    // directly, then premultiply once below for Direct2D source-over.
                    ThrowIfFailed(tintEffect->SetValue(
                                      D2D1_COLORMATRIX_PROP_ALPHA_MODE,
                                      D2D1_COLORMATRIX_ALPHA_MODE_STRAIGHT),
                                  "ID2D1Effect::SetValue(ColorMatrix alpha mode)");
                }
                tintEffect->GetOutput(&tintOutput);
                input = tintOutput.Get();
            }

            ComPtr<ID2D1Effect> premultiplyEffect;
            ComPtr<ID2D1Image> premultiplyOutput;
            if (nonPremultipliedSource_)
            {
                ThrowIfFailed(d2dContext_->CreateEffect(CLSID_D2D1Premultiply, &premultiplyEffect),
                              "ID2D1DeviceContext::CreateEffect(Premultiply)");
                premultiplyEffect->SetInput(0, input);
                premultiplyEffect->GetOutput(&premultiplyOutput);
                input = premultiplyOutput.Get();
            }

            const D2D1_IMAGE_BRUSH_PROPERTIES imageProperties = D2D1::ImageBrushProperties(
                source, ToD2DExtendMode(addressU), ToD2DExtendMode(addressV),
                linearFilter ? D2D1_INTERPOLATION_MODE_LINEAR : D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
            ComPtr<ID2D1ImageBrush> imageBrush;
            ThrowIfFailed(d2dContext_->CreateImageBrush(input, &imageProperties, nullptr, &imageBrush),
                          "ID2D1DeviceContext::CreateImageBrush(SpriteBatch source)");
            // Direct2D clips a negative ImageBrush source-rectangle origin to the image bounds,
            // which would otherwise make local x=0 sample texel zero instead of the requested
            // negative coordinate. Translate the brush's output back by that clipped amount so
            // its inverse coordinate mapping still reaches -1/-2/etc.; the image brush's
            // Clamp/Wrap/Mirror extend mode then implements the same sampler contract as EasyGL.
            const float clippedLeft = static_cast<float>(std::max(0, -localSource.X));
            const float clippedTop = static_cast<float>(std::max(0, -localSource.Y));
            // The correction has to run before reflection. A post-flip translation would send
            // negative UVs to the far edge instead (the exact error D2D-13's Flip probes guard
            // against); the original clipped-origin magnitude remains correct on both axes.
            const D2D1_MATRIX_3X2_F clippedOrigin =
                D2D1::Matrix3x2F::Translation(clippedLeft, clippedTop);
            imageBrush->SetTransform(Multiply(
                clippedOrigin, MakeSpriteBrushTransform(localSource.Width, localSource.Height, effects)));
            d2dContext_->SetTransform(finalTransform);
            d2dContext_->FillRectangle(destination, imageBrush.Get());
            d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());

            // Direct2D may defer these commands until EndDraw, therefore the graph and brush
            // must outlive this method but can be released once EndDraw completed.
            if (tintEffect) transientEffects_.push_back(tintEffect);
            if (tintOutput) transientImages_.push_back(tintOutput);
            if (premultiplyEffect) transientEffects_.push_back(premultiplyEffect);
            if (premultiplyOutput) transientImages_.push_back(premultiplyOutput);
            transientImageBrushes_.push_back(imageBrush);
            return;
        }
        // DrawBitmap is hard-wired to source-over on WineD3D. DrawImage has an explicit image
        // composite mode, so Copy/Add do not depend on mutable primitive-blend state. Translate
        // the source rectangle into the same local destination origin DrawBitmap used above, then
        // let finalTransform provide scale, rotation, viewport, and presentation.
        const D2D1_MATRIX_3X2_F imageTransform = Multiply(
            D2D1::Matrix3x2F::Translation(-static_cast<float>(localSource.X) - bitmapOrigin.X,
                                           -static_cast<float>(localSource.Y) - bitmapOrigin.Y),
            finalTransform);
        const D2D1_POINT_2F imageOffset = D2D1::Point2F(0.0f, 0.0f);
        d2dContext_->SetTransform(imageTransform);
        d2dContext_->DrawImage(
            bitmap, &imageOffset, &source,
            linearFilter ? D2D1_INTERPOLATION_MODE_LINEAR : D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
            ToImageCompositeMode(blendMode_));
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
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode, args.swapInterval,
            args.contextRecoveryEnabled);
    }
#endif
}
