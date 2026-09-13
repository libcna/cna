// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Graphics
{
    TextureCollection::TextureCollection()
        : TextureCollection(nullptr, false)
    {
    }

    TextureCollection::TextureCollection(GraphicsDevice* graphicsDevice, bool vertexStage)
        : textures_(MaxTextures, nullptr)
        , graphicsDevice_(graphicsDevice)
        , vertexStage_(vertexStage)
    {
    }

    int TextureCollection::ActiveTextureCount() const
    {
        if (graphicsDevice_ == nullptr || !vertexStage_)
            return MaxTextures;
        return graphicsDevice_->getGraphicsProfileProperty() == GraphicsProfile::HiDef ? 4 : 0;
    }

    Texture* TextureCollection::operator[](int index) const
    {
        if (graphicsDevice_ != nullptr && graphicsDevice_->getIsDisposedProperty())
            throw System::ObjectDisposedException("GraphicsDevice");
        if (index < 0 || index >= ActiveTextureCount())
        {
            throw System::ArgumentOutOfRangeException("index");
        }
        return textures_[static_cast<std::size_t>(index)];
    }

    void TextureCollection::operator()(int index, Texture* texture)
    {
        if (graphicsDevice_ != nullptr && graphicsDevice_->getIsDisposedProperty())
            throw System::ObjectDisposedException("GraphicsDevice");
        if (texture != nullptr && texture->getIsDisposedProperty())
        {
            throw System::ObjectDisposedException(texture->getNameProperty());
        }
        if (texture != nullptr && graphicsDevice_ != nullptr)
        {
            for (const auto& binding : graphicsDevice_->GetRenderTargets())
            {
                if (binding.getRenderTargetProperty() == texture)
                {
                    throw System::InvalidOperationException(
                        "A texture that is currently bound as a render target cannot be "
                    "bound for sampling.");
                }
            }
            if (vertexStage_)
            {
                const SurfaceFormat format = texture->getFormatProperty();
                if (format != SurfaceFormat::Single
                    && format != SurfaceFormat::Vector2
                    && format != SurfaceFormat::Vector4
                    && format != SurfaceFormat::HalfSingle
                    && format != SurfaceFormat::HalfVector2
                    && format != SurfaceFormat::HalfVector4
                    && format != SurfaceFormat::HdrBlendable)
                {
                    throw System::NotSupportedException(
                        "The texture format is not supported for vertex texture sampling.");
                }
            }
        }
        if (index < 0 || index >= ActiveTextureCount())
        {
            throw System::ArgumentOutOfRangeException("index");
        }
        if (texture != nullptr && graphicsDevice_ != nullptr
            && texture->getGraphicsDeviceProperty() != nullptr
            && texture->getGraphicsDeviceProperty() != graphicsDevice_)
        {
            throw System::InvalidOperationException(
                "The texture belongs to a different GraphicsDevice.");
        }
        textures_[static_cast<std::size_t>(index)] = texture;
    }

    void TextureCollection::RemoveDisposedTexture(const Texture* tex)
    {
        for (auto& slot : textures_)
        {
            if (slot == tex)
                slot = nullptr;
        }
    }
}
