// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <typeinfo>
#include <vector>
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // All entries are small integers exactly representable in binary32; zero tolerance.
    Matrix Affine(float sx, float sy, float sz, float x, float y, float z)
    {
        return Matrix(sx,0,0,0, 0,sy,0,0, 0,0,sz,0, x,y,z,1);
    }
    template<class F> void ExpectShortArray(F action, const char* parameter)
    {
        try { action(); FAIL() << "short array accepted"; }
        catch (const System::ArgumentOutOfRangeException& e)
        {
            EXPECT_EQ(typeid(e), typeid(System::ArgumentOutOfRangeException));
            EXPECT_EQ(e.getParamNameProperty(), parameter);
        }
        catch (...) { FAIL() << "wrong exception type"; }
    }
}

TEST(ModelRuntimeTransformsTest, ShortArraysRefuseBeforeAnyWriteWithExactParameter)
{
    ModelBone root(0,"root"), child(1,"child");
    root.AddChild(&child);
    const Matrix a = Affine(2,3,4,5,6,7), b = Affine(8,9,10,11,12,13);
    root.setTransformProperty(a);
    child.setTransformProperty(b);
    Model model(nullptr,{&root,&child},{});
    // Refresh Model.Copy* IL_000e..0028 checks size before entering any copy loop.
    for (const int size : {0,1})
    {
        SCOPED_TRACE(size);
        std::vector<Matrix> shortArray(static_cast<std::size_t>(size), b);
        const auto before = shortArray;
        ExpectShortArray([&]{ model.CopyAbsoluteBoneTransformsTo(shortArray); },
                         "destinationBoneTransforms");
        EXPECT_EQ(shortArray,before);
        ExpectShortArray([&]{ model.CopyBoneTransformsTo(shortArray); },
                         "destinationBoneTransforms");
        EXPECT_EQ(shortArray,before);
        ExpectShortArray([&]{ model.CopyBoneTransformsFrom(shortArray); },
                         "sourceBoneTransforms");
        EXPECT_EQ(root.getTransformProperty(),a);
        EXPECT_EQ(child.getTransformProperty(),b);
    }
}

TEST(ModelRuntimeTransformsTest, LaterParentUsesExistingDestinationSlotWithoutSorting)
{
    ModelBone child(0,"child"), parent(1,"parent");
    parent.AddChild(&child);
    child.setTransformProperty(Affine(1,1,1,2,3,4));
    parent.setTransformProperty(Affine(2,3,4,5,6,7));
    Model model(nullptr,{&child,&parent},{});
    std::vector<Matrix> out{Affine(1,1,1,0,0,0),Affine(5,7,11,13,17,19)};
    model.CopyAbsoluteBoneTransformsTo(out);
    // Refresh IL_0035..0097 walks collection order and reads the caller's later slot as-is.
    // (2*5+13,3*7+17,4*11+19)=(23,38,63); no recursive/topological repair.
    EXPECT_EQ(out[0],Affine(5,7,11,23,38,63));
    EXPECT_EQ(out[1],Affine(2,3,4,5,6,7));
}

TEST(ModelRuntimeTransformsTest, LocalCopiesIgnoreBoneIndexValues)
{
    ModelBone first(7,"first"), second(3,"second");
    first.AddChild(&second);
    Model model(nullptr,{&first,&second},{});
    const Matrix a(1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16);
    const Matrix b(16,15,14,13, 12,11,10,9, 8,7,6,5, 4,3,2,1);
    const Matrix sentinel = Affine(2,3,4,5,6,7);
    const std::vector<Matrix> source{a,b,sentinel};
    // Refresh From/To IL_0039..0051 copy full matrices by collection position, not bone.Index.
    model.CopyBoneTransformsFrom(source);
    EXPECT_EQ(first.getTransformProperty(),a);
    EXPECT_EQ(second.getTransformProperty(),b);
    EXPECT_EQ(source,(std::vector<Matrix>{a,b,sentinel}));
    std::vector<Matrix> out(3,sentinel);
    model.CopyBoneTransformsTo(out);
    EXPECT_EQ(out,(std::vector<Matrix>{a,b,sentinel}));
}

TEST(ModelRuntimeTransformsTest, EmptyModelCopiesDoNotTouchCallerStorage)
{
    Model model(nullptr,{},{});
    const Matrix sentinel = Affine(2,3,4,5,6,7);
    std::vector<Matrix> empty, out{sentinel};
    // Refresh Copy* IL_0029..005e (absolute ..0099): zero Bones.Count means zero writes.
    model.CopyBoneTransformsFrom(empty);
    model.CopyBoneTransformsTo(empty);
    model.CopyAbsoluteBoneTransformsTo(empty);
    EXPECT_TRUE(empty.empty());
    model.CopyBoneTransformsTo(out);
    model.CopyAbsoluteBoneTransformsTo(out);
    EXPECT_EQ(out,(std::vector<Matrix>{sentinel}));
}
