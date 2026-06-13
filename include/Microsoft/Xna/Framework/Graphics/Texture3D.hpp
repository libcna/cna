// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Represents a 3D (volume) texture. */
    class Texture3D : public GraphicsResource
    {
    public:
        /**
         * @brief Creates a 3D texture with the given dimensions and format.
         * @param device  The graphics device to create the texture on.
         * @param width   Width in texels.
         * @param height  Height in texels.
         * @param depth   Depth (number of slices) in texels.
         * @param mipMap  True to generate a full mipmap chain.
         * @param format  The desired surface format.
         */
        Texture3D(GraphicsDevice& device, int width, int height, int depth, bool mipMap, SurfaceFormat format);

        /** @brief Returns the texture width in texels. */
        [[nodiscard]] int getWidthProperty() const;
        /** @brief Returns the texture height in texels. */
        [[nodiscard]] int getHeightProperty() const;
        /** @brief Returns the texture depth (number of slices) in texels. */
        [[nodiscard]] int getDepthProperty() const;
        /** @brief Returns the surface format of the texture data. */
        [[nodiscard]] SurfaceFormat getFormatProperty() const;
        /** @brief Returns the number of mipmap levels in this texture. */
        [[nodiscard]] int getLevelCountProperty() const;

    private:
        int width_;
        int height_;
        int depth_;
        SurfaceFormat format_;
        int levelCount_;
    };
}
