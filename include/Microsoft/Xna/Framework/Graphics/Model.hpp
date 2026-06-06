#pragma once

#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Represents a 3D model composed of bones and meshes.
    class Model
    {
    public:
        Model() = default;

        [[nodiscard]] const ModelBoneCollection& getBonesProperty() const;
        [[nodiscard]] const ModelMeshCollection& getMeshesProperty() const;
        [[nodiscard]] ModelBone* getRootProperty() const;

        void CopyAbsoluteBoneTransformsTo(std::vector<Matrix>& destinationBoneTransforms) const;
        void Draw(const Matrix& world, const Matrix& view, const Matrix& projection);

    private:
        ModelBoneCollection bones_;
        ModelMeshCollection meshes_;
        ModelBone* root_ = nullptr;
    };
}
