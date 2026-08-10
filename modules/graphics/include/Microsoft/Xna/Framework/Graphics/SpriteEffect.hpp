// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include <typeinfo>

namespace Microsoft::Xna::Framework::Graphics
{
    class EffectParameter;

    /**
     * @brief Default sprite-rendering effect used internally by SpriteBatch.
     *
     * Applies a single world-view-projection matrix transform to a textured quad.
     */
    class SpriteEffect : public Effect
    {
    public:
        /**
         * @brief Constructs a SpriteEffect for the given graphics device.
         *
         * @param device The graphics device that will own this effect.
         */
        explicit SpriteEffect(GraphicsDevice& device);

        /**
         * @brief Creates a clone of this effect.
         *
         * @return Pointer to the cloned Effect.
         */
        [[nodiscard]] Effect* Clone() override;

        /** @brief Returns the fully qualified .NET type name. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Identifies only this exact stock runtime type to 2D renderers. */
        NOXNA [[nodiscard]] bool IsExactStockSpriteEffectEXT() const noexcept override
        {
            return typeid(*this) == typeid(SpriteEffect);
        }

    protected:
        /**
         * @brief Applies the sprite transform matrix parameter to the graphics device.
         */
        void OnApply() override;

    private:
        explicit SpriteEffect(const SpriteEffect& cloneSource);

        void CacheEffectParameters();

        EffectParameter* matrixParam_ = nullptr;
    };
}
