#pragma once

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class EffectParameter;

    /// Default effect used by SpriteBatch.
    class SpriteEffect : public Effect
    {
    public:
        explicit SpriteEffect(GraphicsDevice& device);

        [[nodiscard]] Effect* Clone();

    protected:
        void OnApply() override;

    private:
        explicit SpriteEffect(const SpriteEffect& cloneSource);

        void CacheEffectParameters();

        EffectParameter* matrixParam_ = nullptr;
    };
}
