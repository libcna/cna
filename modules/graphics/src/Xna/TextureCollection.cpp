// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "../Internal/ProfileValidation.hpp"

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "CNA/Diagnostics/Instrumentation.hpp"
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
        // Refresh set_Item IL_0025-0039 checks Texture::GetComPtr(): disposal and the release by
        // its device's disposal are refused alike.
        if (texture != nullptr && texture->IsNativeObjectReleasedInternal())
        {
            // Refresh Framework Helpers::CheckDisposed IL_000d-001d uses runtime type Name.
            throw System::ObjectDisposedException(texture->GetTypeName().substr(texture->GetTypeName().find_last_of('.') + 1));
        }
        if (texture != nullptr && graphicsDevice_ != nullptr)
        {
            for (const auto& binding : graphicsDevice_->GetRenderTargets())
            {
                if (binding.getRenderTargetProperty() == texture)
                {
                    throw System::InvalidOperationException(
                        // Refresh TextureCollection::set_Item IL_0046-0050, MustResolveRenderTarget.
                        "The render target must not be set on the device when it is used as a texture.");
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
                        // XNA TextureCollection.set_Item IL_0051-0081 formats the
                        // profile and SurfaceFormat at binding, before the slot is stored.
                        CNA::Internal::Graphics::ProfileDiagnostic(
                            graphicsDevice_->getGraphicsProfileProperty(),
                            "does not support vertex texture format " +
                            CNA::Internal::Graphics::ProfileFormatName(format) + "."));
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
        Texture*& slot = textures_[static_cast<std::size_t>(index)];
        if (slot != texture)
            CNA_DIAGNOSTICS_FRAME_COUNTER_ADD("Graphics/TextureBindingChanges", 1);
        slot = texture;
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
