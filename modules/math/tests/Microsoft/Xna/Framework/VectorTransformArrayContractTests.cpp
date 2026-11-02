// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <type_traits>
#include <vector>
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/ArgumentException.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    const Matrix matrix(2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53);
    // This nonunit quaternion maps (x,y,z) to (-x-2y,2x-y,z), without normalization.
    const Quaternion rotation(0, 0, 1, 1);

    template<class V> struct TransformCases
    {
        std::vector<V> source, positions, rotations, normals;
    };

    // Hand-evaluated integer dot products; no CNA operation supplies an expectation.
    template<class V> TransformCases<V> Cases()
    {
        if constexpr (std::is_same_v<V, Vector2>)
            return {{V(0, 0), V(1, 2), V(-2, 3)},
                    {V(41, 43), V(65, 72), V(70, 76)},
                    {V(0, 0), V(-5, 0), V(-4, -7)},
                    {V(0, 0), V(24, 29), V(29, 33)}};
        else if constexpr (std::is_same_v<V, Vector3>)
            return {{V(0, 0, 0), V(1, 2, 3), V(-2, 3, -4)},
                    {V(41, 43, 47), V(134, 159, 179), V(-22, -40, -36)},
                    {V(0, 0, 0), V(-5, 0, 3), V(-4, -7, -4)},
                    {V(0, 0, 0), V(93, 116, 132), V(-63, -83, -83)}};
        else
            return {{V(0, 0, 0, 0), V(1, 2, 3, 4), V(-2, 3, -4, 5)},
                    {V(0, 0, 0, 0), V(257, 288, 320, 368), V(142, 132, 152, 160)},
                    {V(0, 0, 0, 0), V(-5, 0, 3, 4), V(-4, -7, -4, 5)}, {}};
    }

    template<class V> class VectorTransformArrayContractTest : public testing::Test {};
    using Vectors = testing::Types<Vector2, Vector3, Vector4>;
    TYPED_TEST_SUITE(VectorTransformArrayContractTest, Vectors);

    TYPED_TEST(VectorTransformArrayContractTest, WholeMatrixHasLiteralResultsAndPreservesTail)
    {
        using V = TypeParam;
        const auto data = Cases<V>();
        auto source = data.source;
        const V guard(99);
        std::vector<V> destination(4, guard);
        V::Transform(source, matrix, destination);
        EXPECT_EQ(destination, (std::vector<V>{data.positions[0], data.positions[1], data.positions[2], guard}));
        EXPECT_EQ(source, data.source);
        V::Transform(source, matrix, source);
        EXPECT_EQ(source, data.positions);
    }

    TYPED_TEST(VectorTransformArrayContractTest, IndexedMatrixWritesOnlyTheRequestedWindow)
    {
        using V = TypeParam;
        const auto data = Cases<V>();
        const V guard(99);
        std::vector<V> destination(4, guard);
        V::Transform(data.source, 1, matrix, destination, 2, 2);
        EXPECT_EQ(destination, (std::vector<V>{guard, guard, data.positions[1], data.positions[2]}));
        auto inplace = data.source;
        V::Transform(inplace, 1, matrix, inplace, 1, 2);
        EXPECT_EQ(inplace, (std::vector<V>{data.source[0], data.positions[1], data.positions[2]}));
    }

    TYPED_TEST(VectorTransformArrayContractTest, WholeQuaternionHasLiteralResultsAndPreservesTail)
    {
        using V = TypeParam;
        const auto data = Cases<V>();
        auto source = data.source;
        const V guard(99);
        std::vector<V> destination(4, guard);
        V::Transform(source, rotation, destination);
        EXPECT_EQ(destination, (std::vector<V>{data.rotations[0], data.rotations[1], data.rotations[2], guard}));
        EXPECT_EQ(source, data.source);
        V::Transform(source, rotation, source);
        EXPECT_EQ(source, data.rotations);
    }

    TYPED_TEST(VectorTransformArrayContractTest, IndexedQuaternionWritesOnlyTheRequestedWindow)
    {
        using V = TypeParam;
        const auto data = Cases<V>();
        const V guard(99);
        std::vector<V> destination(4, guard);
        V::Transform(data.source, 1, rotation, destination, 2, 2);
        EXPECT_EQ(destination, (std::vector<V>{guard, guard, data.rotations[1], data.rotations[2]}));
        auto inplace = data.source;
        V::Transform(inplace, 1, rotation, inplace, 1, 2);
        EXPECT_EQ(inplace, (std::vector<V>{data.source[0], data.rotations[1], data.rotations[2]}));
    }

    TYPED_TEST(VectorTransformArrayContractTest, WholeOverloadsAcceptEmptyAndRefuseShortDestinationBeforeWriting)
    {
        using V = TypeParam;
        const auto data = Cases<V>();
        const std::vector<V> empty;
        const V guard(99);
        for (int mode = 0; mode < 2; ++mode)
        {
            SCOPED_TRACE(mode);
            std::vector<V> destination(2, guard);
            if (mode == 0)
            {
                EXPECT_THROW(V::Transform(data.source, matrix, destination), System::ArgumentException);
                V::Transform(empty, matrix, destination);
            }
            else
            {
                EXPECT_THROW(V::Transform(data.source, rotation, destination), System::ArgumentException);
                V::Transform(empty, rotation, destination);
            }
            EXPECT_EQ(destination, (std::vector<V>{guard, guard}));
        }
    }

    template<class V> class NormalTransformArrayContractTest : public testing::Test {};
    using Normals = testing::Types<Vector2, Vector3>;
    TYPED_TEST_SUITE(NormalTransformArrayContractTest, Normals);

    TYPED_TEST(NormalTransformArrayContractTest, WholeNormalsExcludeTranslationAndPreserveTail)
    {
        using V = TypeParam;
        const auto data = Cases<V>();
        auto source = data.source;
        const V guard(99);
        std::vector<V> destination(4, guard);
        V::TransformNormal(source, matrix, destination);
        EXPECT_EQ(destination, (std::vector<V>{data.normals[0], data.normals[1], data.normals[2], guard}));
        EXPECT_EQ(source, data.source);
        V::TransformNormal(source, matrix, source);
        EXPECT_EQ(source, data.normals);
    }

    TYPED_TEST(NormalTransformArrayContractTest, IndexedNormalsWriteOnlyTheRequestedWindow)
    {
        using V = TypeParam;
        const auto data = Cases<V>();
        const V guard(99);
        std::vector<V> destination(4, guard);
        V::TransformNormal(data.source, 1, matrix, destination, 2, 2);
        EXPECT_EQ(destination, (std::vector<V>{guard, guard, data.normals[1], data.normals[2]}));
        auto inplace = data.source;
        V::TransformNormal(inplace, 1, matrix, inplace, 1, 2);
        EXPECT_EQ(inplace, (std::vector<V>{data.source[0], data.normals[1], data.normals[2]}));
    }

    TYPED_TEST(NormalTransformArrayContractTest, WholeNormalsAcceptEmptyAndRefuseShortDestinationBeforeWriting)
    {
        using V = TypeParam;
        const auto data = Cases<V>();
        const std::vector<V> empty;
        const V guard(99);
        std::vector<V> destination(2, guard);
        EXPECT_THROW(V::TransformNormal(data.source, matrix, destination), System::ArgumentException);
        V::TransformNormal(empty, matrix, destination);
        EXPECT_EQ(destination, (std::vector<V>{guard, guard}));
    }
}

TEST(Vector2TransformContractTest, NonidentityValueAndOutFormsHaveLiteralCoordinates)
{
    const Vector2 input(1, 2);
    Vector2 output(99);
    EXPECT_EQ(Vector2::Transform(input, matrix), Vector2(65, 72));
    Vector2::Transform(input, matrix, output);
    EXPECT_EQ(output, Vector2(65, 72));
    EXPECT_EQ(Vector2::Transform(input, rotation), Vector2(-5, 0));
    Vector2::Transform(input, rotation, output);
    EXPECT_EQ(output, Vector2(-5, 0));
    EXPECT_EQ(Vector2::TransformNormal(input, matrix), Vector2(24, 29));
    Vector2::TransformNormal(input, matrix, output);
    EXPECT_EQ(output, Vector2(24, 29));
}

TEST(Vector2TransformContractTest, OverlappingRangesReadEarlierWritesInForwardOrder)
{
    std::vector<Vector2> values{Vector2(1, 2), Vector2(9), Vector2(8), Vector2(7)};
    Vector2::Transform(values, 0, rotation, values, 1, 3);
    EXPECT_EQ(values, (std::vector<Vector2>{Vector2(1, 2), Vector2(-5, 0), Vector2(5, -10), Vector2(15, 20)}));
}
