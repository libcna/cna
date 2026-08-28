// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

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
        CNAEXT ~EffectMaterial() override = default;

        /** @brief Returns the fully-qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Creates a clone of this effect.
         *
         * @return Pointer to the cloned Effect.
         */
        [[nodiscard]] Effect* Clone() override;

        /**
         * @brief Takes ownership of a texture this material's parameters point at.
         *
         * `EffectParameter` stores a raw `Texture*`, so something has to keep the object
         * alive for as long as the material can be drawn. The XNB reader loads a material's
         * textures into values it then discards, which left every such parameter pointing at
         * freed memory; the material owns them instead.
         *
         * @param texture The texture to keep alive. A null pointer is ignored.
         */
        CNAEXT void RetainParameterTextureEXT(std::shared_ptr<Texture> texture);

        /**
         * @brief How many textures this material is keeping alive for its parameters.
         *
         * Exists so a test can assert the ownership itself rather than infer it from a read
         * through a pointer that may merely happen to still be readable.
         *
         * @return The number of retained textures.
         */
        CNAEXT [[nodiscard]] std::size_t GetRetainedParameterTextureCountEXT() const;

    protected:
        /** @brief Applies effect parameters to the GPU before each draw pass. */
        void OnApply() override;

    private:
        // Kept in load order; nothing looks them up, they only have to outlive the parameters
        // that point into them.
        std::vector<std::shared_ptr<Texture>> retainedParameterTextures_;
    };

} // namespace Microsoft::Xna::Framework::Graphics
