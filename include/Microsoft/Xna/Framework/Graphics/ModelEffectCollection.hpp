#pragma once

#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;

    /// A collection of Effect objects used by a model mesh.
    class ModelEffectCollection
    {
    public:
        [[nodiscard]] Effect* operator[](int index) const;
        [[nodiscard]] int getCountProperty() const;

        [[nodiscard]] bool Contains(const Effect* effect) const;
        void Add(Effect* effect);
        void Remove(const Effect* effect);

        using iterator = std::vector<Effect*>::iterator;
        using const_iterator = std::vector<Effect*>::const_iterator;
        iterator begin();
        iterator end();
        const_iterator begin() const;
        const_iterator end() const;

    private:
        std::vector<Effect*> effects_;
        friend class ModelMesh;
    };
}
