#pragma once

#include <string>

#include "Microsoft/Xna/Framework/Graphics/EffectAnnotationCollection.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;

    class EffectPass
    {
    public:
        EffectPass(Effect* owner, std::string name);

        [[nodiscard]] const std::string& getNameProperty() const;
        [[nodiscard]] EffectAnnotationCollection& getAnnotationsProperty();
        [[nodiscard]] const EffectAnnotationCollection& getAnnotationsProperty() const;

        void Apply();

    private:
        Effect* owner_;
        std::string name_;
        EffectAnnotationCollection annotations_;
    };
}
