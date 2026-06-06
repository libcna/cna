#pragma once

#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Represents a cubemap texture.
    class TextureCube : public GraphicsResource
    {
    public:
        TextureCube(GraphicsDevice& device, int size, bool mipMap, SurfaceFormat format);

        [[nodiscard]] int getSizeProperty() const;
        [[nodiscard]] SurfaceFormat getFormatProperty() const;
        [[nodiscard]] int getLevelCountProperty() const;

    private:
        int size_;
        SurfaceFormat format_;
        int levelCount_;
    };
}
