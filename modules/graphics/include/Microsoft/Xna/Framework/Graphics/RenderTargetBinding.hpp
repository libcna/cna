// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Texture;

    /** @brief Associates a render target texture with a rendering output slot. */
    class RenderTargetBinding
    {
    public:
        /** @brief Constructs a null RenderTargetBinding. */
        RenderTargetBinding();
        /**
         * @brief Constructs a RenderTargetBinding for a 2D render target.
         * @param renderTarget The render target texture to bind.
         * @param arraySlice   Texture array slice index (default 0).
         * @throws System::ArgumentNullException if @p renderTarget is null.
         */
        explicit RenderTargetBinding(Texture* renderTarget, int arraySlice = 0);
        /**
         * @brief Constructs a RenderTargetBinding for a specific cube map face.
         * @param renderTarget The cube render target texture to bind.
         * @param cubeMapFace  The cube face to render into.
         * @throws System::ArgumentNullException if @p renderTarget is null.
         */
        RenderTargetBinding(Texture* renderTarget, CubeMapFace cubeMapFace);

        /** @brief Returns the bound render target texture. */
        [[nodiscard]] Texture* getRenderTargetProperty() const;
        /** @brief Returns the texture array slice index. */
        [[nodiscard]] int getArraySliceProperty() const;
        /** @brief Returns the cube map face (for cube render targets). */
        [[nodiscard]] CubeMapFace getCubeMapFaceProperty() const;

    private:
        Texture* renderTarget_ = nullptr;
        int arraySlice_ = 0;
        CubeMapFace cubeMapFace_ = CubeMapFace::PositiveX;
    };
}
