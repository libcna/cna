// SPDX-License-Identifier: MS-PL
//
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-137: how wide a sprite-font atlas is, and how tall.
//
// The width is not a search over the widths that fit -- CNA's was, and it put SAMPLE-070's
// `DamageFont` into a 256x64 sheet where XNA puts it into a 128x132. It is a formula: the *integer*
// square root of the glyphs' own total area, rounded up to a power of two, with no margin and no
// gap counted. Measured over every one of the 195 distinct sprite fonts the genuine pipeline
// produced for the public XNA samples: the rule answers the reference's own width on all 195, and
// with it the placement rule reproduces every glyph box of all 195. The height is the profile's, on
// this route exactly as on the description one.
//
// The sheet below is built rather than measured, because what is being tested is the arithmetic:
// glyphs whose total area is just over a power of two must push the sheet to the next one, and the
// truncation is what decides it.
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Pipeline/SpriteFontContentPipeline.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace XnaGraphics = Microsoft::Xna::Framework::Graphics;

namespace
{
    /** @brief A magenta-separated sheet of @p count opaque white cells, each @p side texels. */
    [[nodiscard]] std::vector<std::uint8_t> Sheet(const std::uint32_t count, const std::uint32_t side,
                                                  std::uint32_t& width, std::uint32_t& height)
    {
        width = count * (side + 1u) + 1u;
        height = side + 2u;
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4u);
        for (std::size_t at = 0u; at < rgba.size(); at += 4u)
        {
            rgba[at] = 255u;
            rgba[at + 1u] = 0u;
            rgba[at + 2u] = 255u;
            rgba[at + 3u] = 255u;
        }
        for (std::uint32_t cell = 0u; cell < count; ++cell)
        {
            for (std::uint32_t y = 0u; y < side; ++y)
            {
                for (std::uint32_t x = 0u; x < side; ++x)
                {
                    const std::size_t at =
                        ((static_cast<std::size_t>(y + 1u) * width) + cell * (side + 1u) + 1u + x) * 4u;
                    rgba[at] = 255u;
                    rgba[at + 1u] = 255u;
                    rgba[at + 2u] = 255u;
                    rgba[at + 3u] = 255u;
                }
            }
        }
        return rgba;
    }

    [[nodiscard]] std::uint32_t RoundUp(std::uint32_t value)
    {
        std::uint32_t rounded = 1u;
        while (rounded < value) { rounded <<= 1; }
        return rounded;
    }
}

TEST(FontSheetAtlasTest, TheWidthIsTheIntegerRootOfTheGlyphAreaRoundedUp)
{
    // 64 glyphs of 8x8 are 4,096 texels, whose root is exactly 64; 65 of them are 4,160, whose
    // integer root is 64 as well. 66 are 4,224, root 64. It takes 4,225 -- 66 glyphs of 8x8 plus
    // one more texel -- to reach 65 and so 128. The pair below straddles that: 64 glyphs answer 64
    // and 70 answer 128, and a packer that searched for the smallest sheet would answer 64 for both
    // because 70 of them fit a 64x128 as easily as a 128x64.
    for (const auto& [count, expected] : std::vector<std::pair<std::uint32_t, std::uint32_t>>{
             {4u, 16u}, {16u, 32u}, {64u, 64u}, {70u, 128u}})
    {
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        const std::vector<std::uint8_t> rgba = Sheet(count, 8u, width, height);
        const CNA::Content::Cnb::CnbSpriteFontData font = Pipeline::BuildFontFromTextureSheet(
            width, height, rgba, u' ', "sheet", XnaGraphics::GraphicsProfile::HiDef);
        ASSERT_EQ(font.glyphBounds.size(), count);
        std::uint32_t area = 0u;
        for (const auto& bounds : font.glyphBounds)
        {
            area += static_cast<std::uint32_t>(bounds.Width) * static_cast<std::uint32_t>(bounds.Height);
        }
        EXPECT_EQ(font.atlas.width, RoundUp(static_cast<std::uint32_t>(std::sqrt(
                                        static_cast<double>(area)))))
            << count << " glyphs";
        EXPECT_EQ(font.atlas.width, expected) << count << " glyphs";
    }
}

TEST(FontSheetAtlasTest, TheHeightIsTheProfilesRuleOnThisRouteToo)
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    const std::vector<std::uint8_t> rgba = Sheet(40u, 7u, width, height);
    const CNA::Content::Cnb::CnbSpriteFontData hiDef = Pipeline::BuildFontFromTextureSheet(
        width, height, rgba, u' ', "sheet", XnaGraphics::GraphicsProfile::HiDef);
    const CNA::Content::Cnb::CnbSpriteFontData reach = Pipeline::BuildFontFromTextureSheet(
        width, height, rgba, u' ', "sheet", XnaGraphics::GraphicsProfile::Reach);
    EXPECT_EQ(hiDef.atlas.width, reach.atlas.width) << "only the height is the profile's business";
    std::uint32_t used = 0u;
    for (const auto& bounds : hiDef.glyphBounds)
    {
        used = std::max(used, static_cast<std::uint32_t>(bounds.Y + bounds.Height));
    }
    ++used;  // the one-texel margin below the lowest glyph
    EXPECT_EQ(hiDef.atlas.height, (used + 3u) & ~3u);
    EXPECT_EQ(reach.atlas.height, RoundUp(used));
    EXPECT_NE(hiDef.atlas.height, reach.atlas.height)
        << "a sheet where the two rules agree tests neither";
}
