// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/EffectAnnotationCollection.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    int EffectAnnotationCollection::getCountProperty() const { return (int)elements_.size(); }

    EffectAnnotation* EffectAnnotationCollection::operator[](int index)
    {
        return index >= 0 && index < getCountProperty() ? &elements_[index] : nullptr;
    }

    const EffectAnnotation* EffectAnnotationCollection::operator[](int index) const
    {
        return index >= 0 && index < getCountProperty() ? &elements_[index] : nullptr;
    }

    EffectAnnotation* EffectAnnotationCollection::operator[](const std::string& name)
    {
        for (auto& e : elements_)
            if (e.getNameProperty() == name) return &e;
        return nullptr;
    }

    const EffectAnnotation* EffectAnnotationCollection::operator[](const std::string& name) const
    {
        for (const auto& e : elements_)
            if (e.getNameProperty() == name) return &e;
        return nullptr;
    }

    void EffectAnnotationCollection::Add(EffectAnnotation annotation)
    {
        elements_.push_back(std::move(annotation));
    }

    EffectAnnotationCollection::iterator EffectAnnotationCollection::begin() { return elements_.begin(); }
    EffectAnnotationCollection::iterator EffectAnnotationCollection::end()   { return elements_.end(); }
    EffectAnnotationCollection::const_iterator EffectAnnotationCollection::begin() const { return elements_.begin(); }
    EffectAnnotationCollection::const_iterator EffectAnnotationCollection::end()   const { return elements_.end(); }
}
