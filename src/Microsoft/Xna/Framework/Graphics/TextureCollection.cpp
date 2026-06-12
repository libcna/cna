// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Graphics
{
    TextureCollection::TextureCollection()
        : textures_(MaxTextures, nullptr)
    {
    }

    Texture* TextureCollection::operator[](int index) const
    {
        if (index < 0 || index >= MaxTextures)
        {
            throw std::out_of_range("Texture index out of range.");
        }
        return textures_[static_cast<std::size_t>(index)];
    }

    void TextureCollection::operator()(int index, Texture* texture)
    {
        if (index < 0 || index >= MaxTextures)
        {
            throw std::out_of_range("Texture index out of range.");
        }
        textures_[static_cast<std::size_t>(index)] = texture;
    }
}
