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

    private:
        std::vector<Effect*> effects_;
        friend class ModelMesh;
    };
}
