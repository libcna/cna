// SPDX-License-Identifier: MS-PL
#pragma once

#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    class Texture;

    /** @brief A collection of Texture objects, one per texture sampler slot. */
    class TextureCollection
    {
    public:
        /** @brief Maximum number of texture sampler slots. */
        static constexpr int MaxTextures = 16;

        /** @brief Constructs an empty TextureCollection with MaxTextures null slots. */
        TextureCollection();

        /**
         * @brief Returns the texture bound at the given sampler index.
         * @param index Sampler slot index (0 to MaxTextures-1).
         * @return Pointer to the bound Texture, or nullptr if unbound.
         */
        [[nodiscard]] Texture* operator[](int index) const;
        /**
         * @brief Binds a texture to the given sampler slot.
         * @param index   Sampler slot index (0 to MaxTextures-1).
         * @param texture Pointer to the texture to bind, or nullptr to unbind.
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
        void RemoveDisposedTexture(const Texture* tex);

    private:
        std::vector<Texture*> textures_;
    };
}
