#pragma once

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class EffectParameterCollection
    {
    public:
        EffectParameterCollection() = default;

        [[nodiscard]] int getCountProperty() const;
        [[nodiscard]] EffectParameter& operator[](int index);
        [[nodiscard]] const EffectParameter& operator[](int index) const;
        [[nodiscard]] EffectParameter* operator[](const std::string& name);
        [[nodiscard]] const EffectParameter* operator[](const std::string& name) const;

        void Add(EffectParameter param);

        using iterator = std::vector<EffectParameter>::iterator;
        using const_iterator = std::vector<EffectParameter>::const_iterator;
        iterator begin();
        iterator end();
        const_iterator begin() const;
        const_iterator end() const;

    private:
        std::vector<EffectParameter> elements_;
    };
}
