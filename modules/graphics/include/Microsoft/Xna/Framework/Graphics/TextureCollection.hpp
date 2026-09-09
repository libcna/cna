// SPDX-License-Identifier: MS-PL
#pragma once

#include <vector>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class Texture;

    /** @brief A collection of Texture objects, one per texture sampler slot. */
    class TextureCollection
    {
    public:
        /** @brief Maximum number of texture sampler slots. */
        CNAEXT static constexpr int MaxTextures = 16;

        /** @brief Constructs an empty TextureCollection with MaxTextures null slots. */
        CNAEXT TextureCollection();

        /**
         * @brief Returns the texture bound at the given sampler index.
         * @param index Sampler slot index within the owning profile's active range.
         * @return Pointer to the bound Texture, or nullptr if unbound.
         * @throws System::ObjectDisposedException if the owning GraphicsDevice is disposed.
         */
        [[nodiscard]] Texture* operator[](int index) const;
        /**
         * @brief Binds a texture to the given sampler slot.
         * @param index   Sampler slot index within the owning profile's active range.
         * @param texture Pointer to the texture to bind, or nullptr to unbind.
         * @throws System::ObjectDisposedException if the owning GraphicsDevice or texture is
         *         disposed.
         * @throws System::InvalidOperationException if @p texture belongs to another
         *         GraphicsDevice or is currently bound as a render target on the owning device.
         * @throws System::NotSupportedException if a vertex-stage collection receives a texture
         *         format that XNA does not permit for vertex texture fetch.
         */
        void operator()(int index, Texture* texture);

        /**
         * @brief Clears any slot that currently holds the given texture pointer.
         *
         * Called by Texture::Dispose so that disposed textures are automatically
         * unbound from all sampler slots — matching FNA's RemoveDisposedTexture behaviour.
         *
         * @param tex The texture that is being disposed.
         */
        CNAEXT void RemoveDisposedTexture(const Texture* tex);

    private:
        TextureCollection(GraphicsDevice* graphicsDevice, bool vertexStage);

        [[nodiscard]] int ActiveTextureCount() const;

        std::vector<Texture*> textures_;
        GraphicsDevice* graphicsDevice_ = nullptr;
        bool vertexStage_ = false;

        friend class GraphicsDevice;
    };
}
