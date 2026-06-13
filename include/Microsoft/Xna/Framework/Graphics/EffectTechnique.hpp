// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "Microsoft/Xna/Framework/Graphics/EffectAnnotationCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;

    /**
     * @brief Represents a rendering technique within an effect, containing one or more passes.
     *
     * A technique groups related passes that collectively implement one rendering strategy.
     */
    class EffectTechnique
    {
    public:
        /** @brief Constructs a default EffectTechnique with no owner and an empty name. */
        EffectTechnique() = default;

        /**
         * @brief Constructs an EffectTechnique owned by the given effect with the given name.
         *
         * @param owner Pointer to the Effect that owns this technique.
         * @param name  Name of this technique as declared in the effect.
         */
        EffectTechnique(Effect* owner, std::string name);

        /**
         * @brief Gets the name of this technique.
         *
         * @return The technique name string.
         */
        [[nodiscard]] const std::string& getNameProperty() const;

        /**
         * @brief Gets the collection of passes in this technique (mutable overload).
         *
         * @return Reference to the pass collection.
         */
        [[nodiscard]] EffectPassCollection& getPassesProperty();

        /**
         * @brief Gets the collection of passes in this technique (const overload).
         *
         * @return Const reference to the pass collection.
         */
        [[nodiscard]] const EffectPassCollection& getPassesProperty() const;

        /**
         * @brief Gets the annotations attached to this technique (mutable overload).
         *
         * @return Reference to the annotation collection.
         */
        [[nodiscard]] EffectAnnotationCollection& getAnnotationsProperty();

        /**
         * @brief Gets the annotations attached to this technique (const overload).
         *
         * @return Const reference to the annotation collection.
         */
        [[nodiscard]] const EffectAnnotationCollection& getAnnotationsProperty() const;

    private:
        std::string name_;
        EffectPassCollection passes_;
        EffectAnnotationCollection annotations_;
    };
}
