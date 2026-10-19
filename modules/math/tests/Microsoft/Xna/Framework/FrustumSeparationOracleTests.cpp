// SPDX-License-Identifier: MS-PL
// Actual XNA classifications, including the distinction between conservative
// plane containment and convex intersection. Generator recorded in the fixture.
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/ContainmentType.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <string>

using namespace Microsoft::Xna::Framework;

namespace
{
Matrix Frame(int kind)
{
    Matrix m = Matrix::getIdentityProperty();
    if (kind == 1)
    {
        m.M11 = 2;
        m.M22 = 2;
        m.M33 = 2;
        m.M43 = -0.5f;
    }
    if (kind == 2)
    {
        m.M11 = 0.6f;
        m.M12 = 0.8f;
        m.M21 = -0.8f;
        m.M22 = 0.6f;
        m.M41 = 0.25f;
        m.M42 = -0.5f;
    }
    if (kind == 3)
    {
        m.M33 = -8.0f / 7.0f;
        m.M34 = -1;
        m.M43 = -8.0f / 7.0f;
        m.M44 = 0;
    }
    return m;
}
std::map<std::string, std::string> ReadOracle()
{
    const std::filesystem::path relative = "tests/reference/xna40/framework/frustum-separation-oracle.txt";
    std::filesystem::path found;
    for (const auto &start : {std::filesystem::current_path(), std::filesystem::path(__FILE__).parent_path()})
        for (auto dir = start; !dir.empty(); dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative))
            {
                found = dir / relative;
                break;
            }
            if (dir == dir.root_path())
                break;
        }
    std::ifstream input(found);
    std::map<std::string, std::string> rows;
    for (std::string name; input >> name;)
    {
        if (name.starts_with('#'))
        {
            std::string rest;
            std::getline(input, rest);
            continue;
        }
        std::string codes;
        input >> codes;
        rows.emplace(name, codes);
    }
    return rows;
}
std::string Result(bool hit, ContainmentType contains, ContainmentType reverse)
{
    return std::string{hit ? '1' : '0', static_cast<char>('0' + static_cast<int>(contains)),
                       static_cast<char>('0' + static_cast<int>(reverse))};
}
constexpr float XY[]{-3, -1.5f, -1, -0.5f, 0, 0.5f, 1, 1.5f, 3};
constexpr float Z[]{-3, -0.25f, 0, 0.5f, 1, 1.25f};
} // namespace

TEST(FrustumSeparationOracleTest, SpheresMatchGenuineConvexAndContainmentResults)
{
    const auto rows = ReadOracle();
    ASSERT_EQ(rows.size(), 12U);
    for (int kind = 0; kind < 4; ++kind)
    {
        const BoundingFrustum f(Frame(kind));
        const auto &expected = rows.at("sphere" + std::to_string(kind));
        ASSERT_EQ(expected.size(), 5832U);
        std::size_t offset = 0;
        for (float x : XY)
            for (float y : XY)
                for (float z : Z)
                    for (float radius : {0.0f, 0.25f, 1.0f, 3.0f})
                    {
                        SCOPED_TRACE(::testing::Message() << "frame=" << kind << " center=" << x << ',' << y
                                                          << ',' << z << " radius=" << radius);
                        const BoundingSphere s(Vector3(x, y, z), radius);
                        bool hit;
                        ContainmentType contains;
                        f.Intersects(s, hit);
                        f.Contains(s, contains);
                        EXPECT_EQ(Result(f.Intersects(s), f.Contains(s), s.Contains(f)),
                                  expected.substr(offset, 3));
                        EXPECT_EQ(Result(hit, contains, s.Contains(f)), expected.substr(offset, 3));
                        offset += 3;
                    }
    }
}

TEST(FrustumSeparationOracleTest, BoxesMatchGenuineConvexAndContainmentResults)
{
    const auto rows = ReadOracle();
    ASSERT_EQ(rows.size(), 12U);
    for (int kind = 0; kind < 4; ++kind)
    {
        const BoundingFrustum f(Frame(kind));
        const auto &expected = rows.at("box" + std::to_string(kind));
        ASSERT_EQ(expected.size(), 4374U);
        std::size_t offset = 0;
        for (float x : XY)
            for (float y : XY)
                for (float z : Z)
                    for (float extent : {0.125f, 0.5f, 2.0f})
                    {
                        SCOPED_TRACE(::testing::Message() << "frame=" << kind << " center=" << x << ',' << y
                                                          << ',' << z << " extent=" << extent);
                        const Vector3 center(x, y, z);
                        const BoundingBox box(center - Vector3(extent), center + Vector3(extent));
                        bool hit;
                        ContainmentType contains;
                        f.Intersects(box, hit);
                        f.Contains(box, contains);
                        EXPECT_EQ(Result(f.Intersects(box), f.Contains(box), box.Contains(f)),
                                  expected.substr(offset, 3));
                        EXPECT_EQ(Result(hit, contains, box.Contains(f)), expected.substr(offset, 3));
                        offset += 3;
                    }
    }
}

TEST(FrustumSeparationOracleTest, FrustaMatchGenuineConvexAndContainmentResults)
{
    const auto rows = ReadOracle();
    ASSERT_EQ(rows.size(), 12U);
    for (int kind = 0; kind < 4; ++kind)
    {
        const BoundingFrustum f(Frame(kind));
        const auto &expected = rows.at("frustum" + std::to_string(kind));
        ASSERT_EQ(expected.size(), 540U);
        std::size_t offset = 0;
        for (int other = 0; other < 4; ++other)
            for (float x : {-3, -1, 0, 1, 3})
                for (float y : {-3, 0, 3})
                    for (float z : {-2, 0, 2})
                    {
                        SCOPED_TRACE(::testing::Message() << "frame=" << kind << " other=" << other
                                                          << " translation=" << x << ',' << y << ',' << z);
                        Matrix matrix = Frame(other);
                        matrix.M41 += x;
                        matrix.M42 += y;
                        matrix.M43 += z;
                        const BoundingFrustum rhs(matrix);
                        EXPECT_EQ(Result(f.Intersects(rhs), f.Contains(rhs), rhs.Contains(f)),
                                  expected.substr(offset, 3));
                        offset += 3;
                    }
    }
}
