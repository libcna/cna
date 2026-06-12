// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Texture;

    /// Binds a render target texture for rendering.
    class RenderTargetBinding
    {
    public:
        RenderTargetBinding();
        explicit RenderTargetBinding(Texture* renderTarget, int arraySlice = 0);
        RenderTargetBinding(Texture* renderTarget, CubeMapFace cubeMapFace);

        [[nodiscard]] Texture* getRenderTargetProperty() const;
        [[nodiscard]] int getArraySliceProperty() const;
        [[nodiscard]] CubeMapFace getCubeMapFaceProperty() const;

    private:
        Texture* renderTarget_ = nullptr;
        int arraySlice_ = 0;
        CubeMapFace cubeMapFace_ = CubeMapFace::PositiveX;
    };
}
