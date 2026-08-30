// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Graphics/DdsCubeDecoder.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "System/IO/Stream.hpp"
#include "System/FormatException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

// plans/plan_dx9.md Phase D9-10 (D9-103 follow-up): GraphicsProfile.Reach/HiDef cube-texture-size
// ceilings. plans/plan_runtimerenderer.md design decision 9: asked of the active renderer rather than
// convention exactly.

namespace Microsoft::Xna::Framework::Graphics
{
    // Mirrors FNA's Texture.CalculateMipLevels(size) — TextureCube faces are square, so both
    // dimensions are the same and neither halves faster than the other.
    static int CalculateMipLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

    static int mipDim(int base, int level)
    {
        return std::max(1, base >> level);
    }

    // D9-103 follow-up: same profile-CEILING enforcement Texture2D.cpp already established
    // (D9-100's own table: Reach=512, HiDef=4096). Renderers with no profile distinction report
    // no ceiling, which is exactly what they did when this was an #ifdef block.
    static void ValidateCubeSizeForProfileEXT(const GraphicsDevice& device, int size)
    {
        const int profile = static_cast<int>(device.getGraphicsProfileProperty());
        const int maxSize = device.GetRenderer().GetMaxCubeSizeForProfileEXT(profile);
        if (size > maxSize)
        {
            throw System::NotSupportedException(
                "TextureCube: size " + std::to_string(size) + " exceeds GraphicsProfile." +
                (profile == 1 ? std::string("HiDef") : std::string("Reach")) +
                "'s own maximum cube size of " + std::to_string(maxSize));
        }
    }

    TextureCube::~TextureCube() = default;

    TextureCube::TextureCube(GraphicsDevice& device, int size, bool mipMap, SurfaceFormat format)
        : Texture(&device)
        , size_(size)
        , renderer_(nullptr)
    {
        ValidateCubeSizeForProfileEXT(device, size);
        Texture::ValidateFormat(format);
        format_     = format;
        levelCount_ = mipMap ? CalculateMipLevels(size, size) : 1;
        renderer_ = device.GetRenderer().CreateTextureCube(size, mipMap, static_cast<int>(format));
    }

    TextureCube::TextureCube(GraphicsDevice& device, int size, SurfaceFormat format,
                             std::shared_ptr<CNA::Internal::Renderers::ITextureCubeRenderer> renderer,
                             int levelCount)
        : Texture(&device)
        , size_(size)
        , renderer_(std::move(renderer))
    {
        // Task 774 finding: this constructor (used exclusively by RenderTargetCube) previously
        // skipped ValidateFormat entirely, silently accepting any SurfaceFormat even though
        // CreateTextureCube's own renderer call never actually forwards it -- a RenderTargetCube
        // could report a non-Color Format() while its real GPU resource was always Color.
        //
        // plans/plan_modern.md MOD-107: the format is forwarded now (CreateRenderTargetCubeEXT), so the
        // rule that keeps that finding fixed is no longer "Color only" but "whatever the renderer
        // says it really creates". The same tri-state verdict RenderTarget2D consults answers it,
        // so a cube and a 2D target can never disagree about a format.
        // REMED-GFX-245: the profile is asked first, exactly as Texture2D does, and refuses with
        // XNA's own exception type. The cube list is measured, not inferred from the 2D one -- it
        // excludes NormalizedByte2/4 at BOTH profiles.
        if (!Texture::IsCubeFormatAllowedByProfileEXT(device.getGraphicsProfileProperty(), format))
        {
            throw System::NotSupportedException(
                "TextureCube: SurfaceFormat " + std::to_string(static_cast<int>(format)) +
                " is not available for a cube on GraphicsProfile." +
                (device.getGraphicsProfileProperty() == GraphicsProfile::HiDef
                     ? std::string("HiDef")
                     : std::string("Reach")) +
                " -- this is the profile's own restriction, not the renderer's capability");
        }
        switch (device.GetRenderer().ClassifyRenderTargetFormatEXT(static_cast<int>(format)))
        {
            case CNA::Internal::Renderers::RendererFormatVerdict::Supported:
                break;
            case CNA::Internal::Renderers::RendererFormatVerdict::Unsupported:
                throw System::NotSupportedException(
                    "RenderTargetCube: this SurfaceFormat is not renderable on the active renderer.");
            case CNA::Internal::Renderers::RendererFormatVerdict::Defer:
                Texture::ValidateFormat(format);
                break;
        }
        format_     = format;
        levelCount_ = levelCount;
    }

    void TextureCube::Dispose(bool disposing)
    {
        renderer_.reset();
        Texture::Dispose(disposing);
    }

    int TextureCube::getSizeProperty() const { return size_; }

    const std::string& TextureCube::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.TextureCube";
        return name;
    }

    // Color has a vtable pointer (sizeof(Color) == 24), so we must never pass
    // Color* directly to GL. Always unpack to plain uint8_t RGBA and repack on readback.

    static std::vector<uint8_t> colorsToRgba(const Color* data, int startIndex, int count)
    {
        std::vector<uint8_t> rgba(static_cast<std::size_t>(count) * 4);
        for (int i = 0; i < count; ++i)
        {
            rgba[i * 4 + 0] = data[startIndex + i].getRProperty();
            rgba[i * 4 + 1] = data[startIndex + i].getGProperty();
            rgba[i * 4 + 2] = data[startIndex + i].getBProperty();
            rgba[i * 4 + 3] = data[startIndex + i].getAProperty();
        }
        return rgba;
    }

    static void rgbaToColors(const std::vector<uint8_t>& rgba, Color* data, int startIndex, int count)
    {
        for (int i = 0; i < count; ++i)
            data[startIndex + i] = Color(rgba[i * 4 + 0], rgba[i * 4 + 1],
                                         rgba[i * 4 + 2], rgba[i * 4 + 3]);
    }

    void TextureCube::SetData(CubeMapFace face, const Color* data, int elementCount)
    {
        SetData(face, data, 0, elementCount);
    }

    void TextureCube::SetData(CubeMapFace face, const Color* data, int startIndex, int elementCount)
    {
        // Matches FNA's TextureCube.SetData<T>(face,data,startIndex,elementCount), which
        // delegates to the 6-arg overload covering the full face at level 0.
        SetData(face, 0, nullptr, data, startIndex, elementCount);
    }

    static bool IsValidCubeMapFace(CubeMapFace face)
    {
        const int f = static_cast<int>(face);
        return f >= static_cast<int>(CubeMapFace::PositiveX)
            && f <= static_cast<int>(CubeMapFace::NegativeZ);
    }

    void TextureCube::SetData(CubeMapFace face, int level, const Microsoft::Xna::Framework::Rectangle* rect,
                              const Color* data, int startIndex, int elementCount)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("TextureCube");
        if (!IsValidCubeMapFace(face))
            throw std::out_of_range("TextureCube::SetData: face is not a valid CubeMapFace value");
        if (!data)
            throw std::invalid_argument("TextureCube::SetData: data must not be null");
        if (elementCount <= 0)
            throw std::out_of_range("TextureCube::SetData: elementCount must be > 0");
        if (startIndex < 0)
            throw std::out_of_range("TextureCube::SetData: startIndex must be >= 0");
        if (level < 0)
            throw std::out_of_range("TextureCube::SetData: level must be >= 0");

        const int levelSize = mipDim(size_, level);
        int x = 0, y = 0, w = levelSize, h = levelSize;
        if (rect) { x = rect->X; y = rect->Y; w = rect->Width; h = rect->Height; }
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > levelSize || y + h > levelSize)
            throw std::out_of_range("TextureCube::SetData: rectangle out of texture bounds");
        if (elementCount < w * h)
            throw std::out_of_range("TextureCube::SetData: elementCount is less than the number of pixels in the requested region");

        // REMED-GFX-135 (REMED-GFX-127/130's contract, applied to the WRITE direction). Every check
        // above runs BEFORE anything is converted or handed to a renderer, so a rejected call cannot
        // have changed one texel. A renderer that was never created at all (ASCII,
        // Canvas, DIRECTX3 keep IGraphicsRenderer::CreateTextureCube's nullptr default) used to be skipped
        // by a bare `if (renderer_)`, which turned "no storage exists" into a successful-looking
        // upload -- it is the same answer as an unimplemented write, reached one step earlier.
        if (!renderer_)
        {
            throw System::NotSupportedException(
                "TextureCube::SetData: this graphics renderer creates no cube-map texture resource, "
                "so a cube face's content cannot be stored");
        }

        // Converted to the REQUESTED REGION, not to elementCount: the renderer stores exactly w*h
        // texels, so converting more would read source elements the call never uploads and hand
        // renderers a buffer whose length disagrees with the region they are told to write.
        const auto rgba = colorsToRgba(data, startIndex, w * h);
        if (!renderer_->SetData(static_cast<int>(face), level, x, y, w, h,
                               rgba.data(), static_cast<int>(rgba.size())))
        {
            throw System::NotSupportedException(
                "TextureCube::SetData: this graphics renderer did not store the complete requested "
                "cube face region -- the face, mip level or region is not supported here");
        }

        // Level-0-only CPU shadow (see the header's own comment on cpuPixels_ for scope) --
        // lazily created on first write, then mutated in place for every subsequent write to
        // the SAME face; only re-shared with the renderer when the shared_ptr object itself is
        // first created, since mutating the existing buffer is already visible through the
        // renderer's own aliased shared_ptr (same convention Texture2D::SetData(level,rect,...)
        // already established for its own single-face cpuPixels_ buffer). Updated only after the
        // renderer accepted the store above, so a refused write cannot desynchronize the shadow.
        if (level == 0)
        {
            const int faceIdx = static_cast<int>(face);
            auto& shadow = cpuPixels_[faceIdx];
            const bool isNew = !shadow;
            if (isNew)
                shadow = std::make_shared<std::vector<uint8_t>>(static_cast<std::size_t>(size_) * size_ * 4, 0);
            for (int row = 0; row < h; ++row)
                std::memcpy(shadow->data() + (static_cast<std::size_t>(y + row) * size_ + x) * 4,
                            rgba.data() + static_cast<std::size_t>(row) * w * 4,
                            static_cast<std::size_t>(w) * 4);
            if (isNew)
                renderer_->ShareCpuPixels(faceIdx, shadow);
        }
    }

    void TextureCube::GetData(CubeMapFace face, Color* data, int elementCount) const
    {
        GetData(face, data, 0, elementCount);
    }

    void TextureCube::GetData(CubeMapFace face, Color* data, int startIndex, int elementCount) const
    {
        // Matches FNA's TextureCube.GetData<T>(face,data,startIndex,elementCount), which
        // delegates to the 6-arg overload covering the full face at level 0.
        GetData(face, 0, nullptr, data, startIndex, elementCount);
    }

    void TextureCube::GetData(CubeMapFace face, int level, const Microsoft::Xna::Framework::Rectangle* rect,
                              Color* data, int startIndex, int elementCount) const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("TextureCube");
        if (!IsValidCubeMapFace(face))
            throw std::out_of_range("TextureCube::GetData: face is not a valid CubeMapFace value");
        if (!data)
            throw std::invalid_argument("TextureCube::GetData: data must not be null");
        if (elementCount <= 0)
            throw std::out_of_range("TextureCube::GetData: elementCount must be > 0");
        if (startIndex < 0)
            throw std::out_of_range("TextureCube::GetData: startIndex must be >= 0");
        if (level < 0)
            throw std::out_of_range("TextureCube::GetData: level must be >= 0");

        const int levelSize = mipDim(size_, level);
        int x = 0, y = 0, w = levelSize, h = levelSize;
        if (rect) { x = rect->X; y = rect->Y; w = rect->Width; h = rect->Height; }
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > levelSize || y + h > levelSize)
            throw std::out_of_range("TextureCube::GetData: rectangle out of texture bounds");
        if (elementCount < w * h)
            throw std::out_of_range("TextureCube::GetData: elementCount is less than the number of pixels in the requested region");
        Texture::ValidateGetDataFormat(format_, 4);

        // REMED-GFX-130 (REMED-GFX-127's contract, applied to ITextureCubeRenderer). `rgba` is
        // scratch memory THIS layer owns and zero-initializes, so converting it unconditionally is
        // not "leaving the caller's buffer untouched" when the renderer has no readback -- it
        // fabricates a complete transparent-black cube face. Conversion happens only when the
        // renderer reports it wrote the whole region; otherwise the caller's `data` is left
        // byte-for-byte as it was and the missing capability is raised instead of being answered
        // with invented content. A renderer that was never created at all (ASCII,
        // Canvas, DIRECTX3 keep IGraphicsRenderer::CreateTextureCube's nullptr default) is the same
        // answer reached one step earlier: no storage exists, so there is nothing to return.
        if (!renderer_)
        {
            throw System::NotSupportedException(
                "TextureCube::GetData: this graphics renderer creates no cube-map texture resource, "
                "so a cube face's content cannot be read back");
        }

        // Sized to the REQUESTED REGION, not to elementCount: the renderer fills exactly w*h texels,
        // so a larger elementCount would otherwise hand the caller this buffer's untouched tail as
        // if it were content.
        std::vector<uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
        if (!renderer_->GetData(static_cast<int>(face), level, x, y, w, h,
                               rgba.data(), static_cast<int>(rgba.size())))
        {
            throw System::NotSupportedException(
                "TextureCube::GetData: this graphics renderer cannot read a cube face back to the "
                "CPU at the requested mip level");
        }
        rgbaToColors(rgba, data, startIndex, w * h);
    }

    namespace
    {
        // DDS constants — mirrors FNA's Texture.ParseDDS magic numbers (Task 663).
        constexpr uint32_t kDdsMagic        = 0x20534444;
        constexpr uint32_t kDdsHeaderSize   = 124;
        constexpr uint32_t kDdsPixfmtSize   = 32;
        constexpr uint32_t kDdsdHeight      = 0x2;
        constexpr uint32_t kDdsdWidth       = 0x4;
        constexpr uint32_t kDdscapsMipmap   = 0x400000;
        constexpr uint32_t kDdscapsTexture  = 0x1000;
        constexpr uint32_t kDdscaps2Cubemap = 0x200;
        constexpr uint32_t kDdpfFourCC      = 0x4;
        constexpr uint32_t kFourCcDxt1      = 0x31545844;
        constexpr uint32_t kFourCcDxt3      = 0x33545844;
        constexpr uint32_t kFourCcDxt5      = 0x35545844;

        uint32_t ReadU32LE(const uint8_t* p)
        {
            return static_cast<uint32_t>(p[0])
                 | (static_cast<uint32_t>(p[1]) << 8)
                 | (static_cast<uint32_t>(p[2]) << 16)
                 | (static_cast<uint32_t>(p[3]) << 24);
        }

        // Compressed block size in bytes for one mip level — mirrors FNA's
        // Texture.CalculateDDSLevelSize (Dxt1/3/5-only subset; CNA doesn't support the
        // uncompressed/HDR DDS variants FNA also handles, matching Texture2D::FromStream's own
        // established DXT1/3/5-only scope for this exact class of problem).
        int CalculateDDSLevelSize(int width, int height, uint32_t fourCC)
        {
            const int blockSize = (fourCC == kFourCcDxt1) ? 8 : 16;
            width  = std::max(width, 1);
            height = std::max(height, 1);
            return ((width + 3) / 4) * ((height + 3) / 4) * blockSize;
        }
    }

    TextureCube TextureCube::DDSFromStreamEXT(GraphicsDevice& device, System::IO::Stream& stream)
    {
        using System::IO::intcs;
        using System::IO::bytecs;

        const intcs len = stream.getLengthProperty();
        if (len <= 0)
            throw std::runtime_error("TextureCube::DDSFromStreamEXT: stream is empty or length unknown");

        std::vector<bytecs> buf(static_cast<std::size_t>(len));
        stream.Read(buf.data(), 0, len);
        const auto* raw = reinterpret_cast<const uint8_t*>(buf.data());
        const auto rawLen = static_cast<std::size_t>(len);

        // plans/plan_cnb.md CNBF-113: the parsing and DXT decompression that used to live here are
        // now CNA::Internal::Graphics::DecodeDdsCube, a pure-CPU component with no GraphicsDevice
        // in sight. Nothing about the format handling changed -- the code moved so that a headless
        // content compiler could reach it too, which is what let CNB finally produce a cube map.
        // The prefix keeps every diagnostic naming this API rather than the helper.
        const CNA::Internal::Graphics::DecodedDdsCube decoded =
            CNA::Internal::Graphics::DecodeDdsCube(raw, rawLen,
                                                    "TextureCube::DDSFromStreamEXT");

        // CNA deviation from FNA (documented, matches Texture2D::FromStream's own established
        // precedent for the identical DDS/DXT problem): every face/level is fully decompressed to
        // RGBA8 on the CPU via DxtUtil and uploaded as SurfaceFormat::Color, rather than uploading
        // the compressed blocks directly to a real compressed GPU format -- CNA doesn't implement
        // compressed GPU texture formats end-to-end on any renderer (NEXT.md's documented
        // "SurfaceFormat support is Color-only for real GPU formats" limitation).
        TextureCube result(device, decoded.width, decoded.mipCount > 1, SurfaceFormat::Color);

        for (int face = 0; face < 6; ++face)
        {
            int levelSize = decoded.width;
            for (int level = 0; level < decoded.mipCount; ++level)
            {
                const std::vector<uint8_t>& rgba =
                    decoded.faces[static_cast<std::size_t>(face)][static_cast<std::size_t>(level)];
                std::vector<Color> colors(
                    static_cast<std::size_t>(levelSize) * static_cast<std::size_t>(levelSize),
                    Color(0, 0, 0, 0));
                rgbaToColors(rgba, colors.data(), 0, static_cast<int>(colors.size()));
                result.SetData(static_cast<CubeMapFace>(face), level, nullptr,
                               colors.data(), 0, static_cast<int>(colors.size()));
                levelSize = std::max(1, levelSize / 2);
            }
        }
        return result;
    }
}
