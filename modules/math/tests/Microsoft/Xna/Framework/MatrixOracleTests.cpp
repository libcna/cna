// SPDX-License-Identifier: MS-PL
//
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-172: Matrix::Multiply, Matrix::Invert and
// Vector3::Transform against the genuine XNA 4.0 framework, bit for bit
// (tests/reference/xna40/framework/matrix-oracle.txt, produced by
// tools/xna-pipeline-oracle/framework/run-matrix-oracle.sh).
//
// What the measurement settled, after sixteen of the sample corpus's model references disagreed
// with CNA's only in the entries a rotation leaves near zero -- 3.5e-15 where CNA wrote 0:
//
//   * A dot product in XNA's matrix and vector arithmetic is accumulated *wider than `float`* and
//     narrowed once, when the result is stored. XNA is a 32-bit assembly and that arithmetic runs
//     on the x87 unit, which is the same reason `Vector3.Distance` answers what it does
//     (`XNASWEEP-134`). Over the committed cases a `float` accumulation reproduces 48 of 168
//     products and a wide one all 168; for `Vector3.Transform` it is 8 of 60 against 60 of 60.
//   * `CreateRotationX/Y/Z`, `MathHelper.ToRadians` and `MathHelper.ToDegrees` are *not* wide: the
//     sine and cosine are narrowed to `float` before they reach the matrix, and each conversion is
//     a single-precision multiplication by a single-precision constant -- `ToDegrees` by
//     `180f / Pi` rather than by the exactly rounded 57.29578. Both families are here so that the
//     boundary is pinned from both sides.
//
// The `cancel/*` family is the shape that made this visible: a rotation against its own inverse,
// and the conjugation `A^-1 T A` that a scene transform performs, leave entries whose value is
// nothing but the residue of the arithmetic that produced them.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    /** @brief One measured case: what the genuine framework was given and what it answered. */
    struct OracleCase
    {
        std::string name;
        std::string op;
        std::vector<std::uint32_t> a;
        std::vector<std::uint32_t> b;
        std::vector<std::uint32_t> v;
        std::vector<std::uint32_t> result;
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
        const std::filesystem::path relative = "tests/reference/xna40/framework/matrix-oracle.txt";
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
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
                if (line.empty() || line[0] == '#') { continue; }
                std::istringstream words(line);
                std::string keyword;
                words >> keyword;
                if (keyword == "case")
                {
                    OracleCase entry;
                    words >> entry.name;
                    loaded.push_back(entry);
                    continue;
                }
                if (loaded.empty()) { continue; }
                if (keyword == "op")
                {
                    words >> loaded.back().op;
                    continue;
                }
                std::vector<std::uint32_t>* into = nullptr;
                if (keyword == "a" || keyword == "angle" || keyword == "degrees") { into = &loaded.back().a; }
                else if (keyword == "b") { into = &loaded.back().b; }
                else if (keyword == "v") { into = &loaded.back().v; }
                else if (keyword == "result") { into = &loaded.back().result; }
                if (into == nullptr) { continue; }
                std::uint32_t bits = 0;
                while (words >> std::hex >> bits) { into->push_back(bits); }
            }
            return loaded;
        }();
        return cases;
    }

    [[nodiscard]] Matrix Read(const std::vector<std::uint32_t>& words)
    {
        return Matrix(FromBits(words[0]), FromBits(words[1]), FromBits(words[2]), FromBits(words[3]),
                      FromBits(words[4]), FromBits(words[5]), FromBits(words[6]), FromBits(words[7]),
                      FromBits(words[8]), FromBits(words[9]), FromBits(words[10]), FromBits(words[11]),
                      FromBits(words[12]), FromBits(words[13]), FromBits(words[14]), FromBits(words[15]));
    }

    void Entries(const Matrix& value, std::uint32_t (&out)[16])
    {
        const float source[16] = {value.M11, value.M12, value.M13, value.M14,
                                  value.M21, value.M22, value.M23, value.M24,
                                  value.M31, value.M32, value.M33, value.M34,
                                  value.M41, value.M42, value.M43, value.M44};
        for (std::size_t i = 0; i < 16u; ++i) { out[i] = ToBits(source[i]); }
    }
}

TEST(MatrixOracleTest, TheCorpusWasRead)
{
    ASSERT_FALSE(Oracle().empty()) << "no cases read from " << OracleFile().string();
    EXPECT_EQ(Oracle().size(), 444u);
    std::size_t multiplies = 0;
    std::size_t rotations = 0;
    std::size_t inversions = 0;
    std::size_t transforms = 0;
    std::size_t conversions = 0;
    for (const OracleCase& entry : Oracle())
    {
        multiplies += entry.op == "multiply" ? 1 : 0;
        rotations += entry.op.rfind("rotation", 0) == 0 ? 1 : 0;
        inversions += entry.op == "invert" ? 1 : 0;
        transforms += entry.op == "transform3" || entry.op == "transformnormal3" ? 1 : 0;
        conversions += entry.op == "toradians" || entry.op == "todegrees" ? 1 : 0;
    }
    EXPECT_EQ(multiplies, 168u);
    EXPECT_EQ(rotations, 42u);
    EXPECT_EQ(inversions, 52u);
    EXPECT_EQ(transforms, 120u);
    EXPECT_EQ(conversions, 62u);
}

TEST(MatrixOracleTest, EveryMeasuredCaseAnswersTheSameBits)
{
    std::size_t checked = 0;
    for (const OracleCase& entry : Oracle())
    {
        if (entry.op == "multiply")
        {
            std::uint32_t got[16] = {};
            Entries(Matrix::Multiply(Read(entry.a), Read(entry.b)), got);
            for (std::size_t i = 0; i < 16u; ++i)
            {
                EXPECT_EQ(got[i], entry.result[i]) << entry.name << " entry " << i;
            }
        }
        else if (entry.op == "invert")
        {
            std::uint32_t got[16] = {};
            Entries(Matrix::Invert(Read(entry.a)), got);
            for (std::size_t i = 0; i < 16u; ++i)
            {
                EXPECT_EQ(got[i], entry.result[i]) << entry.name << " entry " << i;
            }
        }
        else if (entry.op == "transform3" || entry.op == "transformnormal3")
        {
            const Vector3 vector(FromBits(entry.v[0]), FromBits(entry.v[1]), FromBits(entry.v[2]));
            const Matrix by = Read(entry.b);
            const Vector3 got = entry.op == "transform3" ? Vector3::Transform(vector, by)
                                                        : Vector3::TransformNormal(vector, by);
            EXPECT_EQ(ToBits(got.X), entry.result[0]) << entry.name << " X";
            EXPECT_EQ(ToBits(got.Y), entry.result[1]) << entry.name << " Y";
            EXPECT_EQ(ToBits(got.Z), entry.result[2]) << entry.name << " Z";
        }
        else if (entry.op.rfind("rotation", 0) == 0)
        {
            const float radians = FromBits(entry.a[0]);
            const Matrix made = entry.op == "rotationx" ? Matrix::CreateRotationX(radians)
                              : entry.op == "rotationy" ? Matrix::CreateRotationY(radians)
                                                        : Matrix::CreateRotationZ(radians);
            std::uint32_t got[16] = {};
            Entries(made, got);
            for (std::size_t i = 0; i < 16u; ++i)
            {
                EXPECT_EQ(got[i], entry.result[i]) << entry.name << " entry " << i;
            }
        }
        else if (entry.op == "toradians")
        {
            EXPECT_EQ(ToBits(MathHelper::ToRadians(FromBits(entry.a[0]))), entry.result[0])
                << entry.name;
        }
        else if (entry.op == "todegrees")
        {
            EXPECT_EQ(ToBits(MathHelper::ToDegrees(FromBits(entry.a[0]))), entry.result[0])
                << entry.name;
        }
        else
        {
            FAIL() << "unknown op " << entry.op << " in " << entry.name;
        }
        ++checked;
    }
    EXPECT_EQ(checked, Oracle().size());
}

// The negative control the campaign's own rule asks for: accumulating the dot product in `float`
// is what CNA did, it is algebraically the same, and it answers different bits on most of these
// cases. A test that passed under both would be measuring nothing.
TEST(MatrixOracleTest, ANarrowAccumulationWouldNotHavePassed)
{
    std::size_t disagreements = 0;
    std::size_t multiplies = 0;
    for (const OracleCase& entry : Oracle())
    {
        if (entry.op != "multiply") { continue; }
        ++multiplies;
        const Matrix a = Read(entry.a);
        const Matrix b = Read(entry.b);
        const float left[16] = {a.M11, a.M12, a.M13, a.M14, a.M21, a.M22, a.M23, a.M24,
                                a.M31, a.M32, a.M33, a.M34, a.M41, a.M42, a.M43, a.M44};
        const float right[16] = {b.M11, b.M12, b.M13, b.M14, b.M21, b.M22, b.M23, b.M24,
                                 b.M31, b.M32, b.M33, b.M34, b.M41, b.M42, b.M43, b.M44};
        for (std::size_t i = 0; i < 4u; ++i)
        {
            for (std::size_t j = 0; j < 4u; ++j)
            {
                const float narrow = left[i * 4 + 0] * right[0 * 4 + j] +
                                     left[i * 4 + 1] * right[1 * 4 + j] +
                                     left[i * 4 + 2] * right[2 * 4 + j] +
                                     left[i * 4 + 3] * right[3 * 4 + j];
                if (ToBits(narrow) != entry.result[i * 4 + j])
                {
                    ++disagreements;
                }
            }
        }
    }
    EXPECT_EQ(multiplies, 168u);
    EXPECT_GT(disagreements, 100u)
        << "a float accumulation reproduced the oracle, so these cases separate nothing";
}
