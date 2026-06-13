// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief An Effect subclass used to associate a cloned effect instance with a ModelMeshPart.
     *
     * Created internally by the content pipeline; games do not instantiate EffectMaterial directly.
     */
    class EffectMaterial : public Effect
    {
    public:
        /**
         * @brief Constructs an EffectMaterial by cloning the given source effect.
         *
         * @param cloneSource The effect whose graphics device will be used for this material.
         */
        explicit EffectMaterial(Effect& cloneSource);

        /** @brief Destroys the EffectMaterial. */
        NOXNA ~EffectMaterial() override = default;

        /** @brief Returns the fully-qualified .NET type name. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        /** @brief Applies effect parameters to the GPU before each draw pass. */
        void OnApply() override;
    };

} // namespace Microsoft::Xna::Framework::Graphics
