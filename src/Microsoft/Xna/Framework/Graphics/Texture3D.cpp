// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    Texture3D::Texture3D(GraphicsDevice& device, int width, int height, int depth, bool mipMap, SurfaceFormat format)
        : GraphicsResource(&device)
        , width_(width)
        , height_(height)
        , depth_(depth)
        , format_(format)
        , levelCount_(mipMap ? 1 : 1)
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

    void Texture3D::SetData(const Color* data, int elementCount) {}
    void Texture3D::SetData(const Color* data, int startIndex, int elementCount) {}
    void Texture3D::SetData(int level, int left, int top, int right, int bottom, int front, int back,
                            const Color* data, int startIndex, int elementCount) {}
    void Texture3D::SetDataPointerEXT(int level, int left, int top, int right, int bottom, int front, int back,
                                      const void* data, int dataLength) {}

    void Texture3D::GetData(Color* data, int elementCount) const {}
    void Texture3D::GetData(Color* data, int startIndex, int elementCount) const {}
    void Texture3D::GetData(int level, int left, int top, int right, int bottom, int front, int back,
                            Color* data, int startIndex, int elementCount) const {}
}
