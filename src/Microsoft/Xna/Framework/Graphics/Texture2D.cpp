#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "CNA/Logger.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    using namespace CNA::Internal::Backends;

    Texture2D::Texture2D()
    {
    }

    Texture2D::Texture2D(const std::string& assetName, GraphicsDevice& graphicsDevice)
    {
        using namespace CNA::Internal::Graphics;
        ImageData data = ImageLoader::Load(assetName);
        backend_ = graphicsDevice.GetBackend().CreateTexture(data);
        if (backend_)
        {
            width = backend_->GetWidth();
            height = backend_->GetHeight();
        }
    }

    Texture2D::~Texture2D()
    {
    }

    Rectangle Texture2D::getBoundsProperty() const
    {
        return {0, 0, width, height};
    }

    SDL_Texture* Texture2D::GetNativeTextureInternal() const
    {
        return backend_ ? backend_->GetNativeTexture() : nullptr;
    }
}
