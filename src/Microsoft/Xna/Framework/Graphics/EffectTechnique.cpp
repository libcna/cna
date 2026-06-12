// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    EffectTechnique::EffectTechnique(Effect* owner, std::string name)
        : name_(std::move(name))
    {
        passes_.Add(EffectPass(owner, "P0"));
    }

    const std::string& EffectTechnique::getNameProperty() const { return name_; }

    EffectPassCollection& EffectTechnique::getPassesProperty() { return passes_; }
    const EffectPassCollection& EffectTechnique::getPassesProperty() const { return passes_; }

    EffectAnnotationCollection& EffectTechnique::getAnnotationsProperty() { return annotations_; }
    const EffectAnnotationCollection& EffectTechnique::getAnnotationsProperty() const { return annotations_; }
}
