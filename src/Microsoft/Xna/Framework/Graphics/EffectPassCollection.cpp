// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include <stdexcept>

namespace Microsoft::Xna::Framework::Graphics
{
    int EffectPassCollection::getCountProperty() const { return (int)elements_.size(); }

    EffectPass& EffectPassCollection::operator[](int index) { return elements_.at(index); }
    const EffectPass& EffectPassCollection::operator[](int index) const { return elements_.at(index); }

    EffectPass* EffectPassCollection::operator[](const std::string& name)
    {
        for (auto& e : elements_)
            if (e.getNameProperty() == name) return &e;
        return nullptr;
    }
    const EffectPass* EffectPassCollection::operator[](const std::string& name) const
    {
        for (const auto& e : elements_)
            if (e.getNameProperty() == name) return &e;
        return nullptr;
    }

    void EffectPassCollection::Add(EffectPass pass) { elements_.push_back(std::move(pass)); }

    EffectPassCollection::iterator EffectPassCollection::begin() { return elements_.begin(); }
    EffectPassCollection::iterator EffectPassCollection::end()   { return elements_.end(); }
    EffectPassCollection::const_iterator EffectPassCollection::begin() const { return elements_.begin(); }
    EffectPassCollection::const_iterator EffectPassCollection::end()   const { return elements_.end(); }
}
