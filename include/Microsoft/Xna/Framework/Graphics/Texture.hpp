// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Abstract base class for all texture types. */
    class Texture : public GraphicsResource
    {
    public:
        /** @brief Returns the surface format of the texture data. */
        [[nodiscard]] SurfaceFormat getFormatProperty() const;

        /** @brief Returns the number of mipmap levels in this texture. */
        [[nodiscard]] int getLevelCountProperty() const;

    protected:
        explicit Texture(GraphicsDevice* device = nullptr);

        SurfaceFormat format_ = SurfaceFormat::Color;
        int levelCount_ = 1;
    };
}
