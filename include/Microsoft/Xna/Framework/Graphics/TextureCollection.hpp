#pragma once

#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    class Texture;

    /// A collection of Texture objects, one per texture sampler slot.
    class TextureCollection
    {
    public:
        static constexpr int MaxTextures = 16;

        TextureCollection();

        [[nodiscard]] Texture* operator[](int index) const;
        void operator()(int index, Texture* texture);

    private:
        std::vector<Texture*> textures_;
    };
}
