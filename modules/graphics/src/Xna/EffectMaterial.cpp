// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/EffectMaterial.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    EffectMaterial::EffectMaterial(Effect& cloneSource)
        : Effect(cloneSource)
    {
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
