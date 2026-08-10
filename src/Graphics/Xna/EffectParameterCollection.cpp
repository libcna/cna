// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    int EffectParameterCollection::getCountProperty() const { return (int)elements_.size(); }

    EffectParameter& EffectParameterCollection::operator[](int index) { return *elements_.at(index); }
    const EffectParameter& EffectParameterCollection::operator[](int index) const { return *elements_.at(index); }

    EffectParameter* EffectParameterCollection::operator[](const std::string& name)
    {
        for (auto& e : elements_)
            if (e->getNameProperty() == name) return e.get();
        return nullptr;
    }
    const EffectParameter* EffectParameterCollection::operator[](const std::string& name) const
    {
        for (const auto& e : elements_)
            if (e->getNameProperty() == name) return e.get();
        return nullptr;
    }

    void EffectParameterCollection::Add(EffectParameter param)
    {
        elements_.push_back(std::make_unique<EffectParameter>(std::move(param)));
    }

    EffectParameter* EffectParameterCollection::GetParameterBySemantic(const std::string& semantic)
    {
        for (auto& e : elements_)
            if (e->getSemanticProperty() == semantic) return e.get();
        return nullptr;
    }
    const EffectParameter* EffectParameterCollection::GetParameterBySemantic(const std::string& semantic) const
    {
        for (const auto& e : elements_)
            if (e->getSemanticProperty() == semantic) return e.get();
        return nullptr;
    }

    EffectParameterCollection::iterator EffectParameterCollection::begin() { return iterator(elements_.begin()); }
    EffectParameterCollection::iterator EffectParameterCollection::end()   { return iterator(elements_.end()); }
    EffectParameterCollection::const_iterator EffectParameterCollection::begin() const { return const_iterator(elements_.begin()); }
    EffectParameterCollection::const_iterator EffectParameterCollection::end()   const { return const_iterator(elements_.end()); }
}
