// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Direct2D/Direct2DRenderer.hpp"
#include "CNA/Platform/Detail/Sdl3RendererInterop.hpp"
#include "CNA/Internal/Renderers/Common/NoOp3DResources.hpp"

#include <d2d1effects.h>
#include <d3d11sdklayers.h>

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::Direct2D
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

        /// D2D-124: the debug-layer gate classifies messages by severity, so the emitted line has
        /// to carry it. The mapping is the fixed D3D11 enumeration, not a heuristic on the text.
        [[nodiscard]] const char* DebugMessageSeverityName(D3D11_MESSAGE_SEVERITY severity)
        {
            switch (severity)
            {
                case D3D11_MESSAGE_SEVERITY_CORRUPTION: return "CORRUPTION";
                case D3D11_MESSAGE_SEVERITY_ERROR: return "ERROR";
                case D3D11_MESSAGE_SEVERITY_WARNING: return "WARNING";
                case D3D11_MESSAGE_SEVERITY_INFO: return "INFO";
                case D3D11_MESSAGE_SEVERITY_MESSAGE: return "MESSAGE";
            }
            return "UNKNOWN";
        }

        [[nodiscard]] D2D1_PRIMITIVE_BLEND ToPrimitiveBlend(Direct2DBlendMode blendMode)
        {
            switch (blendMode)
            {
                case Direct2DBlendMode::Copy: return D2D1_PRIMITIVE_BLEND_COPY;
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
                   blendMode == Direct2DBlendMode::Copy;
        }

        [[nodiscard]] D2D1_COMPOSITE_MODE ToImageCompositeMode(Direct2DBlendMode blendMode)
        {
            // DrawImage's explicit composite mode avoids depending on the mutable primitive-blend
            // state. It is also the only Direct2D image API that directly expresses CNA's
            // Opaque/Copy and the exact supported Porter-Duff operations.
            switch (blendMode)
            {
                // SpriteBatch's rasterized quad is bounded: Opaque must never clear unrelated
                // destination pixels merely because DrawImage extends its input to the clip.
                case Direct2DBlendMode::Copy: return D2D1_COMPOSITE_MODE_BOUNDED_SOURCE_COPY;
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

        /// D2D-112: an extremely long SpriteBatch must not grow the transient set without bound.
        /// Both limits are deliberately generous -- an ordinary frame never reaches either, so the
        /// intermediate commit stays an exceptional safety valve rather than a per-frame cost.
        constexpr std::size_t kMaxTransientResources = 1024;
        constexpr std::size_t kMaxTransientBytes = 64u * 1024u * 1024u;

        /// D2D-109: bounded reuse of CPU-materialized sprite bitmaps. The budget is a cache, not a
        /// correctness requirement: exceeding it costs a re-upload, never a wrong pixel.
        constexpr std::size_t kMaxSpriteBitmapCacheEntries = 32;
        constexpr std::size_t kMaxSpriteBitmapCacheBytes = 16u * 1024u * 1024u;

        /// D2D-107: how many distinct decorated brush configurations stay resident. Effect graphs
        /// are small compared with bitmaps, so this is bounded by entry count alone.
        constexpr std::size_t kMaxSpriteBrushCacheEntries = 32;

        /// D2D-111: steady-state capacity for the transient collections. clear() keeps capacity, so
        /// reserving once means an ordinary frame performs no reallocation at all.
        constexpr std::size_t kTransientBitmapReserve = 256;
        constexpr std::size_t kTransientEffectReserve = 128;
        constexpr std::size_t kTransientImageReserve = 256;
        constexpr std::size_t kTransientImageBrushReserve = 128;

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

    bool SupportsDirect2DCapability(CNA::GraphicsCapability capability) noexcept
    {
        // Exhaustive and deliberately without a default arm: a future capability addition must
        // receive an explicit Direct2D decision instead of inheriting a permissive answer.
        switch (capability)
        {
            case CNA::GraphicsCapability::AnisotropicFiltering:
                return true;

            case CNA::GraphicsCapability::ThreeD:
            case CNA::GraphicsCapability::DepthStencilBuffer:
            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
            case CNA::GraphicsCapability::MultipleRenderTargets:
            case CNA::GraphicsCapability::WireFrame:
            case CNA::GraphicsCapability::OcclusionQuery:
            case CNA::GraphicsCapability::CustomEffects:
            case CNA::GraphicsCapability::Texture3D:
            case CNA::GraphicsCapability::MultiStreamVertexInput:
            case CNA::GraphicsCapability::Instancing:
            case CNA::GraphicsCapability::StencilBuffer:
            case CNA::GraphicsCapability::AdditiveBlending:
                return false;
        }
        return false;
    }

    std::size_t CheckedRgbaByteCount(int width, int height)
    {
        if (width <= 0 || height <= 0)
            throw System::ArgumentOutOfRangeException(
                "dimensions", std::to_string(width) + "x" + std::to_string(height),
                "Direct2D RGBA dimensions must be positive.");
        // Fail before multiplication or allocation even on 64-bit hosts, where INT_MAX squared
        // times four still fits size_t. A D3D11-backed Direct2D bitmap can never exceed this API
        // ceiling; each resource constructor also applies the active device's usually stricter
        // ID2D1DeviceContext::GetMaximumBitmapSize() value before allocating pixels.
        if (width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
            height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        {
            throw System::ArgumentOutOfRangeException(
                "dimensions", std::to_string(width) + "x" + std::to_string(height),
                "Direct2D RGBA dimensions exceed the D3D11 texture-dimension ceiling.");
        }
        const std::size_t unsignedWidth = static_cast<std::size_t>(width);
        const std::size_t unsignedHeight = static_cast<std::size_t>(height);
        if (unsignedWidth > std::numeric_limits<std::size_t>::max() / unsignedHeight ||
            unsignedWidth * unsignedHeight > std::numeric_limits<std::size_t>::max() / 4u)
        {
            throw System::ArgumentOutOfRangeException(
                "dimensions", std::to_string(width) + "x" + std::to_string(height),
                "Direct2D RGBA byte-count arithmetic overflowed.");
        }
        return unsignedWidth * unsignedHeight * 4u;
    }

    /// Shared conversion core of CopyRgbaToTightBgra and the in-place bitmap upload path; file
    /// local because the reusable-buffer form is an implementation detail, not renderer API.
    static void ConvertRgbaToTightBgraInto(
        const std::uint8_t* rgba, int stride, int width, int height,
        std::vector<std::uint8_t>& bgra)
    {
        const std::size_t bytes = CheckedRgbaByteCount(width, height);
        const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
        if (!rgba)
            throw std::invalid_argument("Direct2D RGBA upload received null pixels.");
        if (stride < 0 || static_cast<std::size_t>(stride) < rowBytes)
            throw System::ArgumentOutOfRangeException(
                "stride", std::to_string(stride),
                "Direct2D RGBA upload stride is shorter than one complete row.");

        // D2D-106/D2D-111: resize keeps an already sufficient capacity, so a repeatedly reused
        // scratch buffer stops allocating after the first upload of a given size.
        bgra.resize(bytes);
        for (int y = 0; y < height; ++y)
        {
            const std::uint8_t* sourceRow = rgba + static_cast<std::size_t>(y) *
                static_cast<std::size_t>(stride);
            std::uint8_t* destinationRow = bgra.data() + static_cast<std::size_t>(y) * rowBytes;
            for (int x = 0; x < width; ++x)
            {
                destinationRow[x * 4 + 0] = sourceRow[x * 4 + 2];
                destinationRow[x * 4 + 1] = sourceRow[x * 4 + 1];
                destinationRow[x * 4 + 2] = sourceRow[x * 4 + 0];
                destinationRow[x * 4 + 3] = sourceRow[x * 4 + 3];
            }
        }
    }

    std::vector<std::uint8_t> CopyRgbaToTightBgra(
        const std::uint8_t* rgba, int stride, int width, int height)
    {
        std::vector<std::uint8_t> bgra;
        ConvertRgbaToTightBgraInto(rgba, stride, width, height, bgra);
        return bgra;
    }

    std::vector<std::uint8_t> CopyBgraToTightRgba(
        const std::uint8_t* bgra, int stride, int width, int height)
    {
        const std::size_t bytes = CheckedRgbaByteCount(width, height);
        const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
        if (!bgra)
            throw std::invalid_argument("Direct2D BGRA readback received null pixels.");
        if (stride < 0 || static_cast<std::size_t>(stride) < rowBytes)
            throw System::ArgumentOutOfRangeException(
                "stride", std::to_string(stride),
                "Direct2D BGRA readback pitch is shorter than one complete row.");

        std::vector<std::uint8_t> rgba(bytes);
        for (int y = 0; y < height; ++y)
        {
            const std::uint8_t* sourceRow = bgra + static_cast<std::size_t>(y) *
                static_cast<std::size_t>(stride);
            std::uint8_t* destinationRow = rgba.data() + static_cast<std::size_t>(y) * rowBytes;
            for (int x = 0; x < width; ++x)
            {
                destinationRow[x * 4 + 0] = sourceRow[x * 4 + 2];
                destinationRow[x * 4 + 1] = sourceRow[x * 4 + 1];
                destinationRow[x * 4 + 2] = sourceRow[x * 4 + 0];
                destinationRow[x * 4 + 3] = sourceRow[x * 4 + 3];
            }
        }
        return rgba;
    }

    Direct2DBlendMode BlendStateToDirect2DBlendMode(
        int colorSrcBlend, int alphaSrcBlend, int colorDstBlend, int alphaDstBlend,
        int colorBlendFunc, int alphaBlendFunc)
    {
        // Microsoft::Xna::Framework::Graphics::Blend: One=0, Zero=1, SourceAlpha=4,
        // InverseSourceAlpha=5. BlendFunction::Add=0. Direct2D can express Opaque, AlphaBlend and
        // NonPremultiplied; the latter two both select SourceOver while the caller records their
        // source-alpha convention separately.
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
        if ((colorSrcBlend == 4 && colorDstBlend == 0) ||
            (colorSrcBlend == 0 && colorDstBlend == 0))
            throw System::NotSupportedException(
                "Direct2D rejects additive SourceAlpha/One and One/One blend states: its PLUS "
                "composite cannot implement both texture and RenderTarget2D sources consistently.");
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

    std::array<std::uint8_t, 4> CompositePorterDuffReference(
        Direct2DBlendMode blendMode, const std::array<std::uint8_t, 4>& source,
        const std::array<std::uint8_t, 4>& destination)
    {
        // D2D-40: the two factors below are read straight off the Porter-Duff operator table
        // (Fa/Fb over premultiplied colors), deliberately without consulting
        // ToImageCompositeMode/BlendStateToDirect2DBlendMode. A test that compared Direct2D's
        // output against a reference derived from CNA's own mapping would only prove the mapping
        // agrees with itself.
        const double sourceAlpha = static_cast<double>(source[3]) / 255.0;
        const double destinationAlpha = static_cast<double>(destination[3]) / 255.0;
        double sourceFactor = 0.0;
        double destinationFactor = 0.0;
        switch (blendMode)
        {
            case Direct2DBlendMode::Copy:
                sourceFactor = 1.0;
                destinationFactor = 0.0;
                break;
            case Direct2DBlendMode::SourceOver:
                sourceFactor = 1.0;
                destinationFactor = 1.0 - sourceAlpha;
                break;
            case Direct2DBlendMode::DestinationOver:
                sourceFactor = 1.0 - destinationAlpha;
                destinationFactor = 1.0;
                break;
            case Direct2DBlendMode::SourceIn:
                sourceFactor = destinationAlpha;
                destinationFactor = 0.0;
                break;
            case Direct2DBlendMode::DestinationIn:
                sourceFactor = 0.0;
                destinationFactor = sourceAlpha;
                break;
            case Direct2DBlendMode::SourceOut:
                sourceFactor = 1.0 - destinationAlpha;
                destinationFactor = 0.0;
                break;
            case Direct2DBlendMode::DestinationOut:
                sourceFactor = 0.0;
                destinationFactor = 1.0 - sourceAlpha;
                break;
            case Direct2DBlendMode::SourceAtop:
                sourceFactor = destinationAlpha;
                destinationFactor = 1.0 - sourceAlpha;
                break;
            case Direct2DBlendMode::DestinationAtop:
                sourceFactor = 1.0 - destinationAlpha;
                destinationFactor = sourceAlpha;
                break;
            case Direct2DBlendMode::Xor:
                sourceFactor = 1.0 - destinationAlpha;
                destinationFactor = 1.0 - sourceAlpha;
                break;
        }

        std::array<std::uint8_t, 4> result{};
        for (std::size_t channel = 0; channel < result.size(); ++channel)
        {
            const double value = static_cast<double>(source[channel]) * sourceFactor +
                                 static_cast<double>(destination[channel]) * destinationFactor;
            const long rounded = std::lround(value);
            result[channel] = static_cast<std::uint8_t>(std::clamp<long>(rounded, 0, 255));
        }
        return result;
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

    Direct2DTextureRenderer::Direct2DTextureRenderer(Direct2DRenderer& owner, const ImageData& data)
        : owner_(&owner), width_(data.width), height_(data.height)
    {
        const std::size_t bytes = CheckedRgbaByteCount(width_, height_);
        const int maximumDimension = owner_->GetMaxTextureDimension();
        if (width_ > maximumDimension || height_ > maximumDimension)
            throw System::ArgumentOutOfRangeException(
                "dimensions", std::to_string(width_) + "x" + std::to_string(height_),
                "Direct2D Texture2D exceeds ID2D1DeviceContext::GetMaximumBitmapSize().");
        if (data.surfaceFormat != 0)
            throw System::NotSupportedException(
                "Direct2D Texture2D supports only SurfaceFormat::Color (RGBA8 transfer, BGRA8 native storage).");
        if (data.pixels.size() < bytes)
            throw std::runtime_error("Direct2D texture ImageData has fewer than width*height*4 bytes.");
        rgbaPixels_.assign(data.pixels.begin(), data.pixels.begin() + static_cast<std::ptrdiff_t>(bytes));
        int maximumMipCount = 1;
        for (int mipWidth = width_, mipHeight = height_; mipWidth > 1 || mipHeight > 1; ++maximumMipCount)
        {
            mipWidth = std::max(1, mipWidth / 2);
            mipHeight = std::max(1, mipHeight / 2);
        }
        if (data.mipLevels < 1 || data.mipLevels > maximumMipCount)
            throw System::ArgumentOutOfRangeException(
                "mipLevels", std::to_string(data.mipLevels),
                "Direct2D Texture2D mip count must describe a valid full-size prefix down to 1x1.");
        const int mipCount = data.mipLevels;
        mipBitmaps_.resize(static_cast<std::size_t>(mipCount - 1));
        mipRgbaPixels_.resize(static_cast<std::size_t>(mipCount - 1));
        RecreateBitmap();
        owner_->RegisterTexture(this);
    }

    Direct2DTextureRenderer::~Direct2DTextureRenderer()
    {
        if (owner_) owner_->UnregisterTexture(this);
    }

    void Direct2DTextureRenderer::RecreateBitmap()
    {
        ComPtr<ID2D1Bitmap1> replacement;
        replacement.Attach(owner_->CreateBitmapFromRgba(rgbaPixels_.data(), width_, height_));
        std::vector<ComPtr<ID2D1Bitmap1>> replacementMips(mipBitmaps_.size());
        for (std::size_t index = 0; index < mipBitmaps_.size(); ++index)
        {
            const std::vector<uint8_t>& pixels = mipRgbaPixels_[index];
            if (pixels.empty()) continue;
            const int level = static_cast<int>(index) + 1;
            const int levelWidth = std::max(1, width_ >> level);
            const int levelHeight = std::max(1, height_ >> level);
            replacementMips[index].Attach(
                owner_->CreateBitmapFromRgba(pixels.data(), levelWidth, levelHeight));
        }
        bitmap_ = std::move(replacement);
        mipBitmaps_ = std::move(replacementMips);
        deviceGeneration_ = owner_->deviceGeneration_;
        ++contentVersion_;
    }

    ID2D1Bitmap1* Direct2DTextureRenderer::Bitmap() const
    {
        owner_->EnsureResourceGeneration(deviceGeneration_, "Texture2D");
        return bitmap_.Get();
    }

    ID2D1Bitmap1* Direct2DTextureRenderer::BitmapForLevel(int level) const
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

    int Direct2DTextureRenderer::SelectAvailableMipLevel(int preferredLevel) const
    {
        owner_->EnsureResourceGeneration(deviceGeneration_, "Texture2D");
        preferredLevel = std::clamp(preferredLevel, 0, static_cast<int>(mipBitmaps_.size()));
        while (preferredLevel > 0 && !mipBitmaps_[static_cast<std::size_t>(preferredLevel - 1)])
            --preferredLevel;
        return preferredLevel;
    }

    bool Direct2DTextureRenderer::IsMipLevelInitialized(int level) const
    {
        owner_->EnsureResourceGeneration(deviceGeneration_, "Texture2D");
        if (level < 0 || level > static_cast<int>(mipBitmaps_.size())) return false;
        if (level == 0) return true;
        return static_cast<bool>(mipBitmaps_[static_cast<std::size_t>(level - 1)]);
    }

    bool Direct2DTextureRenderer::HasDefinedMipLevel(int level) const noexcept
    {
        if (!owner_ || deviceGeneration_ != owner_->deviceGeneration_) return false;
        if (level < 0 || level > static_cast<int>(mipBitmaps_.size())) return false;
        if (level == 0) return !rgbaPixels_.empty() && static_cast<bool>(bitmap_);
        const std::size_t index = static_cast<std::size_t>(level - 1);
        return !mipRgbaPixels_[index].empty() && static_cast<bool>(mipBitmaps_[index]);
    }

    const std::vector<uint8_t>& Direct2DTextureRenderer::RgbaPixelsForLevel(int level) const
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

    void Direct2DTextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        // D2D-110: ownership of the single recovery copy is HERE, in rgbaPixels_/mipRgbaPixels_,
        // and it is deliberately a copy rather than a share of Texture2D's own cpuPixels_.
        // ITextureRenderer::ShareCpuPixels exists, but the shared layer publishes that pointer only
        // when it (re)creates a renderer, never when it replaces the buffer behind it:
        // Texture2D::storeCpuPixels allocates a fresh vector whenever cpuPixels_ was released by
        // MaybeFreeCpuPixels (which happens with context recovery disabled) and does not re-share
        // it, so a backend holding the old pointer would silently keep a stale shadow. That shadow
        // is not merely a recovery cache: it is the authority for authored mip-linear blending and
        // for the compatibility tint/straight-alpha fallback (D2D-73), so stale means wrong pixels,
        // not just a slower recovery. There is also no share hook for lower mip levels at all.
        // Deduplicating this would therefore require changing the shared contract for every
        // renderer, which is not a Direct2D-local change.

        (void)CheckedRgbaByteCount(width_, height_);
        const std::size_t rowBytes = static_cast<std::size_t>(width_) * 4u;
        if (!rgba)
            throw std::invalid_argument("Direct2DTextureRenderer::UpdatePixels received null pixels.");
        if (stride < 0 || static_cast<std::size_t>(stride) < rowBytes)
            throw System::ArgumentOutOfRangeException(
                "stride", std::to_string(stride),
                "Direct2DTextureRenderer::UpdatePixels stride is smaller than one RGBA row.");
        std::vector<uint8_t> replacementPixels(CheckedRgbaByteCount(width_, height_));
        for (int y = 0; y < height_; ++y)
        {
            std::memcpy(replacementPixels.data() + static_cast<std::size_t>(y) * rowBytes,
                        rgba + static_cast<std::size_t>(y) * stride,
                        rowBytes);
        }
        // Commit any command that still reads the old contents before they change. This gives
        // Immediate SpriteBatch draws snapshot semantics across SetData: everything issued before
        // this call keeps the previous pixels, everything after observes the new ones. Every check
        // that can reject the upload has already run, so no rejected call reaches this flush.
        owner_->EndDrawing("Texture2D SetData");
        // D2D-106: overwrite the existing bitmap instead of allocating a replacement. The
        // create-and-swap path remains as the fallback for a runtime whose CopyFromMemory cannot
        // serve this bitmap, so the shadow and the GPU contents never diverge.
        // Only a bitmap that still belongs to the CURRENT device generation may be written in
        // place. A stale one (created before a device loss this texture did not participate in)
        // must be replaced outright; overwriting it and then claiming the current generation would
        // revive a resource whose storage still belongs to the dead device.
        const bool bitmapIsCurrent = bitmap_ && deviceGeneration_ == owner_->deviceGeneration_;
        if (bitmapIsCurrent && owner_->TryUploadBitmapRgba(*bitmap_.Get(), replacementPixels.data(),
                                                           width_, height_))
        {
            rgbaPixels_ = std::move(replacementPixels);
            deviceGeneration_ = owner_->deviceGeneration_;
            ++contentVersion_;
            return;
        }
        ComPtr<ID2D1Bitmap1> replacementBitmap;
        replacementBitmap.Attach(
            owner_->CreateBitmapFromRgba(replacementPixels.data(), width_, height_));
        rgbaPixels_ = std::move(replacementPixels);
        bitmap_ = std::move(replacementBitmap);
        deviceGeneration_ = owner_->deviceGeneration_;
        ++contentVersion_;
    }

    void Direct2DTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (!rgba) throw std::runtime_error("Direct2DTextureRenderer::UpdatePixelsLevel received null pixels.");
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
        const std::size_t bytes = CheckedRgbaByteCount(expectedWidth, expectedHeight);
        std::vector<uint8_t> replacementPixels(rgba, rgba + bytes);
        owner_->EndDrawing("Texture2D mip SetData");
        const std::size_t index = static_cast<std::size_t>(level - 1);
        // D2D-106: same in-place upload as level zero. Writing one authored mip must not disturb
        // the COM identity of level zero or of any other authored level, which recreating the
        // bitmap here never did either -- but now it does not even reallocate this one.
        ID2D1Bitmap1* const existing =
            deviceGeneration_ == owner_->deviceGeneration_ ? mipBitmaps_[index].Get() : nullptr;
        if (existing && owner_->TryUploadBitmapRgba(*existing, replacementPixels.data(),
                                                     expectedWidth, expectedHeight))
        {
            mipRgbaPixels_[index] = std::move(replacementPixels);
            deviceGeneration_ = owner_->deviceGeneration_;
            ++contentVersion_;
            return;
        }
        ComPtr<ID2D1Bitmap1> replacementBitmap;
        replacementBitmap.Attach(
            owner_->CreateBitmapFromRgba(replacementPixels.data(), expectedWidth, expectedHeight));
        mipRgbaPixels_[index] = std::move(replacementPixels);
        mipBitmaps_[index] = std::move(replacementBitmap);
        deviceGeneration_ = owner_->deviceGeneration_;
        ++contentVersion_;
    }

    bool Direct2DTextureRenderer::GetData(int level, int x, int y, int w, int h,
                                          void* data, int dataLength) const
    {
        owner_->EnsureResourceGeneration(deviceGeneration_, "Texture2D");
        if (!HasDefinedMipLevel(level)) return false;
        const int levelWidth = std::max(1, width_ >> level);
        const int levelHeight = std::max(1, height_ >> level);
        const std::int64_t right = static_cast<std::int64_t>(x) + w;
        const std::int64_t bottom = static_cast<std::int64_t>(y) + h;
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || right > levelWidth || bottom > levelHeight)
            throw System::ArgumentOutOfRangeException(
                "rect", "invalid", "The requested rectangle leaves the Direct2D Texture2D mip level.");
        const std::size_t requiredBytes = CheckedRgbaByteCount(w, h);
        if (!data || dataLength < 0 || static_cast<std::size_t>(dataLength) < requiredBytes)
            throw System::ArgumentOutOfRangeException(
                "dataLength", std::to_string(dataLength),
                "The destination is too small for the requested Direct2D Texture2D pixels.");

        const std::vector<uint8_t>& source = RgbaPixelsForLevel(level);
        auto* destination = static_cast<uint8_t*>(data);
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
        for (int row = 0; row < h; ++row)
        {
            const std::size_t sourceOffset =
                (static_cast<std::size_t>(y + row) * levelWidth + x) * 4u;
            std::memcpy(destination + static_cast<std::size_t>(row) * rowBytes,
                        source.data() + sourceOffset, rowBytes);
        }
        return true;
    }

    Direct2DRenderTargetRenderer::Direct2DRenderTargetRenderer(
        Direct2DRenderer& owner, int width, int height, bool mipMap)
        : owner_(&owner), width_(width), height_(height)
    {
        (void)CheckedRgbaByteCount(width_, height_);
        const int maximumDimension = owner_->GetMaxTextureDimension();
        if (width_ > maximumDimension || height_ > maximumDimension)
            throw System::ArgumentOutOfRangeException(
                "dimensions", std::to_string(width_) + "x" + std::to_string(height_),
                "Direct2D RenderTarget2D exceeds ID2D1DeviceContext::GetMaximumBitmapSize().");
        if (mipMap)
            throw System::NotSupportedException(
                "Direct2D rejects mipmapped RenderTarget2D resources: its former generated chain "
                "was not correct for NPOT targets. Use a non-mipmapped target or D3D11.");
        RecreateBitmap();
        owner_->RegisterRenderTarget(this);
    }

    void Direct2DRenderTargetRenderer::RecreateBitmap()
    {
        D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);
        // RenderTarget2D deliberately has no CPU shadow. A recovered target's old contents are
        // discarded; initialize its replacement as transparent black until the first draw.
        const auto createTargetBitmap = [&](int bitmapWidth, int bitmapHeight,
                                            Microsoft::WRL::ComPtr<ID2D1Bitmap1>& output) {
            std::vector<uint8_t> transparentPixels(CheckedRgbaByteCount(bitmapWidth, bitmapHeight), 0u);
            ThrowIfFailed(owner_->d2dContext_->CreateBitmap(
                              D2D1::SizeU(static_cast<UINT32>(bitmapWidth),
                                          static_cast<UINT32>(bitmapHeight)),
                              transparentPixels.data(), static_cast<UINT32>(bitmapWidth * 4),
                              &properties, &output),
                          "CreateBitmap(render target)");
        };
        ComPtr<ID2D1Bitmap1> replacement;
        createTargetBitmap(width_, height_, replacement);
        bitmap_ = std::move(replacement);
        deviceGeneration_ = owner_->deviceGeneration_;
    }

    Direct2DRenderTargetRenderer::~Direct2DRenderTargetRenderer()
    {
        if (!owner_) return;
        owner_->UnregisterRenderTarget(this);
        // D2D-60: a destructor must never throw. The normal disposal path
        // (RenderTarget2D::Dispose) already rejects destroying a still-bound target with a real,
        // catchable exception before it ever reaches here; ReleaseRenderTarget's still-bound
        // unbind (EndDrawing/EnsureMainTargetSize, each capable of a
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

    ID2D1Bitmap1* Direct2DRenderTargetRenderer::Bitmap() const
    {
        owner_->EnsureResourceGeneration(deviceGeneration_, "RenderTarget2D");
        return bitmap_.Get();
    }

    ID2D1Bitmap1* Direct2DRenderTargetRenderer::BitmapForLevel(int level) const
    {
        owner_->EnsureResourceGeneration(deviceGeneration_, "RenderTarget2D");
        if (level != 0)
        {
            throw System::ArgumentOutOfRangeException(
                "level", std::to_string(level),
                "Direct2D supports only level 0 for non-mipmapped RenderTarget2D resources.");
        }
        return bitmap_.Get();
    }

    bool Direct2DRenderTargetRenderer::HasDefinedMipLevel(int level) const noexcept
    {
        if (!owner_ || deviceGeneration_ != owner_->deviceGeneration_) return false;
        return level == 0 && static_cast<bool>(bitmap_);
    }

    void Direct2DRenderTargetRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        // Validate and prepare the complete upload before flushing any outstanding draw. A bad
        // pointer/pitch therefore cannot create a hidden batch boundary or dirty state.
        const std::vector<uint8_t> bgra = CopyRgbaToTightBgra(rgba, stride, width_, height_);
        ID2D1Bitmap1* const targetBitmap = Bitmap();
        // D2D-87: ID2D1Bitmap::CopyFromMemory bypasses the device context's own drawing pipeline
        // entirely, so it can race an open BeginDraw/EndDraw recording session against this target
        // (or whichever target IS currently active) -- the same reason ReadCurrentTargetPixels
        // flushes before CopyFromRenderTarget. A no-op when nothing is currently being drawn
        // (EndDrawing's own drawing_ guard), so this is safe to call unconditionally here.
        owner_->EndDrawing("render-target SetData");
        ThrowIfFailed(targetBitmap->CopyFromMemory(nullptr, bgra.data(), static_cast<UINT32>(width_ * 4)),
                      "ID2D1Bitmap::CopyFromMemory(render target)");
    }

    void Direct2DRenderTargetRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba,
                                                         int levelW, int levelH)
    {
        if (!rgba)
            throw std::runtime_error("Direct2DRenderTargetRenderer::UpdatePixelsLevel received null pixels.");
        if (level != 0)
            throw System::ArgumentOutOfRangeException(
                "level", std::to_string(level),
                "Direct2D supports only level 0 for non-mipmapped RenderTarget2D resources.");
        if (levelW != width_ || levelH != height_)
            throw System::ArgumentOutOfRangeException(
                "level dimensions", std::to_string(levelW) + "x" + std::to_string(levelH),
                "The Direct2D render-target dimensions do not match level 0.");
        UpdatePixels(rgba, width_ * 4);
    }

    bool Direct2DRenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                               void* data, int dataLength) const
    {
        if (level < 0)
            throw System::ArgumentOutOfRangeException("level", std::to_string(level),
                                                       "level must not be negative.");
        if (level > 0)
        {
            throw System::NotSupportedException(
                "Direct2D RenderTarget2D is non-mipmapped; only level 0 is available, but level " +
                std::to_string(level) + " was requested.");
        }
        const int levelWidth = width_;
        const int levelHeight = height_;
        const std::int64_t right = static_cast<std::int64_t>(x) + w;
        const std::int64_t bottom = static_cast<std::int64_t>(y) + h;
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || right > levelWidth || bottom > levelHeight)
            throw System::ArgumentOutOfRangeException("rect", "invalid", "The requested rectangle leaves the render target.");
        const std::size_t requiredBytes = CheckedRgbaByteCount(w, h);
        if (!data || dataLength < 0 || static_cast<std::size_t>(dataLength) < requiredBytes)
            throw System::ArgumentOutOfRangeException("dataLength", std::to_string(dataLength),
                                                       "The destination is too small for the requested RGBA pixels.");
        owner_->ReadRenderTargetPixels(*this, level, x, y, w, h, static_cast<uint8_t*>(data));
        return true;
    }

    void Direct2DRenderTargetRenderer::BindAsRenderTarget() { owner_->BindRenderTarget(this); }
    void Direct2DRenderTargetRenderer::UnbindAsRenderTarget() { owner_->BindRenderTarget(nullptr); }

    void Direct2DSpriteBatchRenderer::Begin()
    {
        if (begun_) throw std::runtime_error("Direct2D SpriteBatch::Begin called while already begun.");
        owner_->EnsureDrawing();
        begun_ = true;
    }

    void Direct2DSpriteBatchRenderer::End()
    {
        if (!begun_) throw std::runtime_error("Direct2D SpriteBatch::End called before Begin.");
        begun_ = false;
    }

    void Direct2DSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (effect)
            throw std::runtime_error(
                "Direct2D does not support custom SpriteBatch Effects: it has no CNA shader-effect pipeline.");
    }

    void Direct2DSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        if (textureFilter < 0 || textureFilter > 8)
            throw std::runtime_error("Direct2D received an unknown TextureFilter value.");
        textureFilter_ = textureFilter;
    }

    void Direct2DSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        if (addressU < 0 || addressU > 2 || addressV < 0 || addressV > 2)
            throw std::runtime_error("Direct2D received an unknown TextureAddressMode value.");
        addressU_ = addressU;
        addressV_ = addressV;
    }

    void Direct2DSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        if (!begun_) throw std::runtime_error("Direct2D SpriteBatch::Draw called before Begin.");
        owner_->DrawSprite(texture,
                           Rectangle(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()),
                           Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()), Color(255, 255, 255, 255),
                           0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, transform_, textureFilter_, addressU_, addressV_);
    }

    void Direct2DSpriteBatchRenderer::Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color)
    {
        if (!begun_) throw std::runtime_error("Direct2D SpriteBatch::Draw called before Begin.");
        owner_->DrawSprite(texture, destinationRectangle, sourceRectangle, color, 0.0f,
                           Vector2(0.0f, 0.0f), SpriteEffects::None, transform_, textureFilter_, addressU_, addressV_);
    }

    void Direct2DSpriteBatchRenderer::Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color, float rotation,
                                          const Vector2& origin, SpriteEffects effects, float layerDepth)
    {
        (void)layerDepth;
        if (!begun_) throw std::runtime_error("Direct2D SpriteBatch::Draw called before Begin.");
        owner_->DrawSprite(texture, destinationRectangle, sourceRectangle, color, rotation, origin, effects,
                           transform_, textureFilter_, addressU_, addressV_);
    }

    Direct2DRenderer::Direct2DRenderer(
        SDL_Window* window, int virtualWidth, int virtualHeight,
        CnaPresentationMode presentationMode, int swapInterval, bool contextRecoveryEnabled,
        std::function<void(RendererDeviceEvent)> deviceEventCallback,
        int backBufferFormat, int depthStencilFormat, int multiSampleCount)
        : window_(window), virtualWidth_(virtualWidth), virtualHeight_(virtualHeight),
          presentationMode_(presentationMode), swapInterval_(swapInterval),
          contextRecoveryEnabled_(contextRecoveryEnabled),
          deviceEventCallback_(std::move(deviceEventCallback))
    {
        if (!window_) throw std::runtime_error("Direct2DRenderer initialized with null SDL_Window.");
        if (virtualWidth_ < 0 || virtualHeight_ < 0 ||
            ((virtualWidth_ == 0) != (virtualHeight_ == 0)))
            throw System::ArgumentOutOfRangeException(
                "virtual resolution", std::to_string(virtualWidth_) + "x" + std::to_string(virtualHeight_),
                "Direct2D virtual dimensions must either both be zero or both be positive.");
        if (swapInterval_ < 0 || swapInterval_ > 2)
            throw System::ArgumentOutOfRangeException(
                "swapInterval", std::to_string(swapInterval_),
                "Direct2D supports swap intervals 0 (Immediate), 1 (VSync), and 2 (half-rate)." );
        if (backBufferFormat != 0)
            throw System::NotSupportedException(
                "Direct2D supports only SurfaceFormat::Color for the backbuffer.");
        if (depthStencilFormat != 0)
            throw System::NotSupportedException(
                "Direct2D supports only DepthFormat::None for the backbuffer.");
        if (multiSampleCount < 0 || multiSampleCount > 1)
            throw System::NotSupportedException(
                "Direct2D does not support a multisampled backbuffer; request MultiSampleCount 0 or 1.");
        diagnosticsEnabled_ = EnvironmentFlagEnabled("CNA_DIRECT2D_DIAGNOSTICS");
        CreateDeviceResources();
        IGraphicsRenderer::RegisterForWindow(SDL_GetWindowID(window_), this);
    }

    Direct2DRenderer::~Direct2DRenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(SDL_GetWindowID(window_));
        ReleaseDeviceResourcesNoThrow(true);
    }

    void Direct2DRenderer::RegisterTexture(Direct2DTextureRenderer* texture)
    {
        if (!contextRecoveryEnabled_ || !texture) return;
        assert(std::find(recoverableTextures_.begin(), recoverableTextures_.end(), texture) ==
                   recoverableTextures_.end() &&
               "Direct2DTextureRenderer registered twice in recoverableTextures_");
        recoverableTextures_.push_back(texture);
    }

    void Direct2DRenderer::UnregisterTexture(Direct2DTextureRenderer* texture)
    {
        // erase-remove over the whole vector makes this idempotent: unregistering an
        // already-unregistered (or never-registered) pointer is a safe no-op.
        std::erase(recoverableTextures_, texture);
    }

    void Direct2DRenderer::RegisterRenderTarget(Direct2DRenderTargetRenderer* renderTarget)
    {
        if (!contextRecoveryEnabled_ || !renderTarget) return;
        assert(std::find(recoverableRenderTargets_.begin(), recoverableRenderTargets_.end(), renderTarget) ==
                   recoverableRenderTargets_.end() &&
               "Direct2DRenderTargetRenderer registered twice in recoverableRenderTargets_");
        recoverableRenderTargets_.push_back(renderTarget);
    }

    void Direct2DRenderer::UnregisterRenderTarget(Direct2DRenderTargetRenderer* renderTarget)
    {
        std::erase(recoverableRenderTargets_, renderTarget);
    }

    bool Direct2DRenderer::IsRegisteredRenderTarget(const Direct2DRenderTargetRenderer* renderTarget) const
    {
        return std::find(recoverableRenderTargets_.begin(), recoverableRenderTargets_.end(), renderTarget) !=
               recoverableRenderTargets_.end();
    }

    void Direct2DRenderer::EnsureResourceGeneration(std::uint64_t generation,
                                                            const char* resourceKind) const
    {
        if (generation == deviceGeneration_) return;
        throw std::runtime_error(
            std::string("Direct2D ") + resourceKind +
            " belongs to a lost device and was not registered for context recovery; recreate it after "
            "GraphicsDevice::SetContextRecoveryEnabled(false).");
    }

    void Direct2DRenderer::ReportLiveDeviceObjectsNoThrow()
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
            if (!infoQueue)
            {
                std::fprintf(stderr,
                             "[Direct2D diagnostics] live-object report end (no info queue).\n");
                return;
            }
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
                if (FAILED(infoQueue->GetMessage(index, message, &messageBytes))) continue;
                // D2D-124: severity/category/id are machine-read by
                // scripts/verify-direct2d-debug-log.py. Keep the key=value prefix stable; the
                // description keeps the debug layer's own wording so a live-object line stays
                // recognizable as "Live <interface> at 0x..., Refcount: N".
                std::fprintf(stderr, "[Direct2D D3D11 debug] severity=%s category=%d id=%d %.*s\n",
                             DebugMessageSeverityName(message->Severity),
                             static_cast<int>(message->Category), static_cast<int>(message->ID),
                             static_cast<int>(message->DescriptionByteLength),
                             message->pDescription ? message->pDescription : "");
            }
            std::fprintf(stderr, "[Direct2D diagnostics] live-object report end.\n");
        }
        catch (...)
        {
            std::fprintf(stderr,
                         "[Direct2D diagnostics] live-object reporting raised an unexpected exception.\n");
        }
    }

    void Direct2DRenderer::ReleaseDeviceResourcesNoThrow(bool reportLiveObjects)
    {
        if (drawing_ && d2dContext_)
        {
            ClearOutputClips();
            d2dContext_->EndDraw();
        }
        drawing_ = false;
        pendingBlendMode_.reset();
        pendingNonPremultipliedSource_ = false;
        viewportPushed_ = false;
        scissorPushed_ = false;
        transientBitmaps_.clear();
        transientEffects_.clear();
        transientImages_.clear();
        transientImageBrushes_.clear();
        transientByteEstimate_ = 0;
        // D2D-109: cached bitmaps belong to the device being torn down. Their keys carry the old
        // generation and could never match again, but holding them would keep dead COM objects
        // alive and would be reported as leaks by the live-object gate.
        spriteBitmapCache_.clear();
        spriteBitmapCacheBytes_ = 0;
        spriteBrushCache_.clear();
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
                             "transient-high-water=%zu transient-chunk-flushes=%llu "
                             "sprite-bitmap-cache-hits=%llu misses=%llu "
                             "sprite-brush-cache-hits=%llu misses=%llu.\n",
                             usingWarp_ ? "WARP" : "hardware",
                             static_cast<unsigned long long>(endDrawCount_),
                             static_cast<unsigned long long>(transientResourceReleaseCount_),
                             transientResourceHighWater_,
                             static_cast<unsigned long long>(transientChunkFlushCount_),
                             static_cast<unsigned long long>(spriteBitmapCacheHits_),
                             static_cast<unsigned long long>(spriteBitmapCacheMisses_),
                             static_cast<unsigned long long>(spriteBrushCacheHits_),
                             static_cast<unsigned long long>(spriteBrushCacheMisses_));
            ReportLiveDeviceObjectsNoThrow();
        }
        d3dDevice_.Reset();
        debugLayerEnabled_ = false;
    }

    void Direct2DRenderer::RecreateDeviceResourcesForRecovery()
    {
        // D2D-70: this is the single entry point every device-loss detection site (EndDrawing,
        // Present, EnsureMainTargetSize's resize fallback) and both debug hooks
        // (DebugSimulateContextLoss/DebugRestoreContext) funnel through, so firing the public
        // GraphicsDevice::DeviceLost/DeviceResetting/DeviceReset events here -- instead of at each
        // call site -- guarantees the application observes exactly one of each, in XNA's order,
        // per recovery cycle regardless of which path triggered it.
        if (deviceEventCallback_) deviceEventCallback_(RendererDeviceEvent::Lost);

        // Snapshot active-target identity and registries before releasing the old Direct2D device.
        // Only resources that existed while recovery was enabled participate; later resources must
        // not retain stale COM objects silently after the reset.
        Direct2DRenderTargetRenderer* previousActive = activeRenderTarget_;
        const bool restorePreviousActive = IsRegisteredRenderTarget(previousActive);
        const auto textures = recoverableTextures_;
        const auto renderTargets = recoverableRenderTargets_;

        if (deviceEventCallback_) deviceEventCallback_(RendererDeviceEvent::Resetting);
        ReleaseDeviceResourcesNoThrow();
        ++deviceGeneration_;
        CreateDeviceResources();

        // D2D-65: attempt every resource even if one fails, instead of the first exception
        // aborting the loop and silently skipping recreation of everything after it. A resource
        // whose RecreateBitmap() throws never has its deviceGeneration_ updated, so it correctly
        // keeps rejecting use via EnsureResourceGeneration; a retry (another recovery call) safely
        // re-attempts every resource, including ones that already succeeded (redundant but
        // harmless, since RecreateBitmap replaces bitmap_ wholesale).
        std::vector<std::string> recreateFailures;
        for (Direct2DTextureRenderer* texture : textures)
        {
            if (!texture) continue;
            try { texture->RecreateBitmap(); }
            catch (const std::exception& ex) { recreateFailures.emplace_back(ex.what()); }
        }
        for (Direct2DRenderTargetRenderer* renderTarget : renderTargets)
        {
            if (!renderTarget) continue;
            try { renderTarget->RecreateBitmap(); }
            catch (const std::exception& ex) { recreateFailures.emplace_back(ex.what()); }
        }
        const auto throwIfAnyRecreateFailed = [&recreateFailures] {
            if (recreateFailures.empty()) return;
            throw std::runtime_error(
                "Direct2D device recovery recreated the device but failed to recreate " +
                std::to_string(recreateFailures.size()) +
                " resource(s); they remain in a lost state until recovery is retried. First "
                "failure: " + recreateFailures.front());
        };

        // The underlying D3D11/Direct2D device itself is fully usable at this point regardless of
        // which branch below runs next -- CreateDeviceResources() above already succeeded (it
        // throws on its own failure, which this function does not catch, so DeviceReset correctly
        // never fires for that case). A branch throwing afterward reports a resource- or
        // binding-level follow-up the caller must still handle (an explicit rebind, or which
        // resources didn't survive), not a failed device reset.
        if (deviceEventCallback_) deviceEventCallback_(RendererDeviceEvent::Reset);

        if (restorePreviousActive && previousActive)
        {
            activeRenderTarget_ = previousActive;
            d2dContext_->SetTarget(previousActive->Bitmap());
            throwIfAnyRecreateFailed();
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
        throwIfAnyRecreateFailed();
    }

    void Direct2DRenderer::DebugSimulateContextLoss()
    {
        RecreateDeviceResourcesForRecovery();
    }

    void Direct2DRenderer::DebugRestoreContext()
    {
        // Direct2D's debug channel recreates a complete D3D11/DXGI/D2D resource domain atomically,
        // just like the desktop EasyGL path; there is no separately exposed lost-only state.
        RecreateDeviceResourcesForRecovery();
    }

    void Direct2DRenderer::CreateDeviceResources()
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
            ThrowIfFailed(adapter->GetDesc(&adapterDescription), "IDXGIAdapter::GetDesc");
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
        ReserveTransientCapacity();

        const HWND hwnd = GetWindowHandle();
        RECT clientRect{};
        if (!GetClientRect(hwnd, &clientRect))
            throw std::runtime_error("Direct2DRenderer: GetClientRect failed.");
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
        ThrowIfFailed(dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER),
                      "IDXGIFactory::MakeWindowAssociation");
        CreateBackBufferTarget();
    }

    HWND Direct2DRenderer::GetWindowHandle() const
    {
        const SDL_PropertiesID properties = SDL_GetWindowProperties(window_);
        if (properties == 0)
            throw std::runtime_error(
                std::string("Direct2DRenderer: SDL_GetWindowProperties failed: ") + SDL_GetError());
        const HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!hwnd)
            throw std::runtime_error(
                std::string("Direct2DRenderer: SDL did not expose a Win32 HWND: ") + SDL_GetError());
        return hwnd;
    }

    void Direct2DRenderer::CreateBackBufferTarget()
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

    Microsoft::WRL::ComPtr<ID2D1Bitmap1> Direct2DRenderer::CreateLogicalTargetBitmap() const
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

    void Direct2DRenderer::CreateLogicalTarget()
    {
        logicalTarget_ = CreateLogicalTargetBitmap();
        d2dContext_->SetTarget(logicalTarget_.Get());
    }

    float Direct2DRenderer::ObserveWindowDisplayScale()
    {
        // SDL's own two sizes are the portable expression of the display scale: window units come
        // from Windows' DIP domain, the pixel size is the physical client surface. Their ratio
        // changes exactly when the window moves to a differently scaled monitor or the user changes
        // that monitor's scaling -- which is the event WM_DPICHANGED reports.
        int windowWidth = 0;
        int windowHeight = 0;
        int pixelWidth = 0;
        int pixelHeight = 0;
        if (!SDL_GetWindowSize(window_, &windowWidth, &windowHeight) ||
            !SDL_GetWindowSizeInPixels(window_, &pixelWidth, &pixelHeight) || windowWidth <= 0)
        {
            return observedDisplayScale_;
        }
        const float scale = static_cast<float>(pixelWidth) / static_cast<float>(windowWidth);
        if (observedDisplayScale_ != 0.0f && scale != observedDisplayScale_ && diagnosticsEnabled_)
        {
            std::fprintf(stderr,
                         "[Direct2D diagnostics] display scale changed %.4f -> %.4f "
                         "(window %dx%d, pixels %dx%d).\n",
                         static_cast<double>(observedDisplayScale_), static_cast<double>(scale),
                         windowWidth, windowHeight, pixelWidth, pixelHeight);
        }
        observedDisplayScale_ = scale;
        return scale;
    }

    void Direct2DRenderer::EnsureMainTargetSize()
    {
        if (activeRenderTarget_) return;
        // D2D-55: a DPI or monitor change reaches this renderer as a client-pixel size change,
        // which the comparison below already acts on. Nothing else needs to react, because every
        // Direct2D surface is fixed at 96 DPI and every CNA coordinate is a physical client pixel
        // (D2D-54) -- there is no DIP-derived value to recompute. The scale is observed rather than
        // used, so a run that claims presentation evidence records the DPI it actually ran at.
        (void)ObserveWindowDisplayScale();
        const HWND hwnd = GetWindowHandle();
        RECT clientRect{};
        if (!GetClientRect(hwnd, &clientRect)) throw std::runtime_error("Direct2DRenderer: GetClientRect failed.");
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
            // D2D-68: IDXGISwapChain::ResizeBuffers requires every reference to the old
            // swap-chain buffers released first (the Reset() calls above), so a failure here
            // cannot roll back to the previous buffers -- they are already gone. Treat it exactly
            // like any other device loss (the same recovery EndDrawing/Present already use for
            // D2DERR_RECREATE_TARGET/DXGI_ERROR_DEVICE_REMOVED) instead of leaving
            // backBufferTarget_/logicalTarget_ null.
            const HRESULT resizeResult = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(resizeResult))
            {
                RecreateDeviceResourcesForRecovery();
                throw std::runtime_error(
                    "Direct2D device resources were recreated after a failed swap-chain resize (hr=" +
                    FormatHr(resizeResult) + "); redraw the current frame.");
            }
            try
            {
                CreateBackBufferTarget();
            }
            catch (const std::exception&)
            {
                // Same reasoning: CreateBackBufferTarget failing partway (GetBuffer or
                // CreateBitmapFromDxgiSurface) leaves backBufferTarget_/logicalTarget_ null with
                // no prior buffers to roll back to either.
                RecreateDeviceResourcesForRecovery();
                throw std::runtime_error(
                    "Direct2D device resources were recreated after a failed swap-chain target "
                    "rebuild; redraw the current frame.");
            }
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

    void Direct2DRenderer::EnsureDrawing()
    {
        if (drawing_) return;
        EnsureMainTargetSize();
        d2dContext_->BeginDraw();
        drawing_ = true;
        d2dContext_->SetPrimitiveBlend(ToPrimitiveBlend(blendMode_));
        ApplyOutputClips();
    }

    void Direct2DRenderer::ReserveTransientCapacity()
    {
        transientBitmaps_.reserve(kTransientBitmapReserve);
        transientEffects_.reserve(kTransientEffectReserve);
        transientImages_.reserve(kTransientImageReserve);
        transientImageBrushes_.reserve(kTransientImageBrushReserve);
        spriteBitmapCache_.reserve(kMaxSpriteBitmapCacheEntries);
        spriteBrushCache_.reserve(kMaxSpriteBrushCacheEntries);
    }

    std::size_t Direct2DRenderer::TransientResourceCount() const
    {
        return transientBitmaps_.size() + transientEffects_.size() + transientImages_.size() +
               transientImageBrushes_.size();
    }

    void Direct2DRenderer::FlushTransientChunkIfOverBudget()
    {
        if (!drawing_) return;
        if (TransientResourceCount() < kMaxTransientResources &&
            transientByteEstimate_ < kMaxTransientBytes)
            return;

        // Committing here is safe and order-preserving: Direct2D executes the commands of the
        // closed chunk before any command of the next one, and the reopened chunk restores exactly
        // the state EnsureDrawing would have installed -- the same primitive blend and the same
        // viewport/scissor clips. EnsureMainTargetSize is deliberately NOT re-run: a window resize
        // observed halfway through one SpriteBatch would otherwise swap the target underneath it.
        EndDrawing("transient chunk flush");
        ++transientChunkFlushCount_;
        d2dContext_->BeginDraw();
        drawing_ = true;
        d2dContext_->SetPrimitiveBlend(ToPrimitiveBlend(blendMode_));
        ApplyOutputClips();
    }

    void Direct2DRenderer::EndDrawing(const char* operation)
    {
        if (!drawing_) return;
        ClearOutputClips();
        const HRESULT hr = d2dContext_->EndDraw();
        drawing_ = false;
        const std::size_t transientCount = TransientResourceCount();
        ++endDrawCount_;
        transientResourceReleaseCount_ += transientCount;
        transientResourceHighWater_ = std::max(transientResourceHighWater_, transientCount);
        // D2D-111: clear() releases every ComPtr but keeps the reserved capacity, so the next
        // chunk reuses this storage instead of reallocating it.
        transientBitmaps_.clear();
        transientEffects_.clear();
        transientImages_.clear();
        transientImageBrushes_.clear();
        transientByteEstimate_ = 0;
        if (IsDeviceLossHResult(hr))
        {
            RecreateDeviceResourcesForRecovery();
            throw std::runtime_error(
                "Direct2D device resources were recreated after a device loss (hr=" + FormatHr(hr) +
                "); redraw the current frame.");
        }
        ThrowIfFailed(hr, operation);
    }

    void Direct2DRenderer::ReadRenderTargetPixels(
        const Direct2DRenderTargetRenderer& renderTarget, int level, int x, int y, int width, int height,
        uint8_t* pixels)
    {
        // ID2D1Bitmap::CopyFromRenderTarget reads the device context's CURRENT target. A public
        // RenderTarget2D::GetData may name an unbound level-zero target, so select that bitmap
        // temporarily without changing GraphicsDevice's logical RT binding.
        ID2D1Bitmap1* const requestedBitmap = renderTarget.BitmapForLevel(level);
        EndDrawing("render-target readback");
        Microsoft::WRL::ComPtr<ID2D1Image> previousTarget;
        d2dContext_->GetTarget(previousTarget.GetAddressOf());
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

    void Direct2DRenderer::ReadCurrentTargetPixels(
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
            static_cast<UINT32>(static_cast<std::uint64_t>(x) + static_cast<std::uint64_t>(width)),
            static_cast<UINT32>(static_cast<std::uint64_t>(y) + static_cast<std::uint64_t>(height)));
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
        if (!mapped.bits || mapped.pitch > static_cast<UINT32>(std::numeric_limits<int>::max()))
            throw std::runtime_error("Direct2D readback returned an invalid mapped pointer or row pitch.");
        const std::vector<uint8_t> rgba = CopyBgraToTightRgba(
            mapped.bits, static_cast<int>(mapped.pitch), width, height);
        std::memcpy(pixels, rgba.data(), rgba.size());
        mapGuard.Unmap("ID2D1Bitmap1::Unmap(readback)");
    }

    Direct2DRenderer::PresentationTransform Direct2DRenderer::GetPresentationTransform() const
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

    Direct2DRenderer::PresentationTransform Direct2DRenderer::ComputePresentationTransform() const
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

    D2D1_MATRIX_3X2_F Direct2DRenderer::PresentationMatrix() const
    {
        const PresentationTransform transform = GetPresentationTransform();
        return D2D1::Matrix3x2F(transform.scaleX, 0.0f, 0.0f, transform.scaleY,
                                 transform.offsetX, transform.offsetY);
    }

    D2D1_MATRIX_3X2_F Direct2DRenderer::ViewportMatrix() const
    {
        return viewportSet_ ? D2D1::Matrix3x2F::Translation(
                                  static_cast<float>(viewportX_), static_cast<float>(viewportY_))
                            : D2D1::Matrix3x2F::Identity();
    }

    void Direct2DRenderer::ApplyOutputClips()
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

    void Direct2DRenderer::ClearOutputClips()
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

    bool Direct2DRenderer::ProbeEffectSupport(REFCLSID effectId, const char* operation)
    {
        ComPtr<ID2D1Effect> effect;
        const HRESULT result = d2dContext_->CreateEffect(effectId, &effect);
        if (SUCCEEDED(result)) return true;
        if (result == D2DERR_EFFECT_IS_NOT_REGISTERED) return false;
        if (IsDeviceLossHResult(result))
        {
            RecreateDeviceResourcesForRecovery();
            throw std::runtime_error(
                std::string("Direct2D device resources were recreated after device loss while probing ") +
                operation + " (hr=" + FormatHr(result) + "); redraw the current frame.");
        }
        ThrowIfFailed(result, operation);
        return false;
    }

    bool Direct2DRenderer::SupportsColorMatrixEffect()
    {
        if (colorMatrixEffectSupported_.has_value()) return *colorMatrixEffectSupported_;
        colorMatrixEffectSupported_ = ProbeEffectSupport(
            CLSID_D2D1ColorMatrix, "ID2D1DeviceContext::CreateEffect(ColorMatrix capability probe)");
        return *colorMatrixEffectSupported_;
    }

    bool Direct2DRenderer::SupportsPremultiplyEffect()
    {
        if (premultiplyEffectSupported_.has_value()) return *premultiplyEffectSupported_;
        premultiplyEffectSupported_ = ProbeEffectSupport(
            CLSID_D2D1Premultiply, "ID2D1DeviceContext::CreateEffect(Premultiply capability probe)");
        return *premultiplyEffectSupported_;
    }

    void Direct2DRenderer::Clear(float r, float g, float b, float a)
    {
        EnsureDrawing();
        // XNA's GraphicsDevice.Clear always affects the entire active target; it must not inherit
        // a RasterizerState scissor that happened to be active for preceding SpriteBatch work.
        ClearOutputClips();
        d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
        d2dContext_->Clear(D2D1::ColorF(r, g, b, a));
        ApplyOutputClips();
    }

    void Direct2DRenderer::Present()
    {
        if (activeRenderTarget_)
            throw System::NotSupportedException(
                "Direct2D Present is invalid while a RenderTarget2D is bound; unbind it before presentation.");
        // Resize even when the frame contains no Clear/SpriteBatch call. EnsureDrawing normally
        // reaches this path, but an empty frame must not present a stale-size swap-chain surface.
        EnsureMainTargetSize();
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
        try
        {
            EndDrawing("logical backbuffer presentation");
        }
        catch (...)
        {
            // A device-loss failure already restored d2dContext_'s target independently
            // (RecreateDeviceResourcesForRecovery, called from EndDrawing). An ordinary
            // compositing failure does not, so without this the context would stay pointed at
            // backBufferTarget_ (the physical swap chain surface) instead of the logical
            // target/active render target the caller's next frame expects to draw into.
            d2dContext_->SetTarget(logicalTarget_.Get());
            throw;
        }
        d2dContext_->SetTarget(logicalTarget_.Get());

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

    void Direct2DRenderer::GetViewportSize(int& width, int& height)
    {
        const PresentationTransform transform = GetPresentationTransform();
        width = transform.logicalWidth;
        height = transform.logicalHeight;
    }

    void Direct2DRenderer::SetVirtualResolution(int width, int height)
    {
        if (width < 0 || height < 0 || ((width == 0) != (height == 0)))
            throw System::ArgumentOutOfRangeException(
                "virtual resolution", std::to_string(width) + "x" + std::to_string(height),
                "Direct2D virtual dimensions must either both be zero or both be positive.");
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

    void Direct2DRenderer::SetPresentationMode(int mode)
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
                if (!activeRenderTarget_)
                    EndDrawing("presentation-mode change");
                presentationMode_ = static_cast<CnaPresentationMode>(mode);
                if (!activeRenderTarget_)
                {
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

    void Direct2DRenderer::SetSwapInterval(int interval)
    {
        if (interval < 0 || interval > 2)
            throw System::ArgumentOutOfRangeException(
                "interval", std::to_string(interval),
                "Direct2D supports PresentInterval Immediate, One/Default, and Two only.");
        swapInterval_ = interval;
    }

    bool Direct2DRenderer::TransformWindowToLogical(float windowX, float windowY,
                                                            float& logicalX, float& logicalY) const
    {
        if (activeRenderTarget_) return false;
        const PresentationTransform transform = GetPresentationTransform();
        if (transform.scaleX == 0.0f || transform.scaleY == 0.0f) return false;
        logicalX = (windowX - transform.offsetX) / transform.scaleX;
        logicalY = (windowY - transform.offsetY) / transform.scaleY;
        return true;
    }

    bool Direct2DRenderer::TransformLogicalToWindow(float logicalX, float logicalY,
                                                            float& windowX, float& windowY) const
    {
        if (activeRenderTarget_) return false;
        const PresentationTransform transform = GetPresentationTransform();
        windowX = logicalX * transform.scaleX + transform.offsetX;
        windowY = logicalY * transform.scaleY + transform.offsetY;
        return true;
    }

    void Direct2DRenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
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

    std::unique_ptr<ITextureRenderer> Direct2DRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<Direct2DTextureRenderer>(*this, data);
    }

    std::unique_ptr<ISpriteBatchRenderer> Direct2DRenderer::CreateSpriteBatch()
    {
        return std::make_unique<Direct2DSpriteBatchRenderer>(*this);
    }

    std::unique_ptr<IRenderTargetRenderer> Direct2DRenderer::CreateRenderTarget2D(
        int width, int height, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        return CreateRenderTarget2DEXT(
            width, height, depthFormat, preserveContents, mipMap, multiSampleCount, 0);
    }

    std::unique_ptr<IRenderTargetRenderer> Direct2DRenderer::CreateRenderTarget2DEXT(
        int width, int height, int depthFormat, bool /*preserveContents*/, bool mipMap,
        int multiSampleCount, int surfaceFormat)
    {
        if (surfaceFormat != 0)
            throw System::NotSupportedException(
                "Direct2D RenderTarget2D supports only SurfaceFormat::Color.");
        if (depthFormat != 0)
            throw System::NotSupportedException(
                "Direct2D RenderTarget2D supports only DepthFormat::None.");
        if (mipMap)
            throw System::NotSupportedException(
                "Direct2D rejects mipmapped RenderTarget2D resources because its former NPOT "
                "generated-mip path was not spatially correct.");
        if (multiSampleCount < 0 || multiSampleCount > 1)
            throw System::NotSupportedException(
                "Direct2D RenderTarget2D does not support multisampling; request 0 or 1 sample.");
        return std::make_unique<Direct2DRenderTargetRenderer>(*this, width, height, false);
    }

    void Direct2DRenderer::SetRenderTarget2D(IRenderTargetRenderer* renderTarget)
    {
        auto* const direct2dTarget = dynamic_cast<Direct2DRenderTargetRenderer*>(renderTarget);
        if (renderTarget && !direct2dTarget)
            throw std::runtime_error("Direct2D cannot bind a render target created by another graphics renderer.");
        if (direct2dTarget && direct2dTarget->owner_ != this)
            throw std::runtime_error(
                "Direct2D cannot bind a render target created by another GraphicsDevice.");
        BindRenderTarget(direct2dTarget);
    }

    void Direct2DRenderer::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (count < 0)
            throw std::runtime_error("Direct2D received a negative render-target binding count.");
        if (count > 1)
            throw std::runtime_error("Direct2D supports exactly one active 2D render target; MRT is unsupported.");
        if (count > 0 && !renderTargets)
            throw std::runtime_error("Direct2D received null render-target bindings with a nonzero count.");
        if (count > 0 && renderTargets[0].IsRenderTargetCubeFace())
            throw std::runtime_error("Direct2D does not support RenderTargetCube bindings.");
        IRenderTargetRenderer* const renderTarget =
            count > 0 ? renderTargets[0].GetRenderTarget2D() : nullptr;
        if (count > 0 && !renderTarget)
            throw std::runtime_error("Direct2D received a null RenderTarget2D binding.");
        SetRenderTarget2D(renderTarget);
    }

    void Direct2DRenderer::BindRenderTarget(Direct2DRenderTargetRenderer* renderTarget)
    {
        if (activeRenderTarget_ == renderTarget) return;
        // D2D-61: validate the incoming target's device generation (and fetch its native bitmap)
        // before touching any logical or native state. Bitmap() throws for a target belonging to
        // a lost device generation that was never registered for recovery; doing that check after
        // activeRenderTarget_ was already reassigned left the logical and native target pointing
        // at different resources on rejection.
        ID2D1Bitmap1* const nativeTarget = renderTarget ? renderTarget->Bitmap() : nullptr;
        EndDrawing("render-target switch");
        Direct2DRenderTargetRenderer* const previousTarget = activeRenderTarget_;
        if (!renderTarget)
        {
            activeRenderTarget_ = nullptr;
            try
            {
                EnsureMainTargetSize();
            }
            catch (...)
            {
                activeRenderTarget_ = previousTarget;
                if (previousTarget) d2dContext_->SetTarget(previousTarget->Bitmap());
                throw;
            }
        }
        activeRenderTarget_ = renderTarget;
        d2dContext_->SetTarget(renderTarget ? nativeTarget : logicalTarget_.Get());
    }

    void Direct2DRenderer::ReleaseRenderTarget(Direct2DRenderTargetRenderer* renderTarget)
    {
        if (activeRenderTarget_ == renderTarget) BindRenderTarget(nullptr);
    }

    void Direct2DRenderer::SetScissorRect(int x, int y, int width, int height)
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

    void Direct2DRenderer::SetViewport(int x, int y, int width, int height,
                                              float minDepth, float maxDepth)
    {
        if (width < 0 || height < 0)
            throw System::ArgumentOutOfRangeException(
                "viewport", "negative size", "Direct2D viewport dimensions must not be negative.");
        if (!std::isfinite(minDepth) || !std::isfinite(maxDepth) ||
            minDepth != 0.0f || maxDepth != 1.0f)
            throw System::ArgumentOutOfRangeException(
                "depth range", "unsupported",
                "Direct2D's 2D viewport accepts only MinDepth=0 and MaxDepth=1.");
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

    void Direct2DRenderer::ApplyDepthStencilState(
        bool depthEnable, bool depthWriteEnable, int /*depthFunc*/,
        bool stencilEnable, int /*stencilFunc*/,
        int /*stencilPass*/, int /*stencilFail*/, int /*stencilDepthFail*/,
        int /*stencilMask*/, int /*stencilWriteMask*/, int /*referenceStencil*/,
        bool /*twoSidedStencilMode*/,
        int /*ccwStencilFunc*/, int /*ccwStencilPass*/,
        int /*ccwStencilFail*/, int /*ccwStencilDepthFail*/)
    {
        // D2D-99: GraphicsDevice applies its cached DepthStencilState::Default exactly once during
        // construction before application code can run. Permit only that unavoidable bootstrap
        // write; every later depth-enabled state is a real unsupported request and must not be
        // silently accepted by a renderer that advertises no depth buffer. DepthStencilState::None
        // remains the supported 2D state.
        if (initialDeviceDefaultDepthStatePending_)
        {
            initialDeviceDefaultDepthStatePending_ = false;
            if (depthEnable && depthWriteEnable && !stencilEnable) return;
        }
        if (stencilEnable)
        {
            throw System::NotSupportedException(
                "Direct2D does not support DepthStencilState.StencilEnable; it has no depth/stencil "
                "buffer. Use CNA_GRAPHICS_RENDERER=D3D11 for stencil-dependent rendering.");
        }
        if (depthEnable || depthWriteEnable)
            throw System::NotSupportedException(
                "Direct2D supports only DepthStencilState::None after device initialization; it "
                "has no depth buffer. Use CNA_GRAPHICS_RENDERER=D3D11 for depth-tested rendering.");
    }

    void Direct2DRenderer::ApplyRasterizerState(int cullMode, int fillMode,
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
        if (cullMode < 0 || cullMode > 2)
            throw System::ArgumentOutOfRangeException(
                "cullMode", std::to_string(cullMode), "Direct2D received an unknown CullMode value.");
        if (fillMode == fillModeWireFrame)
        {
            throw std::runtime_error(
                "Direct2D does not support RasterizerState.FillMode.WireFrame; it has no "
                "wireframe rasterization pipeline. Use CNA_GRAPHICS_RENDERER=D3D11 for wireframe "
                "rendering.");
        }
        if (fillMode != 0)
            throw System::ArgumentOutOfRangeException(
                "fillMode", std::to_string(fillMode), "Direct2D received an unknown FillMode value.");
        if (!std::isfinite(depthBias) || !std::isfinite(slopeScaleDepthBias))
            throw System::ArgumentOutOfRangeException(
                "depth bias", "non-finite", "Direct2D depth-bias values must be finite.");
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

    void Direct2DRenderer::ApplyBlendState(
        int colorSrcBlend, int alphaSrcBlend, int colorDstBlend, int alphaDstBlend,
        int colorBlendFunc, int alphaBlendFunc, const BlendWriteState& writeState)
    {
        pendingBlendMode_.reset();
        pendingNonPremultipliedSource_ = false;
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
        const Direct2DBlendMode candidateMode = BlendStateToDirect2DBlendMode(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend,
            colorBlendFunc, alphaBlendFunc);
        // NonPremultiplied differs from AlphaBlend only in the source pixel convention.  A
        // transient CPU-generated premultiplied bitmap is used for it on each affected draw.
        pendingBlendMode_ = candidateMode;
        pendingNonPremultipliedSource_ = colorSrcBlend == 4 && colorDstBlend == 5;
    }

    void Direct2DRenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        constexpr float epsilon = 0.0001f;
        if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a))
        {
            pendingBlendMode_.reset();
            pendingNonPremultipliedSource_ = false;
            throw System::ArgumentOutOfRangeException(
                "blendFactor", "non-finite",
                "Direct2D GraphicsDevice.BlendFactor components must be finite.");
        }
        if (std::abs(r - 1.0f) > epsilon || std::abs(g - 1.0f) > epsilon ||
            std::abs(b - 1.0f) > epsilon || std::abs(a - 1.0f) > epsilon)
        {
            std::ostringstream message;
            message << "Direct2D does not support GraphicsDevice.BlendFactor; only Color::White is valid "
                    << "(received " << r << ", " << g << ", " << b << ", " << a << ").";
            pendingBlendMode_.reset();
            pendingNonPremultipliedSource_ = false;
            throw std::runtime_error(message.str());
        }
        if (pendingBlendMode_)
        {
            blendMode_ = *pendingBlendMode_;
            nonPremultipliedSource_ = pendingNonPremultipliedSource_;
            pendingBlendMode_.reset();
            pendingNonPremultipliedSource_ = false;
            if (drawing_) d2dContext_->SetPrimitiveBlend(ToPrimitiveBlend(blendMode_));
        }
    }

    int Direct2DRenderer::ApplyMultiSampleCount(int requestedMultiSampleCount)
    {
        if (requestedMultiSampleCount < 0 || requestedMultiSampleCount > 1)
            throw System::NotSupportedException(
                "Direct2D does not support multisampling; request MultiSampleCount 0 or 1.");
        return 0;
    }

    void Direct2DRenderer::UpdatePresentationFormatEXT(
        int backBufferFormat, int depthStencilFormat, bool /*isFullScreen*/)
    {
        if (backBufferFormat != 0)
            throw System::NotSupportedException(
                "Direct2D supports only SurfaceFormat::Color for the backbuffer.");
        if (depthStencilFormat != 0)
            throw System::NotSupportedException(
                "Direct2D supports only DepthFormat::None for the backbuffer.");
    }

    int Direct2DRenderer::GetMaxTextureDimension() const
    {
        return static_cast<int>(d2dContext_->GetMaximumBitmapSize());
    }

    bool Direct2DRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        return SupportsDirect2DCapability(capability);
    }

    ID2D1Bitmap1* Direct2DRenderer::CreateBitmapFromRgba(const uint8_t* rgba, int width,
                                                         int height) const
    {
        (void) CheckedRgbaByteCount(width, height);
        const int maximumDimension = GetMaxTextureDimension();
        if (width > maximumDimension || height > maximumDimension)
            throw System::ArgumentOutOfRangeException(
                "dimensions", std::to_string(width) + "x" + std::to_string(height),
                "Direct2D bitmap exceeds ID2D1DeviceContext::GetMaximumBitmapSize().");
        const int rowPitch = width * 4;
        const std::vector<uint8_t> bgra = CopyRgbaToTightBgra(rgba, rowPitch, width, height);
        D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            // D2D-44: application content is always premultiplied at this boundary. The former
            // alpha-ignoring variant became unreachable when D2D-28 stopped building an
            // alpha-ignore CPU fallback bitmap, and an unreachable alpha mode in a renderer whose
            // alpha contract is documented per boundary is exactly the kind of thing that misleads.
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);
        ComPtr<ID2D1Bitmap1> bitmap;
        ThrowIfFailed(d2dContext_->CreateBitmap(
                          D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)),
                          bgra.data(), static_cast<UINT32>(rowPitch), &properties, &bitmap),
                      "ID2D1DeviceContext::CreateBitmap");
        return bitmap.Detach();
    }

    bool Direct2DRenderer::TryUploadBitmapRgba(ID2D1Bitmap1& bitmap, const uint8_t* rgba,
                                               int width, int height) const
    {
        // D2D-106: recreating an ID2D1Bitmap1 for every SetData allocates a fresh D3D11 texture and
        // discards a perfectly good one. CopyFromMemory overwrites the existing storage instead, so
        // a repeated upload of unchanged dimensions costs one staging conversion and no COM or GPU
        // allocation at all -- and every untouched mip keeps its COM identity, because nothing here
        // touches the other levels.
        const D2D1_SIZE_U size = bitmap.GetPixelSize();
        if (size.width != static_cast<UINT32>(width) || size.height != static_cast<UINT32>(height))
            return false;
        ConvertRgbaToTightBgraInto(rgba, width * 4, width, height, uploadScratch_);
        const HRESULT hr = bitmap.CopyFromMemory(
            nullptr, uploadScratch_.data(), static_cast<UINT32>(width * 4));
        if (SUCCEEDED(hr)) return true;
        // A lost device must recover rather than silently fall back to a create-and-swap that
        // would fail again for the same reason.
        if (IsDeviceLossHResult(hr))
            ThrowIfFailed(hr, "ID2D1Bitmap::CopyFromMemory(in-place level upload)");
        // Any other failure (a runtime that does not implement CopyFromMemory for this bitmap
        // kind, for instance) is reported so the caller can recreate the bitmap from the same
        // pixels and stay consistent with its shadow.
        return false;
    }

    ID2D1Bitmap1* Direct2DRenderer::FindCachedSpriteBitmap(const SpriteBitmapCacheKey& key) const
    {
        for (const SpriteBitmapCacheEntry& entry : spriteBitmapCache_)
        {
            if (entry.key == key) return entry.bitmap.Get();
        }
        return nullptr;
    }

    void Direct2DRenderer::StoreCachedSpriteBitmap(
        const SpriteBitmapCacheKey& key, const Microsoft::WRL::ComPtr<ID2D1Bitmap1>& bitmap,
        std::size_t byteCount)
    {
        if (!bitmap) return;
        // A single sprite larger than the whole budget would evict everything and still not fit;
        // leave it uncached rather than thrashing the cache for it.
        if (byteCount > kMaxSpriteBitmapCacheBytes) return;
        while (!spriteBitmapCache_.empty() &&
               (spriteBitmapCache_.size() >= kMaxSpriteBitmapCacheEntries ||
                spriteBitmapCacheBytes_ + byteCount > kMaxSpriteBitmapCacheBytes))
        {
            // Oldest first. An evicted bitmap may still be referenced by a command recorded
            // earlier in this chunk, so it moves into the transient set and is released with it
            // at the next EndDraw instead of being destroyed here.
            SpriteBitmapCacheEntry evicted = std::move(spriteBitmapCache_.front());
            spriteBitmapCache_.erase(spriteBitmapCache_.begin());
            spriteBitmapCacheBytes_ -= std::min(spriteBitmapCacheBytes_, evicted.byteCount);
            if (evicted.bitmap) transientBitmaps_.push_back(std::move(evicted.bitmap));
        }
        spriteBitmapCache_.push_back(SpriteBitmapCacheEntry{key, bitmap, byteCount});
        spriteBitmapCacheBytes_ += byteCount;
    }

    ID2D1ImageBrush* Direct2DRenderer::FindCachedSpriteBrush(const SpriteBrushCacheKey& key) const
    {
        for (const SpriteBrushCacheEntry& entry : spriteBrushCache_)
        {
            if (entry.key == key) return entry.brush.Get();
        }
        return nullptr;
    }

    void Direct2DRenderer::StoreCachedSpriteBrush(
        const SpriteBrushCacheKey& key, const Microsoft::WRL::ComPtr<ID2D1Effect>& tintEffect,
        const Microsoft::WRL::ComPtr<ID2D1Image>& tintOutput,
        const Microsoft::WRL::ComPtr<ID2D1Effect>& premultiplyEffect,
        const Microsoft::WRL::ComPtr<ID2D1Image>& premultiplyOutput,
        const Microsoft::WRL::ComPtr<ID2D1ImageBrush>& brush)
    {
        if (!brush) return;
        while (spriteBrushCache_.size() >= kMaxSpriteBrushCacheEntries)
        {
            SpriteBrushCacheEntry evicted = std::move(spriteBrushCache_.front());
            spriteBrushCache_.erase(spriteBrushCache_.begin());
            if (evicted.tintEffect) transientEffects_.push_back(std::move(evicted.tintEffect));
            if (evicted.tintOutput) transientImages_.push_back(std::move(evicted.tintOutput));
            if (evicted.premultiplyEffect)
                transientEffects_.push_back(std::move(evicted.premultiplyEffect));
            if (evicted.premultiplyOutput)
                transientImages_.push_back(std::move(evicted.premultiplyOutput));
            if (evicted.brush) transientImageBrushes_.push_back(std::move(evicted.brush));
        }
        spriteBrushCache_.push_back(SpriteBrushCacheEntry{
            key, tintEffect, tintOutput, premultiplyEffect, premultiplyOutput, brush});
    }

    D2D1_MATRIX_3X2_F Direct2DRenderer::ToD2DMatrix(const Matrix& matrix)
    {
        // Both XNA Matrix and Direct2D use row-vector affine form for 2D points.
        return D2D1::Matrix3x2F(matrix.M11, matrix.M12, matrix.M21, matrix.M22, matrix.M41, matrix.M42);
    }

    D2D1_MATRIX_3X2_F Direct2DRenderer::Multiply(const D2D1_MATRIX_3X2_F& left,
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

    void Direct2DRenderer::MakeSpritePixels(
        const Direct2DTextureRenderer& texture, const Rectangle& sourceRectangle,
        const Color& color, SpriteEffects effects, int addressU, int addressV, int mipLevel,
        std::vector<uint8_t>& result) const
    {
        const int sourceWidth = sourceRectangle.Width;
        const int sourceHeight = sourceRectangle.Height;
        // D2D-109: assign() reuses the caller's buffer (its capacity survives from the previous
        // sprite), so a long batch of CPU-fallback draws stops allocating after the first one. The
        // zero fill is retained because the address-mode branches below may skip texels.
        result.assign(static_cast<std::size_t>(sourceWidth) * sourceHeight * 4u, 0);
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
                // blend preset, and must never additionally attenuate already-premultiplied
                // source RGB a second time.
                const int fragR = sourceR * color.getRProperty() / 255;
                const int fragG = sourceG * color.getGProperty() / 255;
                const int fragB = sourceB * color.getBProperty() / 255;
                const int fragA = alpha * color.getAProperty() / 255;
                output[0] = static_cast<uint8_t>(fragR);
                output[1] = static_cast<uint8_t>(fragG);
                output[2] = static_cast<uint8_t>(fragB);
                output[3] = static_cast<uint8_t>(fragA);
            }
        }
    }

    void Direct2DRenderer::MakeMipBlendedSpritePixels(
        const Direct2DTextureRenderer& texture, const Rectangle& lowerSource,
        const Rectangle& upperSource, int lowerLevel, int upperLevel, float blendFraction,
        const Color& color, SpriteEffects effects, int addressU, int addressV,
        std::vector<uint8_t>& result) const
    {
        // D2D-74: samples both mip levels bracketing a fractional LOD and linearly blends them
        // (in the same premultiplied-or-not space MakeSpritePixels uses) before tinting, rather
        // than snapping to a single nearest level. Output is on lowerSource's pixel grid; upper-
        // level samples are proportionally rescaled onto that same grid.
        const int sourceWidth = lowerSource.Width;
        const int sourceHeight = lowerSource.Height;
        result.assign(static_cast<std::size_t>(sourceWidth) * sourceHeight * 4u, 0);
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
                output[0] = static_cast<uint8_t>(std::lround(fragR));
                output[1] = static_cast<uint8_t>(std::lround(fragG));
                output[2] = static_cast<uint8_t>(std::lround(fragB));
                output[3] = static_cast<uint8_t>(std::lround(fragA));
            }
        }
    }

    void Direct2DRenderer::DrawSprite(
        const ITextureRenderer& texture, const Rectangle& destinationRectangle,
        const Rectangle& sourceRectangle, const Color& color, float rotation, const Vector2& origin,
        SpriteEffects effects, const Matrix& batchTransform, int textureFilter, int addressU, int addressV)
    {
        constexpr int supportedEffectBits =
            static_cast<int>(SpriteEffects::FlipHorizontally) |
            static_cast<int>(SpriteEffects::FlipVertically);
        const int effectBits = static_cast<int>(effects);
        if ((effectBits & ~supportedEffectBits) != 0)
        {
            throw System::ArgumentOutOfRangeException(
                "effects", std::to_string(effectBits),
                "Direct2D SpriteBatch supports only horizontal and vertical SpriteEffects flips.");
        }
        if (sourceRectangle.Width <= 0 || sourceRectangle.Height <= 0 ||
            destinationRectangle.Width == 0 || destinationRectangle.Height == 0)
            return;
        const float transformValues[] = {
            rotation, origin.X, origin.Y,
            batchTransform.M11, batchTransform.M12, batchTransform.M21,
            batchTransform.M22, batchTransform.M41, batchTransform.M42};
        if (std::any_of(std::begin(transformValues), std::end(transformValues),
                        [](float value) { return !std::isfinite(value); }))
        {
            throw System::ArgumentOutOfRangeException(
                "sprite transform", "non-finite",
                "Direct2D SpriteBatch rotation, origin, and 2D matrix components must be finite.");
        }

        const auto* ordinaryTexture = dynamic_cast<const Direct2DTextureRenderer*>(&texture);
        const auto* renderTargetTexture = dynamic_cast<const Direct2DRenderTargetRenderer*>(&texture);
        if (!ordinaryTexture && !renderTargetTexture)
            throw std::runtime_error("Direct2D SpriteBatch received a texture created by another graphics renderer.");
        if ((ordinaryTexture && ordinaryTexture->owner_ != this) ||
            (renderTargetTexture && renderTargetTexture->owner_ != this))
            throw std::runtime_error(
                "Direct2D SpriteBatch received a texture created by another GraphicsDevice.");
        // D2D-86: sampling a RenderTarget2D while it is still the actively bound render target is
        // a read/write alias of the same underlying ID2D1Bitmap1 -- Direct2D's own documented
        // contract requires a bitmap to not be simultaneously the current target and a DrawImage/
        // brush input. Reject explicitly rather than let that produce whatever undefined GPU
        // behavior Direct2D (or WineD3D's reimplementation of it) happens to do.
        if (renderTargetTexture && renderTargetTexture == activeRenderTarget_)
            throw System::NotSupportedException(
                "Direct2D cannot sample a RenderTarget2D as a SpriteBatch source while it is "
                "still the actively bound render target (read/write alias of the same bitmap). "
                "Unbind it first (GraphicsDevice::SetRenderTarget(nullptr) or a different target) "
                "before drawing it as a source.");
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
            // ID2D1Bitmap1 has no implicit mip chain. Ordinary Texture2D selects the nearest
            // authored level from the complete 2D output transform; RenderTarget2D creation
            // rejects mipMap=true and therefore remains at level zero.
            const PresentationTransform presentation = GetPresentationTransform();
            const double fractionalLod = FractionalMipLevelForTransform(
                sourceRectangle.Width, sourceRectangle.Height,
                destinationRectangle.Width, destinationRectangle.Height,
                rotation, batchTransform, presentation.scaleX, presentation.scaleY, &minifying);
            const int preferredMip = std::isfinite(fractionalLod)
                ? static_cast<int>(std::floor(fractionalLod)) : std::numeric_limits<int>::max();
            selectedMipLevel = ordinaryTexture
                ? ordinaryTexture->SelectAvailableMipLevel(preferredMip)
                : 0;

            // D2D-74: for MipLinear-family filters, interpolate between the two mip levels
            // bracketing the fractional LOD instead of snapping to the nearest one. Only ordinary
            // Texture2D sources have the CPU-side RGBA shadow this needs. Both bracketing levels
            // must actually be initialized; an incomplete authored chain falls back to nearest.
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

        // Resource validation may finish a prior Direct2D batch during recovery. Begin a fresh
        // batch so this sprite receives the caller's blend and clip state.
        EnsureDrawing();

        const bool copyBlend = blendMode_ == Direct2DBlendMode::Copy;
        const bool needsTintEffect = !IsWhite(color);
        // D2D-74: a mip-linear blend has no GPU built-in-effect equivalent here, so it always
        // takes the CPU path (MakeMipBlendedSpritePixels), independent of ColorMatrix/Premultiply
        // effect support.
        const bool requiresCpuBitmap = ordinaryTexture &&
            (mipBlendActive || ((needsTintEffect || nonPremultipliedSource_) &&
              ((needsTintEffect && !SupportsColorMatrixEffect()) ||
               (nonPremultipliedSource_ && !SupportsPremultiplyEffect()))));
        // D2D-85: unlike ordinaryTexture (which falls back to MakeSpritePixels' CPU path above),
        // a RenderTarget2D source has no CPU shadow to fall back to. Without this check, the same
        // missing-effect condition would fall through to the ImageBrush branch below and its
        // ThrowIfFailed(CreateEffect(...)) would surface a raw HRESULT (e.g. "hr=0x88990028")
        // wrapped in a generic std::runtime_error instead of a named, documented capability gap.
        if (renderTargetTexture &&
            ((needsTintEffect && !SupportsColorMatrixEffect()) ||
             (nonPremultipliedSource_ && !SupportsPremultiplyEffect())))
        {
            throw System::NotSupportedException(
                "Direct2D cannot tint or apply a NonPremultiplied conversion to a "
                "RenderTarget2D SpriteBatch source on this device: the required GPU "
                "ColorMatrix/Premultiply effect is unavailable, and RenderTarget2D sources "
                "(unlike Texture2D) have no CPU fallback path. Draw with Color::White and a "
                "premultiplied source, or copy the render target to a Texture2D first.");
        }

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
            // D2D-109: the materialized pixels are a pure function of the fields below, so an
            // identical repeated draw (the common "many tinted sprites from one atlas" case) can
            // reuse the bitmap already uploaded for the first of them. Every input of
            // MakeSpritePixels/MakeMipBlendedSpritePixels is part of the key -- content version
            // included, so any SetData invalidates the entries derived from the old pixels.
            SpriteBitmapCacheKey cacheKey;
            cacheKey.source = ordinaryTexture;
            cacheKey.contentVersion = ordinaryTexture->ContentVersion();
            cacheKey.deviceGeneration = deviceGeneration_;
            cacheKey.mipLevel = selectedMipLevel;
            cacheKey.upperMipLevel = mipBlendActive ? mipBlendUpperLevel : -1;
            cacheKey.mipBlendFraction = mipBlendActive ? mipBlendFraction : 0.0f;
            cacheKey.sourceX = localSource.X;
            cacheKey.sourceY = localSource.Y;
            cacheKey.sourceWidth = localSource.Width;
            cacheKey.sourceHeight = localSource.Height;
            cacheKey.color = color.getPackedValueProperty();
            cacheKey.spriteEffects = static_cast<int>(effects);
            cacheKey.addressU = addressU;
            cacheKey.addressV = addressV;
            cacheKey.nonPremultipliedSource = nonPremultipliedSource_;

            if (ID2D1Bitmap1* const cached = FindCachedSpriteBitmap(cacheKey))
            {
                ++spriteBitmapCacheHits_;
                bitmap = cached;
            }
            else
            {
                ++spriteBitmapCacheMisses_;
                if (mipBlendActive)
                {
                    MakeMipBlendedSpritePixels(*ordinaryTexture, localSource, mipBlendUpperSource,
                                               selectedMipLevel, mipBlendUpperLevel, mipBlendFraction,
                                               color, effects, addressU, addressV,
                                               spritePixelScratch_);
                }
                else
                {
                    MakeSpritePixels(*ordinaryTexture, localSource, color, effects, addressU,
                                     addressV, selectedMipLevel, spritePixelScratch_);
                }
                transient.Attach(CreateBitmapFromRgba(spritePixelScratch_.data(), localSource.Width,
                                                       localSource.Height));
                bitmap = transient.Get();
                // Held by the transient set no matter what the cache decides: a bitmap the cache
                // declines (one larger than the whole budget) must still outlive the deferred
                // command recorded below, exactly as before the cache existed.
                transientBitmaps_.push_back(transient);
                StoreCachedSpriteBitmap(cacheKey, transient, spritePixelScratch_.size());
                // D2D-112: the CPU fallback is the only transient whose size is unbounded by the
                // renderer itself, so its footprint is what the chunk byte budget tracks.
                transientByteEstimate_ += spritePixelScratch_.size();
            }
            localSource = Rectangle(0, 0, localSource.Width, localSource.Height);
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
            // D2D-107: an identical decorated configuration reuses the graph built for the
            // previous sprite. The reuse writes NO property on the cached objects -- every value
            // that would be written is part of the key, so the objects already carry it. That is
            // what makes reuse safe even though Direct2D may read a brush's state as late as
            // EndDraw. The destination position is intentionally not part of the key: it lives in
            // the context transform, so a row of sprites from one atlas cell shares one entry.
            SpriteBrushCacheKey brushKey;
            brushKey.image = bitmap;
            brushKey.deviceGeneration = deviceGeneration_;
            brushKey.color = color.getPackedValueProperty();
            brushKey.tinted = needsTintEffect;
            brushKey.straightAlphaTint = needsTintEffect && (nonPremultipliedSource_ || copyBlend);
            brushKey.premultiplyStage = nonPremultipliedSource_;
            brushKey.sourceX = localSource.X;
            brushKey.sourceY = localSource.Y;
            brushKey.sourceWidth = localSource.Width;
            brushKey.sourceHeight = localSource.Height;
            brushKey.extendU = addressU;
            brushKey.extendV = addressV;
            brushKey.interpolationMode = static_cast<int>(interpolation);
            brushKey.spriteEffects = static_cast<int>(effects);
            brushKey.originX = bitmapOrigin.X;
            brushKey.originY = bitmapOrigin.Y;

            ComPtr<ID2D1ImageBrush> imageBrush;
            ComPtr<ID2D1Effect> tintEffect;
            ComPtr<ID2D1Image> tintOutput;
            ComPtr<ID2D1Effect> premultiplyEffect;
            ComPtr<ID2D1Image> premultiplyOutput;
            if (ID2D1ImageBrush* const cachedBrush = FindCachedSpriteBrush(brushKey))
            {
                ++spriteBrushCacheHits_;
                imageBrush = cachedBrush;
            }
            else
            {
                ++spriteBrushCacheMisses_;
                ID2D1Image* input = bitmap;
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
                ThrowIfFailed(d2dContext_->CreateImageBrush(input, &imageProperties, nullptr, &imageBrush),
                              "ID2D1DeviceContext::CreateImageBrush(SpriteBatch source)");
                // ID2D1ImageBrush's sourceRectangle bounds which part of the image is visible, but
                // does not by itself align that rectangle's corner with the brush's local origin --
                // brush-local (0,0) still maps to the underlying image's (0,0) unless SetTransform
                // compensates, for ANY source origin, not just a negative one. D2D-80: the previous
                // std::max(0.0f, -X) formula was a no-op for negative X (its own -X is already
                // positive there) but incorrectly clamped to zero for X >= 0, leaving a
                // positively-offset atlas crop sampling from the image's real origin instead of the
                // requested one. The general, direction-correct shift is -X unconditionally.
                const float clippedLeft = -static_cast<float>(localSource.X);
                const float clippedTop = -static_cast<float>(localSource.Y);
                // The crop correction has to run before reflection. The SpriteBatch origin is a
                // destination-space shift and therefore runs after reflection: without that final
                // translation an ImageBrush is anchored at local zero while FillRectangle starts at
                // -origin, leaving part of a flipped, positively-offset atlas crop unsampled (D2D-82).
                const D2D1_MATRIX_3X2_F clippedOrigin =
                    D2D1::Matrix3x2F::Translation(clippedLeft, clippedTop);
                const D2D1_MATRIX_3X2_F spriteOrigin =
                    D2D1::Matrix3x2F::Translation(-bitmapOrigin.X, -bitmapOrigin.Y);
                imageBrush->SetTransform(Multiply(
                    Multiply(clippedOrigin,
                             MakeSpriteBrushTransform(localSource.Width, localSource.Height, effects)),
                    spriteOrigin));
                StoreCachedSpriteBrush(brushKey, tintEffect, tintOutput, premultiplyEffect,
                                       premultiplyOutput, imageBrush);
            }

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
                const auto restoreApplicationTarget = [this] {
                    if (!d2dContext_) return;
                    ID2D1Image* target = logicalTarget_.Get();
                    if (activeRenderTarget_ &&
                        activeRenderTarget_->deviceGeneration_ == deviceGeneration_)
                    {
                        target = activeRenderTarget_->bitmap_.Get();
                    }
                    d2dContext_->SetTarget(target);
                };
                try
                {
                    d2dContext_->SetTarget(commandList.Get());
                    d2dContext_->BeginDraw();
                    drawing_ = true;
                    d2dContext_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
                    d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
                    d2dContext_->FillRectangle(destination, imageBrush.Get());
                    EndDrawing("decorated sprite command list");
                    ThrowIfFailed(commandList->Close(), "ID2D1CommandList::Close(SpriteBatch source)");
                }
                catch (...)
                {
                    restoreApplicationTarget();
                    throw;
                }

                restoreApplicationTarget();
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
            // must outlive this method but can be released once EndDraw completed. On a cache hit
            // only the brush is set here; the extra transient reference is redundant with the
            // cache's own but preserves exactly the pre-cache lifetime guarantee.
            if (tintEffect) transientEffects_.push_back(tintEffect);
            if (tintOutput) transientImages_.push_back(tintOutput);
            if (premultiplyEffect) transientEffects_.push_back(premultiplyEffect);
            if (premultiplyOutput) transientImages_.push_back(premultiplyOutput);
            transientImageBrushes_.push_back(imageBrush);
            FlushTransientChunkIfOverBudget();
            return;
        }
        // DrawBitmap is hard-wired to source-over on WineD3D. DrawImage has an explicit image
        // composite mode, so Copy/Add do not depend on mutable primitive-blend state. Translate
        // the source rectangle into the same local destination origin DrawBitmap used above, then
        // let finalTransform provide scale, rotation, viewport, and presentation.
        // DrawImage maps imageRectangle's upper-left corner to targetOffset=(0,0). Subtracting
        // localSource.X/Y here a second time shifts an atlas crop twice and was the root of
        // D2D-82's combined crop/origin/rotation Y discrepancy. Only the SpriteBatch origin is a
        // destination-space translation at this stage.
        const D2D1_MATRIX_3X2_F imageTransform = Multiply(
            D2D1::Matrix3x2F::Translation(-bitmapOrigin.X, -bitmapOrigin.Y), finalTransform);
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
        // D2D-114: retain the source bitmap until this chunk's EndDraw, exactly as the brush and
        // command-list branches already retain theirs. Whether Direct2D's own DrawImage keeps a
        // reference for a deferred command is not something CNA should depend on: an application
        // is allowed to dispose a Texture2D after SpriteBatch::End and before Present, and that
        // must not turn into a draw from released memory. The CPU-fallback bitmap is skipped here
        // because it was already added to the transient set when it was created.
        if (!requiresCpuBitmap) transientBitmaps_.push_back(ComPtr<ID2D1Bitmap1>(bitmap));
        d2dContext_->SetTransform(imageTransform);
        d2dContext_->DrawImage(
            bitmap, &imageOffset, &source,
            interpolation,
            ToImageCompositeMode(blendMode_));
        d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
        if (needsCompositeMask) d2dContext_->PopLayer();
        FlushTransientChunkIfOverBudget();
    }

    void Direct2DRenderer::Ensure3DSupported(const char* operation) const
    {
        const_cast<Direct2DRenderer*>(this)->HandleUnsupported3DCall(
            "Direct2D", operation ? operation : "3D operation");
    }

    void Direct2DRenderer::ClearColorAndDepth(float, float, float, float, float)
    {
        HandleUnsupported3DCall("Direct2D", "ClearColorAndDepth");
    }
    void Direct2DRenderer::ClearDepth(float)
    {
        HandleUnsupported3DCall("Direct2D", "ClearDepth");
    }
    void Direct2DRenderer::ClearStencil(int)
    {
        HandleUnsupported3DCall("Direct2D", "ClearStencil");
    }
    void Direct2DRenderer::ClearDepthAndStencil(float, int)
    {
        HandleUnsupported3DCall("Direct2D", "ClearDepthAndStencil");
    }
    void Direct2DRenderer::ClearColorAndStencil(float, float, float, float, int)
    {
        HandleUnsupported3DCall("Direct2D", "ClearColorAndStencil");
    }
    void Direct2DRenderer::ClearColorDepthAndStencil(float, float, float, float, float, int)
    {
        HandleUnsupported3DCall("Direct2D", "ClearColorDepthAndStencil");
    }
    void Direct2DRenderer::SetDepthTestEnabled(bool)
    {
        HandleUnsupported3DCall("Direct2D", "SetDepthTestEnabled");
    }
    void Direct2DRenderer::SetBlendEnabled(bool)
    {
        HandleUnsupported3DCall("Direct2D", "SetBlendEnabled");
    }
    void Direct2DRenderer::SetDepthWriteEnabled(bool)
    {
        HandleUnsupported3DCall("Direct2D", "SetDepthWriteEnabled");
    }
    std::unique_ptr<IVertexBufferRenderer> Direct2DRenderer::CreateVertexBuffer(
        int vertexCapacity)
    {
        HandleUnsupported3DCall("Direct2D", "CreateVertexBuffer");
        return std::make_unique<NoOpVertexBufferRenderer>(vertexCapacity);
    }
    std::unique_ptr<IIndexBufferRenderer> Direct2DRenderer::CreateIndexBuffer16(
        int indexCapacity)
    {
        HandleUnsupported3DCall("Direct2D", "CreateIndexBuffer16");
        return std::make_unique<NoOpIndexBufferRenderer>(indexCapacity);
    }
    std::unique_ptr<ITexture3DRenderer> Direct2DRenderer::CreateTexture3D(
        int width, int height, int depth, bool, int)
    {
        HandleUnsupported3DCall("Direct2D", "CreateTexture3D");
        return std::make_unique<NoOpTexture3DRenderer>(width, height, depth);
    }
    std::unique_ptr<ITextureCubeRenderer> Direct2DRenderer::CreateTextureCube(
        int size, bool, int)
    {
        HandleUnsupported3DCall("Direct2D", "CreateTextureCube");
        return std::make_unique<NoOpTextureCubeRenderer>(size);
    }
    std::unique_ptr<IRenderTargetCubeRenderer> Direct2DRenderer::CreateRenderTargetCube(
        int size, int, bool, bool, int)
    {
        HandleUnsupported3DCall("Direct2D", "CreateRenderTargetCube");
        return std::make_unique<NoOpRenderTargetCubeRenderer>(size);
    }
    std::unique_ptr<IOcclusionQueryRenderer> Direct2DRenderer::CreateOcclusionQuery()
    {
        HandleUnsupported3DCall("Direct2D", "CreateOcclusionQuery");
        return std::make_unique<NoOpOcclusionQueryRenderer>();
    }
    void Direct2DRenderer::DrawColoredPrimitives(const IVertexBufferRenderer&, const Matrix&, const Matrix&,
                                                        const Matrix&, PrimitiveType, int)
    {
        HandleUnsupported3DCall("Direct2D", "DrawColoredPrimitives");
    }
    void Direct2DRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer&, const IIndexBufferRenderer&,
                                                               const Matrix&, const Matrix&, const Matrix&,
                                                               PrimitiveType, int)
    {
        HandleUnsupported3DCall("Direct2D", "DrawIndexedColoredPrimitives");
    }
}

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_DIRECT2D
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<Direct2D::Direct2DRenderer>(
            CNA::Platform::Detail::ResolveSdl3RendererWindow(args.surface.windowId),
            args.virtualWidth, args.virtualHeight, args.presentationMode, args.swapInterval,
            args.contextRecoveryEnabled, args.deviceEventCallback, args.backBufferFormat,
            args.depthStencilFormat, args.multiSampleCount);
    }
#endif
}
