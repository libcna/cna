// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-099: CNA's float-to-integer packing against what the
// genuine XNA 4.0 framework does with the same inputs
// (tests/reference/xna40/framework/framework-packing-oracle.json, produced by
// tools/xna-pipeline-oracle/framework/run-framework-oracle.sh).
//
// The measurement that started this: the content pipeline's VectorConverter rounds where CNA,
// following FNA, truncated -- Color(new Vector4(0.25f, 0.5f, 0.75f, 1)) is {64, 128, 191, 255} on
// XNA and was {63, 127, 191, 255} here. The oracle then pinned the rest of the rule: every
// float-taking Color constructor, Color.PackFromVector4 and every PackedVector constructor
// saturates the channel and rounds it to the nearest integer with ties to EVEN, and a NaN channel
// packs as 0. Color.Lerp and Color.Multiply are the exception that proves it -- they interpolate
// in byte units and truncate, which is why they are measured here too.
//
// Every case in the corpus is reproduced, and CoversEveryMeasuredCase fails if a case is added to
// the corpus without a reproduction here, so the denominator cannot drift.
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Alpha8.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgr565.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra4444.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra5551.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Byte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfSingle.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedShort2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedShort4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rg32.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba1010102.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba64.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Short2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Short4.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using namespace Microsoft::Xna::Framework::Graphics::PackedVector;

namespace
{
    constexpr float Nan = std::numeric_limits<float>::quiet_NaN();
    constexpr float Inf = std::numeric_limits<float>::infinity();

    // --------------------------------------------------------------------------------------------
    // The oracle corpus
    // --------------------------------------------------------------------------------------------
    std::filesystem::path CorpusFile()
    {
        const std::filesystem::path relative = "tests/reference/xna40/framework/framework-packing-oracle.json";
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty(); dir = dir.parent_path())
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
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty(); dir = dir.parent_path())
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

    const std::map<std::string, std::string>& Oracle()
    {
        static const std::map<std::string, std::string> cases = []
        {
            std::map<std::string, std::string> map;
            std::ifstream in(CorpusFile());
            std::string line;
            const std::regex pattern("\\{\"case\": \"([^\"]*)\", \"result\": \"((?:[^\"\\\\]|\\\\.)*)\"\\}");
            while (std::getline(in, line))
            {
                std::smatch match;
                if (std::regex_search(line, match, pattern))
                {
                    map[match[1]] = match[2];
                }
            }
            return map;
        }();
        return cases;
    }

    // --------------------------------------------------------------------------------------------
    // The oracle's formatting, reproduced
    // --------------------------------------------------------------------------------------------
    std::string Hex(std::uint64_t value, int digits)
    {
        static const char* symbols = "0123456789ABCDEF";
        std::string out(static_cast<std::size_t>(digits), '0');
        for (int i = digits - 1; i >= 0; --i)
        {
            out[static_cast<std::size_t>(i)] = symbols[value & 0xFU];
            value >>= 4;
        }
        return out;
    }

    /** @brief The oracle's Describe(Color): "R,G,B,A packed=AABBGGRR". */
    std::string Describe(const Color& color)
    {
        std::ostringstream text;
        text << static_cast<int>(color.getRProperty()) << ',' << static_cast<int>(color.getGProperty()) << ','
             << static_cast<int>(color.getBProperty()) << ',' << static_cast<int>(color.getAProperty())
             << " packed=" << Hex(color.getPackedValueProperty(), 8);
        return text.str();
    }

    Color PackedColor(const Vector4& vector)
    {
        Color color(0, 0, 0, 0);
        color.PackFromVector4(vector);
        return color;
    }

    // --------------------------------------------------------------------------------------------
    // One reproduction per measured case
    // --------------------------------------------------------------------------------------------
    const std::map<std::string, std::function<std::string()>>& Reproductions()
    {
        static const std::map<std::string, std::function<std::string()>> map = {
            // Color: the float constructors saturate and round, ties to even.
            {"color/vector4_quarters", [] { return Describe(Color(Vector4(0.25f, 0.5f, 0.75f, 1.0f))); }},
            {"color/floats_quarters", [] { return Describe(Color(0.25f, 0.5f, 0.75f, 1.0f)); }},
            {"color/vector3_quarters", [] { return Describe(Color(Vector3(0.25f, 0.5f, 0.75f))); }},
            {"color/floats_rgb_quarters", [] { return Describe(Color(0.25f, 0.5f, 0.75f)); }},
            {"color/vector4_barely_above_zero",
             [] { return Describe(Color(Vector4(0.002f, 0.001f, 0.0019f, 0.0021f))); }},
            {"color/vector4_tie_even",
             [] {
                 return Describe(Color(Vector4(126.5f / 255.0f, 127.5f / 255.0f, 128.5f / 255.0f, 129.5f / 255.0f)));
             }},
            {"color/vector4_out_of_range", [] { return Describe(Color(Vector4(2.0f, -1.0f, 0.5f, 1.0f))); }},
            {"color/floats_out_of_range", [] { return Describe(Color(2.0f, -1.0f, 0.5f, 1.0f)); }},
            {"color/vector4_nan", [] { return Describe(Color(Vector4(Nan, 0.5f, 0.5f, 0.5f))); }},
            {"color/vector4_infinities", [] { return Describe(Color(Vector4(Inf, -Inf, 0.5f, 1.0f))); }},
            {"color/vector4_extreme_finite", [] { return Describe(Color(Vector4(1e30f, -1e30f, 0.0f, 0.0f))); }},
            {"color/ints_out_of_range", [] { return Describe(Color(300, -5, 128, 255)); }},

            // Color.FromNonPremultiplied: premultiplies, then packs the same way.
            {"color/from_non_premultiplied_vector4",
             [] { return Describe(Color::FromNonPremultiplied(Vector4(1.0f, 0.0f, 0.25f, 0.5f))); }},
            {"color/from_non_premultiplied_ints",
             [] { return Describe(Color::FromNonPremultiplied(255, 0, 64, 128)); }},

            // Color.Lerp / Color.Multiply: byte-unit arithmetic that TRUNCATES, and a Lerp amount
            // clamped to [0,1].
            {"color/lerp_half",
             [] { return Describe(Color::Lerp(Color(0, 0, 0, 0), Color(255, 255, 255, 255), 0.5f)); }},
            {"color/lerp_odd_ties", [] { return Describe(Color::Lerp(Color(0, 1, 2, 3), Color(1, 2, 3, 4), 0.5f)); }},
            {"color/lerp_amount_above_one",
             [] { return Describe(Color::Lerp(Color(100, 100, 100, 100), Color(200, 200, 200, 200), 2.0f)); }},
            {"color/lerp_amount_below_zero",
             [] { return Describe(Color::Lerp(Color(100, 100, 100, 100), Color(200, 200, 200, 200), -1.0f)); }},
            {"color/multiply_half", [] { return Describe(Color::Multiply(Color(255, 255, 255, 255), 0.5f)); }},
            {"color/multiply_odd_ties", [] { return Describe(Color::Multiply(Color(1, 3, 5, 7), 0.5f)); }},
            {"color/multiply_above_range", [] { return Describe(Color::Multiply(Color(200, 200, 200, 200), 2.0f)); }},
            {"color/operator_multiply_half", [] { return Describe(Color(255, 255, 255, 255) * 0.5f); }},

            // Color.PackFromVector4: identical to the constructor, unlike FNA's.
            {"color/packfromvector4_quarters", [] { return Describe(PackedColor(Vector4(0.25f, 0.5f, 0.75f, 1.0f))); }},
            {"color/packfromvector4_out_of_range",
             [] { return Describe(PackedColor(Vector4(2.0f, -1.0f, 0.5f, 1.0f))); }},
            {"color/packfromvector4_nan", [] { return Describe(PackedColor(Vector4(Nan, 0.5f, 0.5f, 0.5f))); }},
            {"color/packfromvector4_infinities", [] { return Describe(PackedColor(Vector4(Inf, -Inf, 0.5f, 1.0f))); }},
            {"color/packfromvector4_extreme_finite",
             [] { return Describe(PackedColor(Vector4(1e30f, -1e30f, 0.0f, 0.0f))); }},

            // PackedVector, unnormalized channels.
            {"packed/Byte4/ties", [] { return Hex(Byte4(0.5f, 1.5f, 2.5f, 3.5f).getPackedValueProperty(), 8); }},
            {"packed/Byte4/fractions", [] { return Hex(Byte4(0.4f, 0.6f, 1.4f, 1.6f).getPackedValueProperty(), 8); }},
            {"packed/Byte4/vector4_quarters",
             [] { return Hex(Byte4(Vector4(0.25f, 0.5f, 0.75f, 1.0f)).getPackedValueProperty(), 8); }},
            {"packed/Byte4/out_of_range",
             [] { return Hex(Byte4(300.0f, -5.0f, 255.0f, 0.0f).getPackedValueProperty(), 8); }},
            {"packed/Byte4/nan_and_infinities",
             [] { return Hex(Byte4(Nan, Inf, -Inf, 1e30f).getPackedValueProperty(), 8); }},
            {"packed/Short2/ties", [] { return Hex(Short2(0.5f, 1.5f).getPackedValueProperty(), 8); }},
            {"packed/Short2/negative_ties", [] { return Hex(Short2(-0.5f, -1.5f).getPackedValueProperty(), 8); }},
            {"packed/Short2/out_of_range",
             [] { return Hex(Short2(40000.0f, -40000.0f).getPackedValueProperty(), 8); }},
            {"packed/Short2/nan_and_infinity", [] { return Hex(Short2(Nan, Inf).getPackedValueProperty(), 8); }},
            {"packed/Short4/ties", [] { return Hex(Short4(0.5f, 1.5f, 2.5f, 3.5f).getPackedValueProperty(), 16); }},
            {"packed/Short4/negative_ties",
             [] { return Hex(Short4(-0.5f, -1.5f, -2.5f, -3.5f).getPackedValueProperty(), 16); }},
            {"packed/Short4/out_of_range",
             [] { return Hex(Short4(40000.0f, -40000.0f, 32767.0f, -32768.0f).getPackedValueProperty(), 16); }},
            {"packed/Short4/nan_and_infinities",
             [] { return Hex(Short4(Nan, Inf, -Inf, 1e30f).getPackedValueProperty(), 16); }},

            // PackedVector, normalized channels.
            {"packed/NormalizedByte2/quarters",
             [] { return Hex(NormalizedByte2(0.25f, -0.25f).getPackedValueProperty(), 4); }},
            {"packed/NormalizedByte4/quarters",
             [] { return Hex(NormalizedByte4(0.25f, -0.25f, 0.5f, -0.5f).getPackedValueProperty(), 8); }},
            {"packed/NormalizedByte4/ties",
             [] {
                 return Hex(NormalizedByte4(0.5f / 127.0f, 1.5f / 127.0f, 2.5f / 127.0f, 3.5f / 127.0f)
                                .getPackedValueProperty(),
                            8);
             }},
            {"packed/NormalizedByte4/negative_ties",
             [] {
                 return Hex(NormalizedByte4(-0.5f / 127.0f, -1.5f / 127.0f, -2.5f / 127.0f, -3.5f / 127.0f)
                                .getPackedValueProperty(),
                            8);
             }},
            {"packed/NormalizedByte4/nan_and_infinities",
             [] { return Hex(NormalizedByte4(Nan, Inf, -Inf, 2.0f).getPackedValueProperty(), 8); }},
            {"packed/NormalizedShort2/quarters",
             [] { return Hex(NormalizedShort2(0.25f, -0.25f).getPackedValueProperty(), 8); }},
            {"packed/NormalizedShort4/quarters",
             [] { return Hex(NormalizedShort4(0.25f, -0.25f, 0.5f, -0.5f).getPackedValueProperty(), 16); }},
            {"packed/NormalizedShort4/ties",
             [] {
                 return Hex(NormalizedShort4(0.5f / 32767.0f, 1.5f / 32767.0f, 2.5f / 32767.0f, 3.5f / 32767.0f)
                                .getPackedValueProperty(),
                            16);
             }},
            {"packed/NormalizedShort4/nan_and_infinities",
             [] { return Hex(NormalizedShort4(Nan, Inf, -Inf, 2.0f).getPackedValueProperty(), 16); }},

            // PackedVector, the colour layouts.
            {"packed/Alpha8/quarters", [] { return Hex(Alpha8(0.25f).getPackedValueProperty(), 2); }},
            {"packed/Alpha8/nan", [] { return Hex(Alpha8(Nan).getPackedValueProperty(), 2); }},
            {"packed/Alpha8/ties",
             [] {
                 return Hex(Alpha8(0.5f / 255.0f).getPackedValueProperty(), 2) + "," +
                        Hex(Alpha8(1.5f / 255.0f).getPackedValueProperty(), 2) + "," +
                        Hex(Alpha8(2.5f / 255.0f).getPackedValueProperty(), 2);
             }},
            {"packed/Bgr565/quarters", [] { return Hex(Bgr565(0.25f, 0.5f, 0.75f).getPackedValueProperty(), 4); }},
            {"packed/Bgr565/ties",
             [] { return Hex(Bgr565(0.5f / 31.0f, 0.5f / 63.0f, 1.5f / 31.0f).getPackedValueProperty(), 4); }},
            {"packed/Bgra4444/quarters",
             [] { return Hex(Bgra4444(0.25f, 0.5f, 0.75f, 1.0f).getPackedValueProperty(), 4); }},
            {"packed/Bgra5551/quarters",
             [] { return Hex(Bgra5551(0.25f, 0.5f, 0.75f, 1.0f).getPackedValueProperty(), 4); }},
            {"packed/Rg32/quarters", [] { return Hex(Rg32(0.25f, 0.5f).getPackedValueProperty(), 8); }},
            {"packed/Rg32/ties",
             [] { return Hex(Rg32(0.5f / 65535.0f, 1.5f / 65535.0f).getPackedValueProperty(), 8); }},
            {"packed/Rgba64/quarters",
             [] { return Hex(Rgba64(0.25f, 0.5f, 0.75f, 1.0f).getPackedValueProperty(), 16); }},
            {"packed/Rgba1010102/quarters",
             [] { return Hex(Rgba1010102(0.25f, 0.5f, 0.75f, 1.0f).getPackedValueProperty(), 8); }},
            {"packed/Rgba1010102/ties",
             [] {
                 return Hex(Rgba1010102(0.5f / 1023.0f, 1.5f / 1023.0f, 2.5f / 1023.0f, 0.5f / 3.0f)
                                .getPackedValueProperty(),
                            8);
             }},

            // PackedVector, half-float channels: no rounding rule of their own, but they pin that
            // CNA's IEEE 754 binary16 conversion agrees with the runtime's.
            {"packed/HalfVector2/quarters", [] { return Hex(HalfVector2(0.25f, 0.5f).getPackedValueProperty(), 8); }},
            {"packed/HalfVector4/quarters",
             [] { return Hex(HalfVector4(0.25f, 0.5f, 0.75f, 1.0f).getPackedValueProperty(), 16); }},
            {"packed/HalfSingle/quarter", [] { return Hex(HalfSingle(0.25f).getPackedValueProperty(), 4); }},
        };
        return map;
    }

    /**
     * @brief Cases whose reproduction is a float list, compared numerically rather than as text.
     *
     * The oracle prints them with .NET's "R" format, whose digit count no C++ formatter matches
     * exactly; the values themselves are what is being pinned.
     */
    const std::map<std::string, std::vector<float>>& FloatReproductions()
    {
        static const std::map<std::string, std::vector<float>> map = {
            {"color/tovector4_roundtrip",
             [] {
                 const Vector4 v = Color(10, 20, 30, 40).ToVector4();
                 return std::vector<float>{v.X, v.Y, v.Z, v.W};
             }()},
            {"packed/Byte4/tovector4",
             [] {
                 const Vector4 v = Byte4(10.0f, 20.0f, 30.0f, 40.0f).ToVector4();
                 return std::vector<float>{v.X, v.Y, v.Z, v.W};
             }()},
        };
        return map;
    }

    /**
     * @brief Measured cases CNA cannot reproduce, each with the reason.
     *
     * XNA's packed-vector structs all override ToString() to print their packed value as hex;
     * CNA's do not implement ToString() at all. That is a real gap in the XNA surface of
     * modules/graphics, recorded in plans/plan_bindings_upstream.md -- not a packing difference,
     * which is what this file is about.
     */
    const std::set<std::string>& Unreproduced()
    {
        static const std::set<std::string> names = {"packed/Byte4/tostring", "packed/Short2/tostring"};
        return names;
    }

    std::vector<float> ParseFloats(const std::string& text)
    {
        std::vector<float> values;
        std::istringstream in(text);
        std::string field;
        while (std::getline(in, field, ','))
        {
            values.push_back(std::stof(field));
        }
        return values;
    }
}

TEST(XnaFrameworkPacking, CorpusIsPresent)
{
    ASSERT_FALSE(Oracle().empty()) << "expected the measured corpus at " << CorpusFile();
}

TEST(XnaFrameworkPacking, EveryMeasuredValueMatchesXna)
{
    for (const auto& [name, reproduce] : Reproductions())
    {
        const auto measured = Oracle().find(name);
        ASSERT_NE(measured, Oracle().end()) << "no measurement named " << name;
        EXPECT_EQ(reproduce(), measured->second) << "case " << name;
    }
}

TEST(XnaFrameworkPacking, EveryMeasuredVectorMatchesXna)
{
    for (const auto& [name, values] : FloatReproductions())
    {
        const auto measured = Oracle().find(name);
        ASSERT_NE(measured, Oracle().end()) << "no measurement named " << name;
        const std::vector<float> expected = ParseFloats(measured->second);
        ASSERT_EQ(values.size(), expected.size()) << "case " << name;
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            EXPECT_FLOAT_EQ(values[i], expected[i]) << "case " << name << " component " << i;
        }
    }
}

TEST(XnaFrameworkPacking, CoversEveryMeasuredCase)
{
    std::vector<std::string> uncovered;
    for (const auto& [name, result] : Oracle())
    {
        (void)result;
        if (Reproductions().count(name) == 0 && FloatReproductions().count(name) == 0 &&
            Unreproduced().count(name) == 0)
        {
            uncovered.push_back(name);
        }
    }
    EXPECT_TRUE(uncovered.empty()) << "measured cases with no reproduction here: " << [&uncovered] {
        std::string joined;
        for (const std::string& name : uncovered)
        {
            joined += (joined.empty() ? "" : ", ") + name;
        }
        return joined;
    }();
}
