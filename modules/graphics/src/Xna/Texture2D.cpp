// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "CNA/Logger.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Alpha8.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgr565.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra5551.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra4444.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfSingle.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rg32.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba1010102.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba64.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/IO/Stream.hpp"
#include "System/FormatException.hpp"
#include "System/NotSupportedException.hpp"

// plan_dx9.md Phase D9-10 (D9-103): GraphicsProfile.Reach/HiDef texture-size ceilings are real,
// enforced-at-creation-time, ONLY on this renderer -- the other 9 CNA renderers have no profile
// distinction to enforce (matches GraphicsAdapter.cpp's own #ifdef CNA_RENDERER_DIRECTX9 convention).
#ifdef CNA_RENDERER_DIRECTX9
#include "CNA/Internal/Renderers/DirectX9/D3D9ProfileCapabilities.hpp"
#endif

namespace Microsoft::Xna::Framework::Graphics
{
    using namespace CNA::Internal::Renderers;
    using namespace CNA::Internal::Graphics;

    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------

#ifdef CNA_RENDERER_DIRECTX9
    // D9-103: a HiDef-only size requested on a Reach device (or a size exceeding even HiDef's own
    // 4096 ceiling) throws the XNA-correct exception (System::NotSupportedException, matching
    // this file's own established convention for other unsupported-request cases) -- D9-100's own
    // table, checked as a profile CEILING, not a hardware query: even if this dev environment's
    // real device could technically allocate a larger texture, a Reach-profile game is restricted
    // to 2048 and a HiDef-profile game to 4096, matching real XNA's own portability guarantee.
    static void ValidateTextureSizeForProfileEXT(const GraphicsDevice& device, int w, int h)
    {
        const int profile = static_cast<int>(device.getGraphicsProfileProperty());
        const int maxSize = CNA::Internal::Renderers::DirectX9::MaxTextureSizeForProfileEXT(profile);
        if (w > maxSize || h > maxSize)
        {
            throw System::NotSupportedException(
                "Texture2D: " + std::to_string(w) + "x" + std::to_string(h) +
                " exceeds GraphicsProfile." + (profile == 1 ? std::string("HiDef") : std::string("Reach")) +
                "'s own maximum texture size of " + std::to_string(maxSize));
        }
    }
#endif

    // REMED-CONTENT-001: the native graphics APIs' own validation does not substitute for this --
    // Vulkan's validation layer is advisory (RADV proceeds anyway), and wgpu-native validates
    // lazily at submit time, past CNA's own creation-time null checks. Reject before any
    // renderer-specific texture creation is attempted, using the active renderer's real reported
    // maximum rather than a value guessed independently of it.
    static void ValidateTextureDimensionEXT(const GraphicsDevice& device, int w, int h)
    {
        const int maxDim = device.GetMaxTextureDimension();
        if (w > maxDim || h > maxDim)
        {
            throw System::NotSupportedException(
                "Texture2D: " + std::to_string(w) + "x" + std::to_string(h) +
                " exceeds this device's maximum texture dimension of " + std::to_string(maxDim));
        }
    }

    static void ValidateTexture2DFormatEXT(SurfaceFormat format)
    {
#ifdef CNA_RENDERER_SKIA
        if (format == SurfaceFormat::Color
            || format == SurfaceFormat::Bgr565
            || format == SurfaceFormat::Bgra5551
            || format == SurfaceFormat::Bgra4444
            || format == SurfaceFormat::Rgba1010102
            || format == SurfaceFormat::Rg32
            || format == SurfaceFormat::Rgba64
            || format == SurfaceFormat::Alpha8
            || format == SurfaceFormat::ColorBgraEXT
            || format == SurfaceFormat::ColorSrgbEXT
            || format == SurfaceFormat::ByteEXT
            || format == SurfaceFormat::UShortEXT
            || format == SurfaceFormat::Single
            || format == SurfaceFormat::Vector2
            || format == SurfaceFormat::Vector4
            || format == SurfaceFormat::HalfSingle
            || format == SurfaceFormat::HalfVector2
            || format == SurfaceFormat::HalfVector4
            || format == SurfaceFormat::NormalizedByte2
            || format == SurfaceFormat::NormalizedByte4
            || format == SurfaceFormat::HdrBlendable
            || format == SurfaceFormat::Dxt1
            || format == SurfaceFormat::Dxt3
            || format == SurfaceFormat::Dxt5
            || format == SurfaceFormat::Bc7EXT
            || format == SurfaceFormat::Bc7SrgbEXT)
        {
            return;
        }
        throw std::runtime_error(
            "Skia Texture2D SurfaceFormat has not passed its promotion gate.");
#else
        Texture::ValidateFormat(format);
#endif
    }

    [[nodiscard]] static bool IsColorTransferFormatEXT(SurfaceFormat format) noexcept
    {
        if (format == SurfaceFormat::Color)
            return true;
#ifdef CNA_RENDERER_SKIA
        // Skia stores each promoted format in its own native layout, so a `Color*` transfer is
        // only meaningful for the two that are genuinely 32-bit RGBA-shaped. Every other promoted
        // format has a typed overload that reads its real bits instead.
        return format == SurfaceFormat::ColorBgraEXT
            || format == SurfaceFormat::ColorSrgbEXT;
#else
        // Every other renderer keeps exactly the contract it had before this gate existed: the
        // `Color*` overloads accept any format whose texel is a multiple of four bytes, which is
        // the rule Texture::ValidateGetDataFormat(format, 4) applies on the next line. Returning a
        // flat false here would withdraw a public route those renderers genuinely serve --
        // MouseCursor::FromTexture2D reads a ColorSrgbEXT texture through it -- from renderers this
        // lane does not otherwise touch.
        return Texture::GetFormatSizeEXT(format) % 4 == 0;
#endif
    }

    /// SKIA-140/141: Dxt1/Dxt3/Dxt5/Bc7EXT/Bc7SrgbEXT transfer raw compressed blocks through the
    /// same CNAEXT byte-array overloads ByteEXT uses, matching how real XNA/FNA upload compressed
    /// content through the generic SetData<byte>/GetData<byte> overload rather than a dedicated
    /// method.
    [[nodiscard]] static bool IsCompressedTransferFormatEXT(SurfaceFormat format) noexcept
    {
#ifdef CNA_RENDERER_SKIA
        return format == SurfaceFormat::Dxt1
            || format == SurfaceFormat::Dxt3
            || format == SurfaceFormat::Dxt5
            || format == SurfaceFormat::Bc7EXT
            || format == SurfaceFormat::Bc7SrgbEXT;
#else
        (void)format;
        return false;
#endif
    }

    static int mipDim(int base, int level)
    {
        return std::max(1, base >> level);
    }

    static void validateMipLevel(const char* api, int level, int levelCount)
    {
        if (level < 0)
            throw std::out_of_range(std::string(api) + ": level must be >= 0");
        if (level >= levelCount)
            throw std::out_of_range(
                std::string(api) + ": level " + std::to_string(level) +
                " must be less than LevelCount " + std::to_string(levelCount));
    }

    static void validateTransferWindow(const char* api, int startIndex, int elementCount,
                                       int requiredElements);

    void Texture2D::MaybeFreeCpuPixels()
    {
        if (graphicsDevice_ && !graphicsDevice_->contextRecoveryEnabled_)
            cpuPixels_.reset();
    }

    void Texture2D::storeCpuPixels(const uint8_t* rgba, int pixelCount)
    {
        if (!cpuPixels_) cpuPixels_ = std::make_shared<std::vector<uint8_t>>();
        cpuPixels_->assign(rgba, rgba + static_cast<std::size_t>(pixelCount) * 4);
    }

    std::vector<uint8_t>& Texture2D::getMipBuffer(int level)
    {
        validateMipLevel("Texture2D::SetData", level, levelCount_);
        if (level == 0) {
            if (!cpuPixels_) cpuPixels_ = std::make_shared<std::vector<uint8_t>>();
            if (cpuPixels_->empty())
            {
                const std::size_t sz = static_cast<std::size_t>(mipDim(width, 0))
                    * mipDim(height, 0) * static_cast<std::size_t>(getBytesPerTexel());
                if (sz > 0) cpuPixels_->assign(sz, 0);
            }
            return *cpuPixels_;
        }
        const int idx = level - 1;
        if (!extraMipLevels_) extraMipLevels_ = std::make_shared<std::vector<std::vector<uint8_t>>>();
        if (static_cast<int>(extraMipLevels_->size()) <= idx)
            extraMipLevels_->resize(static_cast<std::size_t>(idx + 1));
        if ((*extraMipLevels_)[idx].empty())
        {
            const int w = mipDim(width, level);
            const int h = mipDim(height, level);
            (*extraMipLevels_)[idx].assign(
                static_cast<std::size_t>(w) * h * static_cast<std::size_t>(getBytesPerTexel()), 0);
        }
        return (*extraMipLevels_)[idx];
    }

    const std::vector<uint8_t>* Texture2D::getMipBufferConst(int level) const
    {
        validateMipLevel("Texture2D::GetData", level, levelCount_);
        if (level == 0) return (!cpuPixels_ || cpuPixels_->empty()) ? nullptr : cpuPixels_.get();
        if (!extraMipLevels_) return nullptr;
        const int idx = level - 1;
        if (static_cast<int>(extraMipLevels_->size()) <= idx) return nullptr;
        return (*extraMipLevels_)[idx].empty() ? nullptr : &(*extraMipLevels_)[idx];
    }

    int Texture2D::getBytesPerTexel() const
    {
        if (Texture::GetBlockSizeSquaredEXT(format_) != 1)
            throw System::NotSupportedException(
                "Texture2D: block-compressed transfer storage is not enabled by this format phase");
        return Texture::GetFormatSizeEXT(format_);
    }

    // -----------------------------------------------------------------------
    // Constructors
    // -----------------------------------------------------------------------

    Texture2D::Texture2D() = default;

    Texture2D::Texture2D(const std::string& assetName, GraphicsDevice& graphicsDevice)
        : Texture(&graphicsDevice)
    {
        ImageData data = ImageLoader::Load(assetName);
        width    = data.width;
        height   = data.height;
        renderer_   = graphicsDevice.GetRenderer().CreateTexture(data);
        cpuPixels_ = std::make_shared<std::vector<uint8_t>>(std::move(data.pixels));
        renderer_->ShareCpuPixels(cpuPixels_);
        MaybeFreeCpuPixels();
    }

    Texture2D::Texture2D(const std::string& assetName)
    {
        ImageData data = ImageLoader::Load(assetName);
        width    = data.width;
        height   = data.height;
        cpuPixels_ = std::make_shared<std::vector<uint8_t>>(std::move(data.pixels));
        // No GraphicsDevice — renderer stays null until attached.
    }

    Texture2D::Texture2D(GraphicsDevice& graphicsDevice, int w, int h)
        : Texture(&graphicsDevice), width(w), height(h)
    {
#ifdef CNA_RENDERER_DIRECTX9
        ValidateTextureSizeForProfileEXT(graphicsDevice, w, h);
#endif
        ValidateTextureDimensionEXT(graphicsDevice, w, h);
        ImageData data;
        data.width  = w;
        data.height = h;
        data.pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
        renderer_   = graphicsDevice.GetRenderer().CreateTexture(data);
        cpuPixels_ = std::make_shared<std::vector<uint8_t>>(std::move(data.pixels));
        renderer_->ShareCpuPixels(cpuPixels_);
        MaybeFreeCpuPixels();
    }

    static int CalculateMipLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

    Texture2D::Texture2D(GraphicsDevice& graphicsDevice, int w, int h,
                         bool mipMap, SurfaceFormat format)
        : Texture(&graphicsDevice), width(w), height(h)
    {
#ifdef CNA_RENDERER_DIRECTX9
        ValidateTextureSizeForProfileEXT(graphicsDevice, w, h);
#endif
        ValidateTextureDimensionEXT(graphicsDevice, w, h);
        ValidateTexture2DFormatEXT(format);
        format_     = format;
        levelCount_ = mipMap ? CalculateMipLevels(w, h) : 1;
        ImageData data;
        data.width     = w;
        data.height    = h;
        data.mipLevels = levelCount_;
        data.surfaceFormat = static_cast<int>(format);
        // Block-compressed formats are addressed in padded 4x4 blocks, not per texel: a level's
        // byte count is ceil(w/4) * ceil(h/4) blocks, never the raw texel count used below for
        // every other format (SKIA-140).
        if (Texture::GetBlockSizeSquaredEXT(format) != 1)
        {
            const std::size_t blockCols = static_cast<std::size_t>((w + 3) / 4);
            const std::size_t blockRows = static_cast<std::size_t>((h + 3) / 4);
            data.pixels.assign(
                blockCols * blockRows * static_cast<std::size_t>(Texture::GetFormatSizeEXT(format)),
                0);
        }
        else
        {
            data.pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h)
                                   * static_cast<std::size_t>(Texture::GetFormatSizeEXT(format)),
                               0);
        }
        renderer_   = graphicsDevice.GetRenderer().CreateTexture(data);
        cpuPixels_ = std::make_shared<std::vector<uint8_t>>(std::move(data.pixels));
        renderer_->ShareCpuPixels(cpuPixels_);
        MaybeFreeCpuPixels();
    }

    Texture2D::Texture2D(GraphicsDevice& device, int w, int h, SurfaceFormat fmt,
                         int lvlCount, std::shared_ptr<ITextureRenderer> renderer)
        : Texture(&device), width(w), height(h), renderer_(std::move(renderer))
    {
        // Task 774 finding: this constructor (used exclusively by RenderTarget2D) previously
        // skipped format validation entirely, silently accepting any SurfaceFormat even though
        // CreateRenderTarget2D's own renderer call never actually forwarded it -- a RenderTarget2D
        // could report a non-Color Format() while its real GPU resource was always Color.
        //
        // SKIA-142: that validation now happens one call frame up, in RenderTarget2D.cpp's
        // CreateValidatedRenderTargetRenderer -- which must run (and throw on an unsupported
        // format) before `renderer` above can even be constructed, since it supplies this
        // constructor's `renderer` argument. Re-validating here with the base Texture::ValidateFormat
        // (a hardcoded Color-only check, the same for every renderer) would silently re-reject every
        // one of the thirteen formats CreateValidatedRenderTargetRenderer's Skia-aware gate just
        // accepted, so it must not run again; `fmt` is already known consistent with `renderer`.
        format_     = fmt;
        levelCount_ = lvlCount;
        gpuOnlyContent_ = true;
    }

    Texture2D::~Texture2D() = default;

    void Texture2D::Dispose(bool disposing)
    {
        renderer_.reset();
        Texture::Dispose(disposing);
    }

    // -----------------------------------------------------------------------
    // Properties
    // -----------------------------------------------------------------------

    const std::string& Texture2D::GetTypeName() const
    {
        static const std::string name = "Texture2D";
        return name;
    }

    Rectangle Texture2D::getBoundsProperty() const
    {
        return {0, 0, width, height};
    }

    // -----------------------------------------------------------------------
    // SetData
    // -----------------------------------------------------------------------

    void Texture2D::SetData(const Color* data, int elementCount)
    {
        if (!IsColorTransferFormatEXT(format_))
            throw std::invalid_argument(
                "Texture2D::SetData: Color data requires a Color-compatible 32-bit format");
        if (!graphicsDevice_ || !data || elementCount <= 0) return;
        const int total = width * height;
        if (elementCount < total)
            throw std::out_of_range("Texture2D::SetData: elementCount is less than the number of pixels in the texture");
        ImageData img;
        img.width     = width;
        img.height    = height;
        img.mipLevels = levelCount_;
        img.surfaceFormat = static_cast<int>(format_);
        img.pixels.resize(static_cast<std::size_t>(total) * 4);
        for (int i = 0; i < total; ++i)
        {
            img.pixels[i * 4 + 0] = data[i].getRProperty();
            img.pixels[i * 4 + 1] = data[i].getGProperty();
            img.pixels[i * 4 + 2] = data[i].getBProperty();
            img.pixels[i * 4 + 3] = data[i].getAProperty();
        }
        // RenderTarget2D uses this inherited overload too. Replacing an existing renderer would
        // turn its render-target renderer into an ordinary Texture2D renderer and leave its cached
        // IRenderTargetRenderer pointer dangling. Update the existing level-0 resource instead.
        //
        // REMED-GFX-223: this in-place update must stay confined to render targets. An ordinary
        // Texture2D's renderer is shareable -- ContentManager's weak texture cache hands the same
        // ITextureRenderer to every wrapper reconstructed from a cache hit -- so updating it in
        // place would publish this upload to every other holder of that texture, which is exactly
        // the aliasing CnjCacheIsolationTests pins (plan_cnj.md CNB-33). Building a fresh renderer
        // detaches this wrapper, which is what a full-level upload has always done here.
        if (renderer_ && gpuOnlyContent_)
        {
            renderer_->UpdatePixels(img.pixels.data(), width * 4);
            // Subsequent draws change the target surface directly, so retain no stale CPU
            // upload shadow that could mask real target readback or generated descendants.
            cpuPixels_.reset();
            extraMipLevels_.reset();
            return;
        }

        renderer_   = graphicsDevice_->GetRenderer().CreateTexture(img);
        cpuPixels_ = std::make_shared<std::vector<uint8_t>>(std::move(img.pixels));
        renderer_->ShareCpuPixels(cpuPixels_);
        MaybeFreeCpuPixels();
    }

    void Texture2D::SetData(int level, const Rectangle* rect,
                            const Color* data, int startIndex, int elementCount)
    {
        if (!IsColorTransferFormatEXT(format_))
            throw std::invalid_argument(
                "Texture2D::SetData: Color data requires a Color-compatible 32-bit format");
        if (!data || elementCount <= 0)
            throw std::invalid_argument("Texture2D::SetData: data must not be null");
        if (startIndex < 0)
            throw std::out_of_range("Texture2D::SetData: startIndex must be >= 0");
        validateMipLevel("Texture2D::SetData", level, levelCount_);

        const int levelW = mipDim(width,  level);
        const int levelH = mipDim(height, level);

        int x = 0, y = 0, w = levelW, h = levelH;
        if (rect)
        {
            x = rect->X; y = rect->Y;
            w = rect->Width; h = rect->Height;
        }
        if (x < 0 || y < 0 || x + w > levelW || y + h > levelH)
            throw std::out_of_range("Texture2D::SetData: rectangle out of texture bounds");
        if (elementCount < w * h)
            throw std::out_of_range("Texture2D::SetData: elementCount is less than the number of pixels in the requested region");

        // getMipBuffer(0) lazily re-creates cpuPixels_ as a zero-filled buffer when it has
        // been freed (context recovery disabled — see MaybeFreeCpuPixels). If the requested
        // region does not cover the whole level, UpdatePixels below would re-upload that
        // zero-filled buffer wholesale and silently overwrite already-uploaded GPU content
        // outside the region with black. Fail loudly instead of corrupting the texture.
        const bool coversFullLevel = (x == 0 && y == 0 && w == levelW && h == levelH);

        // RenderTarget2D content has one authoritative owner: its live renderer. A previous
        // higher-level upload may have left a common-layer staging shadow, but rendering or a
        // parent upload is allowed to regenerate that level later. Drop every target mip staging
        // buffer before composing this transfer so partial updates are always seeded from the
        // renderer's current chain rather than from stale bytes.
        if (gpuOnlyContent_)
        {
            cpuPixels_.reset();
            extraMipLevels_.reset();
        }

        if (level == 0 && renderer_ && !cpuPixels_ && !coversFullLevel)
        {
            if (!gpuOnlyContent_)
            {
                throw std::runtime_error(
                    "Texture2D::SetData: partial update requires CPU-side pixel storage, which was "
                    "freed because context recovery is disabled (see GraphicsDevice::SetContextRecoveryEnabled); "
                    "update the full level via SetData(Color*, int) instead");
            }

            // A render target can have rendered content but deliberately keeps no CPU shadow.
            // Seed a temporary full-level shadow from its actual surface before applying a partial
            // upload, so untouched texels are not accidentally replaced with transparent black.
            std::vector<uint8_t> currentPixels(static_cast<std::size_t>(levelW) * levelH * 4u);
            if (!renderer_->GetData(0, 0, 0, levelW, levelH, currentPixels.data(),
                                   static_cast<int>(currentPixels.size())))
            {
                throw System::NotSupportedException(
                    "Texture2D::SetData: this graphics renderer cannot preserve untouched render-target pixels "
                    "for a partial update");
            }
            cpuPixels_ = std::make_shared<std::vector<uint8_t>>(std::move(currentPixels));
        }

        // A renderer may own deterministic generated bytes for a level that the caller has not
        // authored yet. Seed a partial update from those real bytes before creating the ordinary
        // caller-authored shadow; otherwise getMipBuffer would preserve the rectangle against
        // zeroes and destroy generated texels outside it. Full-level writes need no seed.
        if (level > 0 && renderer_ && !coversFullLevel && !getMipBufferConst(level)
            && renderer_->HasDefinedMipLevel(level))
        {
            std::vector<uint8_t> currentPixels(
                static_cast<std::size_t>(levelW) * levelH * 4u);
            if (!renderer_->GetData(level, 0, 0, levelW, levelH, currentPixels.data(),
                                   static_cast<int>(currentPixels.size())))
            {
                throw System::NotSupportedException(
                    "Texture2D::SetData: this graphics renderer cannot preserve untouched "
                    "generated mip pixels for a partial update");
            }
            getMipBuffer(level) = std::move(currentPixels);
        }

        std::vector<uint8_t>& buf = getMipBuffer(level);

        for (int row = 0; row < h; ++row)
        {
            for (int col = 0; col < w; ++col)
            {
                const int src = startIndex + row * w + col;
                const int dst = ((y + row) * levelW + (x + col)) * 4;
                buf[dst + 0] = data[src].getRProperty();
                buf[dst + 1] = data[src].getGProperty();
                buf[dst + 2] = data[src].getBProperty();
                buf[dst + 3] = data[src].getAProperty();
            }
        }

        if (level == 0)
        {
            if (renderer_)
                renderer_->UpdatePixels(buf.data(), levelW * 4);
            else if (graphicsDevice_)
            {
                ImageData img;
                img.width     = width;
                img.height    = height;
                img.mipLevels = levelCount_;
                img.surfaceFormat = static_cast<int>(format_);
                img.pixels    = buf;
                renderer_   = graphicsDevice_->GetRenderer().CreateTexture(img);
                renderer_->ShareCpuPixels(cpuPixels_);
            }
            if (gpuOnlyContent_)
            {
                // See the full-level overload: target rendering must always read back the live
                // surface rather than this temporary partial-update staging buffer.
                cpuPixels_.reset();
                extraMipLevels_.reset();
                return;
            }
            MaybeFreeCpuPixels();
        }
        else if (renderer_)
        {
            renderer_->UpdatePixelsLevel(level, buf.data(), levelW, levelH);
            if (gpuOnlyContent_)
                extraMipLevels_.reset();
        }
    }

    void Texture2D::SetDataBytes(int level, const Rectangle* rect, const std::uint8_t* data,
                                 int startIndex, int elementCount, int elementBytes)
    {
        if (!data || elementCount <= 0)
            throw std::invalid_argument("Texture2D::SetData: data must not be null");
        if (startIndex < 0)
            throw std::out_of_range("Texture2D::SetData: startIndex must be >= 0");
        validateMipLevel("Texture2D::SetData", level, levelCount_);
        const int bytesPerTexel = getBytesPerTexel();
        if (elementBytes != bytesPerTexel)
            throw std::invalid_argument(
                "Texture2D::SetData: packed element type does not match the texture format");

        const int levelW = mipDim(width, level);
        const int levelH = mipDim(height, level);
        int x = 0;
        int y = 0;
        int w = levelW;
        int h = levelH;
        if (rect)
        {
            x = rect->X;
            y = rect->Y;
            w = rect->Width;
            h = rect->Height;
        }
        if (x < 0 || y < 0 || w <= 0 || h <= 0
            || w > levelW || h > levelH || x > levelW - w || y > levelH - h)
        {
            throw std::out_of_range("Texture2D::SetData: rectangle out of texture bounds");
        }
        const int requiredElements = w * h;
        if (elementCount < requiredElements)
            throw std::out_of_range(
                "Texture2D::SetData: elementCount is less than the requested region");

        const bool coversFullLevel = x == 0 && y == 0 && w == levelW && h == levelH;
        if (gpuOnlyContent_)
        {
            cpuPixels_.reset();
            extraMipLevels_.reset();
        }

        if (level == 0 && renderer_ && !cpuPixels_ && !coversFullLevel)
        {
            if (!gpuOnlyContent_)
            {
                throw std::runtime_error(
                    "Texture2D::SetData: partial update requires CPU-side pixel storage, which "
                    "was freed because context recovery is disabled");
            }
            std::vector<uint8_t> currentPixels(
                static_cast<std::size_t>(levelW) * levelH * bytesPerTexel);
            if (!renderer_->GetData(0, 0, 0, levelW, levelH, currentPixels.data(),
                                   static_cast<int>(currentPixels.size())))
            {
                throw System::NotSupportedException(
                    "Texture2D::SetData: renderer cannot preserve untouched target pixels");
            }
            cpuPixels_ = std::make_shared<std::vector<uint8_t>>(std::move(currentPixels));
        }

        if (level > 0 && renderer_ && !coversFullLevel && !getMipBufferConst(level)
            && renderer_->HasDefinedMipLevel(level))
        {
            std::vector<uint8_t> currentPixels(
                static_cast<std::size_t>(levelW) * levelH * bytesPerTexel);
            if (!renderer_->GetData(level, 0, 0, levelW, levelH, currentPixels.data(),
                                   static_cast<int>(currentPixels.size())))
            {
                throw System::NotSupportedException(
                    "Texture2D::SetData: renderer cannot preserve untouched generated mip pixels");
            }
            getMipBuffer(level) = std::move(currentPixels);
        }

        std::vector<uint8_t>& destination = getMipBuffer(level);
        const std::size_t rowBytes = static_cast<std::size_t>(w) * bytesPerTexel;
        for (int row = 0; row < h; ++row)
        {
            const std::size_t sourceOffset = static_cast<std::size_t>(startIndex + row * w)
                * bytesPerTexel;
            const std::size_t destinationOffset =
                (static_cast<std::size_t>(y + row) * levelW + x) * bytesPerTexel;
            std::memcpy(destination.data() + destinationOffset, data + sourceOffset, rowBytes);
        }

        if (level == 0)
        {
            if (renderer_)
            {
                renderer_->UpdatePixels(destination.data(), levelW * bytesPerTexel);
            }
            else if (graphicsDevice_)
            {
                ImageData image;
                image.width = width;
                image.height = height;
                image.mipLevels = levelCount_;
                image.surfaceFormat = static_cast<int>(format_);
                image.pixels = destination;
                renderer_ = graphicsDevice_->GetRenderer().CreateTexture(image);
                renderer_->ShareCpuPixels(cpuPixels_);
            }
            if (gpuOnlyContent_)
            {
                cpuPixels_.reset();
                extraMipLevels_.reset();
                return;
            }
            MaybeFreeCpuPixels();
        }
        else if (renderer_)
        {
            renderer_->UpdatePixelsLevel(level, destination.data(), levelW, levelH);
            if (gpuOnlyContent_)
                extraMipLevels_.reset();
        }
    }

    void Texture2D::SetCompressedDataBytes(int level, const Rectangle* rect,
                                           const std::uint8_t* data, int startIndex,
                                           int elementCount)
    {
        if (!data || elementCount <= 0)
            throw std::invalid_argument("Texture2D::SetData: data must not be null");
        if (startIndex < 0)
            throw std::out_of_range("Texture2D::SetData: startIndex must be >= 0");
        validateMipLevel("Texture2D::SetData", level, levelCount_);
        if (!renderer_)
            throw std::runtime_error("Texture2D::SetData: compressed texture has no renderer");

        const int levelW = mipDim(width, level);
        const int levelH = mipDim(height, level);
        int x = 0, y = 0, w = levelW, h = levelH;
        if (rect)
        {
            x = rect->X; y = rect->Y;
            w = rect->Width; h = rect->Height;
        }

        // A partial rect must start on a block boundary and either be block-aligned or reach
        // the level's actual (possibly NPOT) edge -- the same policy real block-compression
        // hardware/drivers enforce for sub-level updates. This is intentionally more exact than
        // FNA's own raw w*h*GetFormatSizeEXT/GetBlockSizeSquaredEXT validation formula, which
        // under-counts the true padded byte requirement for a rectangle whose edge falls inside
        // a partial NPOT tail block; every byte count below uses the exact padded block count.
        const bool touchesRightEdge = x + w == levelW;
        const bool touchesBottomEdge = y + h == levelH;
        if (x < 0 || y < 0 || w <= 0 || h <= 0
            || w > levelW || h > levelH || x > levelW - w || y > levelH - h
            || (x % 4) != 0 || (y % 4) != 0
            || ((w % 4) != 0 && !touchesRightEdge)
            || ((h % 4) != 0 && !touchesBottomEdge))
        {
            throw std::out_of_range(
                "Texture2D::SetData: compressed rectangle must be block-aligned and within bounds");
        }

        const int blockByteSize = Texture::GetFormatSizeEXT(format_);
        const int blockCols = (w + 3) / 4;
        const int blockRows = (h + 3) / 4;
        const int requiredElements = blockCols * blockRows * blockByteSize;
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);

        const int levelBlockCols = (levelW + 3) / 4;
        const int levelBlockRows = (levelH + 3) / 4;
        const std::size_t levelByteCount =
            static_cast<std::size_t>(levelBlockCols) * levelBlockRows * blockByteSize;
        std::vector<std::uint8_t> levelBytes(levelByteCount);
        const bool coversFullLevel = x == 0 && y == 0 && touchesRightEdge && touchesBottomEdge;
        if (coversFullLevel)
        {
            std::memcpy(levelBytes.data(), data + startIndex, levelByteCount);
        }
        else
        {
            // No lasting compressed CPU shadow is kept (unlike getMipBuffer for other formats):
            // the renderer is always the authoritative store, so a partial update reads it back,
            // patches only the requested block rectangle, and re-uploads the whole level.
            if (!renderer_->GetData(level, 0, 0, levelW, levelH, levelBytes.data(),
                                   static_cast<int>(levelBytes.size())))
            {
                throw System::NotSupportedException(
                    "Texture2D::SetData: renderer cannot preserve untouched compressed block bytes");
            }
            const int blockX = x / 4;
            const int blockY = y / 4;
            const std::size_t levelRowBytes =
                static_cast<std::size_t>(levelBlockCols) * blockByteSize;
            const std::size_t rectRowBytes = static_cast<std::size_t>(blockCols) * blockByteSize;
            for (int row = 0; row < blockRows; ++row)
            {
                std::memcpy(
                    levelBytes.data() + (static_cast<std::size_t>(blockY + row) * levelRowBytes
                                         + static_cast<std::size_t>(blockX) * blockByteSize),
                    data + startIndex + static_cast<std::size_t>(row) * rectRowBytes,
                    rectRowBytes);
            }
        }

        renderer_->UpdatePixelsLevel(level, levelBytes.data(), levelW, levelH);
    }

    namespace
    {
        template <typename Packed, typename Word>
        std::vector<std::uint8_t> EncodePackedElements(
            const Packed* data, int startIndex, int elementCount)
        {
            if (!data || elementCount <= 0)
                throw std::invalid_argument("Texture2D::SetData: data must not be null");
            if (startIndex < 0)
                throw std::out_of_range("Texture2D::SetData: startIndex must be >= 0");
            std::vector<std::uint8_t> result(
                static_cast<std::size_t>(elementCount) * sizeof(Word));
            for (int index = 0; index < elementCount; ++index)
            {
                const Word word = static_cast<Word>(
                    data[startIndex + index].getPackedValueProperty());
                for (std::size_t byte = 0; byte < sizeof(Word); ++byte)
                {
                    result[static_cast<std::size_t>(index) * sizeof(Word) + byte] =
                        static_cast<std::uint8_t>(word >> (byte * 8u));
                }
            }
            return result;
        }

        template <typename Packed, typename Word>
        void DecodePackedElements(const std::vector<std::uint8_t>& bytes, int requiredElements,
                                  Packed* data, int startIndex)
        {
            for (int index = 0; index < requiredElements; ++index)
            {
                Word word = 0;
                for (std::size_t byte = 0; byte < sizeof(Word); ++byte)
                {
                    word |= static_cast<Word>(bytes[static_cast<std::size_t>(index)
                                                     * sizeof(Word) + byte])
                        << (byte * 8u);
                }
                data[startIndex + index].setPackedValueProperty(word);
            }
        }

        template <typename Word>
        std::vector<std::uint8_t> EncodeUnsignedElements(
            const Word* data, int startIndex, int elementCount)
        {
            static_assert(std::is_unsigned_v<Word>);
            if (!data || elementCount <= 0)
                throw std::invalid_argument("Texture2D::SetData: data must not be null");
            if (startIndex < 0)
                throw std::out_of_range("Texture2D::SetData: startIndex must be >= 0");
            std::vector<std::uint8_t> result(
                static_cast<std::size_t>(elementCount) * sizeof(Word));
            for (int index = 0; index < elementCount; ++index)
            {
                const Word word = data[startIndex + index];
                for (std::size_t byte = 0; byte < sizeof(Word); ++byte)
                {
                    result[static_cast<std::size_t>(index) * sizeof(Word) + byte] =
                        static_cast<std::uint8_t>(word >> (byte * 8u));
                }
            }
            return result;
        }

        template <typename Word>
        void DecodeUnsignedElements(const std::vector<std::uint8_t>& bytes, int requiredElements,
                                    Word* data, int startIndex)
        {
            static_assert(std::is_unsigned_v<Word>);
            for (int index = 0; index < requiredElements; ++index)
            {
                Word word = 0;
                for (std::size_t byte = 0; byte < sizeof(Word); ++byte)
                {
                    word |= static_cast<Word>(bytes[static_cast<std::size_t>(index)
                                                     * sizeof(Word) + byte])
                        << (byte * 8u);
                }
                data[startIndex + index] = word;
            }
        }

        template <typename Element>
        struct FloatElementTraits;

        template <>
        struct FloatElementTraits<float>
        {
            static constexpr int Components = 1;
            [[nodiscard]] static float Get(const float& value, int) noexcept { return value; }
            static void Set(float& value, int, float component) noexcept { value = component; }
        };

        template <>
        struct FloatElementTraits<Vector2>
        {
            static constexpr int Components = 2;
            [[nodiscard]] static float Get(const Vector2& value, int component) noexcept
            {
                return component == 0 ? value.X : value.Y;
            }
            static void Set(Vector2& value, int component, float channel) noexcept
            {
                if (component == 0) value.X = channel;
                else value.Y = channel;
            }
        };

        template <>
        struct FloatElementTraits<Vector4>
        {
            static constexpr int Components = 4;
            [[nodiscard]] static float Get(const Vector4& value, int component) noexcept
            {
                switch (component)
                {
                    case 0: return value.X;
                    case 1: return value.Y;
                    case 2: return value.Z;
                    default: return value.W;
                }
            }
            static void Set(Vector4& value, int component, float channel) noexcept
            {
                switch (component)
                {
                    case 0: value.X = channel; break;
                    case 1: value.Y = channel; break;
                    case 2: value.Z = channel; break;
                    default: value.W = channel; break;
                }
            }
        };

        template <typename Element>
        std::vector<std::uint8_t> EncodeFloatElements(
            const Element* data, int startIndex, int elementCount)
        {
            if (!data || elementCount <= 0)
                throw std::invalid_argument("Texture2D::SetData: data must not be null");
            if (startIndex < 0)
                throw std::out_of_range("Texture2D::SetData: startIndex must be >= 0");
            constexpr int components = FloatElementTraits<Element>::Components;
            std::vector<std::uint8_t> result(
                static_cast<std::size_t>(elementCount) * components * sizeof(float));
            for (int index = 0; index < elementCount; ++index)
            {
                for (int component = 0; component < components; ++component)
                {
                    const std::uint32_t bits = std::bit_cast<std::uint32_t>(
                        FloatElementTraits<Element>::Get(data[startIndex + index], component));
                    const std::size_t offset =
                        (static_cast<std::size_t>(index) * components + component) * sizeof(float);
                    for (std::size_t byte = 0; byte < sizeof(float); ++byte)
                        result[offset + byte] = static_cast<std::uint8_t>(bits >> (byte * 8u));
                }
            }
            return result;
        }

        template <typename Element>
        void DecodeFloatElements(const std::vector<std::uint8_t>& bytes, int requiredElements,
                                 Element* data, int startIndex)
        {
            constexpr int components = FloatElementTraits<Element>::Components;
            for (int index = 0; index < requiredElements; ++index)
            {
                for (int component = 0; component < components; ++component)
                {
                    const std::size_t offset =
                        (static_cast<std::size_t>(index) * components + component) * sizeof(float);
                    std::uint32_t bits = 0u;
                    for (std::size_t byte = 0; byte < sizeof(float); ++byte)
                        bits |= static_cast<std::uint32_t>(bytes[offset + byte]) << (byte * 8u);
                    FloatElementTraits<Element>::Set(
                        data[startIndex + index], component, std::bit_cast<float>(bits));
                }
            }
        }

        int ValidatedRequestedTexelCount(const char* api, int baseWidth, int baseHeight,
                                         int level, int levelCount, const Rectangle* rect)
        {
            validateMipLevel(api, level, levelCount);
            const int levelWidth = mipDim(baseWidth, level);
            const int levelHeight = mipDim(baseHeight, level);
            if (!rect)
                return levelWidth * levelHeight;
            if (rect->X < 0 || rect->Y < 0 || rect->Width <= 0 || rect->Height <= 0
                || rect->Width > levelWidth || rect->Height > levelHeight
                || rect->X > levelWidth - rect->Width || rect->Y > levelHeight - rect->Height)
            {
                throw std::out_of_range(std::string(api) + ": rectangle out of texture bounds");
            }
            return rect->Width * rect->Height;
        }
    }

    void Texture2D::SetData(const PackedVector::Bgr565* data, int elementCount)
    {
        SetData(0, nullptr, data, 0, elementCount);
    }

    void Texture2D::SetData(int level, const Rectangle* rect, const PackedVector::Bgr565* data,
                            int startIndex, int elementCount)
    {
        if (format_ != SurfaceFormat::Bgr565)
            throw std::invalid_argument("Texture2D::SetData: Bgr565 data requires Bgr565 format");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::SetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);
        const auto bytes = EncodePackedElements<PackedVector::Bgr565, std::uint16_t>(
            data, startIndex, requiredElements);
        SetDataBytes(level, rect, bytes.data(), 0, requiredElements, 2);
    }

    void Texture2D::SetData(const PackedVector::Bgra4444* data, int elementCount)
    {
        SetData(0, nullptr, data, 0, elementCount);
    }

    void Texture2D::SetData(int level, const Rectangle* rect, const PackedVector::Bgra4444* data,
                            int startIndex, int elementCount)
    {
        if (format_ != SurfaceFormat::Bgra4444)
            throw std::invalid_argument(
                "Texture2D::SetData: Bgra4444 data requires Bgra4444 format");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::SetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);
        const auto bytes = EncodePackedElements<PackedVector::Bgra4444, std::uint16_t>(
            data, startIndex, requiredElements);
        SetDataBytes(level, rect, bytes.data(), 0, requiredElements, 2);
    }

    void Texture2D::SetData(const PackedVector::Bgra5551* data, int elementCount)
    {
        SetData(0, nullptr, data, 0, elementCount);
    }

    void Texture2D::SetData(int level, const Rectangle* rect, const PackedVector::Bgra5551* data,
                            int startIndex, int elementCount)
    {
        if (format_ != SurfaceFormat::Bgra5551)
            throw std::invalid_argument(
                "Texture2D::SetData: Bgra5551 data requires Bgra5551 format");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::SetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);
        const auto bytes = EncodePackedElements<PackedVector::Bgra5551, std::uint16_t>(
            data, startIndex, requiredElements);
        SetDataBytes(level, rect, bytes.data(), 0, requiredElements, 2);
    }

    void Texture2D::SetData(const PackedVector::Rgba1010102* data, int elementCount)
    {
        SetData(0, nullptr, data, 0, elementCount);
    }

    void Texture2D::SetData(int level, const Rectangle* rect,
                            const PackedVector::Rgba1010102* data,
                            int startIndex, int elementCount)
    {
        if (format_ != SurfaceFormat::Rgba1010102)
            throw std::invalid_argument(
                "Texture2D::SetData: Rgba1010102 data requires Rgba1010102 format");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::SetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);
        const auto bytes = EncodePackedElements<PackedVector::Rgba1010102, std::uint32_t>(
            data, startIndex, requiredElements);
        SetDataBytes(level, rect, bytes.data(), 0, requiredElements, 4);
    }

    void Texture2D::SetData(const PackedVector::Alpha8* data, int elementCount)
    {
        SetData(0, nullptr, data, 0, elementCount);
    }

    void Texture2D::SetData(int level, const Rectangle* rect, const PackedVector::Alpha8* data,
                            int startIndex, int elementCount)
    {
        if (format_ != SurfaceFormat::Alpha8)
            throw std::invalid_argument("Texture2D::SetData: Alpha8 data requires Alpha8 format");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::SetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);
        const auto bytes = EncodePackedElements<PackedVector::Alpha8, std::uint8_t>(
            data, startIndex, requiredElements);
        SetDataBytes(level, rect, bytes.data(), 0, requiredElements, 1);
    }

    void Texture2D::SetData(const PackedVector::Rg32* data, int elementCount)
    {
        SetData(0, nullptr, data, 0, elementCount);
    }

    void Texture2D::SetData(int level, const Rectangle* rect, const PackedVector::Rg32* data,
                            int startIndex, int elementCount)
    {
        if (format_ != SurfaceFormat::Rg32)
            throw std::invalid_argument("Texture2D::SetData: Rg32 data requires Rg32 format");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::SetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);
        const auto bytes = EncodePackedElements<PackedVector::Rg32, std::uint32_t>(
            data, startIndex, requiredElements);
        SetDataBytes(level, rect, bytes.data(), 0, requiredElements, 4);
    }

    void Texture2D::SetData(const PackedVector::Rgba64* data, int elementCount)
    {
        SetData(0, nullptr, data, 0, elementCount);
    }

    void Texture2D::SetData(int level, const Rectangle* rect, const PackedVector::Rgba64* data,
                            int startIndex, int elementCount)
    {
        if (format_ != SurfaceFormat::Rgba64)
            throw std::invalid_argument("Texture2D::SetData: Rgba64 data requires Rgba64 format");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::SetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);
        const auto bytes = EncodePackedElements<PackedVector::Rgba64, std::uint64_t>(
            data, startIndex, requiredElements);
        SetDataBytes(level, rect, bytes.data(), 0, requiredElements, 8);
    }

    void Texture2D::SetData(const PackedVector::NormalizedByte2* data, int elementCount)
    {
        SetData(0, nullptr, data, 0, elementCount);
    }

    void Texture2D::SetData(int level, const Rectangle* rect,
                            const PackedVector::NormalizedByte2* data,
                            int startIndex, int elementCount)
    {
        if (format_ != SurfaceFormat::NormalizedByte2)
            throw std::invalid_argument(
                "Texture2D::SetData: NormalizedByte2 data requires NormalizedByte2 format");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::SetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);
        const auto bytes = EncodePackedElements<PackedVector::NormalizedByte2, std::uint16_t>(
            data, startIndex, requiredElements);
        SetDataBytes(level, rect, bytes.data(), 0, requiredElements, 2);
    }

    void Texture2D::SetData(const PackedVector::NormalizedByte4* data, int elementCount)
    {
        SetData(0, nullptr, data, 0, elementCount);
    }

    void Texture2D::SetData(int level, const Rectangle* rect,
                            const PackedVector::NormalizedByte4* data,
                            int startIndex, int elementCount)
    {
        if (format_ != SurfaceFormat::NormalizedByte4)
            throw std::invalid_argument(
                "Texture2D::SetData: NormalizedByte4 data requires NormalizedByte4 format");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::SetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);
        const auto bytes = EncodePackedElements<PackedVector::NormalizedByte4, std::uint32_t>(
            data, startIndex, requiredElements);
        SetDataBytes(level, rect, bytes.data(), 0, requiredElements, 4);
    }

    void Texture2D::SetData(const float* data, int elementCount)
    {
        SetData(0, nullptr, data, 0, elementCount);
    }

    void Texture2D::SetData(int level, const Rectangle* rect, const float* data,
                            int startIndex, int elementCount)
    {
        if (format_ != SurfaceFormat::Single)
            throw std::invalid_argument("Texture2D::SetData: float data requires Single format");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::SetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);
        const auto bytes = EncodeFloatElements(data, startIndex, requiredElements);
        SetDataBytes(level, rect, bytes.data(), 0, requiredElements, 4);
    }

    void Texture2D::SetData(const Vector2* data, int elementCount)
    {
        SetData(0, nullptr, data, 0, elementCount);
    }

    void Texture2D::SetData(int level, const Rectangle* rect, const Vector2* data,
                            int startIndex, int elementCount)
    {
        if (format_ != SurfaceFormat::Vector2)
            throw std::invalid_argument("Texture2D::SetData: Vector2 data requires Vector2 format");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::SetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);
        const auto bytes = EncodeFloatElements(data, startIndex, requiredElements);
        SetDataBytes(level, rect, bytes.data(), 0, requiredElements, 8);
    }

    void Texture2D::SetData(const Vector4* data, int elementCount)
    {
        SetData(0, nullptr, data, 0, elementCount);
    }

    void Texture2D::SetData(int level, const Rectangle* rect, const Vector4* data,
                            int startIndex, int elementCount)
    {
        if (format_ != SurfaceFormat::Vector4)
            throw std::invalid_argument("Texture2D::SetData: Vector4 data requires Vector4 format");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::SetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);
        const auto bytes = EncodeFloatElements(data, startIndex, requiredElements);
        SetDataBytes(level, rect, bytes.data(), 0, requiredElements, 16);
    }

    void Texture2D::SetData(const PackedVector::HalfSingle* data, int elementCount)
    {
        SetData(0, nullptr, data, 0, elementCount);
    }

    void Texture2D::SetData(int level, const Rectangle* rect,
                            const PackedVector::HalfSingle* data,
                            int startIndex, int elementCount)
    {
        if (format_ != SurfaceFormat::HalfSingle)
            throw std::invalid_argument(
                "Texture2D::SetData: HalfSingle data requires HalfSingle format");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::SetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);
        const auto bytes = EncodePackedElements<PackedVector::HalfSingle, std::uint16_t>(
            data, startIndex, requiredElements);
        SetDataBytes(level, rect, bytes.data(), 0, requiredElements, 2);
    }

    void Texture2D::SetData(const PackedVector::HalfVector2* data, int elementCount)
    {
        SetData(0, nullptr, data, 0, elementCount);
    }

    void Texture2D::SetData(int level, const Rectangle* rect,
                            const PackedVector::HalfVector2* data,
                            int startIndex, int elementCount)
    {
        if (format_ != SurfaceFormat::HalfVector2)
            throw std::invalid_argument(
                "Texture2D::SetData: HalfVector2 data requires HalfVector2 format");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::SetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);
        const auto bytes = EncodePackedElements<PackedVector::HalfVector2, std::uint32_t>(
            data, startIndex, requiredElements);
        SetDataBytes(level, rect, bytes.data(), 0, requiredElements, 4);
    }

    void Texture2D::SetData(const PackedVector::HalfVector4* data, int elementCount)
    {
        SetData(0, nullptr, data, 0, elementCount);
    }

    void Texture2D::SetData(int level, const Rectangle* rect,
                            const PackedVector::HalfVector4* data,
                            int startIndex, int elementCount)
    {
        if (format_ != SurfaceFormat::HalfVector4 && format_ != SurfaceFormat::HdrBlendable)
            throw std::invalid_argument(
                "Texture2D::SetData: HalfVector4 data requires HalfVector4 or HdrBlendable format");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::SetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);
        const auto bytes = EncodePackedElements<PackedVector::HalfVector4, std::uint64_t>(
            data, startIndex, requiredElements);
        SetDataBytes(level, rect, bytes.data(), 0, requiredElements, 8);
    }

    void Texture2D::SetData(const std::uint8_t* data, int elementCount)
    {
        SetData(0, nullptr, data, 0, elementCount);
    }

    void Texture2D::SetData(int level, const Rectangle* rect, const std::uint8_t* data,
                            int startIndex, int elementCount)
    {
        if (IsCompressedTransferFormatEXT(format_))
        {
            SetCompressedDataBytes(level, rect, data, startIndex, elementCount);
            return;
        }
        if (format_ != SurfaceFormat::ByteEXT)
            throw std::invalid_argument("Texture2D::SetData: byte data requires ByteEXT format");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::SetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);
        const auto bytes = EncodeUnsignedElements(data, startIndex, requiredElements);
        SetDataBytes(level, rect, bytes.data(), 0, requiredElements, 1);
    }

    void Texture2D::SetData(const std::uint16_t* data, int elementCount)
    {
        SetData(0, nullptr, data, 0, elementCount);
    }

    void Texture2D::SetData(int level, const Rectangle* rect, const std::uint16_t* data,
                            int startIndex, int elementCount)
    {
        if (format_ != SurfaceFormat::UShortEXT)
            throw std::invalid_argument(
                "Texture2D::SetData: ushort data requires UShortEXT format");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::SetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::SetData", startIndex, elementCount, requiredElements);
        const auto bytes = EncodeUnsignedElements(data, startIndex, requiredElements);
        SetDataBytes(level, rect, bytes.data(), 0, requiredElements, 2);
    }

    void Texture2D::SetDataRGBA(const uint8_t* data, int pixelCount)
    {
        if (format_ != SurfaceFormat::Color)
            throw std::invalid_argument(
                "Texture2D::SetDataRGBA: raw RGBA requires SurfaceFormat::Color");
        if (!renderer_ || !data || pixelCount <= 0) return;
        if (gpuOnlyContent_)
        {
            renderer_->UpdatePixels(data, width * 4);
            cpuPixels_.reset();
            extraMipLevels_.reset();
            return;
        }
        storeCpuPixels(data, pixelCount);
        renderer_->UpdatePixels(data, width * 4);
    }

    // -----------------------------------------------------------------------
    // GetData
    // -----------------------------------------------------------------------

    // REMED-GFX-149 / REMED-GFX-128. The destination window every GetData overload is authorised to
    // write, validated once, in ELEMENTS.
    //
    //   startIndex   -- an index into the CALLER'S array. XNA documents it as "index of the first
    //                   element to get" and FNA transfers to
    //                   `AddrOfPinnedObject() + startIndex * elementSizeInBytes`, i.e. the pinned
    //                   DESTINATION array's base. It is never a source-texel offset and never a
    //                   byte offset.
    //   elementCount -- the destination capacity available from startIndex. FNA hands it to the
    //                   native transfer as `elementCount * elementSizeInBytes` and derives the
    //                   transfer size from the requested REGION, so a capacity larger than the
    //                   region is legal (`GetData<T>(T[] data)` passes `data.Length`) and must be
    //                   left untouched, while a smaller one must be rejected before any transfer.
    //
    // The sum is computed in 64 bits and rejected before use. Evaluated as `int` it wraps for large
    // arguments and the wrapped value then passes every subsequent bound, so the copy reads and
    // writes far outside both buffers -- measured as a SIGSEGV, not as a rejected call.
    static void validateTransferWindow(const char* api, int startIndex, int elementCount,
                                       int requiredElements)
    {
        if (elementCount < requiredElements)
            throw std::out_of_range(std::string(api) +
                ": elementCount is less than the number of pixels in the requested region");
        if (static_cast<std::int64_t>(startIndex) + static_cast<std::int64_t>(elementCount) >
            static_cast<std::int64_t>(std::numeric_limits<int>::max()))
            throw std::out_of_range(std::string(api) +
                ": startIndex + elementCount does not fit in a 32-bit element index");
    }

    /// Records exactly what a transfer was asked for and what it is authorised to write, so the
    /// shared layer's interpretation can be compared against a renderer's without a debugger.
    static void traceTransfer(const char* overload, int resourceW, int resourceH, int level,
                              int x, int y, int w, int h, int startIndex, int elementCount,
                              int requiredElements, const char* source)
    {
        const char* trace = std::getenv("CNA_TEXTURE_TRANSFER_TRACE");
        if (trace == nullptr || *trace == '\0') return;
        std::fprintf(stderr,
                     "[GFX-149] Texture2D::GetData overload=%s resource=%dx%d level=%d "
                     "region=(%d,%d,%dx%d) bytesPerElement=4 bytesPerPixel=4 startIndex=%d "
                     "elementCount=%d requiredElements=%d requiredBytes=%lld "
                     "destElements=[%d,%d) destBytes=[%lld,%lld) rowPitchBytes=%d source=%s\n",
                     overload, resourceW, resourceH, level, x, y, w, h, startIndex, elementCount,
                     requiredElements, static_cast<long long>(requiredElements) * 4,
                     startIndex, startIndex + requiredElements,
                     static_cast<long long>(startIndex) * 4,
                     (static_cast<long long>(startIndex) + requiredElements) * 4,
                     w * 4, source);
        std::fflush(stderr);
    }

    void Texture2D::GetData(Color* data, int startIndex, int elementCount) const
    {
        if (!IsColorTransferFormatEXT(format_))
            throw std::invalid_argument(
                "Texture2D::GetData: Color data requires a Color-compatible 32-bit format");
        if (!data || elementCount <= 0)
            throw std::invalid_argument("data must not be null and elementCount must be > 0");
        if (startIndex < 0)
            throw std::out_of_range("Texture2D::GetData: startIndex must be >= 0");
        Texture::ValidateGetDataFormat(format_, 4);

        // The requested region of this overload is the COMPLETE level 0 -- never elementCount,
        // never a viewport, never a previous request. Argument validation runs BEFORE the storage
        // and capability decision below, so an undersized or overflowing request is answered with
        // its own specific error on every renderer rather than being weakened into a capability
        // rejection (REMED-GFX-162's precedence, from the texture side).
        const int total = width * height;
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, total);

        // RenderTarget2D delegates level zero to the renderer even when a CPU shadow exists: a
        // temporary SetData staging buffer is not authoritative, because a later render into the
        // target may have replaced its contents.
        //
        // The renderer result is PREFERRED, not required. Where the renderer cannot read its colour
        // attachment back, an existing shadow is still the best answer available and is what this
        // overload returned before the preference was introduced -- so fall through to it rather
        // than withdrawing a working readback from every renderer whose render targets are not
        // CPU-readable. The NotSupportedException below stays reachable, and stays the right
        // answer, only when there is genuinely nothing to return: no readback AND no shadow.
        if (gpuOnlyContent_ && renderer_ && total > 0)
        {
            traceTransfer("whole-level(gpu)", width, height, 0, 0, 0, width, height,
                          startIndex, elementCount, total, "renderer");
            // REMED-GFX-127: `pixels` is scratch memory THIS layer owns and zero-initializes,
            // so converting it unconditionally is not "leaving the caller's buffer untouched"
            // when the renderer has no readback -- it fabricates a complete transparent-black
            // frame. Conversion happens only when the renderer reports it wrote the whole
            // region; otherwise the caller's `data` is left byte-for-byte as it was.
            //
            // Sized to the REQUESTED REGION, not to elementCount: the renderer fills exactly
            // width*height texels, so a larger elementCount would otherwise hand the caller
            // this buffer's untouched tail as if it were content.
            std::vector<uint8_t> pixels(static_cast<std::size_t>(total) * 4, 0);
            if (renderer_->GetData(0, 0, 0, width, height, pixels.data(),
                                  static_cast<int>(pixels.size())))
            {
                for (int i = 0; i < total; ++i)
                {
                    const int src = i * 4;
                    data[startIndex + i] =
                        Color(pixels[src + 0], pixels[src + 1], pixels[src + 2], pixels[src + 3]);
                }
                return;
            }
            if (!cpuPixels_ || cpuPixels_->empty())
            {
                throw System::NotSupportedException(
                    "Texture2D::GetData: this graphics renderer cannot read a render target's "
                    "colour attachment back to the CPU");
            }
        }

        // For a plain Texture2D, an empty shadow means it was freed because context recovery is
        // disabled (MaybeFreeCpuPixels). That must throw rather than silently read back whatever
        // the renderer texture currently holds.
        if (!cpuPixels_ || cpuPixels_->empty())
        {
            throw std::runtime_error("Texture2D::GetData: no CPU-side pixel data available");
        }

        const auto& px = *cpuPixels_;
        if (px.size() < static_cast<std::size_t>(total) * 4)
            throw std::runtime_error("Texture2D::GetData: no CPU-side pixel data available");

        traceTransfer("whole-level(cpu)", width, height, 0, 0, 0, width, height,
                      startIndex, elementCount, total, "cpuPixels_");
        for (int i = 0; i < total; ++i)
        {
            const int src = i * 4;
            data[startIndex + i] = Color(px[src + 0], px[src + 1], px[src + 2], px[src + 3]);
        }
    }

    void Texture2D::GetData(Color* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect,
                            Color* data, int startIndex, int elementCount) const
    {
        if (!IsColorTransferFormatEXT(format_))
            throw std::invalid_argument(
                "Texture2D::GetData: Color data requires a Color-compatible 32-bit format");
        if (!data || elementCount <= 0)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        if (startIndex < 0)
            throw std::out_of_range("Texture2D::GetData: startIndex must be >= 0");
        validateMipLevel("Texture2D::GetData", level, levelCount_);
        Texture::ValidateGetDataFormat(format_, 4);

        // Delegate before touching the CPU-side mip shadow at all -- the 3-arg overload has its
        // own complete bounds checking and (for a RenderTarget2D with no shadow) renderer fallback.
        if (level == 0 && rect == nullptr)
        {
            GetData(data, startIndex, elementCount);
            return;
        }

        // A RenderTarget2D's renderer is authoritative for every mip. Its descendants may be
        // regenerated after a later render pass or parent SetData, so a common-layer transfer
        // staging buffer must never mask the live target chain during readback.
        const std::vector<uint8_t>* buf = gpuOnlyContent_ ? nullptr : getMipBufferConst(level);
        if (!buf)
        {
            // Render targets normally own live renderer pixels. A plain texture may also expose a
            // renderer-generated mip that has no caller-authored CPU shadow, but only through the
            // explicit HasDefinedMipLevel contract. Level zero still follows the three-argument
            // overload's stricter context-recovery rule above.
            const bool rendererOwnsDefinedMip =
                level > 0 && renderer_ && renderer_->HasDefinedMipLevel(level);
            if (renderer_ && (gpuOnlyContent_ || rendererOwnsDefinedMip))
            {
                const int levelW = mipDim(width, level);
                const int levelH = mipDim(height, level);
                int x = 0, y = 0, w = levelW, h = levelH;
                if (rect) { x = rect->X; y = rect->Y; w = rect->Width; h = rect->Height; }
                if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > levelW || y + h > levelH)
                    throw std::out_of_range("Texture2D::GetData: rectangle out of texture bounds");
                // REMED-GFX-149: the required element count comes from THIS rectangle's own
                // dimensions at THIS mip level -- never from level 0 and never from the full
                // resource -- and the destination window is checked overflow-safely.
                validateTransferWindow("Texture2D::GetData", startIndex, elementCount, w * h);
                traceTransfer(rendererOwnsDefinedMip ? "rectangle(defined-mip)" : "rectangle(gpu)",
                              levelW, levelH, level, x, y, w, h,
                              startIndex, elementCount, w * h, "renderer");

                // REMED-GFX-127: same contract as the whole-level overload above -- scratch memory
                // this layer zero-initialized is never handed to the caller as if it were content.
                std::vector<uint8_t> pixels(static_cast<std::size_t>(w) * h * 4, 0);
                if (!renderer_->GetData(level, x, y, w, h, pixels.data(),
                                       static_cast<int>(pixels.size())))
                {
                    throw System::NotSupportedException(
                        "Texture2D::GetData: this graphics renderer cannot read the requested "
                        "renderer-owned mip pixels back to the CPU");
                }
                for (int row = 0; row < h; ++row)
                    for (int col = 0; col < w; ++col)
                    {
                        const int src = (row * w + col) * 4;
                        const int dst = startIndex + row * w + col;
                        data[dst] = Color(pixels[src + 0], pixels[src + 1], pixels[src + 2], pixels[src + 3]);
                    }
                return;
            }
            throw std::runtime_error("Texture2D::GetData: no CPU-side pixel data for requested mip level");
        }

        const int levelW = mipDim(width,  level);
        const int levelH = mipDim(height, level);

        int x = 0, y = 0, w = levelW, h = levelH;
        if (rect)
        {
            x = rect->X; y = rect->Y;
            w = rect->Width; h = rect->Height;
        }

        if (x < 0 || y < 0 || x + w > levelW || y + h > levelH)
            throw std::out_of_range("Texture2D::GetData: rectangle out of texture bounds");
        // REMED-GFX-149: as above -- this rectangle's own dimensions at this mip level, and an
        // overflow-safe destination window.
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, w * h);
        traceTransfer("rectangle(cpu)", levelW, levelH, level, x, y, w, h,
                      startIndex, elementCount, w * h, "mipShadow");

        for (int row = 0; row < h; ++row)
        {
            for (int col = 0; col < w; ++col)
            {
                const int src = ((y + row) * levelW + (x + col)) * 4;
                const int dst = startIndex + row * w + col;
                data[dst] = Color((*buf)[src + 0],
                                  (*buf)[src + 1],
                                  (*buf)[src + 2],
                                  (*buf)[src + 3]);
            }
        }
    }

    void Texture2D::GetDataBytes(int level, const Rectangle* rect, std::uint8_t* data,
                                 int startIndex, int elementCount, int elementBytes) const
    {
        if (!data || elementCount <= 0)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        if (startIndex < 0)
            throw std::out_of_range("Texture2D::GetData: startIndex must be >= 0");
        validateMipLevel("Texture2D::GetData", level, levelCount_);
        const int bytesPerTexel = getBytesPerTexel();
        if (elementBytes != bytesPerTexel)
            throw std::invalid_argument(
                "Texture2D::GetData: packed element type does not match the texture format");

        const int levelW = mipDim(width, level);
        const int levelH = mipDim(height, level);
        int x = 0;
        int y = 0;
        int w = levelW;
        int h = levelH;
        if (rect)
        {
            x = rect->X;
            y = rect->Y;
            w = rect->Width;
            h = rect->Height;
        }
        if (x < 0 || y < 0 || w <= 0 || h <= 0
            || w > levelW || h > levelH || x > levelW - w || y > levelH - h)
        {
            throw std::out_of_range("Texture2D::GetData: rectangle out of texture bounds");
        }
        const int requiredElements = w * h;
        validateTransferWindow(
            "Texture2D::GetData", startIndex, elementCount, requiredElements);

        const std::vector<uint8_t>* source = gpuOnlyContent_ ? nullptr : getMipBufferConst(level);
        if (source)
        {
            const std::size_t expectedBytes = static_cast<std::size_t>(levelW) * levelH
                * bytesPerTexel;
            if (source->size() < expectedBytes)
                throw std::runtime_error(
                    "Texture2D::GetData: CPU-side packed pixel storage is incomplete");
            const std::size_t copyBytes = static_cast<std::size_t>(w) * bytesPerTexel;
            for (int row = 0; row < h; ++row)
            {
                const std::size_t sourceOffset =
                    (static_cast<std::size_t>(y + row) * levelW + x) * bytesPerTexel;
                const std::size_t destinationOffset =
                    static_cast<std::size_t>(startIndex + row * w) * bytesPerTexel;
                std::memcpy(data + destinationOffset, source->data() + sourceOffset, copyBytes);
            }
            return;
        }

        const bool rendererOwnsDefinedMip = level > 0 && renderer_
            && renderer_->HasDefinedMipLevel(level);
        if (!renderer_ || (!gpuOnlyContent_ && !rendererOwnsDefinedMip))
            throw std::runtime_error(
                "Texture2D::GetData: no CPU-side packed pixel data for requested mip level");

        std::vector<uint8_t> scratch(
            static_cast<std::size_t>(requiredElements) * bytesPerTexel);
        if (!renderer_->GetData(level, x, y, w, h, scratch.data(),
                               static_cast<int>(scratch.size())))
        {
            throw System::NotSupportedException(
                "Texture2D::GetData: renderer cannot read the requested packed pixels");
        }
        std::memcpy(data + static_cast<std::size_t>(startIndex) * bytesPerTexel,
                    scratch.data(), scratch.size());
    }

    void Texture2D::GetCompressedDataBytes(int level, const Rectangle* rect, std::uint8_t* data,
                                           int startIndex, int elementCount) const
    {
        if (!data || elementCount <= 0)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        if (startIndex < 0)
            throw std::out_of_range("Texture2D::GetData: startIndex must be >= 0");
        validateMipLevel("Texture2D::GetData", level, levelCount_);
        if (!renderer_)
            throw std::runtime_error("Texture2D::GetData: compressed texture has no renderer");

        const int levelW = mipDim(width, level);
        const int levelH = mipDim(height, level);
        int x = 0, y = 0, w = levelW, h = levelH;
        if (rect)
        {
            x = rect->X; y = rect->Y;
            w = rect->Width; h = rect->Height;
        }

        const bool touchesRightEdge = x + w == levelW;
        const bool touchesBottomEdge = y + h == levelH;
        if (x < 0 || y < 0 || w <= 0 || h <= 0
            || w > levelW || h > levelH || x > levelW - w || y > levelH - h
            || (x % 4) != 0 || (y % 4) != 0
            || ((w % 4) != 0 && !touchesRightEdge)
            || ((h % 4) != 0 && !touchesBottomEdge))
        {
            throw std::out_of_range(
                "Texture2D::GetData: compressed rectangle must be block-aligned and within bounds");
        }

        const int blockByteSize = Texture::GetFormatSizeEXT(format_);
        const int blockCols = (w + 3) / 4;
        const int blockRows = (h + 3) / 4;
        const int requiredElements = blockCols * blockRows * blockByteSize;
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);

        if (!renderer_->GetData(level, x, y, w, h, data + startIndex, requiredElements))
        {
            throw System::NotSupportedException(
                "Texture2D::GetData: renderer cannot read the requested compressed block bytes");
        }
    }

    void Texture2D::GetData(PackedVector::Bgr565* data, int startIndex, int elementCount) const
    {
        GetData(0, nullptr, data, startIndex, elementCount);
    }

    void Texture2D::GetData(PackedVector::Bgr565* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect, PackedVector::Bgr565* data,
                            int startIndex, int elementCount) const
    {
        if (format_ != SurfaceFormat::Bgr565)
            throw std::invalid_argument("Texture2D::GetData: Bgr565 data requires Bgr565 format");
        if (!data)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::GetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(requiredElements) * 2u);
        GetDataBytes(level, rect, bytes.data(), 0, requiredElements, 2);
        DecodePackedElements<PackedVector::Bgr565, std::uint16_t>(
            bytes, requiredElements, data, startIndex);
    }

    void Texture2D::GetData(PackedVector::Bgra4444* data, int startIndex, int elementCount) const
    {
        GetData(0, nullptr, data, startIndex, elementCount);
    }

    void Texture2D::GetData(PackedVector::Bgra4444* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect, PackedVector::Bgra4444* data,
                            int startIndex, int elementCount) const
    {
        if (format_ != SurfaceFormat::Bgra4444)
            throw std::invalid_argument(
                "Texture2D::GetData: Bgra4444 data requires Bgra4444 format");
        if (!data)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::GetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(requiredElements) * 2u);
        GetDataBytes(level, rect, bytes.data(), 0, requiredElements, 2);
        DecodePackedElements<PackedVector::Bgra4444, std::uint16_t>(
            bytes, requiredElements, data, startIndex);
    }

    void Texture2D::GetData(PackedVector::Bgra5551* data, int startIndex, int elementCount) const
    {
        GetData(0, nullptr, data, startIndex, elementCount);
    }

    void Texture2D::GetData(PackedVector::Bgra5551* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect, PackedVector::Bgra5551* data,
                            int startIndex, int elementCount) const
    {
        if (format_ != SurfaceFormat::Bgra5551)
            throw std::invalid_argument(
                "Texture2D::GetData: Bgra5551 data requires Bgra5551 format");
        if (!data)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::GetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(requiredElements) * 2u);
        GetDataBytes(level, rect, bytes.data(), 0, requiredElements, 2);
        DecodePackedElements<PackedVector::Bgra5551, std::uint16_t>(
            bytes, requiredElements, data, startIndex);
    }

    void Texture2D::GetData(PackedVector::Rgba1010102* data, int startIndex,
                            int elementCount) const
    {
        GetData(0, nullptr, data, startIndex, elementCount);
    }

    void Texture2D::GetData(PackedVector::Rgba1010102* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect,
                            PackedVector::Rgba1010102* data,
                            int startIndex, int elementCount) const
    {
        if (format_ != SurfaceFormat::Rgba1010102)
            throw std::invalid_argument(
                "Texture2D::GetData: Rgba1010102 data requires Rgba1010102 format");
        if (!data)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::GetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(requiredElements) * 4u);
        GetDataBytes(level, rect, bytes.data(), 0, requiredElements, 4);
        DecodePackedElements<PackedVector::Rgba1010102, std::uint32_t>(
            bytes, requiredElements, data, startIndex);
    }

    void Texture2D::GetData(PackedVector::Alpha8* data, int startIndex, int elementCount) const
    {
        GetData(0, nullptr, data, startIndex, elementCount);
    }

    void Texture2D::GetData(PackedVector::Alpha8* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect, PackedVector::Alpha8* data,
                            int startIndex, int elementCount) const
    {
        if (format_ != SurfaceFormat::Alpha8)
            throw std::invalid_argument("Texture2D::GetData: Alpha8 data requires Alpha8 format");
        if (!data)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::GetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(requiredElements));
        GetDataBytes(level, rect, bytes.data(), 0, requiredElements, 1);
        DecodePackedElements<PackedVector::Alpha8, std::uint8_t>(
            bytes, requiredElements, data, startIndex);
    }

    void Texture2D::GetData(PackedVector::Rg32* data, int startIndex, int elementCount) const
    {
        GetData(0, nullptr, data, startIndex, elementCount);
    }

    void Texture2D::GetData(PackedVector::Rg32* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect, PackedVector::Rg32* data,
                            int startIndex, int elementCount) const
    {
        if (format_ != SurfaceFormat::Rg32)
            throw std::invalid_argument("Texture2D::GetData: Rg32 data requires Rg32 format");
        if (!data)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::GetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(requiredElements) * 4u);
        GetDataBytes(level, rect, bytes.data(), 0, requiredElements, 4);
        DecodePackedElements<PackedVector::Rg32, std::uint32_t>(
            bytes, requiredElements, data, startIndex);
    }

    void Texture2D::GetData(PackedVector::Rgba64* data, int startIndex, int elementCount) const
    {
        GetData(0, nullptr, data, startIndex, elementCount);
    }

    void Texture2D::GetData(PackedVector::Rgba64* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect, PackedVector::Rgba64* data,
                            int startIndex, int elementCount) const
    {
        if (format_ != SurfaceFormat::Rgba64)
            throw std::invalid_argument("Texture2D::GetData: Rgba64 data requires Rgba64 format");
        if (!data)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::GetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(requiredElements) * 8u);
        GetDataBytes(level, rect, bytes.data(), 0, requiredElements, 8);
        DecodePackedElements<PackedVector::Rgba64, std::uint64_t>(
            bytes, requiredElements, data, startIndex);
    }

    void Texture2D::GetData(PackedVector::NormalizedByte2* data,
                            int startIndex, int elementCount) const
    {
        GetData(0, nullptr, data, startIndex, elementCount);
    }

    void Texture2D::GetData(PackedVector::NormalizedByte2* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect,
                            PackedVector::NormalizedByte2* data,
                            int startIndex, int elementCount) const
    {
        if (format_ != SurfaceFormat::NormalizedByte2)
            throw std::invalid_argument(
                "Texture2D::GetData: NormalizedByte2 data requires NormalizedByte2 format");
        if (!data)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::GetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(requiredElements) * 2u);
        GetDataBytes(level, rect, bytes.data(), 0, requiredElements, 2);
        DecodePackedElements<PackedVector::NormalizedByte2, std::uint16_t>(
            bytes, requiredElements, data, startIndex);
    }

    void Texture2D::GetData(PackedVector::NormalizedByte4* data,
                            int startIndex, int elementCount) const
    {
        GetData(0, nullptr, data, startIndex, elementCount);
    }

    void Texture2D::GetData(PackedVector::NormalizedByte4* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect,
                            PackedVector::NormalizedByte4* data,
                            int startIndex, int elementCount) const
    {
        if (format_ != SurfaceFormat::NormalizedByte4)
            throw std::invalid_argument(
                "Texture2D::GetData: NormalizedByte4 data requires NormalizedByte4 format");
        if (!data)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::GetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(requiredElements) * 4u);
        GetDataBytes(level, rect, bytes.data(), 0, requiredElements, 4);
        DecodePackedElements<PackedVector::NormalizedByte4, std::uint32_t>(
            bytes, requiredElements, data, startIndex);
    }

    void Texture2D::GetData(float* data, int startIndex, int elementCount) const
    {
        GetData(0, nullptr, data, startIndex, elementCount);
    }

    void Texture2D::GetData(float* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect, float* data,
                            int startIndex, int elementCount) const
    {
        if (format_ != SurfaceFormat::Single)
            throw std::invalid_argument("Texture2D::GetData: float data requires Single format");
        if (!data)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::GetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(requiredElements) * 4u);
        GetDataBytes(level, rect, bytes.data(), 0, requiredElements, 4);
        DecodeFloatElements(bytes, requiredElements, data, startIndex);
    }

    void Texture2D::GetData(Vector2* data, int startIndex, int elementCount) const
    {
        GetData(0, nullptr, data, startIndex, elementCount);
    }

    void Texture2D::GetData(Vector2* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect, Vector2* data,
                            int startIndex, int elementCount) const
    {
        if (format_ != SurfaceFormat::Vector2)
            throw std::invalid_argument("Texture2D::GetData: Vector2 data requires Vector2 format");
        if (!data)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::GetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(requiredElements) * 8u);
        GetDataBytes(level, rect, bytes.data(), 0, requiredElements, 8);
        DecodeFloatElements(bytes, requiredElements, data, startIndex);
    }

    void Texture2D::GetData(Vector4* data, int startIndex, int elementCount) const
    {
        GetData(0, nullptr, data, startIndex, elementCount);
    }

    void Texture2D::GetData(Vector4* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect, Vector4* data,
                            int startIndex, int elementCount) const
    {
        if (format_ != SurfaceFormat::Vector4)
            throw std::invalid_argument("Texture2D::GetData: Vector4 data requires Vector4 format");
        if (!data)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::GetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(requiredElements) * 16u);
        GetDataBytes(level, rect, bytes.data(), 0, requiredElements, 16);
        DecodeFloatElements(bytes, requiredElements, data, startIndex);
    }

    void Texture2D::GetData(PackedVector::HalfSingle* data,
                            int startIndex, int elementCount) const
    {
        GetData(0, nullptr, data, startIndex, elementCount);
    }

    void Texture2D::GetData(PackedVector::HalfSingle* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect,
                            PackedVector::HalfSingle* data,
                            int startIndex, int elementCount) const
    {
        if (format_ != SurfaceFormat::HalfSingle)
            throw std::invalid_argument(
                "Texture2D::GetData: HalfSingle data requires HalfSingle format");
        if (!data)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::GetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(requiredElements) * 2u);
        GetDataBytes(level, rect, bytes.data(), 0, requiredElements, 2);
        DecodePackedElements<PackedVector::HalfSingle, std::uint16_t>(
            bytes, requiredElements, data, startIndex);
    }

    void Texture2D::GetData(PackedVector::HalfVector2* data,
                            int startIndex, int elementCount) const
    {
        GetData(0, nullptr, data, startIndex, elementCount);
    }

    void Texture2D::GetData(PackedVector::HalfVector2* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect,
                            PackedVector::HalfVector2* data,
                            int startIndex, int elementCount) const
    {
        if (format_ != SurfaceFormat::HalfVector2)
            throw std::invalid_argument(
                "Texture2D::GetData: HalfVector2 data requires HalfVector2 format");
        if (!data)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::GetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(requiredElements) * 4u);
        GetDataBytes(level, rect, bytes.data(), 0, requiredElements, 4);
        DecodePackedElements<PackedVector::HalfVector2, std::uint32_t>(
            bytes, requiredElements, data, startIndex);
    }

    void Texture2D::GetData(PackedVector::HalfVector4* data,
                            int startIndex, int elementCount) const
    {
        GetData(0, nullptr, data, startIndex, elementCount);
    }

    void Texture2D::GetData(PackedVector::HalfVector4* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect,
                            PackedVector::HalfVector4* data,
                            int startIndex, int elementCount) const
    {
        if (format_ != SurfaceFormat::HalfVector4 && format_ != SurfaceFormat::HdrBlendable)
            throw std::invalid_argument(
                "Texture2D::GetData: HalfVector4 data requires HalfVector4 or HdrBlendable format");
        if (!data)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::GetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(requiredElements) * 8u);
        GetDataBytes(level, rect, bytes.data(), 0, requiredElements, 8);
        DecodePackedElements<PackedVector::HalfVector4, std::uint64_t>(
            bytes, requiredElements, data, startIndex);
    }

    void Texture2D::GetData(std::uint8_t* data, int startIndex, int elementCount) const
    {
        GetData(0, nullptr, data, startIndex, elementCount);
    }

    void Texture2D::GetData(std::uint8_t* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect, std::uint8_t* data,
                            int startIndex, int elementCount) const
    {
        if (IsCompressedTransferFormatEXT(format_))
        {
            GetCompressedDataBytes(level, rect, data, startIndex, elementCount);
            return;
        }
        if (format_ != SurfaceFormat::ByteEXT)
            throw std::invalid_argument("Texture2D::GetData: byte data requires ByteEXT format");
        if (!data)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::GetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(requiredElements));
        GetDataBytes(level, rect, bytes.data(), 0, requiredElements, 1);
        DecodeUnsignedElements(bytes, requiredElements, data, startIndex);
    }

    void Texture2D::GetData(std::uint16_t* data, int startIndex, int elementCount) const
    {
        GetData(0, nullptr, data, startIndex, elementCount);
    }

    void Texture2D::GetData(std::uint16_t* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect, std::uint16_t* data,
                            int startIndex, int elementCount) const
    {
        if (format_ != SurfaceFormat::UShortEXT)
            throw std::invalid_argument(
                "Texture2D::GetData: ushort data requires UShortEXT format");
        if (!data)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        const int requiredElements = ValidatedRequestedTexelCount(
            "Texture2D::GetData", width, height, level, levelCount_, rect);
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, requiredElements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(requiredElements) * 2u);
        GetDataBytes(level, rect, bytes.data(), 0, requiredElements, 2);
        DecodeUnsignedElements(bytes, requiredElements, data, startIndex);
    }

    // -----------------------------------------------------------------------
    // FromStream
    // -----------------------------------------------------------------------

    struct DecodedTexture2D final
    {
        int width = 0;
        int height = 0;
        std::vector<std::vector<uint8_t>> rgbaLevels;
    };

    static uint32_t ReadU32Le(const uint8_t* bytes)
    {
        return static_cast<uint32_t>(bytes[0])
             | (static_cast<uint32_t>(bytes[1]) << 8)
             | (static_cast<uint32_t>(bytes[2]) << 16)
             | (static_cast<uint32_t>(bytes[3]) << 24);
    }

    static std::size_t DdsLevelByteCount(int width, int height, uint32_t fourCC)
    {
        const std::size_t blockWidth = (static_cast<std::size_t>(width) + 3u) / 4u;
        const std::size_t blockHeight = (static_cast<std::size_t>(height) + 3u) / 4u;
        const std::size_t bytesPerBlock = fourCC == 0x31545844u ? 8u : 16u;
        if (blockWidth > std::numeric_limits<std::size_t>::max() / blockHeight
            || blockWidth * blockHeight > std::numeric_limits<std::size_t>::max() / bytesPerBlock)
        {
            throw System::FormatException(
                "Texture2D::FromStream: DDS mip byte count overflows the host address space");
        }
        return blockWidth * blockHeight * bytesPerBlock;
    }

    // SKIA-130: distinguish an ordinary non-DDS image from a malformed DDS.
    // Once the DDS magic is present, every declared field and level is validated here and errors
    // are reported as DDS errors instead of falling through to an unrelated image decoder.
    static bool TryDecodeDds(const uint8_t* buf, std::size_t len, int maximumTextureDimension,
                             DecodedTexture2D& decoded)
    {
        if (len < 4 || buf[0] != 'D' || buf[1] != 'D' || buf[2] != 'S' || buf[3] != ' ')
            return false;
        if (len < 128)
            throw System::FormatException("Texture2D::FromStream: truncated DDS header");

        // DDS_HEADER fields (all little-endian uint32)
        auto r32 = [&](std::size_t off) -> uint32_t {
            return ReadU32Le(buf + off);
        };

        if (r32(4) != 124u || r32(76) != 32u)
            throw System::FormatException("Texture2D::FromStream: invalid DDS header size");

        const uint32_t rawHeight = r32(12);
        const uint32_t rawWidth = r32(16);
        if (rawWidth == 0u || rawHeight == 0u
            || rawWidth > static_cast<uint32_t>(std::numeric_limits<int>::max())
            || rawHeight > static_cast<uint32_t>(std::numeric_limits<int>::max()))
        {
            throw System::FormatException("Texture2D::FromStream: invalid DDS dimensions");
        }
        const int width = static_cast<int>(rawWidth);
        const int height = static_cast<int>(rawHeight);
        if (width > maximumTextureDimension || height > maximumTextureDimension)
        {
            throw System::NotSupportedException(
                "Texture2D::FromStream: DDS dimensions exceed this device's maximum texture "
                "dimension of " + std::to_string(maximumTextureDimension));
        }

        // DDS_PIXELFORMAT starts at offset 76; dwFourCC at offset 84
        const uint32_t fourCC = r32(84);
        // fourCC codes: 'DXT1'=0x31545844, 'DXT3'=0x33545844, 'DXT5'=0x35545844
        if (fourCC != 0x31545844u && fourCC != 0x33545844u && fourCC != 0x35545844u)
        {
            throw System::NotSupportedException(
                "Texture2D::FromStream: unsupported DDS pixel format "
                "(only DXT1/DXT3/DXT5 are supported)");
        }

        const int maximumLevels = CalculateMipLevels(width, height);
        const uint32_t declaredMipCount = r32(28);
        if (declaredMipCount > static_cast<uint32_t>(maximumLevels))
        {
            throw System::FormatException(
                "Texture2D::FromStream: DDS declares more mip levels than its dimensions allow");
        }
        const int mipCount = declaredMipCount == 0u ? 1 : static_cast<int>(declaredMipCount);
        // Texture2D's XNA-compatible public allocation is either level zero or a complete chain.
        // Reject a prefix instead of allocating the complete chain and silently generating levels
        // the asset never contained.
        if (mipCount != 1 && mipCount != maximumLevels)
        {
            throw System::FormatException(
                "Texture2D::FromStream: DDS mip chain is incomplete; expected level zero only or "
                "the complete chain");
        }

        DecodedTexture2D result;
        result.width = width;
        result.height = height;
        result.rgbaLevels.reserve(static_cast<std::size_t>(mipCount));

        std::size_t offset = 128u;
        int levelWidth = width;
        int levelHeight = height;
        for (int level = 0; level < mipCount; ++level)
        {
            const std::size_t levelBytes = DdsLevelByteCount(levelWidth, levelHeight, fourCC);
            if (offset > len || levelBytes > len - offset)
            {
                throw System::FormatException(
                    "Texture2D::FromStream: truncated DDS mip level " + std::to_string(level));
            }

            const uint8_t* levelData = buf + offset;
            if (fourCC == 0x31545844u)
                result.rgbaLevels.push_back(DxtUtil::DecompressDxt1(
                    levelData, levelBytes, levelWidth, levelHeight));
            else if (fourCC == 0x33545844u)
                result.rgbaLevels.push_back(DxtUtil::DecompressDxt3(
                    levelData, levelBytes, levelWidth, levelHeight));
            else
                result.rgbaLevels.push_back(DxtUtil::DecompressDxt5(
                    levelData, levelBytes, levelWidth, levelHeight));

            offset += levelBytes;
            levelWidth = std::max(1, levelWidth / 2);
            levelHeight = std::max(1, levelHeight / 2);
        }

        decoded = std::move(result);
        return true;
    }

    // Reads the entire stream and decodes it into RGBA8 pixel data — DDS/DXT1/3/5 via
    // DxtUtil, everything else through ImageLoader (see docs/texture-stream-formats.md for
    // the formats verified by CI).
    static DecodedTexture2D DecodeStreamToImageData(
        System::IO::Stream& stream, int maximumTextureDimension)
    {
        using System::IO::intcs;
        using System::IO::bytecs;

        intcs len = stream.getLengthProperty();
        if (len <= 0)
            throw std::runtime_error("Texture2D::FromStream: stream is empty or length unknown");

        std::vector<bytecs> buf(static_cast<std::size_t>(len));
        intcs bytesRead = 0;
        while (bytesRead < len)
        {
            const intcs count = stream.Read(buf.data(), bytesRead, len - bytesRead);
            if (count <= 0)
                throw std::runtime_error("Texture2D::FromStream: stream ended before its declared length");
            bytesRead += count;
        }

        const auto* raw = reinterpret_cast<const uint8_t*>(buf.data());

        DecodedTexture2D decoded;
        if (!TryDecodeDds(raw, static_cast<std::size_t>(len), maximumTextureDimension, decoded))
        {
            ImageData image = ImageLoader::LoadFromMemory(raw, static_cast<std::size_t>(len));
            decoded.width = image.width;
            decoded.height = image.height;
            decoded.rgbaLevels.push_back(std::move(image.pixels));
        }
        return decoded;
    }

    Texture2D Texture2D::MakeTextureFromPixels(GraphicsDevice& device, int w, int h,
                                               std::vector<std::uint8_t>&& rgba)
    {
        std::vector<std::vector<std::uint8_t>> levels;
        levels.push_back(std::move(rgba));
        return MakeTextureFromMipPixels(device, w, h, std::move(levels));
    }

    Texture2D Texture2D::MakeTextureFromMipPixels(
        GraphicsDevice& device, int w, int h,
        std::vector<std::vector<std::uint8_t>>&& rgbaLevels)
    {
#ifdef CNA_RENDERER_DIRECTX9
        ValidateTextureSizeForProfileEXT(device, w, h);
#endif
        ValidateTextureDimensionEXT(device, w, h);
        const int maximumLevels = CalculateMipLevels(w, h);
        if (rgbaLevels.empty()
            || (rgbaLevels.size() != 1u
                && rgbaLevels.size() != static_cast<std::size_t>(maximumLevels)))
        {
            throw std::invalid_argument(
                "Texture2D::FromStream: decoded mip chain must contain level zero or every level");
        }

        int levelWidth = w;
        int levelHeight = h;
        for (std::size_t level = 0; level < rgbaLevels.size(); ++level)
        {
            const std::size_t expected = static_cast<std::size_t>(levelWidth)
                                       * static_cast<std::size_t>(levelHeight) * 4u;
            if (rgbaLevels[level].size() != expected)
            {
                throw std::invalid_argument(
                    "Texture2D::FromStream: decoded RGBA mip level has the wrong byte count");
            }
            levelWidth = std::max(1, levelWidth / 2);
            levelHeight = std::max(1, levelHeight / 2);
        }

        ImageData img;
        img.width  = w;
        img.height = h;
        img.mipLevels = static_cast<int>(rgbaLevels.size());
        img.pixels = std::move(rgbaLevels[0]);

        Texture2D tex;
        tex.graphicsDevice_ = &device;
        tex.width           = w;
        tex.height          = h;
        tex.levelCount_      = img.mipLevels;
        tex.renderer_        = device.GetRenderer().CreateTexture(img);
        tex.cpuPixels_      = std::make_shared<std::vector<uint8_t>>(std::move(img.pixels));
        tex.renderer_->ShareCpuPixels(tex.cpuPixels_);

        if (rgbaLevels.size() > 1u)
        {
            tex.extraMipLevels_ = std::make_shared<std::vector<std::vector<uint8_t>>>();
            tex.extraMipLevels_->reserve(rgbaLevels.size() - 1u);
            levelWidth = std::max(1, w / 2);
            levelHeight = std::max(1, h / 2);
            for (std::size_t level = 1; level < rgbaLevels.size(); ++level)
            {
                tex.extraMipLevels_->push_back(std::move(rgbaLevels[level]));
                const auto& pixels = tex.extraMipLevels_->back();
                tex.renderer_->UpdatePixelsLevel(
                    static_cast<int>(level), pixels.data(), levelWidth, levelHeight);
                levelWidth = std::max(1, levelWidth / 2);
                levelHeight = std::max(1, levelHeight / 2);
            }
        }
        tex.MaybeFreeCpuPixels();
        return tex;
    }

    Texture2D Texture2D::FromStream(GraphicsDevice& graphicsDevice, System::IO::Stream& stream)
    {
        DecodedTexture2D decoded = DecodeStreamToImageData(
            stream, graphicsDevice.GetMaxTextureDimension());
        return MakeTextureFromMipPixels(
            graphicsDevice, decoded.width, decoded.height, std::move(decoded.rgbaLevels));
    }

    Texture2D Texture2D::FromStream(GraphicsDevice& graphicsDevice, System::IO::Stream& stream,
                                    int width, int height, bool zoom)
    {
        DecodedTexture2D decoded = DecodeStreamToImageData(
            stream, graphicsDevice.GetMaxTextureDimension());
        const std::vector<uint8_t>& levelZero = decoded.rgbaLevels.front();
        ImageData resized = ImageLoader::ResizeRgba(
            levelZero.data(), decoded.width, decoded.height, width, height, zoom);
        return MakeTextureFromPixels(
            graphicsDevice, resized.width, resized.height, std::move(resized.pixels));
    }

    // -----------------------------------------------------------------------
    // SaveAsPng
    // -----------------------------------------------------------------------

    void Texture2D::SaveAsPng(System::IO::Stream* stream, int targetWidth, int targetHeight) const
    {
        if (!stream)
            throw std::invalid_argument("Texture2D::SaveAsPng: stream is null");
        if (!cpuPixels_ || cpuPixels_->empty())
            throw std::runtime_error("Texture2D::SaveAsPng: no CPU-side pixel data available");

        const std::vector<uint8_t> encoded = ImageLoader::EncodePng(
            cpuPixels_->data(), width, height, targetWidth, targetHeight);
        stream->Write(reinterpret_cast<const System::IO::bytecs*>(encoded.data()), 0,
                      static_cast<System::IO::intcs>(encoded.size()));
    }

    void Texture2D::SaveAsPng(const std::string& filename) const
    {
        if (!cpuPixels_ || cpuPixels_->empty())
            throw std::runtime_error("Texture2D::SaveAsPng: no CPU-side pixel data available");

        ImageLoader::SavePng(cpuPixels_->data(), width, height, filename);
    }

    // -----------------------------------------------------------------------
    // SaveAsJpeg
    // -----------------------------------------------------------------------

    // Mirrors FNA's Texture2D.SaveAsJpeg quality lookup: FNA_GRAPHICS_JPEG_SAVE_QUALITY env var,
    // falling back to 100 if unset or unparseable.
    static int GetJpegSaveQuality()
    {
        const char* qualityString = std::getenv("FNA_GRAPHICS_JPEG_SAVE_QUALITY");
        if (qualityString && *qualityString)
        {
            try { return std::stoi(qualityString); }
            catch (...) { /* fall through to default */ }
        }
        return 100;
    }

    void Texture2D::SaveAsJpeg(System::IO::Stream* stream, int targetWidth, int targetHeight) const
    {
        if (!stream)
            throw std::invalid_argument("Texture2D::SaveAsJpeg: stream is null");
        if (!cpuPixels_ || cpuPixels_->empty())
            throw std::runtime_error("Texture2D::SaveAsJpeg: no CPU-side pixel data available");

        const std::vector<uint8_t> encoded = ImageLoader::EncodeJpeg(
            cpuPixels_->data(), width, height, targetWidth, targetHeight, GetJpegSaveQuality());
        stream->Write(reinterpret_cast<const System::IO::bytecs*>(encoded.data()), 0,
                      static_cast<System::IO::intcs>(encoded.size()));
    }

    void Texture2D::SaveAsJpeg(const std::string& filename) const
    {
        if (!cpuPixels_ || cpuPixels_->empty())
            throw std::runtime_error("Texture2D::SaveAsJpeg: no CPU-side pixel data available");

        ImageLoader::SaveJpeg(
            cpuPixels_->data(), width, height, filename, GetJpegSaveQuality());
    }

    Texture2D Texture2D::CreateFromPixels(GraphicsDevice& device,
                                          int w, int h,
                                          const std::vector<std::uint8_t>& rgba)
    {
        ImageData data;
        data.width  = w;
        data.height = h;
        data.pixels = rgba;
        Texture2D tex;
        tex.graphicsDevice_ = &device;
        tex.width           = w;
        tex.height          = h;
        tex.renderer_        = device.GetRenderer().CreateTexture(data);
        tex.cpuPixels_      = std::make_shared<std::vector<uint8_t>>(std::move(data.pixels));
        tex.renderer_->ShareCpuPixels(tex.cpuPixels_);
        tex.MaybeFreeCpuPixels();
        return tex;
    }

    Texture2D Texture2D::CreateCpuOnlyForTests(int w, int h, SurfaceFormat format,
                                               const std::vector<Color>& pixels)
    {
        Texture2D tex;              // default ctor: no GraphicsDevice, null renderer
        tex.width       = w;
        tex.height      = h;
        tex.format_     = format;
        tex.levelCount_ = 1;

        auto buf = std::make_shared<std::vector<uint8_t>>();
        buf->reserve(pixels.size() * 4);
        for (const Color& c : pixels)
        {
            buf->push_back(c.getRProperty());
            buf->push_back(c.getGProperty());
            buf->push_back(c.getBProperty());
            buf->push_back(c.getAProperty());
        }
        tex.cpuPixels_ = std::move(buf);
        return tex;
    }

    Texture2D Texture2D::CreateWithRendererForTests(int w, int h,
                                                   std::shared_ptr<ITextureRenderer> renderer)
    {
        Texture2D tex;              // default ctor: no GraphicsDevice
        tex.width   = w;
        tex.height  = h;
        tex.renderer_ = std::move(renderer);
        return tex;
    }

    Texture2D Texture2D::ReconstructFromCache(GraphicsDevice& device,
                                              int w, int h,
                                              SurfaceFormat fmt,
                                              int levelCount,
                                              std::shared_ptr<ITextureRenderer> renderer,
                                              std::shared_ptr<std::vector<uint8_t>> cpuPixels)
    {
        Texture2D tex(device, w, h, fmt, levelCount, std::move(renderer));
        // REMED-GFX-223: the constructor above belongs to RenderTarget2D and therefore raises
        // gpuOnlyContent_, but a cache hit reconstructs an ordinary content texture -- its pixels
        // come from the loader and SetData, never from rendering into it. Leaving the flag raised
        // declares the shared cached renderer authoritative, which makes GetData stop consulting
        // the CPU shadow this factory is about to install and makes SetData mutate the cached
        // renderer in place instead of detaching from it.
        tex.gpuOnlyContent_ = false;
        tex.cpuPixels_ = std::move(cpuPixels);
        return tex;
    }
}
