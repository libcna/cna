#pragma once

#include <string>

#include "Microsoft/Xna/Framework/Graphics/EffectAnnotationCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;

    class EffectTechnique
    {
    public:
        EffectTechnique() = default;
        EffectTechnique(Effect* owner, std::string name);

        [[nodiscard]] const std::string& getNameProperty() const;
        [[nodiscard]] EffectPassCollection& getPassesProperty();
        [[nodiscard]] const EffectPassCollection& getPassesProperty() const;
        [[nodiscard]] EffectAnnotationCollection& getAnnotationsProperty();
        [[nodiscard]] const EffectAnnotationCollection& getAnnotationsProperty() const;

    private:
        std::string name_;
        EffectPassCollection passes_;
        EffectAnnotationCollection annotations_;
    };
}
