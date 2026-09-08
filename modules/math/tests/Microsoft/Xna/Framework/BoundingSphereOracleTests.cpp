// SPDX-License-Identifier: MS-PL
//
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-134: BoundingSphere::CreateFromPoints against the
// genuine XNA 4.0 framework, bit for bit
// (tests/reference/xna40/framework/bounding-sphere-oracle.txt, produced by
// tools/xna-pipeline-oracle/framework/run-bounding-sphere-oracle.sh).
//
// What the measurement settled, after 123 of the sample corpus's model references carried a mesh
// bounding-sphere centre a few parts in ten million away from XNA's:
//
//   * `Vector3.Length`, `Distance` and `DistanceSquared` accumulate `x*x + y*y + z*z` wider than
//     `float` and narrow once, because XNA is a 32-bit assembly and that arithmetic runs on the
//     x87 unit. Over 200 random pairs a `float` accumulation agrees with XNA on none of them and a
//     `double` one on all of them.
//   * The seed radius is *half the distance between the pair*, not the distance from their
//     midpoint to either end -- the two differ by an ulp often enough to change which points the
//     growth pass then touches.
//   * The growth is `r' = (r + d) / 2` with the centre slid by `(1 - r'/d)` of the difference, not
//     the algebraically equal "move by half the overshoot along the unit direction, then take the
//     radius as the distance to the point" CNA had.
//
// The `box/*` family is the shape that made this visible: a mesh's control-point list repeats its
// corners, so the growth pass keeps meeting points that lie exactly on the sphere, and whether
// each one grows it is decided in the last bit.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    /** @brief One measured case: the points handed to XNA and the sphere it answered. */
    struct OracleCase
    {
        std::string name;
        std::vector<Vector3> points;
        std::uint32_t center[3] = {0, 0, 0};
        std::uint32_t radius = 0;
    };

    [[nodiscard]] float FromBits(std::uint32_t bits)
    {
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    [[nodiscard]] std::uint32_t ToBits(float value)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    [[nodiscard]] std::filesystem::path OracleFile()
    {
        const std::filesystem::path relative =
            "tests/reference/xna40/framework/bounding-sphere-oracle.txt";
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative))
            {
                return dir / relative;
            }
            if (dir == dir.root_path())
            {
                break;
            }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative))
            {
                return dir / relative;
            }
            if (dir == dir.root_path())
            {
                break;
            }
        }
        return relative;
    }

    [[nodiscard]] const std::vector<OracleCase>& Oracle()
    {
        static const std::vector<OracleCase> cases = []
        {
            std::vector<OracleCase> loaded;
            std::ifstream in(OracleFile());
            std::string line;
            while (std::getline(in, line))
            {
                if (line.empty() || line[0] == '#')
                {
                    continue;
                }
                std::istringstream words(line);
                std::string keyword;
                words >> keyword;
                if (keyword == "case")
                {
                    OracleCase entry;
                    words >> entry.name;
                    loaded.push_back(entry);
                }
                else if (keyword == "point" && !loaded.empty())
                {
                    std::uint32_t x = 0;
                    std::uint32_t y = 0;
                    std::uint32_t z = 0;
                    words >> std::hex >> x >> y >> z;
                    loaded.back().points.emplace_back(FromBits(x), FromBits(y), FromBits(z));
                }
                else if (keyword == "result" && !loaded.empty())
                {
                    words >> std::hex >> loaded.back().center[0] >> loaded.back().center[1]
                          >> loaded.back().center[2] >> loaded.back().radius;
                }
            }
            return loaded;
        }();
        return cases;
    }
}

TEST(BoundingSphereOracleTest, TheCorpusWasRead)
{
    ASSERT_FALSE(Oracle().empty()) << "no cases read from " << OracleFile().string();
    EXPECT_EQ(Oracle().size(), 431u);
    std::size_t seeds = 0;
    std::size_t grows = 0;
    std::size_t steps = 0;
    std::size_t boxes = 0;
    for (const OracleCase& entry : Oracle())
    {
        seeds += entry.name.rfind("seed/", 0) == 0 ? 1 : 0;
        grows += entry.name.rfind("grow/", 0) == 0 ? 1 : 0;
        steps += entry.name.rfind("step/", 0) == 0 ? 1 : 0;
        boxes += entry.name.rfind("box/", 0) == 0 ? 1 : 0;
    }
    EXPECT_EQ(seeds, 200u);
    EXPECT_EQ(grows, 60u);
    EXPECT_EQ(steps, 150u);
    EXPECT_EQ(boxes, 21u);
}

TEST(BoundingSphereOracleTest, EveryMeasuredPointSetAnswersTheSameBits)
{
    std::size_t checked = 0;
    for (const OracleCase& entry : Oracle())
    {
        const BoundingSphere sphere = BoundingSphere::CreateFromPoints(entry.points);
        EXPECT_EQ(ToBits(sphere.Center.X), entry.center[0]) << entry.name << " centre X";
        EXPECT_EQ(ToBits(sphere.Center.Y), entry.center[1]) << entry.name << " centre Y";
        EXPECT_EQ(ToBits(sphere.Center.Z), entry.center[2]) << entry.name << " centre Z";
        EXPECT_EQ(ToBits(sphere.Radius), entry.radius) << entry.name << " radius";
        ++checked;
    }
    EXPECT_EQ(checked, Oracle().size());
}

// The negative control the campaign's own rule asks for: the growth CNA used to have is
// algebraically the same and answers different bits, so a test that passed under both would be
// measuring nothing. This reproduces the old form and requires that it disagrees.
TEST(BoundingSphereOracleTest, TheFormerGrowthWouldNotHavePassed)
{
    std::size_t disagreements = 0;
    for (const OracleCase& entry : Oracle())
    {
        Vector3 minx(3.402823466e+38F, 3.402823466e+38F, 3.402823466e+38F);
        Vector3 maxx = -minx;
        Vector3 miny = minx;
        Vector3 maxy = -minx;
        Vector3 minz = minx;
        Vector3 maxz = -minx;
        for (const Vector3& point : entry.points)
        {
            if (point.X < minx.X) minx = point;
            if (point.X > maxx.X) maxx = point;
            if (point.Y < miny.Y) miny = point;
            if (point.Y > maxy.Y) maxy = point;
            if (point.Z < minz.Z) minz = point;
            if (point.Z > maxz.Z) maxz = point;
        }
        const float sqx = Vector3::DistanceSquared(maxx, minx);
        const float sqy = Vector3::DistanceSquared(maxy, miny);
        const float sqz = Vector3::DistanceSquared(maxz, minz);
        Vector3 low = minx;
        Vector3 high = maxx;
        if (sqy >= sqx && sqy >= sqz) { low = miny; high = maxy; }
        if (sqz >= sqx && sqz >= sqy) { low = minz; high = maxz; }
        Vector3 center = (low + high) * 0.5f;
        float radius = Vector3::Distance(high, center);
        for (const Vector3& point : entry.points)
        {
            const Vector3 difference = point - center;
            const float squared = difference.LengthSquared();
            if (squared > radius * radius)
            {
                const float distance = std::sqrt(squared);
                const Vector3 direction = difference / distance;
                center = center + direction * ((distance - radius) * 0.5f);
                radius = Vector3::Distance(point, center);
            }
        }
        if (ToBits(center.X) != entry.center[0] || ToBits(center.Y) != entry.center[1] ||
            ToBits(center.Z) != entry.center[2] || ToBits(radius) != entry.radius)
        {
            ++disagreements;
        }
    }
    EXPECT_GT(disagreements, 100u)
        << "the former implementation reproduced the oracle, so this corpus separates nothing";
}
