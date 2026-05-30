#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    void EffectPass::Apply()
    {
        if (owner_)
        {
            owner_->Apply();
        }
    }
}
