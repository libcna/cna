#include "Microsoft/Xna/Framework/Graphics/EffectTechniqueCollection.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    int EffectTechniqueCollection::getCountProperty() const { return (int)elements_.size(); }

    EffectTechnique& EffectTechniqueCollection::operator[](int index) { return elements_.at(index); }
    const EffectTechnique& EffectTechniqueCollection::operator[](int index) const { return elements_.at(index); }

    EffectTechnique* EffectTechniqueCollection::operator[](const std::string& name)
    {
        for (auto& e : elements_)
            if (e.getNameProperty() == name) return &e;
        return nullptr;
    }
    const EffectTechnique* EffectTechniqueCollection::operator[](const std::string& name) const
    {
        for (const auto& e : elements_)
            if (e.getNameProperty() == name) return &e;
        return nullptr;
    }

    void EffectTechniqueCollection::Add(EffectTechnique technique) { elements_.push_back(std::move(technique)); }

    EffectTechniqueCollection::iterator EffectTechniqueCollection::begin() { return elements_.begin(); }
    EffectTechniqueCollection::iterator EffectTechniqueCollection::end()   { return elements_.end(); }
    EffectTechniqueCollection::const_iterator EffectTechniqueCollection::begin() const { return elements_.begin(); }
    EffectTechniqueCollection::const_iterator EffectTechniqueCollection::end()   const { return elements_.end(); }
}
