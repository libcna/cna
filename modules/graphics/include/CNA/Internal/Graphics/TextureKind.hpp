// SPDX-License-Identifier: MS-PL
#pragma once
// Asking a texture whether it is a cube map or a volume without naming either class's type
// information: renderers' effect binding is linked into every program, and a dynamic_cast to
// TextureCube or Texture3D there linked both classes into every program, 2D games included
// (docs/link-size-architecture.md). These are null-safe, like the casts they replace.

#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"

namespace CNA::Internal::Graphics
{
    /** @brief @p texture as a TextureCube. @param texture Any texture, or null. @return It, or null. */
    [[nodiscard]] inline const Microsoft::Xna::Framework::Graphics::TextureCube* AsTextureCube(
        const Microsoft::Xna::Framework::Graphics::Texture* texture) noexcept
    {
        return texture != nullptr ? texture->AsTextureCubeEXT() : nullptr;
    }

    /** @brief @p texture as a Texture3D. @param texture Any texture, or null. @return It, or null. */
    [[nodiscard]] inline const Microsoft::Xna::Framework::Graphics::Texture3D* AsTexture3D(
        const Microsoft::Xna::Framework::Graphics::Texture* texture) noexcept
    {
        return texture != nullptr ? texture->AsTexture3DEXT() : nullptr;
    }
}
