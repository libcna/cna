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
}
