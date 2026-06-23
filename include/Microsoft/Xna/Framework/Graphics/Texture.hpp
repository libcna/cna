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

    public:
        /**
         * @brief Throws std::runtime_error if @p fmt is not yet implemented.
         *
         * Only SurfaceFormat::Color is currently supported by all backends.
         * Texture2D, Texture3D, and TextureCube public constructors call this
         * so callers get a clear error instead of silent RGBA8 misinterpretation.
         *
         * @param fmt The SurfaceFormat to validate.
         */
        static void ValidateFormat(SurfaceFormat fmt);

    protected:
        explicit Texture(GraphicsDevice* device = nullptr);

        SurfaceFormat format_ = SurfaceFormat::Color;
        int levelCount_ = 1;
    };
}
