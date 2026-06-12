// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "Microsoft/Xna/Framework/Graphics/EffectAnnotationCollection.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;

    /// Represents a single rendering pass within an effect technique.
    class EffectPass
    {
    public:
        /// Constructs an EffectPass owned by the given effect with the given name.
        EffectPass(Effect* owner, std::string name);

        /// Gets the name of this pass.
        [[nodiscard]] const std::string& getNameProperty() const;
        /// Gets the annotations attached to this pass.
        [[nodiscard]] EffectAnnotationCollection& getAnnotationsProperty();
        /// Gets the annotations attached to this pass (const overload).
        [[nodiscard]] const EffectAnnotationCollection& getAnnotationsProperty() const;

        /// Applies this pass to the graphics device, making the pass's shaders and state active.
        void Apply();

    private:
        Effect* owner_;
        std::string name_;
        EffectAnnotationCollection annotations_;
    };
}
