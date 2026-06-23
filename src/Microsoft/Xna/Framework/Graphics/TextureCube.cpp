// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <cstdint>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    TextureCube::~TextureCube() = default;

    TextureCube::TextureCube(GraphicsDevice& device, int size, bool mipMap, SurfaceFormat format)
        : GraphicsResource(&device)
        , size_(size)
        , format_(format)
        , levelCount_(1)
        , backend_(nullptr)
    {
        Texture::ValidateFormat(format);
        backend_ = device.GetBackend().CreateTextureCube(size, mipMap, static_cast<int>(format));
    }

    TextureCube::TextureCube(GraphicsDevice& device, int size, SurfaceFormat format,
                             std::unique_ptr<CNA::Internal::Backends::ITextureCubeBackend> backend)
        : GraphicsResource(&device)
        , size_(size)
        , format_(format)
        , levelCount_(1)
        , backend_(std::move(backend))
    {
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

    void TextureCube::SetData(CubeMapFace face, const Color* data, int elementCount)
    {
        if (!backend_) return;
        std::vector<uint8_t> rgba(static_cast<std::size_t>(elementCount) * 4);
        for (int i = 0; i < elementCount; ++i)
        {
            rgba[i * 4 + 0] = data[i].getRProperty();
            rgba[i * 4 + 1] = data[i].getGProperty();
            rgba[i * 4 + 2] = data[i].getBProperty();
            rgba[i * 4 + 3] = data[i].getAProperty();
        }
        backend_->SetData(static_cast<int>(face), 0, 0, 0, size_, size_,
                          rgba.data(), static_cast<int>(rgba.size()));
    }

    void TextureCube::SetData(CubeMapFace face, int level, const Microsoft::Xna::Framework::Rectangle* rect,
                              const Color* data, int startIndex, int elementCount)
    {
        if (!backend_) return;
        int x = 0, y = 0, w = size_, h = size_;
        if (rect) { x = rect->X; y = rect->Y; w = rect->Width; h = rect->Height; }
        std::vector<uint8_t> rgba(static_cast<std::size_t>(elementCount) * 4);
        for (int i = 0; i < elementCount; ++i)
        {
            rgba[i * 4 + 0] = data[startIndex + i].getRProperty();
            rgba[i * 4 + 1] = data[startIndex + i].getGProperty();
            rgba[i * 4 + 2] = data[startIndex + i].getBProperty();
            rgba[i * 4 + 3] = data[startIndex + i].getAProperty();
        }
        backend_->SetData(static_cast<int>(face), level, x, y, w, h,
                          rgba.data(), static_cast<int>(rgba.size()));
    }

    void TextureCube::GetData(CubeMapFace face, Color* data, int elementCount) const
    {
        if (!backend_) return;
        std::vector<uint8_t> rgba(static_cast<std::size_t>(elementCount) * 4);
        backend_->GetData(static_cast<int>(face), 0, 0, 0, size_, size_,
                          rgba.data(), static_cast<int>(rgba.size()));
        for (int i = 0; i < elementCount; ++i)
            data[i] = Color(rgba[i * 4 + 0], rgba[i * 4 + 1], rgba[i * 4 + 2], rgba[i * 4 + 3]);
    }

    void TextureCube::GetData(CubeMapFace face, int level, const Microsoft::Xna::Framework::Rectangle* rect,
                              Color* data, int startIndex, int elementCount) const
    {
        if (!backend_) return;
        int x = 0, y = 0, w = size_, h = size_;
        if (rect) { x = rect->X; y = rect->Y; w = rect->Width; h = rect->Height; }
        std::vector<uint8_t> rgba(static_cast<std::size_t>(elementCount) * 4);
        backend_->GetData(static_cast<int>(face), level, x, y, w, h,
                          rgba.data(), static_cast<int>(rgba.size()));
        for (int i = 0; i < elementCount; ++i)
            data[startIndex + i] = Color(rgba[i * 4 + 0], rgba[i * 4 + 1],
                                         rgba[i * 4 + 2], rgba[i * 4 + 3]);
    }

    TextureCube TextureCube::DDSFromStreamEXT(GraphicsDevice& device, System::IO::Stream& stream)
    {
        return TextureCube(device, 1, false, SurfaceFormat::Color);
    }
}
