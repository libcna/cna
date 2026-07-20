// SPDX-License-Identifier: MS-PL
//
// REMED-CONTENT-009: CheckedMultiplyOrThrow() is the shared overflow-safe-multiplication helper
// extracted while fixing a signed-integer-overflow UB finding (confirmed by UBSan) inside
// Texture2DContentTypeReader.cpp's own REMED-CONTENT-001 decoded-byte-size validation. These
// tests exercise the helper directly, independent of any one reader's byte-stream plumbing.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "CNA/Internal/Xnb/XnbArithmetic.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using CNA::Internal::Xnb::CheckedMultiplyOrThrow;
using Microsoft::Xna::Framework::Content::ContentLoadException;

TEST(XnbArithmeticTest, EmptyFactorListReturnsMultiplicativeIdentity)
{
    EXPECT_EQ(CheckedMultiplyOrThrow({}, "test"), 1);
}

TEST(XnbArithmeticTest, SingleFactorReturnsItself)
{
    EXPECT_EQ(CheckedMultiplyOrThrow({42}, "test"), 42);
}

TEST(XnbArithmeticTest, SimpleProductComputesCorrectly)
{
    EXPECT_EQ(CheckedMultiplyOrThrow({4, 4, 4}, "test"), 64);
}

TEST(XnbArithmeticTest, AnyZeroFactorProducesZeroWithoutThrowing)
{
    EXPECT_EQ(CheckedMultiplyOrThrow({0, 2147483647LL, 4}, "test"), 0);
}

// The exact shape that triggered REMED-CONTENT-009: two factors each near INT32_MAX multiply to a
// value still representable in int64_t (~4.6e18, under INT64_MAX's ~9.22e18), but the "* 4" a
// texture reader appends for the bytes-per-pixel factor pushes the running product over INT64_MAX
// -- undefined behavior with raw multiplication, a clean exception here.
TEST(XnbArithmeticTest, TwoInt32MaxFactorsThenMultiplyByFourOverflowsAndThrows)
{
    constexpr int64_t maxInt32 = std::numeric_limits<int32_t>::max();
    EXPECT_THROW(CheckedMultiplyOrThrow({maxInt32, maxInt32, 4}, "TestReader"), ContentLoadException);
}

// The two-factor product ALONE (no trailing "* 4") does NOT overflow int64_t -- confirms the
// helper doesn't over-reject a case that is genuinely representable, only the case that actually
// overflows.
TEST(XnbArithmeticTest, TwoInt32MaxFactorsAloneDoesNotOverflow)
{
    constexpr int64_t maxInt32 = std::numeric_limits<int32_t>::max();
    const int64_t expected = maxInt32 * maxInt32; // computed here in a context where it's known safe
    EXPECT_EQ(CheckedMultiplyOrThrow({maxInt32, maxInt32}, "test"), expected);
}

TEST(XnbArithmeticTest, ThreeInt32MaxFactorsOverflowsAndThrows)
{
    // Texture3DReader's own shape: width * height * depth * 4, one more factor than
    // Texture2DReader/TextureCubeReader -- confirms the helper scales to more than 2 real factors.
    constexpr int64_t maxInt32 = std::numeric_limits<int32_t>::max();
    EXPECT_THROW(CheckedMultiplyOrThrow({maxInt32, maxInt32, maxInt32, 4}, "Texture3DReader"),
                 ContentLoadException);
}

TEST(XnbArithmeticTest, ProductExactlyAtInt64MaxDoesNotThrow)
{
    constexpr int64_t maxInt64 = std::numeric_limits<int64_t>::max();
    EXPECT_EQ(CheckedMultiplyOrThrow({maxInt64, 1}, "test"), maxInt64);
}

TEST(XnbArithmeticTest, ProductOneOverInt64MaxThrows)
{
    // INT64_MAX is odd, so INT64_MAX/2 (integer division, floors) times 2 is one less than
    // INT64_MAX -- multiplying by 3 instead pushes just past the boundary.
    constexpr int64_t maxInt64 = std::numeric_limits<int64_t>::max();
    const int64_t half = maxInt64 / 2;
    EXPECT_THROW(CheckedMultiplyOrThrow({half, 3}, "test"), ContentLoadException);
}

TEST(XnbArithmeticTest, NegativeFactorThrows)
{
    EXPECT_THROW(CheckedMultiplyOrThrow({-1, 5}, "test"), ContentLoadException);
}

TEST(XnbArithmeticTest, ExceptionMessageIncludesReaderName)
{
    constexpr int64_t maxInt32 = std::numeric_limits<int32_t>::max();
    try
    {
        CheckedMultiplyOrThrow({maxInt32, maxInt32, 4}, "MyReaderName");
        FAIL() << "expected ContentLoadException";
    }
    catch (const ContentLoadException& e)
    {
        EXPECT_NE(std::string(e.what()).find("MyReaderName"), std::string::npos);
    }
}
