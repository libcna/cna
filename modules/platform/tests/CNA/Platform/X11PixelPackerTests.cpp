// SPDX-License-Identifier: MS-PL
//
// plans/plan_native_platform_validation.md NPV-0116: the software presenter's RGB-to-visual pixel
// conversion, tested with no X server at all.
//
// The presenter used to derive every channel's shift and width from the visual's masks for each
// pixel it wrote. X11PixelPacker derives them once. These tests hold the packer to exactly the
// values the per-pixel formula produced, for every channel value on each visual layout an X
// server actually offers, so the speed-up cannot have changed a single pixel.

#include <gtest/gtest.h>

#include "../../../src/X11/X11GraphicsServices.hpp"

#include <cstdint>

namespace {

using CNA::Platform::X11::X11PixelPacker;

/// The presenter's original conversion, kept verbatim as the reference.
unsigned long ReferencePackChannel(const unsigned long mask, const unsigned int value)
{
    if (mask == 0)
    {
        return 0;
    }
    int shift = 0;
    unsigned long probe = mask;
    while ((probe & 1u) == 0)
    {
        probe >>= 1;
        ++shift;
    }
    int bits = 0;
    while ((probe & 1u) != 0)
    {
        probe >>= 1;
        ++bits;
    }
    const unsigned long scaled = bits >= 8 ? (value << (bits - 8)) : (value >> (8 - bits));
    return (scaled << shift) & mask;
}

void ExpectMatchesReference(const unsigned long red, const unsigned long green,
                            const unsigned long blue)
{
    const X11PixelPacker packer(red, green, blue);
    for (unsigned int value = 0; value < 256; ++value)
    {
        // Each channel on its own, so a mismatch names the channel, then all three together.
        ASSERT_EQ(packer.Pack(value, 0, 0), ReferencePackChannel(red, value)) << "red " << value;
        ASSERT_EQ(packer.Pack(0, value, 0), ReferencePackChannel(green, value)) << "green " << value;
        ASSERT_EQ(packer.Pack(0, 0, value), ReferencePackChannel(blue, value)) << "blue " << value;
        const unsigned int other = 255u - value;
        ASSERT_EQ(packer.Pack(value, other, value / 2u),
                  ReferencePackChannel(red, value) | ReferencePackChannel(green, other) |
                      ReferencePackChannel(blue, value / 2u))
            << "mixed " << value;
    }
}

TEST(X11PixelPacking, ATwentyFourBitRgbVisualMatchesThePerPixelFormula)
{
    ExpectMatchesReference(0x00FF0000uL, 0x0000FF00uL, 0x000000FFuL);
}

TEST(X11PixelPacking, ABgrVisualMatchesThePerPixelFormula)
{
    ExpectMatchesReference(0x000000FFuL, 0x0000FF00uL, 0x00FF0000uL);
}

TEST(X11PixelPacking, ASixteenBit565VisualMatchesThePerPixelFormula)
{
    ExpectMatchesReference(0xF800uL, 0x07E0uL, 0x001FuL);
}

TEST(X11PixelPacking, AFifteenBit555VisualMatchesThePerPixelFormula)
{
    ExpectMatchesReference(0x7C00uL, 0x03E0uL, 0x001FuL);
}

TEST(X11PixelPacking, AThirtyBitVisualMatchesThePerPixelFormula)
{
    // Depth 30 (2-10-10-10): channels wider than the 8-bit source are shifted up.
    ExpectMatchesReference(0x3FF00000uL, 0x000FFC00uL, 0x000003FFuL);
}

TEST(X11PixelPacking, KnownValuesLandWhereTheVisualSaysTheyGo)
{
    // Independent of the reference formula: the colour the presenter's pixel test uses.
    EXPECT_EQ(X11PixelPacker(0x00FF0000uL, 0x0000FF00uL, 0x000000FFuL).Pack(0xFF, 0x80, 0x20),
              0x00FF8020uL);
    EXPECT_EQ(X11PixelPacker(0xF800uL, 0x07E0uL, 0x001FuL).Pack(0xFF, 0x80, 0x20),
              (0x1FuL << 11) | (0x20uL << 5) | 0x04uL);
}

TEST(X11PixelPacking, AMissingChannelContributesNothing)
{
    const X11PixelPacker packer(0x00FF0000uL, 0, 0x000000FFuL);
    EXPECT_EQ(packer.Pack(0x12, 0xFF, 0x34), 0x00120034uL);
}

} // namespace
