// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/Direct2D/Direct2DGraphicsBackend.hpp"

#include <d2d1effects.h>
#include <d3d11sdklayers.h>

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

        [[nodiscard]] bool EnvironmentFlagEnabled(const char* name)
        {
            const char* const value = std::getenv(name);
            return value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0 &&
                   std::strcmp(value, "false") != 0 && std::strcmp(value, "FALSE") != 0;
        }

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
            // D2D-69: IsDeviceLossHResult (defined below, declared in the header) is the single
            // classifier every device-loss check in this file uses.
            if (IsDeviceLossHResult(hr))
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

        /// D2D-92: RAII guard for ID2D1Bitmap1::Map/Unmap. An exception raised while the bitmap is
        /// mapped (e.g. during the pixel-copy loop) must still unmap it -- best-effort, since a
        /// destructor cannot safely propagate a second failure while already unwinding. The normal,
        /// non-exceptional path instead calls the explicit Unmap() below, whose HRESULT is checked
        /// and thrown on failure rather than discarded.
        class ScopedBitmapMap final
        {
        public:
            ScopedBitmapMap(ID2D1Bitmap1& bitmap, D2D1_MAP_OPTIONS options) : bitmap_(bitmap)
            {
                ThrowIfFailed(bitmap_.Map(options, &mapped_), "ID2D1Bitmap1::Map");
                active_ = true;
            }
            ScopedBitmapMap(const ScopedBitmapMap&) = delete;
            ScopedBitmapMap& operator=(const ScopedBitmapMap&) = delete;
            ~ScopedBitmapMap()
            {
                if (active_) bitmap_.Unmap();
            }

            [[nodiscard]] const D2D1_MAPPED_RECT& Rect() const { return mapped_; }

            void Unmap(const char* operation)
            {
                if (!active_) return;
                active_ = false;
                ThrowIfFailed(bitmap_.Unmap(), operation);
            }

        private:
            ID2D1Bitmap1& bitmap_;
            D2D1_MAPPED_RECT mapped_{};
            bool active_ = false;
        };

        [[nodiscard]] D2D1_PRIMITIVE_BLEND ToPrimitiveBlend(Direct2DBlendMode blendMode)
        {
            switch (blendMode)
            {
                case Direct2DBlendMode::Copy: return D2D1_PRIMITIVE_BLEND_COPY;
                case Direct2DBlendMode::Add: return D2D1_PRIMITIVE_BLEND_ADD;
                case Direct2DBlendMode::SourceOver: return D2D1_PRIMITIVE_BLEND_SOURCE_OVER;
                case Direct2DBlendMode::DestinationOver:
                case Direct2DBlendMode::SourceIn:
                case Direct2DBlendMode::DestinationIn:
                case Direct2DBlendMode::SourceOut:
                case Direct2DBlendMode::DestinationOut:
                case Direct2DBlendMode::SourceAtop:
                case Direct2DBlendMode::DestinationAtop:
                case Direct2DBlendMode::Xor:
                    break;
            }
            return D2D1_PRIMITIVE_BLEND_SOURCE_OVER;
        }

        [[nodiscard]] bool HasPrimitiveBlendEquivalent(Direct2DBlendMode blendMode)
        {
            return blendMode == Direct2DBlendMode::SourceOver ||
                   blendMode == Direct2DBlendMode::Copy ||
                   blendMode == Direct2DBlendMode::Add;
        }

        [[nodiscard]] D2D1_COMPOSITE_MODE ToImageCompositeMode(Direct2DBlendMode blendMode)
        {
            // DrawImage's explicit composite mode avoids depending on the mutable primitive-blend
            // state. It is also the only Direct2D image API that directly expresses both CNA
            // SpriteBatch operations absent from ordinary source-over: Additive and Opaque/Copy.
            switch (blendMode)
            {
                // SpriteBatch's rasterized quad is bounded: Opaque must never clear unrelated
                // destination pixels merely because DrawImage extends its input to the clip.
                case Direct2DBlendMode::Copy: return D2D1_COMPOSITE_MODE_BOUNDED_SOURCE_COPY;
                case Direct2DBlendMode::Add: return D2D1_COMPOSITE_MODE_PLUS;
                case Direct2DBlendMode::SourceOver: return D2D1_COMPOSITE_MODE_SOURCE_OVER;
                case Direct2DBlendMode::DestinationOver: return D2D1_COMPOSITE_MODE_DESTINATION_OVER;
                case Direct2DBlendMode::SourceIn: return D2D1_COMPOSITE_MODE_SOURCE_IN;
                case Direct2DBlendMode::DestinationIn: return D2D1_COMPOSITE_MODE_DESTINATION_IN;
                case Direct2DBlendMode::SourceOut: return D2D1_COMPOSITE_MODE_SOURCE_OUT;
                case Direct2DBlendMode::DestinationOut: return D2D1_COMPOSITE_MODE_DESTINATION_OUT;
                case Direct2DBlendMode::SourceAtop: return D2D1_COMPOSITE_MODE_SOURCE_ATOP;
                case Direct2DBlendMode::DestinationAtop: return D2D1_COMPOSITE_MODE_DESTINATION_ATOP;
                case Direct2DBlendMode::Xor: return D2D1_COMPOSITE_MODE_XOR;
            }
            return D2D1_COMPOSITE_MODE_SOURCE_OVER;
        }

        [[nodiscard]] D2D1_INTERPOLATION_MODE ToInterpolationMode(int textureFilter,
                                                                   bool minifying)
        {
            // TextureFilter ordinals are documented in TextureFilter.hpp. Direct2D has no
            // implicit mip chain for our per-level bitmaps, but it does expose the requested
            // in-plane spatial filter; the four mixed modes select their min or mag component
            // honestly. Interpolation ACROSS mip levels for the MipLinear-family values below is
            // handled separately (D2D-74, MakeMipBlendedSpritePixels), not by this in-plane mode.
            switch (textureFilter)
            {
                case 0: return D2D1_INTERPOLATION_MODE_LINEAR; // Linear
                case 1: return D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR; // Point
                case 2: return D2D1_INTERPOLATION_MODE_ANISOTROPIC;
                case 3: return D2D1_INTERPOLATION_MODE_LINEAR; // LinearMipPoint
                case 4: return D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR; // PointMipLinear
                case 5: case 6: // MinLinearMagPoint*
                    return minifying ? D2D1_INTERPOLATION_MODE_LINEAR
                                      : D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
                case 7: case 8: // MinPointMagLinear*
                    return minifying ? D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                                      : D2D1_INTERPOLATION_MODE_LINEAR;
            }
            throw std::runtime_error("Direct2D received an unknown TextureFilter value.");
        }

        /// D2D-74: TextureFilter values whose MipFilter component is Linear (interpolate between
        /// the two mip levels bracketing the LOD), as opposed to Point (nearest level only).
        /// TextureFilter ordinals: Linear=0, Point=1, Anisotropic=2, LinearMipPoint=3,
        /// PointMipLinear=4, MinLinearMagPointMipLinear=5, MinLinearMagPointMipPoint=6,
        /// MinPointMagLinearMipLinear=7, MinPointMagLinearMipPoint=8.
        [[nodiscard]] bool FilterWantsMipLinear(int textureFilter)
        {
            return textureFilter == 0 || textureFilter == 4 || textureFilter == 5 || textureFilter == 7;
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

        [[nodiscard]] D2D1_MATRIX_5X4_F MakeSpriteTintMatrix(const Color& color)
        {
            const float r = static_cast<float>(color.getRProperty()) / 255.0f;
            const float g = static_cast<float>(color.getGProperty()) / 255.0f;
            const float b = static_cast<float>(color.getBProperty()) / 255.0f;
            const float a = static_cast<float>(color.getAProperty()) / 255.0f;
            return D2D1::Matrix5x4F(r, 0.0f, 0.0f, 0.0f,
                                    0.0f, g, 0.0f, 0.0f,
                                    0.0f, 0.0f, b, 0.0f,
                                    0.0f, 0.0f, 0.0f, a,
                                    0.0f, 0.0f, 0.0f, 0.0f);
        }

        struct MinificationSigma
        {
            double sigmaMin = 1.0;
            bool minifying = false;
        };

        /// Shared core of PreferredMipLevelForTransform/FractionalMipLevelForTransform: the
        /// smallest singular value of scale * rotation * batch * presentation's linear part, the
        /// most strongly minified direction, correct for shear, rotation and non-uniform
        /// presentation scaling.
        [[nodiscard]] MinificationSigma ComputeMinificationSigma(
            int sourceWidth, int sourceHeight, int destinationWidth, int destinationHeight,
            float rotation, const Matrix& batchTransform, float presentationScaleX,
            float presentationScaleY)
        {
            if (sourceWidth <= 0 || sourceHeight <= 0 || destinationWidth == 0 || destinationHeight == 0)
                return {1.0, false};

            const double scaleX = std::abs(static_cast<double>(destinationWidth)) / sourceWidth;
            const double scaleY = std::abs(static_cast<double>(destinationHeight)) / sourceHeight;
            const double cosine = std::cos(static_cast<double>(rotation));
            const double sine = std::sin(static_cast<double>(rotation));

            const double a = (scaleX * cosine * batchTransform.M11 + scaleX * sine * batchTransform.M21) *
                             std::abs(static_cast<double>(presentationScaleX));
            const double b = (scaleX * cosine * batchTransform.M12 + scaleX * sine * batchTransform.M22) *
                             std::abs(static_cast<double>(presentationScaleY));
            const double c = (-scaleY * sine * batchTransform.M11 + scaleY * cosine * batchTransform.M21) *
                             std::abs(static_cast<double>(presentationScaleX));
            const double d = (-scaleY * sine * batchTransform.M12 + scaleY * cosine * batchTransform.M22) *
                             std::abs(static_cast<double>(presentationScaleY));
            const double trace = a * a + b * b + c * c + d * d;
            const double determinant = a * d - b * c;
            const double discriminant = std::sqrt(std::max(0.0, trace * trace - 4.0 * determinant * determinant));
            const double sigmaMinSquared = std::max(0.0, 0.5 * (trace - discriminant));
            const double sigmaMin = std::sqrt(sigmaMinSquared);
            return {sigmaMin, sigmaMin < 1.0};
        }
    }

    bool IsDeviceLossHResult(HRESULT hr)
    {
        return hr == D2DERR_RECREATE_TARGET ||
               hr == DXGI_ERROR_DEVICE_REMOVED ||
               hr == DXGI_ERROR_DEVICE_RESET ||
               hr == DXGI_ERROR_DEVICE_HUNG ||
               hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
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
        if (colorSrcBlend == 0 && colorDstBlend == 0) return Direct2DBlendMode::Add; // premultiplied Plus
        if (colorSrcBlend == 9 && colorDstBlend == 0) return Direct2DBlendMode::DestinationOver;
        if (colorSrcBlend == 8 && colorDstBlend == 1) return Direct2DBlendMode::SourceIn;
        if (colorSrcBlend == 1 && colorDstBlend == 4) return Direct2DBlendMode::DestinationIn;
        if (colorSrcBlend == 9 && colorDstBlend == 1) return Direct2DBlendMode::SourceOut;
        if (colorSrcBlend == 1 && colorDstBlend == 5) return Direct2DBlendMode::DestinationOut;
        if (colorSrcBlend == 8 && colorDstBlend == 5) return Direct2DBlendMode::SourceAtop;
        if (colorSrcBlend == 9 && colorDstBlend == 4) return Direct2DBlendMode::DestinationAtop;
        if (colorSrcBlend == 9 && colorDstBlend == 5) return Direct2DBlendMode::Xor;
        throw std::runtime_error(
            "Direct2D supports only its exact SpriteBatch presets and symmetric Add Porter-Duff factors.");
    }

    int PreferredMipLevelForTransform(
        int sourceWidth, int sourceHeight, int destinationWidth, int destinationHeight,
        float rotation, const Matrix& batchTransform, float presentationScaleX,
        float presentationScaleY, bool* minifying)
    {
        const MinificationSigma sigma = ComputeMinificationSigma(
            sourceWidth, sourceHeight, destinationWidth, destinationHeight, rotation,
            batchTransform, presentationScaleX, presentationScaleY);
        if (minifying) *minifying = sigma.minifying;
        if (!sigma.minifying) return 0;
        if (sigma.sigmaMin <= std::numeric_limits<double>::epsilon()) return std::numeric_limits<int>::max();
        return std::max(0, static_cast<int>(std::floor(std::log2(1.0 / sigma.sigmaMin))));
    }

    double FractionalMipLevelForTransform(
        int sourceWidth, int sourceHeight, int destinationWidth, int destinationHeight,
        float rotation, const Matrix& batchTransform, float presentationScaleX,
        float presentationScaleY, bool* minifying)
    {
        const MinificationSigma sigma = ComputeMinificationSigma(
            sourceWidth, sourceHeight, destinationWidth, destinationHeight, rotation,
            batchTransform, presentationScaleX, presentationScaleY);
        if (minifying) *minifying = sigma.minifying;
        if (!sigma.minifying) return 0.0;
        if (sigma.sigmaMin <= std::numeric_limits<double>::epsilon())
            return std::numeric_limits<double>::infinity();
        return std::max(0.0, std::log2(1.0 / sigma.sigmaMin));
    }

    Rectangle MapSourceRectangleToMip(const Rectangle& sourceRectangle,
                                      int baseWidth, int baseHeight,
                                      int mipWidth, int mipHeight)
    {
        if (baseWidth <= 0 || baseHeight <= 0 || mipWidth <= 0 || mipHeight <= 0)
            throw std::runtime_error("Direct2D mip source mapping requires positive dimensions.");
        if (sourceRectangle.Width <= 0 || sourceRectangle.Height <= 0)
            throw System::ArgumentOutOfRangeException(
                "sourceRectangle", "non-positive size",
                "The Direct2D mip source rectangle must have positive dimensions.");
        const double scaleX = static_cast<double>(mipWidth) / baseWidth;
        const double scaleY = static_cast<double>(mipHeight) / baseHeight;
        const std::int64_t right = static_cast<std::int64_t>(sourceRectangle.X) + sourceRectangle.Width;
        const std::int64_t bottom = static_cast<std::int64_t>(sourceRectangle.Y) + sourceRectangle.Height;
        const auto checkedInt = [](double value) {
            if (!std::isfinite(value) || value < std::numeric_limits<int>::min() ||
                value > std::numeric_limits<int>::max())
                throw System::ArgumentOutOfRangeException(
                    "sourceRectangle", "overflow",
                    "The Direct2D mip-mapped source rectangle cannot be represented by Int32 coordinates.");
            return static_cast<int>(value);
        };
        const int left = checkedInt(std::floor(sourceRectangle.X * scaleX));
        const int top = checkedInt(std::floor(sourceRectangle.Y * scaleY));
        const int mappedRight = checkedInt(std::ceil(static_cast<double>(right) * scaleX));
        const int mappedBottom = checkedInt(std::ceil(static_cast<double>(bottom) * scaleY));
        const std::int64_t width = static_cast<std::int64_t>(mappedRight) - left;
        const std::int64_t height = static_cast<std::int64_t>(mappedBottom) - top;
        if (width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max())
            throw System::ArgumentOutOfRangeException(
                "sourceRectangle", "overflow",
                "The Direct2D mip-mapped source size cannot be represented by Int32 dimensions.");
        return Rectangle(left, top, std::max(1, static_cast<int>(width)),
                         std::max(1, static_cast<int>(height)));
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

    bool Direct2DTextureBackend::IsMipLevelInitialized(int level) const
    {
        owner_->EnsureResourceGeneration(deviceGeneration_, "Texture2D");
        if (level < 0 || level > static_cast<int>(mipBitmaps_.size())) return false;
        if (level == 0) return true;
        return static_cast<bool>(mipBitmaps_[static_cast<std::size_t>(level - 1)]);
    }

    const std::vector<uint8_t>& Direct2DTextureBackend::RgbaPixelsForLevel(int level) const
    {
        owner_->EnsureResourceGeneration(deviceGeneration_, "Texture2D");
        if (level < 0 || level > static_cast<int>(mipRgbaPixels_.size()))
            throw System::ArgumentOutOfRangeException("level", std::to_string(level),
                                                       "The requested Direct2D texture mip level does not exist.");
        if (level == 0) return rgbaPixels_;
        const std::vector<uint8_t>& result = mipRgbaPixels_[static_cast<std::size_t>(level - 1)];
        if (result.empty())
            throw std::runtime_error(
                "Direct2D Texture2D mip level has not been initialized with Texture2D::SetData(level, ...).");
        return result;
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
        if (!owner_) return;
        owner_->UnregisterRenderTarget(this);
        // D2D-60: a destructor must never throw. The normal disposal path
        // (RenderTarget2D::Dispose) already rejects destroying a still-bound target with a real,
        // catchable exception before it ever reaches here; ReleaseRenderTarget's still-bound
        // unbind (EndDrawing/EnsureMipLevelsCurrent/EnsureMainTargetSize, each capable of a
        // Direct2D HRESULT failure) is therefore only reached for edge cases such as a target
        // bound directly through BindAsRenderTarget() (bypassing GraphicsDevice) or teardown
        // ordering with an already-lost device. Best-effort no-throw cleanup here beats
        // std::terminate; a caller that needs to observe such a failure must unbind explicitly
        // (UnbindAsRenderTarget()) before destruction.
        try
        {
            owner_->ReleaseRenderTarget(this);
        }
        catch (...)
        {
        }
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

    void Direct2DRenderTargetBackend::UpdatePixelsLevel(int level, const uint8_t* rgba,
                                                         int levelW, int levelH)
    {
        if (!rgba)
            throw std::runtime_error("Direct2DRenderTargetBackend::UpdatePixelsLevel received null pixels.");
        if (level < 0 || level > static_cast<int>(mipBitmaps_.size()))
            throw System::ArgumentOutOfRangeException(
                "level", std::to_string(level),
                "The requested Direct2D render-target mip level does not exist.");
        const int expectedWidth = std::max(1, width_ >> level);
        const int expectedHeight = std::max(1, height_ >> level);
        if (levelW != expectedWidth || levelH != expectedHeight)
            throw System::ArgumentOutOfRangeException(
                "level dimensions", std::to_string(levelW) + "x" + std::to_string(levelH),
                "The Direct2D render-target mip dimensions do not match the requested level.");
        if (level == 0)
        {
            UpdatePixels(rgba, expectedWidth * 4);
            return;
        }

        // Finish/generate any pending level-zero rendering before replacing this explicitly
        // authored level. The next write to level zero marks the whole generated chain dirty and
        // intentionally supersedes authored lower levels, matching RenderTarget2D's auto-mip role.
        EnsureMipLevelsCurrent();
        std::vector<uint8_t> bgra(static_cast<std::size_t>(expectedWidth) * expectedHeight * 4u);
        for (int y = 0; y < expectedHeight; ++y)
        {
            for (int x = 0; x < expectedWidth; ++x)
            {
                const uint8_t* source = rgba + (static_cast<std::size_t>(y) * expectedWidth + x) * 4u;
                uint8_t* destination = bgra.data() + (static_cast<std::size_t>(y) * expectedWidth + x) * 4u;
                destination[0] = source[2];
                destination[1] = source[1];
                destination[2] = source[0];
                destination[3] = source[3];
            }
        }
        ThrowIfFailed(mipBitmaps_[static_cast<std::size_t>(level - 1)]->CopyFromMemory(
                          nullptr, bgra.data(), static_cast<UINT32>(expectedWidth * 4)),
                      "ID2D1Bitmap::CopyFromMemory(render-target mip)");
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
        if (textureFilter < 0 || textureFilter > 8)
            throw std::runtime_error("Direct2D received an unknown TextureFilter value.");
        textureFilter_ = textureFilter;
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
                           0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, transform_, textureFilter_, addressU_, addressV_);
    }

    void Direct2DSpriteBatchBackend::Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color)
    {
        if (!begun_) throw std::runtime_error("Direct2D SpriteBatch::Draw called before Begin.");
        owner_->DrawSprite(texture, destinationRectangle, sourceRectangle, color, 0.0f,
                           Vector2(0.0f, 0.0f), SpriteEffects::None, transform_, textureFilter_, addressU_, addressV_);
    }

    void Direct2DSpriteBatchBackend::Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color, float rotation,
                                          const Vector2& origin, SpriteEffects effects, float layerDepth)
    {
        (void)layerDepth;
        if (!begun_) throw std::runtime_error("Direct2D SpriteBatch::Draw called before Begin.");
        owner_->DrawSprite(texture, destinationRectangle, sourceRectangle, color, rotation, origin, effects,
                           transform_, textureFilter_, addressU_, addressV_);
    }

    Direct2DGraphicsBackend::Direct2DGraphicsBackend(
        SDL_Window* window, int virtualWidth, int virtualHeight,
        CnaPresentationMode presentationMode, int swapInterval, bool contextRecoveryEnabled)
        : window_(window), virtualWidth_(virtualWidth), virtualHeight_(virtualHeight),
          presentationMode_(presentationMode), swapInterval_(std::clamp(swapInterval, 0, 4)),
          contextRecoveryEnabled_(contextRecoveryEnabled)
    {
        if (!window_) throw std::runtime_error("Direct2DGraphicsBackend initialized with null SDL_Window.");
        diagnosticsEnabled_ = EnvironmentFlagEnabled("CNA_DIRECT2D_DIAGNOSTICS");
        CreateDeviceResources();
        IGraphicsBackend::RegisterForWindow(window_, this);
    }

    Direct2DGraphicsBackend::~Direct2DGraphicsBackend()
    {
        IGraphicsBackend::UnregisterForWindow(window_);
        ReleaseDeviceResourcesNoThrow(true);
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

    void Direct2DGraphicsBackend::ReportLiveDeviceObjectsNoThrow()
    {
        if (!debugLayerEnabled_ || !d3dDevice_) return;
        try
        {
            ComPtr<ID3D11Debug> debug;
            if (FAILED(d3dDevice_.As(&debug)))
            {
                std::fprintf(stderr,
                             "[Direct2D diagnostics] D3D11 debug interface unavailable at shutdown.\n");
                return;
            }
            ComPtr<ID3D11InfoQueue> infoQueue;
            if (SUCCEEDED(d3dDevice_.As(&infoQueue))) infoQueue->ClearStoredMessages();
            // DETAIL is present in both MinGW's baseline SDK headers and current native Windows
            // SDKs. Do not depend on the newer IGNORE_INTERNAL flag: the uploaded message list is
            // more useful when it remains cross-toolchain compilable and complete.
            const HRESULT reportResult = debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
            std::fprintf(stderr,
                         "[Direct2D diagnostics] ReportLiveDeviceObjects hr=%s.\n",
                         FormatHr(reportResult).c_str());
            if (!infoQueue) return;
            const UINT64 messageCount = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
            std::fprintf(stderr,
                         "[Direct2D diagnostics] debug-layer stored messages=%llu.\n",
                         static_cast<unsigned long long>(messageCount));
            for (UINT64 index = 0; index < messageCount; ++index)
            {
                SIZE_T messageBytes = 0;
                if (FAILED(infoQueue->GetMessage(index, nullptr, &messageBytes)) || messageBytes == 0)
                    continue;
                std::vector<std::uint8_t> storage(messageBytes);
                auto* const message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
                if (SUCCEEDED(infoQueue->GetMessage(index, message, &messageBytes)))
                    std::fprintf(stderr, "[Direct2D D3D11 debug] %.*s\n",
                                 static_cast<int>(message->DescriptionByteLength),
                                 message->pDescription ? message->pDescription : "");
            }
        }
        catch (...)
        {
            std::fprintf(stderr,
                         "[Direct2D diagnostics] live-object reporting raised an unexpected exception.\n");
        }
    }

    void Direct2DGraphicsBackend::ReleaseDeviceResourcesNoThrow(bool reportLiveObjects)
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
        colorMatrixEffectSupported_.reset();
        premultiplyEffectSupported_.reset();
        activeRenderTarget_ = nullptr;
        if (d2dContext_) d2dContext_->SetTarget(nullptr);
        logicalTarget_.Reset();
        backBufferTarget_.Reset();
        backBufferTexture_.Reset();
        swapChain_.Reset();
        d2dContext_.Reset();
        d2dFactory_.Reset();
        if (d3dContext_)
        {
            d3dContext_->ClearState();
            d3dContext_->Flush();
        }
        d3dContext_.Reset();
        if (reportLiveObjects)
        {
            if (diagnosticsEnabled_)
                std::fprintf(stderr,
                             "[Direct2D diagnostics] driver=%s EndDraw=%llu transient-releases=%llu "
                             "transient-high-water=%zu.\n",
                             usingWarp_ ? "WARP" : "hardware",
                             static_cast<unsigned long long>(endDrawCount_),
                             static_cast<unsigned long long>(transientResourceReleaseCount_),
                             transientResourceHighWater_);
            ReportLiveDeviceObjectsNoThrow();
        }
        d3dDevice_.Reset();
        debugLayerEnabled_ = false;
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
            return;
        }

        // D2D-63: activeRenderTarget_ stays null here (matching the fresh logical/backbuffer
        // target the device context now actually points at), rather than silently leaving it
        // pointing at an unrecoverable render target GraphicsDevice's own public binding still
        // believes is active. Without this, a subsequent Clear/Draw would silently land on the
        // backbuffer instead of the render target the game explicitly bound and never unbound --
        // no error, no crash, just the wrong destination. Surface the mismatch loudly instead: the
        // caller must explicitly rebind (or accept the backbuffer) before drawing again.
        if (previousActive)
        {
            throw std::runtime_error(
                "Direct2D device resources were recreated after a context loss and the previously "
                "active render target was not recoverable (created while "
                "GraphicsDevice::SetContextRecoveryEnabled(false)); rebind an explicit render "
                "target before drawing again.");
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
        const bool requestDebugLayer = EnvironmentFlagEnabled("CNA_DIRECT2D_DEBUG_LAYER");
        const bool forceWarp = EnvironmentFlagEnabled("CNA_DIRECT2D_FORCE_WARP");
        if (requestDebugLayer) flags |= D3D11_CREATE_DEVICE_DEBUG;
        constexpr D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        D3D_FEATURE_LEVEL createdFeatureLevel{};
        D3D_DRIVER_TYPE driverType = forceWarp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE;
        HRESULT hr = D3D11CreateDevice(nullptr, driverType, nullptr, flags,
                                       featureLevels, static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
                                       &d3dDevice_, &createdFeatureLevel, &d3dContext_);
        if (FAILED(hr) && !forceWarp)
        {
            // WARP retains a functional Direct2D path on VMs/RDP sessions without a hardware D3D
            // adapter; this is an intentional Direct2D fallback, not an SDL renderer fallback.
            driverType = D3D_DRIVER_TYPE_WARP;
            hr = D3D11CreateDevice(nullptr, driverType, nullptr, flags,
                                   featureLevels, static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
                                   &d3dDevice_, &createdFeatureLevel, &d3dContext_);
        }
        ThrowIfFailed(hr, forceWarp ? "D3D11CreateDevice (forced WARP)"
                                    : "D3D11CreateDevice (hardware and WARP)");
        usingWarp_ = driverType == D3D_DRIVER_TYPE_WARP;
        debugLayerEnabled_ = requestDebugLayer;

        ComPtr<IDXGIDevice> dxgiDevice;
        ThrowIfFailed(d3dDevice_.As(&dxgiDevice), "QueryInterface(IDXGIDevice)");
        ComPtr<IDXGIAdapter> adapter;
        ThrowIfFailed(dxgiDevice->GetAdapter(&adapter), "IDXGIDevice::GetAdapter");
        if (diagnosticsEnabled_)
        {
            DXGI_ADAPTER_DESC adapterDescription{};
            adapter->GetDesc(&adapterDescription);
            std::fprintf(stderr,
                         "[Direct2D diagnostics] created driver=%s feature-level=0x%X "
                         "adapter-vendor=0x%04X adapter-device=0x%04X debug-layer=%s.\n",
                         usingWarp_ ? "WARP" : "hardware",
                         static_cast<unsigned int>(createdFeatureLevel),
                         adapterDescription.VendorId, adapterDescription.DeviceId,
                         debugLayerEnabled_ ? "enabled" : "disabled");
        }
        ComPtr<IDXGIFactory2> dxgiFactory;
        ThrowIfFailed(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory)), "IDXGIAdapter::GetParent(IDXGIFactory2)");

        D2D1_FACTORY_OPTIONS factoryOptions{};
        factoryOptions.debugLevel = requestDebugLayer
            ? D2D1_DEBUG_LEVEL_INFORMATION : D2D1_DEBUG_LEVEL_NONE;
        ThrowIfFailed(D2D1CreateFactory(
                          D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                          &factoryOptions,
                          reinterpret_cast<void**>(d2dFactory_.ReleaseAndGetAddressOf())),
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
        logicalTarget_.Reset();
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
        CreateLogicalTarget();
    }

    Microsoft::WRL::ComPtr<ID2D1Bitmap1> Direct2DGraphicsBackend::CreateLogicalTargetBitmap() const
    {
        const PresentationTransform presentation = GetPresentationTransform();
        const int logicalWidth = std::max(1, presentation.logicalWidth);
        const int logicalHeight = std::max(1, presentation.logicalHeight);
        D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);
        ComPtr<ID2D1Bitmap1> bitmap;
        ThrowIfFailed(d2dContext_->CreateBitmap(
                          D2D1::SizeU(static_cast<UINT32>(logicalWidth),
                                      static_cast<UINT32>(logicalHeight)),
                          nullptr, 0, &properties, &bitmap),
                      "ID2D1DeviceContext::CreateBitmap(logical backbuffer)");
        return bitmap;
    }

    void Direct2DGraphicsBackend::CreateLogicalTarget()
    {
        logicalTarget_ = CreateLogicalTargetBitmap();
        d2dContext_->SetTarget(logicalTarget_.Get());
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
        if (current.width != width || current.height != height)
        {
            if (drawing_) EndDrawing("resize");
            d2dContext_->SetTarget(nullptr);
            logicalTarget_.Reset();
            backBufferTarget_.Reset();
            backBufferTexture_.Reset();
            ThrowIfFailed(swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0),
                          "IDXGISwapChain::ResizeBuffers");
            CreateBackBufferTarget();
            return;
        }

        const PresentationTransform desired = GetPresentationTransform();
        const D2D1_SIZE_U logicalSize = logicalTarget_->GetPixelSize();
        if (logicalSize.width == static_cast<UINT32>(std::max(1, desired.logicalWidth)) &&
            logicalSize.height == static_cast<UINT32>(std::max(1, desired.logicalHeight)))
            return;
        if (drawing_) EndDrawing("logical backbuffer resize");
        // D2D-50/D2D-52: create the replacement before releasing the current logical target. A
        // failed CreateBitmap (e.g. requested dimensions exceeding the device's maximum bitmap
        // size, reachable from an extreme SetVirtualResolution/SetPresentationMode request) then
        // leaves the previous logical target -- and the current draw target -- intact and usable,
        // instead of a null logicalTarget_ that would crash the next dereference.
        ComPtr<ID2D1Bitmap1> replacement = CreateLogicalTargetBitmap();
        logicalTarget_ = replacement;
        d2dContext_->SetTarget(logicalTarget_.Get());
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
        const std::size_t transientCount = transientBitmaps_.size() + transientEffects_.size() +
            transientImages_.size() + transientImageBrushes_.size();
        ++endDrawCount_;
        transientResourceReleaseCount_ += transientCount;
        transientResourceHighWater_ = std::max(transientResourceHighWater_, transientCount);
        transientBitmaps_.clear();
        transientEffects_.clear();
        transientImages_.clear();
        transientImageBrushes_.clear();
        if (IsDeviceLossHResult(hr))
        {
            RecreateDeviceResourcesForRecovery();
            throw std::runtime_error(
                "Direct2D device resources were recreated after a device loss (hr=" + FormatHr(hr) +
                "); redraw the current frame.");
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
                                                    : logicalTarget_.Get());
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

        ScopedBitmapMap mapGuard(*readableBitmap.Get(), D2D1_MAP_OPTIONS_READ);
        const D2D1_MAPPED_RECT& mapped = mapGuard.Rect();
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
        mapGuard.Unmap("ID2D1Bitmap1::Unmap(readback)");
    }

    Direct2DGraphicsBackend::PresentationTransform Direct2DGraphicsBackend::GetPresentationTransform() const
    {
        PresentationTransform result = ComputePresentationTransform();
        // D2D-53: guarantee at least a 1x1 logical framebuffer regardless of presentation mode or
        // aspect ratio. FixedHeightDynamicWidth's lround(physicalWidth * virtualHeight_ /
        // physicalHeight) can legitimately round to 0 for an extreme physical/virtual ratio; every
        // other branch is defensively clamped here too so no downstream transform or bitmap
        // creation ever sees a zero dimension.
        result.logicalWidth = std::max(1, result.logicalWidth);
        result.logicalHeight = std::max(1, result.logicalHeight);
        return result;
    }

    Direct2DGraphicsBackend::PresentationTransform Direct2DGraphicsBackend::ComputePresentationTransform() const
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
        // NativeBackBuffer draws at the swap chain's own physical size: unlike every other
        // presentation mode it applies no virtual-resolution scaling at all, so the logical
        // target must equal the physical backbuffer, not the (possibly much smaller) virtual
        // resolution left at an identity scale/offset.
        if (virtualWidth_ <= 0 || virtualHeight_ <= 0 ||
            presentationMode_ == CnaPresentationMode::NativeBackBuffer)
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
        // Application rendering targets the logical framebuffer. Presentation is a separate
        // Present-time DrawImage pass, so viewport/scissor clips stay in logical target pixels.
        d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
        if (viewportSet_)
        {
            d2dContext_->PushAxisAlignedClip(
                D2D1::RectF(static_cast<float>(viewportX_), static_cast<float>(viewportY_),
                             static_cast<float>(viewportX_) + static_cast<float>(viewportWidth_),
                             static_cast<float>(viewportY_) + static_cast<float>(viewportHeight_)),
                D2D1_ANTIALIAS_MODE_ALIASED);
            viewportPushed_ = true;
        }
        if (scissorTestEnabled_ && scissorActive_)
        {
            d2dContext_->PushAxisAlignedClip(
                D2D1::RectF(static_cast<float>(scissorRect_.X), static_cast<float>(scissorRect_.Y),
                             static_cast<float>(scissorRect_.X) + static_cast<float>(scissorRect_.Width),
                             static_cast<float>(scissorRect_.Y) + static_cast<float>(scissorRect_.Height)),
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

    bool Direct2DGraphicsBackend::SupportsColorMatrixEffect()
    {
        if (colorMatrixEffectSupported_.has_value()) return *colorMatrixEffectSupported_;
        ComPtr<ID2D1Effect> effect;
        colorMatrixEffectSupported_ = SUCCEEDED(
            d2dContext_->CreateEffect(CLSID_D2D1ColorMatrix, &effect));
        return *colorMatrixEffectSupported_;
    }

    bool Direct2DGraphicsBackend::SupportsPremultiplyEffect()
    {
        if (premultiplyEffectSupported_.has_value()) return *premultiplyEffectSupported_;
        ComPtr<ID2D1Effect> effect;
        premultiplyEffectSupported_ = SUCCEEDED(
            d2dContext_->CreateEffect(CLSID_D2D1Premultiply, &effect));
        return *premultiplyEffectSupported_;
    }

    void Direct2DGraphicsBackend::MarkActiveRenderTargetMipLevelsDirty()
    {
        if (activeRenderTarget_) activeRenderTarget_->MarkMipLevelsDirty();
    }

    void Direct2DGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        EnsureDrawing();
        // XNA's GraphicsDevice.Clear always affects the entire active target; it must not inherit
        // a RasterizerState scissor that happened to be active for preceding SpriteBatch work.
        ClearOutputClips();
        d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
        d2dContext_->Clear(D2D1::ColorF(r, g, b, a));
        MarkActiveRenderTargetMipLevelsDirty();
        ApplyOutputClips();
    }

    void Direct2DGraphicsBackend::Present()
    {
        EndDrawing("ID2D1DeviceContext::EndDraw");

        // Presentation is a second, purely Direct2D pass. Application drawing remains in the
        // logical bitmap so GetBackBufferData can read exact logical pixels independently of
        // letterbox/stretch filtering and the physical HWND size.
        d2dContext_->SetTarget(backBufferTarget_.Get());
        d2dContext_->BeginDraw();
        drawing_ = true;
        d2dContext_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
        d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
        d2dContext_->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        d2dContext_->SetTransform(PresentationMatrix());
        d2dContext_->DrawImage(logicalTarget_.Get(), D2D1_INTERPOLATION_MODE_LINEAR,
                               D2D1_COMPOSITE_MODE_SOURCE_OVER);
        d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
        EndDrawing("logical backbuffer presentation");
        d2dContext_->SetTarget(activeRenderTarget_ ? activeRenderTarget_->bitmap_.Get()
                                                   : logicalTarget_.Get());

        const HRESULT hr = swapChain_->Present(static_cast<UINT>(swapInterval_), 0);
        if (IsDeviceLossHResult(hr))
        {
            RecreateDeviceResourcesForRecovery();
            throw std::runtime_error(
                "Direct2D device resources were recreated after a device loss (hr=" + FormatHr(hr) +
                "); redraw the current frame.");
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
        const int previousWidth = virtualWidth_;
        const int previousHeight = virtualHeight_;
        if (!activeRenderTarget_) EndDrawing("virtual-resolution change");
        virtualWidth_ = width;
        virtualHeight_ = height;
        if (!activeRenderTarget_)
        {
            try
            {
                EnsureMainTargetSize();
            }
            catch (...)
            {
                // D2D-50: a failed recreate (CreateLogicalTargetBitmap already leaves the actual
                // logical target itself intact) must not also leave virtualWidth_/virtualHeight_
                // pointing at dimensions the target never adopted.
                virtualWidth_ = previousWidth;
                virtualHeight_ = previousHeight;
                throw;
            }
        }
    }

    void Direct2DGraphicsBackend::SetPresentationMode(int mode)
    {
        switch (static_cast<CnaPresentationMode>(mode))
        {
            case CnaPresentationMode::FixedHeightDynamicWidth:
            case CnaPresentationMode::Letterbox:
            case CnaPresentationMode::Overscan:
            case CnaPresentationMode::Stretch:
            case CnaPresentationMode::NativeBackBuffer:
            {
                const CnaPresentationMode previousMode = presentationMode_;
                presentationMode_ = static_cast<CnaPresentationMode>(mode);
                if (!activeRenderTarget_)
                {
                    EndDrawing("presentation-mode change");
                    try
                    {
                        EnsureMainTargetSize();
                    }
                    catch (...)
                    {
                        // D2D-51: same transactional guarantee as SetVirtualResolution above.
                        presentationMode_ = previousMode;
                        throw;
                    }
                }
                return;
            }
        }
        throw std::runtime_error("Direct2D received an unknown CnaPresentationMode value.");
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
            const std::int64_t right = static_cast<std::int64_t>(x) + w;
            const std::int64_t bottom = static_cast<std::int64_t>(y) + h;
            if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
                right > activeRenderTarget_->GetWidth() || bottom > activeRenderTarget_->GetHeight())
            {
                throw System::ArgumentOutOfRangeException(
                    "rect", "invalid", "The requested active render-target rectangle is outside bounds.");
            }
            ReadCurrentTargetPixels(
                x, y, w, h,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), pixels);
            return;
        }
        EnsureMainTargetSize();
        const D2D1_SIZE_U logicalSize = logicalTarget_->GetPixelSize();
        const std::int64_t right = static_cast<std::int64_t>(x) + w;
        const std::int64_t bottom = static_cast<std::int64_t>(y) + h;
        if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
            right > static_cast<int>(logicalSize.width) ||
            bottom > static_cast<int>(logicalSize.height))
            throw System::ArgumentOutOfRangeException("rect", "invalid", "The requested back-buffer rectangle is outside bounds.");
        EndDrawing("logical backbuffer readback");
        d2dContext_->SetTarget(logicalTarget_.Get());
        ReadCurrentTargetPixels(
            x, y, w, h,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), pixels);
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
        auto* const direct2dTarget = dynamic_cast<Direct2DRenderTargetBackend*>(renderTarget);
        if (renderTarget && !direct2dTarget)
            throw std::runtime_error("Direct2D cannot bind a render target created by another graphics backend.");
        if (direct2dTarget && direct2dTarget->owner_ != this)
            throw std::runtime_error(
                "Direct2D cannot bind a render target created by another GraphicsDevice.");
        BindRenderTarget(direct2dTarget);
    }

    void Direct2DGraphicsBackend::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (count < 0)
            throw std::runtime_error("Direct2D received a negative render-target binding count.");
        if (count > 1)
            throw std::runtime_error("Direct2D supports exactly one active 2D render target; MRT is unsupported.");
        if (count > 0 && !renderTargets)
            throw std::runtime_error("Direct2D received null render-target bindings with a nonzero count.");
        if (count > 0 && renderTargets[0].IsRenderTargetCubeFace())
            throw std::runtime_error("Direct2D does not support RenderTargetCube bindings.");
        SetRenderTarget2D(count > 0 ? renderTargets[0].GetRenderTarget2D() : nullptr);
    }

    void Direct2DGraphicsBackend::BindRenderTarget(Direct2DRenderTargetBackend* renderTarget)
    {
        if (activeRenderTarget_ == renderTarget) return;
        // D2D-61: validate the incoming target's device generation (and fetch its native bitmap)
        // before touching any logical or native state. Bitmap() throws for a target belonging to
        // a lost device generation that was never registered for recovery; doing that check after
        // activeRenderTarget_ was already reassigned left the logical and native target pointing
        // at different resources on rejection.
        ID2D1Bitmap1* const nativeTarget = renderTarget ? renderTarget->Bitmap() : nullptr;
        EndDrawing("render-target switch");
        if (activeRenderTarget_) activeRenderTarget_->EnsureMipLevelsCurrent();
        activeRenderTarget_ = renderTarget;
        if (!renderTarget) EnsureMainTargetSize();
        d2dContext_->SetTarget(renderTarget ? nativeTarget : logicalTarget_.Get());
        if (renderTarget) renderTarget->MarkMipLevelsDirty();
    }

    void Direct2DGraphicsBackend::ReleaseRenderTarget(Direct2DRenderTargetBackend* renderTarget)
    {
        if (activeRenderTarget_ == renderTarget) BindRenderTarget(nullptr);
    }

    void Direct2DGraphicsBackend::SetScissorRect(int x, int y, int width, int height)
    {
        if (width < 0 || height < 0)
            throw System::ArgumentOutOfRangeException(
                "scissor rectangle", "negative size", "Direct2D scissor dimensions must not be negative.");
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
        if (width < 0 || height < 0)
            throw System::ArgumentOutOfRangeException(
                "viewport", "negative size", "Direct2D viewport dimensions must not be negative.");
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

    void Direct2DGraphicsBackend::ApplyDepthStencilState(
        bool /*depthEnable*/, bool /*depthWriteEnable*/, int /*depthFunc*/,
        bool stencilEnable, int /*stencilFunc*/,
        int /*stencilPass*/, int /*stencilFail*/, int /*stencilDepthFail*/,
        int /*stencilMask*/, int /*stencilWriteMask*/, int /*referenceStencil*/,
        bool /*twoSidedStencilMode*/,
        int /*ccwStencilFunc*/, int /*ccwStencilPass*/,
        int /*ccwStencilFail*/, int /*ccwStencilDepthFail*/)
    {
        // D2D-99: Direct2D never allocates a depth or stencil buffer (RenderTarget2D has no real
        // depth buffer; SupportsCapability(DepthStencilBuffer) is false), so DepthBufferEnable/
        // WriteEnable/Function can never have an observable effect regardless of value. They are
        // silently accepted -- including GraphicsDevice's own constructor-forced
        // DepthStencilState::Default (DepthBufferEnable=true), which every GraphicsDevice applies
        // unconditionally before any game code runs. Stencil testing is a real per-pixel masking
        // feature a game could depend on, with no Direct2D equivalent, so it is rejected outright
        // (DepthStencilState::Default/DepthRead/None all leave StencilEnable=false, so this can
        // never reject that same constructor-forced default).
        if (stencilEnable)
        {
            throw std::runtime_error(
                "Direct2D does not support DepthStencilState.StencilEnable; it has no depth/stencil "
                "buffer. Use CNA_GRAPHICS_BACKEND=D3D11 for stencil-dependent rendering.");
        }
    }

    void Direct2DGraphicsBackend::ApplyRasterizerState(int /*cullMode*/, int fillMode,
                                                        bool scissorTestEnable, float depthBias,
                                                        float slopeScaleDepthBias)
    {
        // D2D-100: FillMode.WireFrame and a nonzero depth bias are real, silently-wrong-if-ignored
        // capability gaps (Direct2D has no wireframe rasterization and no depth buffer to bias
        // against), so they fail with a named exception instead of silently drawing solid/unbiased.
        // CullMode is always accepted regardless of value: Direct2D's SpriteBatch has no
        // user-supplied triangle winding to cull at all (every draw is a fixed, internally
        // generated quad), and GraphicsDevice's own constructor unconditionally applies
        // RasterizerState.CullCounterClockwise before any game code runs -- rejecting any cull
        // mode would break every Direct2D application at startup. The default RasterizerState and
        // all three named Cull* presets leave FillMode/DepthBias/SlopeScaleDepthBias at their
        // inert defaults (Solid, 0, 0), so this can never reject that same constructor-forced
        // default.
        constexpr int fillModeWireFrame = 1; // FillMode: Solid=0, WireFrame=1
        if (fillMode == fillModeWireFrame)
        {
            throw std::runtime_error(
                "Direct2D does not support RasterizerState.FillMode.WireFrame; it has no "
                "wireframe rasterization pipeline. Use CNA_GRAPHICS_BACKEND=D3D11 for wireframe "
                "rendering.");
        }
        if (depthBias != 0.0f || slopeScaleDepthBias != 0.0f)
        {
            throw std::runtime_error(
                "Direct2D does not support RasterizerState.DepthBias/SlopeScaleDepthBias; it has "
                "no depth buffer to bias against.");
        }
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

    bool Direct2DGraphicsBackend::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        // D2D1_INTERPOLATION_MODE_ANISOTROPIC is a native ID2D1DeviceContext 2D sampler mode.
        // Every other capability currently enumerated by CNA belongs to the intentionally absent
        // 3D/depth/query/custom-effect surface of this backend.
        return capability == CNA::GraphicsCapability::AnisotropicFiltering;
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
        const Color& color, SpriteEffects effects, int addressU, int addressV, int mipLevel) const
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
        const int mipWidth = std::max(1, texture.GetWidth() >> mipLevel);
        const int mipHeight = std::max(1, texture.GetHeight() >> mipLevel);
        const std::vector<uint8_t>& pixels = texture.RgbaPixelsForLevel(mipLevel);
        for (int dy = 0; dy < sourceHeight; ++dy)
        {
            for (int dx = 0; dx < sourceWidth; ++dx)
            {
                const int unflippedX = flipH ? sourceWidth - 1 - dx : dx;
                const int unflippedY = flipV ? sourceHeight - 1 - dy : dy;
                const int sx = sampleCoordinate(sourceRectangle.X + unflippedX, mipWidth, addressU);
                const int sy = sampleCoordinate(sourceRectangle.Y + unflippedY, mipHeight, addressV);
                if (sx < 0 || sy < 0) continue;
                const uint8_t* input = pixels.data() + (static_cast<std::size_t>(sy) * mipWidth + sx) * 4u;
                const int alpha = input[3];
                const int sourceR = nonPremultipliedSource_ ? input[0] * alpha / 255 : input[0];
                const int sourceG = nonPremultipliedSource_ ? input[1] * alpha / 255 : input[1];
                const int sourceB = nonPremultipliedSource_ ? input[2] * alpha / 255 : input[2];
                uint8_t* output = result.data() + (static_cast<std::size_t>(dy) * sourceWidth + dx) * 4u;
                // D2D-34: EasyGL's SpriteBatch shader is FragColor = texture * Color, a purely
                // component-wise multiply -- Color.A scales only the resulting alpha, for every
                // blend preset, and must never additionally attenuate the (already premultiplied,
                // for source-over/add) source RGB a second time.
                const int fragR = sourceR * color.getRProperty() / 255;
                const int fragG = sourceG * color.getGProperty() / 255;
                const int fragB = sourceB * color.getBProperty() / 255;
                const int fragA = alpha * color.getAProperty() / 255;
                if (blendMode_ == Direct2DBlendMode::Add)
                {
                    // D2D-35: BlendState.Additive is SourceAlpha/One for both the color AND alpha
                    // channel (FNA: colorSourceBlend=alphaSourceBlend=SourceAlpha, both
                    // destination factors One), not Direct2D's D2D1_COMPOSITE_MODE_PLUS, which is
                    // an unconditional One/One add with no per-fragment factor at all. Direct2D
                    // exposes no general blend-factor API, so the SourceAlpha factor is folded
                    // into the source pixels here instead: pre-multiplying by the fragment's own
                    // resulting alpha (and squaring alpha itself, since the alpha channel's own
                    // blend factor is also SourceAlpha) makes PLUS's implicit *1 reproduce
                    // SourceAlpha/One exactly: PLUS(fragRGB*fragA, fragA*fragA) + dst*1
                    // == fragRGB*fragA + dst.rgb, fragA*fragA + dst.a.
                    output[0] = static_cast<uint8_t>(fragR * fragA / 255);
                    output[1] = static_cast<uint8_t>(fragG * fragA / 255);
                    output[2] = static_cast<uint8_t>(fragB * fragA / 255);
                    output[3] = static_cast<uint8_t>(fragA * fragA / 255);
                }
                else
                {
                    output[0] = static_cast<uint8_t>(fragR);
                    output[1] = static_cast<uint8_t>(fragG);
                    output[2] = static_cast<uint8_t>(fragB);
                    output[3] = static_cast<uint8_t>(fragA);
                }
            }
        }
        return result;
    }

    std::vector<uint8_t> Direct2DGraphicsBackend::MakeMipBlendedSpritePixels(
        const Direct2DTextureBackend& texture, const Rectangle& lowerSource,
        const Rectangle& upperSource, int lowerLevel, int upperLevel, float blendFraction,
        const Color& color, SpriteEffects effects, int addressU, int addressV) const
    {
        // D2D-74: samples both mip levels bracketing a fractional LOD and linearly blends them
        // (in the same premultiplied-or-not space MakeSpritePixels uses) before tinting, rather
        // than snapping to a single nearest level. Output is on lowerSource's pixel grid; upper-
        // level samples are proportionally rescaled onto that same grid.
        const int sourceWidth = lowerSource.Width;
        const int sourceHeight = lowerSource.Height;
        std::vector<uint8_t> result(static_cast<std::size_t>(sourceWidth) * sourceHeight * 4u, 0);
        const bool flipH = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0;
        const bool flipV = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0;
        const auto sampleCoordinate = [](int coordinate, int size, int addressMode) -> int
        {
            if (coordinate >= 0 && coordinate < size) return coordinate;
            if (addressMode == 0) return PositiveModulo(coordinate, size); // Wrap
            if (addressMode == 2) return MirrorCoordinate(coordinate, size); // Mirror
            return std::clamp(coordinate, 0, size - 1);
        };
        const int lowerMipWidth = std::max(1, texture.GetWidth() >> lowerLevel);
        const int lowerMipHeight = std::max(1, texture.GetHeight() >> lowerLevel);
        const int upperMipWidth = std::max(1, texture.GetWidth() >> upperLevel);
        const int upperMipHeight = std::max(1, texture.GetHeight() >> upperLevel);
        const std::vector<uint8_t>& lowerPixels = texture.RgbaPixelsForLevel(lowerLevel);
        const std::vector<uint8_t>& upperPixels = texture.RgbaPixelsForLevel(upperLevel);
        const double xScale = static_cast<double>(upperSource.Width) / std::max(1, lowerSource.Width);
        const double yScale = static_cast<double>(upperSource.Height) / std::max(1, lowerSource.Height);
        const double weightLower = 1.0 - static_cast<double>(blendFraction);
        const double weightUpper = static_cast<double>(blendFraction);

        for (int dy = 0; dy < sourceHeight; ++dy)
        {
            for (int dx = 0; dx < sourceWidth; ++dx)
            {
                const int unflippedX = flipH ? sourceWidth - 1 - dx : dx;
                const int unflippedY = flipV ? sourceHeight - 1 - dy : dy;
                const int lsx = sampleCoordinate(lowerSource.X + unflippedX, lowerMipWidth, addressU);
                const int lsy = sampleCoordinate(lowerSource.Y + unflippedY, lowerMipHeight, addressV);
                const int usx = sampleCoordinate(
                    upperSource.X + static_cast<int>(std::floor(unflippedX * xScale)), upperMipWidth, addressU);
                const int usy = sampleCoordinate(
                    upperSource.Y + static_cast<int>(std::floor(unflippedY * yScale)), upperMipHeight, addressV);
                if (lsx < 0 || lsy < 0 || usx < 0 || usy < 0) continue;

                const uint8_t* lowerInput = lowerPixels.data() + (static_cast<std::size_t>(lsy) * lowerMipWidth + lsx) * 4u;
                const uint8_t* upperInput = upperPixels.data() + (static_cast<std::size_t>(usy) * upperMipWidth + usx) * 4u;
                const auto premultipliedChannel = [&](const uint8_t* input, int channel) -> double
                {
                    return nonPremultipliedSource_
                        ? static_cast<double>(input[channel]) * input[3] / 255.0
                        : static_cast<double>(input[channel]);
                };
                const double blendedR = premultipliedChannel(lowerInput, 0) * weightLower +
                                        premultipliedChannel(upperInput, 0) * weightUpper;
                const double blendedG = premultipliedChannel(lowerInput, 1) * weightLower +
                                        premultipliedChannel(upperInput, 1) * weightUpper;
                const double blendedB = premultipliedChannel(lowerInput, 2) * weightLower +
                                        premultipliedChannel(upperInput, 2) * weightUpper;
                const double blendedA = static_cast<double>(lowerInput[3]) * weightLower +
                                        static_cast<double>(upperInput[3]) * weightUpper;

                uint8_t* output = result.data() + (static_cast<std::size_t>(dy) * sourceWidth + dx) * 4u;
                // D2D-34: same independent RGB/A tint as MakeSpritePixels -- Color.A scales only
                // the blended alpha, never the blended RGB a second time.
                const double fragR = blendedR * color.getRProperty() / 255.0;
                const double fragG = blendedG * color.getGProperty() / 255.0;
                const double fragB = blendedB * color.getBProperty() / 255.0;
                const double fragA = blendedA * color.getAProperty() / 255.0;
                if (blendMode_ == Direct2DBlendMode::Add)
                {
                    // D2D-35: same SourceAlpha/One fold as MakeSpritePixels, for a mip-linear
                    // blend under BlendState.Additive.
                    output[0] = static_cast<uint8_t>(std::lround(fragR * fragA / 255.0));
                    output[1] = static_cast<uint8_t>(std::lround(fragG * fragA / 255.0));
                    output[2] = static_cast<uint8_t>(std::lround(fragB * fragA / 255.0));
                    output[3] = static_cast<uint8_t>(std::lround(fragA * fragA / 255.0));
                }
                else
                {
                    output[0] = static_cast<uint8_t>(std::lround(fragR));
                    output[1] = static_cast<uint8_t>(std::lround(fragG));
                    output[2] = static_cast<uint8_t>(std::lround(fragB));
                    output[3] = static_cast<uint8_t>(std::lround(fragA));
                }
            }
        }
        return result;
    }

    void Direct2DGraphicsBackend::DrawSprite(
        const ITextureBackend& texture, const Rectangle& destinationRectangle,
        const Rectangle& sourceRectangle, const Color& color, float rotation, const Vector2& origin,
        SpriteEffects effects, const Matrix& batchTransform, int textureFilter, int addressU, int addressV)
    {
        if (sourceRectangle.Width <= 0 || sourceRectangle.Height <= 0 ||
            destinationRectangle.Width == 0 || destinationRectangle.Height == 0)
            return;

        const auto* ordinaryTexture = dynamic_cast<const Direct2DTextureBackend*>(&texture);
        const auto* renderTargetTexture = dynamic_cast<const Direct2DRenderTargetBackend*>(&texture);
        if (!ordinaryTexture && !renderTargetTexture)
            throw std::runtime_error("Direct2D SpriteBatch received a texture created by another graphics backend.");
        if ((ordinaryTexture && ordinaryTexture->owner_ != this) ||
            (renderTargetTexture && renderTargetTexture->owner_ != this))
            throw std::runtime_error(
                "Direct2D SpriteBatch received a texture created by another GraphicsDevice.");
        const std::int64_t sourceRight = static_cast<std::int64_t>(sourceRectangle.X) + sourceRectangle.Width;
        const std::int64_t sourceBottom = static_cast<std::int64_t>(sourceRectangle.Y) + sourceRectangle.Height;
        const bool sourceOutside = sourceRectangle.X < 0 || sourceRectangle.Y < 0 ||
            sourceRight > texture.GetWidth() || sourceBottom > texture.GetHeight();
        Rectangle localSource = sourceRectangle;
        Vector2 bitmapOrigin = origin;
        int selectedMipLevel = 0;
        bool minifying = false;
        bool mipBlendActive = false;
        int mipBlendUpperLevel = 0;
        float mipBlendFraction = 0.0f;
        Rectangle mipBlendUpperSource{};
        if (ordinaryTexture || renderTargetTexture)
        {
            // ID2D1Bitmap1 has no implicit mip chain. Select the nearest authored/generated level
            // from the complete 2D output transform, including SpriteBatch shear/scale and the
            // eventual physical presentation scale.
            const PresentationTransform presentation = GetPresentationTransform();
            const double fractionalLod = FractionalMipLevelForTransform(
                sourceRectangle.Width, sourceRectangle.Height,
                destinationRectangle.Width, destinationRectangle.Height,
                rotation, batchTransform, presentation.scaleX, presentation.scaleY, &minifying);
            const int preferredMip = std::isfinite(fractionalLod)
                ? static_cast<int>(std::floor(fractionalLod)) : std::numeric_limits<int>::max();
            selectedMipLevel = ordinaryTexture
                ? ordinaryTexture->SelectAvailableMipLevel(preferredMip)
                : renderTargetTexture->SelectAvailableMipLevel(preferredMip);

            // D2D-74: for MipLinear-family filters, interpolate between the two mip levels
            // bracketing the fractional LOD instead of snapping to the nearest one. Only ordinary
            // Texture2D sources have the CPU-side RGBA shadow this needs; RenderTarget2D mips stay
            // GPU-only/nearest-level (a separate, still-open limitation). Both bracketing levels
            // must actually be initialized -- an incomplete mip chain still falls back to nearest.
            if (ordinaryTexture && FilterWantsMipLinear(textureFilter) && std::isfinite(fractionalLod))
            {
                const int maxLevel = ordinaryTexture->MaxMipLevel();
                const int lowerLevel = std::clamp(static_cast<int>(std::floor(fractionalLod)), 0, maxLevel);
                const int upperLevel = std::min(lowerLevel + 1, maxLevel);
                if (upperLevel > lowerLevel && ordinaryTexture->IsMipLevelInitialized(lowerLevel) &&
                    ordinaryTexture->IsMipLevelInitialized(upperLevel))
                {
                    mipBlendActive = true;
                    mipBlendUpperLevel = upperLevel;
                    mipBlendFraction = static_cast<float>(fractionalLod - lowerLevel);
                    selectedMipLevel = lowerLevel;
                }
            }

            if (selectedMipLevel > 0)
            {
                const int mipWidth = std::max(1, texture.GetWidth() >> selectedMipLevel);
                const int mipHeight = std::max(1, texture.GetHeight() >> selectedMipLevel);
                localSource = MapSourceRectangleToMip(sourceRectangle, texture.GetWidth(), texture.GetHeight(),
                                                      mipWidth, mipHeight);
                bitmapOrigin = Vector2(
                    origin.X * static_cast<float>(mipWidth) / texture.GetWidth(),
                    origin.Y * static_cast<float>(mipHeight) / texture.GetHeight());
            }
            if (mipBlendActive)
            {
                const int upperMipWidth = std::max(1, texture.GetWidth() >> mipBlendUpperLevel);
                const int upperMipHeight = std::max(1, texture.GetHeight() >> mipBlendUpperLevel);
                mipBlendUpperSource = MapSourceRectangleToMip(sourceRectangle, texture.GetWidth(),
                                                               texture.GetHeight(), upperMipWidth, upperMipHeight);
            }
        }
        const D2D1_INTERPOLATION_MODE interpolation = ToInterpolationMode(textureFilter, minifying);

        // Selecting a dirty RenderTarget2D mip level finishes the prior Direct2D batch, generates
        // the chain, and restores the logical target. Begin a fresh batch afterwards so the sprite
        // below still receives the caller's blend/clip state.
        EnsureDrawing();

        const bool copyBlend = blendMode_ == Direct2DBlendMode::Copy;
        const bool needsTintEffect = !IsWhite(color);
        // D2D-74: a mip-linear blend has no GPU built-in-effect equivalent here, so it always
        // takes the CPU path (MakeMipBlendedSpritePixels), independent of ColorMatrix/Premultiply
        // effect support. D2D-35: Additive's SourceAlpha/One factor likewise has no Direct2D
        // built-in-effect equivalent (ColorMatrix cannot square alpha), so it also always takes
        // the CPU path (MakeSpritePixels' Add-mode branch), even for an untinted Color::White
        // draw that would otherwise go straight to the GPU with the wrong D2D1_COMPOSITE_MODE_PLUS
        // semantics.
        const bool requiresCpuBitmap = ordinaryTexture &&
            (mipBlendActive || blendMode_ == Direct2DBlendMode::Add ||
             ((needsTintEffect || nonPremultipliedSource_) &&
              ((needsTintEffect && !SupportsColorMatrixEffect()) ||
               (nonPremultipliedSource_ && !SupportsPremultiplyEffect()))));

        ComPtr<ID2D1Bitmap1> transient;
        ID2D1Bitmap1* bitmap = ordinaryTexture
            ? ordinaryTexture->BitmapForLevel(selectedMipLevel)
            : renderTargetTexture->BitmapForLevel(selectedMipLevel);
        if (requiresCpuBitmap)
        {
            const UINT32 maximumBitmapSize = d2dContext_->GetMaximumBitmapSize();
            if (localSource.Width > static_cast<int>(maximumBitmapSize) ||
                localSource.Height > static_cast<int>(maximumBitmapSize))
                throw System::ArgumentOutOfRangeException(
                    "sourceRectangle", "too large",
                    "The Direct2D CPU fallback source exceeds the device maximum bitmap size.");
            const std::vector<uint8_t> pixels = mipBlendActive
                ? MakeMipBlendedSpritePixels(*ordinaryTexture, localSource, mipBlendUpperSource,
                                              selectedMipLevel, mipBlendUpperLevel, mipBlendFraction,
                                              color, effects, addressU, addressV)
                : MakeSpritePixels(*ordinaryTexture, localSource, color, effects, addressU, addressV,
                                   selectedMipLevel);
            transient.Attach(CreateBitmapFromRgba(pixels.data(), localSource.Width, localSource.Height,
                                                   false));
            bitmap = transient.Get();
            localSource = Rectangle(0, 0, localSource.Width, localSource.Height);
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
        // viewport moves that output inside the logical target; Present() later maps the complete
        // logical framebuffer to the physical swap chain. ApplyOutputClips installs the matching
        // logical viewport/scissor bounds.
        const D2D1_MATRIX_3X2_F finalTransform = Multiply(
            Multiply(spriteTransform, batch), ViewportMatrix());
        d2dContext_->SetTransform(finalTransform);
        const D2D1_RECT_F destination = D2D1::RectF(-bitmapOrigin.X, -bitmapOrigin.Y,
                                                     static_cast<float>(localSource.Width) - bitmapOrigin.X,
                                                     static_cast<float>(localSource.Height) - bitmapOrigin.Y);
        const D2D1_RECT_F source = D2D1::RectF(
            static_cast<float>(localSource.X), static_cast<float>(localSource.Y),
            static_cast<float>(static_cast<std::int64_t>(localSource.X) + localSource.Width),
            static_cast<float>(static_cast<std::int64_t>(localSource.Y) + localSource.Height));

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
            if (needsTintEffect)
            {
                ThrowIfFailed(d2dContext_->CreateEffect(CLSID_D2D1ColorMatrix, &tintEffect),
                              "ID2D1DeviceContext::CreateEffect(ColorMatrix)");
                tintEffect->SetInput(0, input);
                ThrowIfFailed(tintEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX,
                                                   MakeSpriteTintMatrix(color)),
                              "ID2D1Effect::SetValue(ColorMatrix)");
                if (nonPremultipliedSource_ || copyBlend)
                {
                    // NonPremultiplied deliberately treats the stored channels as straight and
                    // premultiplies once below. Copy/Opaque also needs straight matrix semantics:
                    // Color.A scales the copied alpha without attenuating RGB's source factor One.
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
                interpolation);
            ComPtr<ID2D1ImageBrush> imageBrush;
            ThrowIfFailed(d2dContext_->CreateImageBrush(input, &imageProperties, nullptr, &imageBrush),
                          "ID2D1DeviceContext::CreateImageBrush(SpriteBatch source)");
            // Direct2D clips a negative ImageBrush source-rectangle origin to the image bounds,
            // which would otherwise make local x=0 sample texel zero instead of the requested
            // negative coordinate. Translate the brush's output back by that clipped amount so
            // its inverse coordinate mapping still reaches -1/-2/etc.; the image brush's
            // Clamp/Wrap/Mirror extend mode then implements the same sampler contract as EasyGL.
            const float clippedLeft = std::max(0.0f, -static_cast<float>(localSource.X));
            const float clippedTop = std::max(0.0f, -static_cast<float>(localSource.Y));
            // The correction has to run before reflection. A post-flip translation would send
            // negative UVs to the far edge instead (the exact error D2D-13's Flip probes guard
            // against); the original clipped-origin magnitude remains correct on both axes.
            const D2D1_MATRIX_3X2_F clippedOrigin =
                D2D1::Matrix3x2F::Translation(clippedLeft, clippedTop);
            imageBrush->SetTransform(Multiply(
                clippedOrigin, MakeSpriteBrushTransform(localSource.Width, localSource.Height, effects)));

            if (HasPrimitiveBlendEquivalent(blendMode_))
            {
                d2dContext_->SetTransform(finalTransform);
                d2dContext_->FillRectangle(destination, imageBrush.Get());
                d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
            }
            else
            {
                // FillRectangle can only consume D2D1_PRIMITIVE_BLEND. Materialize the decorated
                // brush into a Direct2D command list with Copy, then apply the exact Porter-Duff
                // mode through DrawImage. This remains a 2D command stream; no D3D blend pass or
                // shader pipeline is introduced.
                EndDrawing("decorated sprite composite source");
                ComPtr<ID2D1CommandList> commandList;
                ThrowIfFailed(d2dContext_->CreateCommandList(&commandList),
                              "ID2D1DeviceContext::CreateCommandList(SpriteBatch source)");
                d2dContext_->SetTarget(commandList.Get());
                d2dContext_->BeginDraw();
                drawing_ = true;
                d2dContext_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
                d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
                d2dContext_->FillRectangle(destination, imageBrush.Get());
                EndDrawing("decorated sprite command list");
                ThrowIfFailed(commandList->Close(), "ID2D1CommandList::Close(SpriteBatch source)");

                d2dContext_->SetTarget(activeRenderTarget_ ? activeRenderTarget_->bitmap_.Get()
                                                           : logicalTarget_.Get());
                EnsureDrawing();
                ComPtr<ID2D1RectangleGeometry> compositeMask;
                ThrowIfFailed(d2dFactory_->CreateRectangleGeometry(destination, &compositeMask),
                              "ID2D1Factory::CreateRectangleGeometry(SpriteBatch composite mask)");
                d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
                const D2D1_LAYER_PARAMETERS1 layerParameters = D2D1::LayerParameters1(
                    D2D1::InfiniteRect(), compositeMask.Get(), D2D1_ANTIALIAS_MODE_ALIASED,
                    finalTransform);
                d2dContext_->PushLayer(layerParameters, nullptr);
                d2dContext_->SetTransform(finalTransform);
                const D2D1_POINT_2F commandOffset = D2D1::Point2F(0.0f, 0.0f);
                d2dContext_->DrawImage(commandList.Get(), &commandOffset, nullptr,
                                       interpolation, ToImageCompositeMode(blendMode_));
                d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
                d2dContext_->PopLayer();
                ComPtr<ID2D1Image> commandImage;
                ThrowIfFailed(commandList.As(&commandImage),
                              "QueryInterface(ID2D1Image command list)");
                transientImages_.push_back(commandImage);
            }

            // Direct2D may defer these commands until EndDraw, therefore the graph and brush
            // must outlive this method but can be released once EndDraw completed.
            if (tintEffect) transientEffects_.push_back(tintEffect);
            if (tintOutput) transientImages_.push_back(tintOutput);
            if (premultiplyEffect) transientEffects_.push_back(premultiplyEffect);
            if (premultiplyOutput) transientImages_.push_back(premultiplyOutput);
            transientImageBrushes_.push_back(imageBrush);
            MarkActiveRenderTargetMipLevelsDirty();
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
        ComPtr<ID2D1RectangleGeometry> compositeMask;
        const bool needsCompositeMask = blendMode_ != Direct2DBlendMode::SourceOver;
        if (needsCompositeMask)
        {
            ThrowIfFailed(d2dFactory_->CreateRectangleGeometry(destination, &compositeMask),
                          "ID2D1Factory::CreateRectangleGeometry(SpriteBatch composite mask)");
            d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
            const D2D1_LAYER_PARAMETERS1 layerParameters = D2D1::LayerParameters1(
                D2D1::InfiniteRect(), compositeMask.Get(), D2D1_ANTIALIAS_MODE_ALIASED,
                finalTransform);
            d2dContext_->PushLayer(layerParameters, nullptr);
        }
        d2dContext_->SetTransform(imageTransform);
        d2dContext_->DrawImage(
            bitmap, &imageOffset, &source,
            interpolation,
            ToImageCompositeMode(blendMode_));
        d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
        if (needsCompositeMask) d2dContext_->PopLayer();
        MarkActiveRenderTargetMipLevelsDirty();
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
