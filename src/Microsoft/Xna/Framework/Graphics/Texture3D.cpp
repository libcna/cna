// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <cstdint>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    Texture3D::~Texture3D() = default;

    Texture3D::Texture3D(GraphicsDevice& device, int width, int height, int depth, bool mipMap, SurfaceFormat format)
        : GraphicsResource(&device)
        , width_(width)
        , height_(height)
        , depth_(depth)
        , format_(format)
        , levelCount_(mipMap ? 1 : 1)
        , backend_(device.GetBackend().CreateTexture3D(width, height, depth, mipMap, static_cast<int>(format)))
    {
    }

    int Texture3D::getWidthProperty() const { return width_; }
    int Texture3D::getHeightProperty() const { return height_; }
    int Texture3D::getDepthProperty() const { return depth_; }
    SurfaceFormat Texture3D::getFormatProperty() const { return format_; }
    int Texture3D::getLevelCountProperty() const { return levelCount_; }

    const std::string& Texture3D::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.Texture3D";
        return name;
    }

    // Color has a vtable pointer (sizeof(Color) == 24), so we must never pass
    // Color* directly to GL. Always unpack to plain uint8_t RGBA first.

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

    void Texture3D::SetData(const Color* data, int elementCount)
    {
        const auto rgba = colorsToRgba(data, 0, elementCount);
        SetDataPointerEXT(0, 0, 0, width_, height_, 0, depth_,
                          rgba.data(), static_cast<int>(rgba.size()));
    }

    void Texture3D::SetData(const Color* data, int startIndex, int elementCount)
    {
        const auto rgba = colorsToRgba(data, startIndex, elementCount);
        SetDataPointerEXT(0, 0, 0, width_, height_, 0, depth_,
                          rgba.data(), static_cast<int>(rgba.size()));
    }

    void Texture3D::SetData(int level, int left, int top, int right, int bottom, int front, int back,
                            const Color* data, int startIndex, int elementCount)
    {
        const auto rgba = colorsToRgba(data, startIndex, elementCount);
        SetDataPointerEXT(level, left, top, right, bottom, front, back,
                          rgba.data(), static_cast<int>(rgba.size()));
    }

    void Texture3D::SetDataPointerEXT(int level, int left, int top, int right, int bottom, int front, int back,
                                      const void* data, int dataLength)
    {
        if (backend_)
            backend_->SetData(level, left, top, front,
                              right - left, bottom - top, back - front,
                              data, dataLength);
    }

    static void rgbaToColors(const std::vector<uint8_t>& rgba, Color* data, int startIndex, int count)
    {
        for (int i = 0; i < count; ++i)
            data[startIndex + i] = Color(rgba[i * 4 + 0], rgba[i * 4 + 1],
                                         rgba[i * 4 + 2], rgba[i * 4 + 3]);
    }

    void Texture3D::GetData(Color* data, int elementCount) const
    {
        if (!backend_) return;
        std::vector<uint8_t> rgba(static_cast<std::size_t>(elementCount) * 4);
        backend_->GetData(0, 0, 0, 0, width_, height_, depth_,
                          rgba.data(), static_cast<int>(rgba.size()));
        rgbaToColors(rgba, data, 0, elementCount);
    }

    void Texture3D::GetData(Color* data, int startIndex, int elementCount) const
    {
        if (!backend_) return;
        std::vector<uint8_t> rgba(static_cast<std::size_t>(elementCount) * 4);
        backend_->GetData(0, 0, 0, 0, width_, height_, depth_,
                          rgba.data(), static_cast<int>(rgba.size()));
        rgbaToColors(rgba, data, startIndex, elementCount);
    }

    void Texture3D::GetData(int level, int left, int top, int right, int bottom, int front, int back,
                            Color* data, int startIndex, int elementCount) const
    {
        if (!backend_) return;
        std::vector<uint8_t> rgba(static_cast<std::size_t>(elementCount) * 4);
        backend_->GetData(level, left, top, front,
                          right - left, bottom - top, back - front,
                          rgba.data(), static_cast<int>(rgba.size()));
        rgbaToColors(rgba, data, startIndex, elementCount);
    }
}
