// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Graphics/SpriteTextureBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"

#include <utility>

namespace CNA::Internal::Graphics
{
    SpriteTextureBinding::SpriteTextureBinding(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice* device,
        std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D*> identity)
        : device_(device), identity_(std::move(identity))
    {
    }

    void SpriteTextureBinding::Apply() const
    {
        if (device_ != nullptr && identity_)
            device_->getTexturesProperty()(0, *identity_);
    }
}
