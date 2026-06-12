// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "Microsoft/Xna/Framework/Graphics/EffectAnnotationCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;

    /// Represents a rendering technique within an effect, containing one or more passes.
    class EffectTechnique
    {
    public:
        /// Constructs a default EffectTechnique.
        EffectTechnique() = default;
        /// Constructs an EffectTechnique owned by the given effect with the given name.
        EffectTechnique(Effect* owner, std::string name);

        /// Gets the name of this technique.
        [[nodiscard]] const std::string& getNameProperty() const;
        /// Gets the collection of passes in this technique.
        [[nodiscard]] EffectPassCollection& getPassesProperty();
        /// Gets the collection of passes in this technique (const overload).
        [[nodiscard]] const EffectPassCollection& getPassesProperty() const;
        /// Gets the annotations attached to this technique.
        [[nodiscard]] EffectAnnotationCollection& getAnnotationsProperty();
        /// Gets the annotations attached to this technique (const overload).
        [[nodiscard]] const EffectAnnotationCollection& getAnnotationsProperty() const;

    private:
        std::string name_;
        EffectPassCollection passes_;
        EffectAnnotationCollection annotations_;
    };
}
