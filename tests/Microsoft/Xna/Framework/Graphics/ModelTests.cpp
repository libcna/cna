// SPDX-License-Identifier: MS-PL
// Task 435: Model::CopyAbsoluteBoneTransformsTo unit tests, against a known 3-bone hierarchy
// (root -> child -> grandchild).
//
// GraphicsDevice* is confirmed completely unused by Model's 3-arg constructor (Model.cpp), so
// nullptr is safe here, matching the established ModelMeshTests.cpp/ModelMeshCollectionTests.cpp
// convention of never touching a real GraphicsDevice for pure-data tests.

#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // root --Translate(1,0,0)--> child --Scale(2)--> grandchild --Translate(0,1,0)-->
    //
    // Scale+translate (not just translation-only) deliberately chosen so the absolute-transform
    // multiply ORDER is genuinely discriminating: bone.Transform * dest[parentIndex] (FNA's real
    // order) gives a different result than the swapped dest[parentIndex] * bone.Transform would.
    struct KnownHierarchy
    {
        ModelBone root  { 0, "Root" };
        ModelBone child { 1, "Child" };
        ModelBone grandchild { 2, "Grandchild" };
        Model model;

        KnownHierarchy()
            : model(nullptr, { &root, &child, &grandchild }, {})
        {
            root.setTransformProperty(Matrix::CreateTranslation(1.0f, 0.0f, 0.0f));
            child.setTransformProperty(Matrix::CreateScale(2.0f));
            grandchild.setTransformProperty(Matrix::CreateTranslation(0.0f, 1.0f, 0.0f));

            root.AddChild(&child);
            child.AddChild(&grandchild);
        }
    };

    bool VectorNear(Vector3 a, Vector3 b, float tol = 0.0001f)
    {
        return std::abs(a.X - b.X) <= tol && std::abs(a.Y - b.Y) <= tol && std::abs(a.Z - b.Z) <= tol;
    }
}

TEST(ModelTest, CopyAbsoluteBoneTransformsToRootUsesItsOwnTransform)
{
    KnownHierarchy h;
    std::vector<Matrix> dest(3);
    h.model.CopyAbsoluteBoneTransformsTo(dest);

    // Root has no parent -> absolute transform is exactly its own local transform.
    Vector3 origin = Vector3::Transform(Vector3(0, 0, 0), dest[0]);
    EXPECT_TRUE(VectorNear(origin, Vector3(1.0f, 0.0f, 0.0f)));
}

TEST(ModelTest, CopyAbsoluteBoneTransformsToChildComposesWithParent)
{
    KnownHierarchy h;
    std::vector<Matrix> dest(3);
    h.model.CopyAbsoluteBoneTransformsTo(dest);

    // child.Transform = Scale(2), composed with root's Translate(1,0,0).
    // point (1,0,0) -> Scale(2) -> (2,0,0) -> Translate(1,0,0) -> (3,0,0).
    Vector3 p = Vector3::Transform(Vector3(1.0f, 0.0f, 0.0f), dest[1]);
    EXPECT_TRUE(VectorNear(p, Vector3(3.0f, 0.0f, 0.0f)));
}

TEST(ModelTest, CopyAbsoluteBoneTransformsToGrandchildComposesInCorrectMultiplyOrder)
{
    KnownHierarchy h;
    std::vector<Matrix> dest(3);
    h.model.CopyAbsoluteBoneTransformsTo(dest);

    // Correct order (bone.Transform * dest[parentIndex], applied left-to-right in XNA's
    // row-vector convention): point (1,0,0) -> Translate(0,1,0) -> (1,1,0)
    //                                       -> Scale(2)          -> (2,2,0)
    //                                       -> Translate(1,0,0)  -> (3,2,0)
    Vector3 p = Vector3::Transform(Vector3(1.0f, 0.0f, 0.0f), dest[2]);
    EXPECT_TRUE(VectorNear(p, Vector3(3.0f, 2.0f, 0.0f)));

    // The swapped multiply order (dest[parentIndex] * bone.Transform) would instead give (3,1,0)
    // for this same input point -- confirming this test point genuinely discriminates order.
    EXPECT_FALSE(VectorNear(p, Vector3(3.0f, 1.0f, 0.0f)));
}

TEST(ModelTest, CopyAbsoluteBoneTransformsToThrowsWhenDestinationTooSmall)
{
    KnownHierarchy h;
    std::vector<Matrix> dest(2); // fewer than the 3 bones in this hierarchy
    EXPECT_THROW(h.model.CopyAbsoluteBoneTransformsTo(dest), std::out_of_range);
}
