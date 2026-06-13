// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Represents a cube map texture (six faces of equal size). */
    class TextureCube : public GraphicsResource
    {
    public:
        /**
         * @brief Creates a cube map texture with the given face size and format.
         * @param device  The graphics device to create the texture on.
         * @param size    Width and height of each cube face in texels.
         * @param mipMap  True to generate a full mipmap chain.
         * @param format  The desired surface format.
         */
        TextureCube(GraphicsDevice& device, int size, bool mipMap, SurfaceFormat format);

        /** @brief Returns the width and height of a cube map face in texels. */
        [[nodiscard]] int getSizeProperty() const;
        /** @brief Returns the surface format of the texture data. */
        [[nodiscard]] SurfaceFormat getFormatProperty() const;
        /** @brief Returns the number of mipmap levels in this texture. */
        [[nodiscard]] int getLevelCountProperty() const;

    private:
        int size_;
        SurfaceFormat format_;
        int levelCount_;
    };
}
