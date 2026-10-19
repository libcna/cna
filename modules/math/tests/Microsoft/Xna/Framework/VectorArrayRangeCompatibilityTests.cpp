// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <limits>
#include <typeinfo>
#include <vector>
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/ArgumentException.hpp"
#include "System/IndexOutOfRangeException.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    template<class V> class VectorArrayRangeCompatibilityTest : public testing::Test {};
    using Vectors = testing::Types<Vector2, Vector3, Vector4>;
    TYPED_TEST_SUITE(VectorArrayRangeCompatibilityTest, Vectors);

    TYPED_TEST(VectorArrayRangeCompatibilityTest, EmptyRangesAndExactRefusalContracts)
    {
        using V = TypeParam;
        constexpr int largest = std::numeric_limits<int>::max(), smallest = std::numeric_limits<int>::min();
        // The identical 25 windows pass all eight genuine XNA range overloads.
        constexpr std::array<std::array<int, 3>, 25> cases = {{
            {0,0,-1}, {2,2,-1}, {3,3,-1}, {4,0,-1}, {0,4,-1},
            {-1,-1,-1}, {largest,largest,smallest}, {0,0,smallest},
            {2,2,0}, {3,0,0}, {0,3,0}, {-1,-1,0}, {smallest,smallest,0},
            {0,0,1}, {1,1,1}, {0,0,2}, {-1,0,1}, {0,-1,1},
            {2,0,1}, {0,2,1}, {largest,0,1}, {0,largest,1},
            {0,0,largest}, {smallest,0,largest}, {0,smallest,largest}
        }};
        const std::vector<V> source{V(1), V(2)};
        const V guard(99);
        const auto matrix = Matrix::getIdentityProperty();
        for (const auto& window : cases) for (int mode = 0; mode < 3; ++mode)
        {
            const auto [si, di, length] = window;
            if constexpr (!requires(std::vector<V>& destination) {
                V::TransformNormal(source, si, matrix, destination, di, length);
            }) if (mode == 2) continue;
            SCOPED_TRACE(testing::Message() << "source=" << si << " destination=" << di <<
                " length=" << length << " mode=" << mode);
            enum class Result { Success, Argument, Index, Other } actual = Result::Success;
            const auto expected = 2 < static_cast<std::int64_t>(si) + length ||
                2 < static_cast<std::int64_t>(di) + length ? Result::Argument :
                length > 0 && (si < 0 || di < 0) ? Result::Index : Result::Success;
            std::vector<V> destination(2, guard);
            try
            {
                if (mode == 0) V::Transform(source, si, matrix, destination, di, length);
                else if (mode == 1) V::Transform(source, si, Quaternion::Identity, destination, di, length);
                else if constexpr (requires { V::TransformNormal(source, si, matrix, destination, di, length); })
                    V::TransformNormal(source, si, matrix, destination, di, length);
            }
            catch (const System::ArgumentException& error)
            {
                actual = typeid(error) == typeid(System::ArgumentException) ? Result::Argument : Result::Other;
            }
            catch (const System::IndexOutOfRangeException&) { actual = Result::Index; }
            catch (...) { actual = Result::Other; }
            EXPECT_EQ(actual, expected);
            for (int i = 0; i < 2; ++i)
            {
                const V wanted = expected == Result::Success && length > 0 && i >= di &&
                    static_cast<std::int64_t>(i) < static_cast<std::int64_t>(di) + length ?
                    source[si + i - di] : guard;
                EXPECT_EQ(destination[i], wanted);
            }
        }
    }
}
