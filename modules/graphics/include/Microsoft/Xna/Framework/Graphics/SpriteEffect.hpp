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
     * XNA 4.0 has no public SpriteEffect; this is FNA's class, and it is public in CNA only. Its
     * one parameter is FNA's `MatrixTransform` (a 4x4 single-precision matrix), which every Apply
     * sets from the device's current viewport.
     */
    class CNAEXT SpriteEffect : public Effect
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
         * The clone has its own `MatrixTransform` parameter holding the source's current value.
         *
         * @return Pointer to the cloned Effect.
         * @throws System::ObjectDisposedException If this effect has been disposed.
         */
        [[nodiscard]] Effect* Clone() override;

        /** @brief Returns the fully qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Identifies only this exact stock runtime type to 2D renderers. */
        CNAEXT [[nodiscard]] bool IsExactStockSpriteEffectEXT() const noexcept override
        {
            return typeid(*this) == typeid(SpriteEffect);
        }

    protected:
        /**
         * @brief Sets `MatrixTransform` to FNA's sprite projection for the current viewport.
         *
         * The value is `CreateTranslation(-0.5, -0.5, 0) * CreateOrthographicOffCenter(0, width,
         * height, 0, 0, 1)` for the device's viewport width and height.
         */
        void OnApply() override;

    private:
        explicit SpriteEffect(const SpriteEffect& cloneSource);

        void CacheEffectParameters();

        EffectParameter* matrixParam_ = nullptr;
    };
}
