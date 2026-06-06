#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    TextureCube::TextureCube(GraphicsDevice& device, int size, bool mipMap, SurfaceFormat format)
        : GraphicsResource(&device)
        , size_(size)
        , format_(format)
        , levelCount_(mipMap ? 1 : 1)
    {
    }

    int TextureCube::getSizeProperty() const { return size_; }
    SurfaceFormat TextureCube::getFormatProperty() const { return format_; }
    int TextureCube::getLevelCountProperty() const { return levelCount_; }
}
