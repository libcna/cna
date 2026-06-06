#pragma once

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class EffectPassCollection
    {
    public:
        EffectPassCollection() = default;

        [[nodiscard]] int getCountProperty() const;
        [[nodiscard]] EffectPass& operator[](int index);
        [[nodiscard]] const EffectPass& operator[](int index) const;
        [[nodiscard]] EffectPass* operator[](const std::string& name);
        [[nodiscard]] const EffectPass* operator[](const std::string& name) const;

        void Add(EffectPass pass);

        using iterator = std::vector<EffectPass>::iterator;
        using const_iterator = std::vector<EffectPass>::const_iterator;
        iterator begin();
        iterator end();
        const_iterator begin() const;
        const_iterator end() const;

    private:
        std::vector<EffectPass> elements_;
    };
}
