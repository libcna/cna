// SPDX-License-Identifier: MS-PL
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

    const std::string& TextureCube::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.TextureCube";
        return name;
    }

    void TextureCube::SetData(CubeMapFace face, const Color* data, int elementCount) {}
    void TextureCube::SetData(CubeMapFace face, int level, const Microsoft::Xna::Framework::Rectangle* rect,
                              const Color* data, int startIndex, int elementCount) {}

    void TextureCube::GetData(CubeMapFace face, Color* data, int elementCount) const {}
    void TextureCube::GetData(CubeMapFace face, int level, const Microsoft::Xna::Framework::Rectangle* rect,
                              Color* data, int startIndex, int elementCount) const {}

    TextureCube TextureCube::DDSFromStreamEXT(GraphicsDevice& device, System::IO::Stream& stream)
    {
        return TextureCube(device, 1, false, SurfaceFormat::Color);
    }
}
