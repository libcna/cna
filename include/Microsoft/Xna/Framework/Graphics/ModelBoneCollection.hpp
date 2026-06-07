#pragma once

#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    class ModelBone;

    /// A collection of ModelBone objects.
    class ModelBoneCollection
    {
    public:
        ModelBoneCollection() = default;

        [[nodiscard]] ModelBone* operator[](int index) const;
        [[nodiscard]] ModelBone* operator[](const std::string& name) const;
        [[nodiscard]] int getCountProperty() const;

    private:
        std::vector<ModelBone*> bones_;
        friend class Model;
    };
}
