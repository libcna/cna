// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class EffectTechniqueCollection
    {
    public:
        EffectTechniqueCollection() = default;

        [[nodiscard]] int getCountProperty() const;
        [[nodiscard]] EffectTechnique& operator[](int index);
        [[nodiscard]] const EffectTechnique& operator[](int index) const;
        [[nodiscard]] EffectTechnique* operator[](const std::string& name);
        [[nodiscard]] const EffectTechnique* operator[](const std::string& name) const;

        void Add(EffectTechnique technique);

        using iterator = std::vector<EffectTechnique>::iterator;
        using const_iterator = std::vector<EffectTechnique>::const_iterator;
        iterator begin();
        iterator end();
        const_iterator begin() const;
        const_iterator end() const;

    private:
        std::vector<EffectTechnique> elements_;
    };
}
