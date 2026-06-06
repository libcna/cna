#pragma once

#include <string>

#include "Microsoft/Xna/Framework/Graphics/ModelEffectCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class ModelBone;

    /// Represents a mesh within a Model, containing one or more mesh parts.
    class ModelMesh
    {
    public:
        [[nodiscard]] const std::string& getNameProperty() const;
        [[nodiscard]] ModelBone* getParentBoneProperty() const;
        [[nodiscard]] const ModelMeshPartCollection& getMeshPartsProperty() const;
        [[nodiscard]] const ModelEffectCollection& getEffectsProperty() const;

        void Draw();

    private:
        std::string name_;
        ModelBone* parentBone_ = nullptr;
        ModelMeshPartCollection meshParts_;
        ModelEffectCollection effects_;

        friend class Model;
    };
}
