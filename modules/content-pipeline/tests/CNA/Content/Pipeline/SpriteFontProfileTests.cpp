// SPDX-License-Identifier: MS-PL
//
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-104/105: the two rules the sample corpus settled
// about a `.spritefont`, both of which change the bytes of every font built.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Pipeline/SpriteFontContentPipeline.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace XnaGraphics = Microsoft::Xna::Framework::Graphics;

namespace
{
    const std::filesystem::path kProfileTestFont =
        "tests/assets/fonts/LiberationMono-Regular.ttf";

    class Scratch
    {
    public:
        explicit Scratch(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_fontprofile_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }
        ~Scratch()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }
        Scratch(const Scratch&) = delete;
        Scratch& operator=(const Scratch&) = delete;
        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    Pipeline::FontDescription Description(const std::filesystem::path& font, const float size)
    {
        Pipeline::FontDescription description;
        description.fontName = "Liberation Mono";
        description.size = size;
        description.useKerning = true;
        description.characterRegions = {{u' ', u'~'}};
        description.resolvedFontFile = font;
        return description;
    }
}

// The height of a sprite-font atlas is the graphics profile's rule, not the packer's. Measured
// over the 196 distinct atlases the genuine pipeline produced for the public XNA samples: of the
// 186 whose two candidate answers differ, every Reach one is the packed height rounded up to a
// power of two (119) and every HiDef one is it rounded up to four (67), with no exception either
// way. CNA rounded to a power of two whatever the profile said, so a HiDef font carried a
// mostly-empty sheet: the CardsStarterKit sample's three are 256x144, 256x196 and 256x120 in XNA's
// own build against a 256x256 here, 70,830 bytes of `.xnb` where XNA writes 42,158.
TEST(SpriteFontProfileTest, TheAtlasHeightIsTheProfilesRule)
{
    if (!Pipeline::IsFontRasterizationAvailable())
    {
        GTEST_SKIP() << "this build has no font rasterizer";
    }
    if (!std::filesystem::exists(kProfileTestFont))
    {
        GTEST_SKIP() << "the vendored test font is missing";
    }
    std::vector<std::string> warnings;
    const Pipeline::FontDescription description = Description(kProfileTestFont, 14.0f);

    const CNA::Content::Cnb::CnbSpriteFontData reach = Pipeline::RasterizeFontDescription(
        description, warnings, Pipeline::ContentStrictness::Strict, XnaGraphics::GraphicsProfile::Reach);
    const CNA::Content::Cnb::CnbSpriteFontData hiDef = Pipeline::RasterizeFontDescription(
        description, warnings, Pipeline::ContentStrictness::Strict, XnaGraphics::GraphicsProfile::HiDef);

    EXPECT_EQ(reach.atlas.width, hiDef.atlas.width) << "only the height is the profile's business";
    std::uint32_t used = 0u;
    for (const auto& bounds : hiDef.glyphBounds)
    {
        used = std::max(used, static_cast<std::uint32_t>(bounds.Y + bounds.Height));
    }
    ASSERT_GT(used, 0u);
    EXPECT_EQ(hiDef.atlas.height % 4u, 0u);
    EXPECT_LT(hiDef.atlas.height, used + 4u) << "HiDef is the packed height rounded up to four";
    EXPECT_GE(hiDef.atlas.height, used);

    EXPECT_EQ(reach.atlas.height & (reach.atlas.height - 1u), 0u)
        << "Reach is the packed height rounded up to a power of two";
    EXPECT_GE(reach.atlas.height, hiDef.atlas.height);
}

// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-105. `<FontName>` is matched against the `name`
// table's entry 1 -- the family Windows matches a LOGFONT against -- and not against FreeType's
// own `family_name`, which prefers the *typographic* family (entry 16) where a font declares one.
// `Moire-ExtraBold.ttf` declares family 'Moire ExtraBold' and typographic family 'Moire', so the
// second reading answers 'Moire' with the extra-bold face and the CardsStarterKit sample's regular
// font came out extra bold: 2 of its 95 glyphs the size XNA's own build gave them.
TEST(SpriteFontProfileTest, AFamilyIsMatchedAgainstTheNameWindowsMatches)
{
    if (!Pipeline::IsFontRasterizationAvailable())
    {
        GTEST_SKIP() << "this build has no font rasterizer";
    }
    if (!std::filesystem::exists(kProfileTestFont))
    {
        GTEST_SKIP() << "the vendored test font is missing";
    }
    const Scratch scratch("family_id1");
    std::filesystem::copy_file(kProfileTestFont, scratch.Path() / "font.ttf",
                               std::filesystem::copy_options::overwrite_existing);
    // The vendored font's entry 1 is 'Liberation Mono' and it declares no typographic family, so
    // both readings agree on it; what the rule has to answer is that the *file name* is not the
    // family, and that a family it does not declare is not found.
    EXPECT_EQ(Pipeline::FindFontFamilyBeside(scratch.Path(), "Liberation Mono").filename().string(),
              "font.ttf");
    EXPECT_TRUE(Pipeline::FindFontFamilyBeside(scratch.Path(), "font").empty());
    EXPECT_TRUE(Pipeline::FindFontFamilyBeside(scratch.Path(), "Liberation").empty())
        << "a family name is matched whole, not as a prefix";
}
