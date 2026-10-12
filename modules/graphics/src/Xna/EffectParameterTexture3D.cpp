// SPDX-License-Identifier: MS-PL
// EffectParameter::GetValueTexture3D().
//
// Link boundary: its dynamic_cast names Texture3D, so it is apart from EffectParameter.cpp, which
// every program with a SpriteBatch links (docs/link-size-architecture.md).
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    Texture3D* EffectParameter::GetValueTexture3D() const
    {
        if (!compiledStorage_) return texture3DData_;
        RequireTextureGetterParameter(EffectParameterType::Texture3D);
        return dynamic_cast<Texture3D*>(compiledStorage_->texture);
    }
}
