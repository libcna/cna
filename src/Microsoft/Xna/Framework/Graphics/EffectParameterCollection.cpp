#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    int EffectParameterCollection::getCountProperty() const { return (int)elements_.size(); }

    EffectParameter& EffectParameterCollection::operator[](int index) { return elements_.at(index); }
    const EffectParameter& EffectParameterCollection::operator[](int index) const { return elements_.at(index); }

    EffectParameter* EffectParameterCollection::operator[](const std::string& name)
    {
        for (auto& e : elements_)
            if (e.getNameProperty() == name) return &e;
        return nullptr;
    }
    const EffectParameter* EffectParameterCollection::operator[](const std::string& name) const
    {
        for (const auto& e : elements_)
            if (e.getNameProperty() == name) return &e;
        return nullptr;
    }

    void EffectParameterCollection::Add(EffectParameter param) { elements_.push_back(std::move(param)); }

    EffectParameterCollection::iterator EffectParameterCollection::begin() { return elements_.begin(); }
    EffectParameterCollection::iterator EffectParameterCollection::end()   { return elements_.end(); }
    EffectParameterCollection::const_iterator EffectParameterCollection::begin() const { return elements_.begin(); }
    EffectParameterCollection::const_iterator EffectParameterCollection::end()   const { return elements_.end(); }
}
