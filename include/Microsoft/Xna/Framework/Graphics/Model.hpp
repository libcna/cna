#pragma once

#include <vector>

#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "System/Object.hpp"
#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class ModelBone;
    class ModelMesh;

    /// Represents a 3D model composed of bones and meshes.
    class Model
    {
    public:
        Model() = default;
        NOXNA Model(GraphicsDevice* graphicsDevice,
                    std::vector<ModelBone*> bones,
                    std::vector<ModelMesh*> meshes);

        [[nodiscard]] const ModelBoneCollection& getBonesProperty() const;
        [[nodiscard]] const ModelMeshCollection& getMeshesProperty() const;
        [[nodiscard]] ModelBone* getRootProperty() const;
        [[nodiscard]] System::Object* getTagProperty() const;
        void setTagProperty(System::Object* value);

        void CopyAbsoluteBoneTransformsTo(std::vector<Matrix>& destinationBoneTransforms) const;
        void CopyBoneTransformsFrom(const std::vector<Matrix>& sourceBoneTransforms);
        void CopyBoneTransformsTo(std::vector<Matrix>& destinationBoneTransforms) const;
        void Draw(const Matrix& world, const Matrix& view, const Matrix& projection);

    private:
        ModelBoneCollection bones_;
        ModelMeshCollection meshes_;
        ModelBone* root_ = nullptr;
        System::Object* tag_ = nullptr;

        static std::vector<Matrix> sharedDrawBoneMatrices_;
    };
}
