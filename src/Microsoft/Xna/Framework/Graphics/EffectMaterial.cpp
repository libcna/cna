// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/EffectMaterial.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    EffectMaterial::EffectMaterial(Effect& cloneSource)
        : Effect(cloneSource.getGraphicsDeviceInternal())
    {
    }

    const std::string& EffectMaterial::GetTypeName() const
    {
        static const std::string name =
            "Microsoft.Xna.Framework.Graphics.EffectMaterial";
        return name;
    }

    void EffectMaterial::OnApply()
    {
    }

} // namespace Microsoft::Xna::Framework::Graphics
