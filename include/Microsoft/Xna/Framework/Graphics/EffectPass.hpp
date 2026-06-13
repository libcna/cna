// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "Microsoft/Xna/Framework/Graphics/EffectAnnotationCollection.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;

    /**
     * @brief Represents a single rendering pass within an effect technique.
     *
     * Each pass encapsulates vertex and pixel shader programs together with
     * any associated render-state changes.
     */
    class EffectPass
    {
    public:
        /**
         * @brief Constructs an EffectPass owned by the given effect with the given name.
         *
         * @param owner Pointer to the Effect that owns this pass.
         * @param name  Name of this pass as declared in the effect.
         */
        EffectPass(Effect* owner, std::string name);

        /**
         * @brief Gets the name of this pass.
         *
         * @return The pass name string.
         */
        [[nodiscard]] const std::string& getNameProperty() const;

        /**
         * @brief Gets the annotations attached to this pass (mutable overload).
         *
         * @return Reference to the annotation collection.
         */
        [[nodiscard]] EffectAnnotationCollection& getAnnotationsProperty();

        /**
         * @brief Gets the annotations attached to this pass (const overload).
         *
         * @return Const reference to the annotation collection.
         */
        [[nodiscard]] const EffectAnnotationCollection& getAnnotationsProperty() const;

        /**
         * @brief Applies this pass to the graphics device, making its shaders and state active.
         *
         * Must be called before issuing draw calls that should use this pass.
         */
        void Apply();

    private:
        Effect* owner_;
        std::string name_;
        EffectAnnotationCollection annotations_;
    };
}
