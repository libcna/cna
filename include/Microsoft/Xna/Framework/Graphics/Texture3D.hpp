// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Represents a 3D (volume) texture.
    class Texture3D : public GraphicsResource
    {
    public:
        Texture3D(GraphicsDevice& device, int width, int height, int depth, bool mipMap, SurfaceFormat format);

        [[nodiscard]] int getWidthProperty() const;
        [[nodiscard]] int getHeightProperty() const;
        [[nodiscard]] int getDepthProperty() const;
        [[nodiscard]] SurfaceFormat getFormatProperty() const;
        [[nodiscard]] int getLevelCountProperty() const;

    private:
        int width_;
        int height_;
        int depth_;
        SurfaceFormat format_;
        int levelCount_;
    };
}
