// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/EffectMaterial.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    EffectMaterial::EffectMaterial(Effect& cloneSource)
        : Effect(cloneSource)
    {
    }

    void EffectMaterial::RetainParameterTextureEXT(std::shared_ptr<Texture> texture)
    {
        if (texture != nullptr)
        {
            retainedParameterTextures_.push_back(std::move(texture));
        }
    }

    std::size_t EffectMaterial::GetRetainedParameterTextureCountEXT() const
    {
        return retainedParameterTextures_.size();
    }

    const std::string& EffectMaterial::GetTypeName() const
    {
        static const std::string name =
            "Microsoft.Xna.Framework.Graphics.EffectMaterial";
        return name;
    }

    Effect* EffectMaterial::Clone()
    {
        return new EffectMaterial(static_cast<Effect&>(*this));
    }

    void EffectMaterial::OnApply()
    {
    }

} // namespace Microsoft::Xna::Framework::Graphics
