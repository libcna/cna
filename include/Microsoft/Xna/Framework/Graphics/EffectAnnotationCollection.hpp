// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>
#include <memory>

#include "Microsoft/Xna/Framework/Graphics/EffectAnnotation.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class EffectAnnotationCollection
    {
    public:
        EffectAnnotationCollection() = default;

        [[nodiscard]] int getCountProperty() const;
        [[nodiscard]] EffectAnnotation& operator[](int index);
        [[nodiscard]] const EffectAnnotation& operator[](int index) const;
        [[nodiscard]] EffectAnnotation* operator[](const std::string& name);
        [[nodiscard]] const EffectAnnotation* operator[](const std::string& name) const;

        void Add(EffectAnnotation annotation);

        using iterator = std::vector<EffectAnnotation>::iterator;
        using const_iterator = std::vector<EffectAnnotation>::const_iterator;
        iterator begin();
        iterator end();
        const_iterator begin() const;
        const_iterator end() const;

    private:
        std::vector<EffectAnnotation> elements_;
    };
}
