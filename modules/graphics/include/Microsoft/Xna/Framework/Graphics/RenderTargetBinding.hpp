// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Texture;
    class RenderTarget2D;
    class RenderTargetCube;

    /** @brief Associates a render target texture with a rendering output slot. */
    class RenderTargetBinding
    {
    public:
        /** @brief Constructs a null RenderTargetBinding. */
        RenderTargetBinding();
        /**
         * @brief Constructs a binding for a 2D render target and supports XNA's implicit conversion.
         * @param renderTarget The render target texture to bind.
         * @throws System::ArgumentNullException if @p renderTarget is null.
         */
        RenderTargetBinding(RenderTarget2D* renderTarget);
        /**
         * @brief Constructs a RenderTargetBinding for a specific cube map face.
         * @param renderTarget The cube render target texture to bind.
         * @param cubeMapFace  The cube face to render into.
         * @throws System::ArgumentNullException if @p renderTarget is null.
         */
        RenderTargetBinding(RenderTargetCube* renderTarget, CubeMapFace cubeMapFace);

        /**
         * @brief Constructs a binding from a generic texture and optional array slice.
         * @param renderTarget The texture to bind.
         * @param arraySlice Texture array slice index.
         * @throws System::ArgumentNullException if @p renderTarget is null.
         */
        CNAEXT explicit RenderTargetBinding(Texture* renderTarget, int arraySlice = 0);
        /**
         * @brief Constructs a cube-face binding from a generic texture.
         * @param renderTarget The texture to bind.
         * @param cubeMapFace The cube face to render into.
         * @throws System::ArgumentNullException if @p renderTarget is null.
         */
        CNAEXT RenderTargetBinding(Texture* renderTarget, CubeMapFace cubeMapFace);

        /** @brief Returns the bound render target texture. */
        [[nodiscard]] Texture* getRenderTargetProperty() const;
        /** @brief Returns the texture array slice index. */
        CNAEXT [[nodiscard]] int getArraySliceProperty() const;
        /** @brief Returns the cube map face (for cube render targets). */
        [[nodiscard]] CubeMapFace getCubeMapFaceProperty() const;

    private:
        void ReplaceRenderTargetAfterMove(const Texture* source, Texture* destination) noexcept;

        Texture* renderTarget_ = nullptr;
        int arraySlice_ = 0;
        CubeMapFace cubeMapFace_ = CubeMapFace::PositiveX;

        friend class GraphicsDevice;
    };
}
