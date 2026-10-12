// SPDX-License-Identifier: MS-PL
// EffectParameter::GetValueTextureCube().
//
// Link boundary: its dynamic_cast names TextureCube, so it is apart from EffectParameter.cpp, which
// every program with a SpriteBatch links (docs/link-size-architecture.md).
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    TextureCube* EffectParameter::GetValueTextureCube() const
    {
        if (!compiledStorage_) return textureCubeData_;
        RequireTextureGetterParameter(EffectParameterType::TextureCube);
        return dynamic_cast<TextureCube*>(compiledStorage_->texture);
    }
}
