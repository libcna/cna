#pragma once

#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Abstract base class for all texture types.
    class Texture : public GraphicsResource
    {
    public:
        /// Gets the format of the texture data.
        [[nodiscard]] SurfaceFormat getFormatProperty() const;

        /// Gets the number of mipmap levels in this texture.
        [[nodiscard]] int getLevelCountProperty() const;

    protected:
        explicit Texture(GraphicsDevice* device = nullptr);

        SurfaceFormat format_ = SurfaceFormat::Color;
        int levelCount_ = 1;
    };
}
