// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    Texture::Texture(GraphicsDevice* device)
        : GraphicsResource(device)
    {
    }

    void Texture::ValidateFormat(SurfaceFormat fmt)
    {
        if (fmt == SurfaceFormat::Color)
            return;
        throw std::runtime_error(
            "Texture: SurfaceFormat " + std::to_string(static_cast<int>(fmt)) +
            " is not implemented; only SurfaceFormat::Color is currently supported");
    }

    SurfaceFormat Texture::getFormatProperty() const
    {
        return format_;
    }

    int Texture::getLevelCountProperty() const
    {
        return levelCount_;
    }
}
