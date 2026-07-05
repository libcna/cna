// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

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

    TextureCube::~TextureCube() = default;

    TextureCube::TextureCube(GraphicsDevice& device, int size, bool mipMap, SurfaceFormat format)
        : GraphicsResource(&device)
        , size_(size)
        , format_(format)
        , levelCount_(mipMap ? CalculateMipLevels(size, size) : 1)
        , backend_(nullptr)
    {
        Texture::ValidateFormat(format);
        backend_ = device.GetBackend().CreateTextureCube(size, mipMap, static_cast<int>(format));
    }

    TextureCube::TextureCube(GraphicsDevice& device, int size, SurfaceFormat format,
                             std::unique_ptr<CNA::Internal::Backends::ITextureCubeBackend> backend,
                             int levelCount)
        : GraphicsResource(&device)
        , size_(size)
        , format_(format)
        , levelCount_(levelCount)
        , backend_(std::move(backend))
    {
    }

    void TextureCube::Dispose(bool disposing)
    {
        backend_.reset();
        GraphicsResource::Dispose(disposing);
    }

    int TextureCube::getSizeProperty() const { return size_; }
    SurfaceFormat TextureCube::getFormatProperty() const { return format_; }
    int TextureCube::getLevelCountProperty() const { return levelCount_; }

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
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize)
            throw std::out_of_range("TextureCube::SetData: rectangle out of texture bounds");

        const auto rgba = colorsToRgba(data, startIndex, elementCount);
        if (backend_)
            backend_->SetData(static_cast<int>(face), level, x, y, w, h,
                              rgba.data(), static_cast<int>(rgba.size()));
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
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize)
            throw std::out_of_range("TextureCube::GetData: rectangle out of texture bounds");
        Texture::ValidateGetDataFormat(format_, 4);

        if (!backend_) return;
        std::vector<uint8_t> rgba(static_cast<std::size_t>(elementCount) * 4);
        backend_->GetData(static_cast<int>(face), level, x, y, w, h,
                          rgba.data(), static_cast<int>(rgba.size()));
        rgbaToColors(rgba, data, startIndex, elementCount);
    }

    TextureCube TextureCube::DDSFromStreamEXT(GraphicsDevice& device, System::IO::Stream& stream)
    {
        // NOTE (Task 272 audit): this is a stub, not a real implementation. It ignores `stream`
        // entirely and always returns a blank 1x1 Color cube map, regardless of what DDS data (if
        // any) was provided. A real implementation needs: DDS header parsing (magic, size, mip
        // levels, isCube flag — Texture.ParseDDS in FNA), per-face/per-level DXT decode (reusing
        // DxtUtil, already used by Texture2D::FromStream's TryDecodeDds), and 6 x levelCount
        // SetData calls. See AUDIT.md, "TextureCube detailed audit", for the full FNA reference
        // walkthrough. Left unimplemented here — this is a substantial feature, not a guard fix.
        return TextureCube(device, 1, false, SurfaceFormat::Color);
    }
}
