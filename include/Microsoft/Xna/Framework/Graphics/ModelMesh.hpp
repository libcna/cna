#pragma once

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelEffectCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "System/Object.hpp"
#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class ModelBone;
    class ModelMeshPart;

    /// Represents a mesh within a Model, containing one or more mesh parts.
    class ModelMesh
    {
    public:
        NOXNA ModelMesh(GraphicsDevice* graphicsDevice, std::vector<ModelMeshPart*> parts);
        NOXNA ModelMesh(GraphicsDevice* graphicsDevice, std::string name,
                        std::vector<ModelMeshPart*> parts);

        [[nodiscard]] BoundingSphere getBoundingSphereProperty() const;
        [[nodiscard]] const ModelEffectCollection& getEffectsProperty() const;
        [[nodiscard]] ModelEffectCollection& getEffectsPropertyMutable();
        [[nodiscard]] const ModelMeshPartCollection& getMeshPartsProperty() const;
        [[nodiscard]] const std::string& getNameProperty() const;
        [[nodiscard]] ModelBone* getParentBoneProperty() const;
        [[nodiscard]] System::Object* getTagProperty() const;
        void setTagProperty(System::Object* value);

        void Draw();

    private:
        GraphicsDevice* graphicsDevice_ = nullptr;
        BoundingSphere boundingSphere_;
        std::string name_;
        ModelBone* parentBone_ = nullptr;
        ModelMeshPartCollection meshParts_;
        ModelEffectCollection effects_;
        System::Object* tag_ = nullptr;

        friend class Model;
    };
}
