// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-50/XNAP-51/XNAP-52: the `.spritefont` source route.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Pipeline/CnjContentPipeline.hpp"
#include "CNA/Content/Pipeline/SpriteFontContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnbOutputContentPipeline.hpp"
#include "CNA/Internal/Xml.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace Cnb = CNA::Content::Cnb;
namespace Xnb = CNA::Internal::Xnb;

namespace
{
    const std::filesystem::path kTestFont =
        "tests/assets/fonts/LiberationMono-Regular.ttf";

    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_spritefont_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }

        ~ScratchDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        ScratchDirectory(const ScratchDirectory&) = delete;
        ScratchDirectory& operator=(const ScratchDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    void WriteText(const std::filesystem::path& path, const std::string& text)
    {
        if (path.has_parent_path()) { std::filesystem::create_directories(path.parent_path()); }
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    /**
     * @brief Builds a complete .spritefont document.
     *
     * Every field the route reads is a parameter rather than appended extra text: the parser
     * refuses a document that declares a field twice, so a test that wants a non-default
     * `<Style>` has to replace the element rather than add a second one.
     */
    std::string SpriteFontXml(const std::string& fontName, const std::string& size = "14",
                              const std::string& style = "Regular",
                              const std::string& useKerning = "true",
                              const std::string& spacing = "0")
    {
        return R"(<?xml version="1.0" encoding="utf-8"?>
<XnaContent xmlns:Graphics="Microsoft.Xna.Framework.Content.Pipeline.Graphics">
  <Asset Type="Graphics:FontDescription">
    <FontName>)" + fontName + R"(</FontName>
    <Size>)" + size + R"(</Size>
    <Spacing>)" + spacing + R"(</Spacing>
    <UseKerning>)" + useKerning + R"(</UseKerning>
    <Style>)" + style + R"(</Style>
    <DefaultCharacter>*</DefaultCharacter>
    <CharacterRegions>
      <CharacterRegion>
        <Start>&#32;</Start>
        <End>&#126;</End>
      </CharacterRegion>
    </CharacterRegions>
  </Asset>
</XnaContent>
)";
    }

    /** @brief Stages a description plus the vendored test font into a scratch source root. */
    bool StageFontProject(const ScratchDirectory& scratch, const std::string& xml)
    {
        if (!std::filesystem::exists(kTestFont)) { return false; }
        std::filesystem::copy_file(kTestFont, scratch.Path() / kTestFont.filename(),
                                   std::filesystem::copy_options::overwrite_existing);
        WriteText(scratch.Path() / "Console.spritefont", xml);
        return true;
    }

    std::shared_ptr<const Pipeline::ContentPipelineRegistry> MakeRegistry()
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterSpriteFontSourceContentPipeline(*registry);
        Pipeline::RegisterCnjContentPipeline(*registry);
        Pipeline::RegisterXnbOutputContentPipeline(*registry, {});
        return registry;
    }

    Pipeline::ContentBuildResult BuildFont(const ScratchDirectory& scratch,
                                           const Pipeline::ContentOutputFormat format)
    {
        const Pipeline::ContentPipeline pipeline(MakeRegistry());
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = scratch.Path();
        request.source = "Console.spritefont";
        request.logicalName = "Fonts/Console";
        request.outputFormat = format;
        return pipeline.Build(request);
    }
}

// -- the XML reader (XNAP-50) ------------------------------------------------------------------

TEST(XmlReaderTest, ElementsAttributesTextAndEntitiesAreRead)
{
    const CNA::Internal::XmlElement root = CNA::Internal::ParseXml(
        R"(<?xml version="1.0"?><!-- a comment --><root a="1" b='two'>
             <child>hello &amp; goodbye</child>
             <child>&#65;&#x42;</child>
             <empty/>
           </root>)",
        "memory");

    EXPECT_EQ(root.name, "root");
    EXPECT_EQ(root.attributes.at("a"), "1");
    EXPECT_EQ(root.attributes.at("b"), "two");
    ASSERT_EQ(root.FindAll("child").size(), 2u);
    EXPECT_EQ(root.FindAll("child")[0]->TrimmedText(), "hello & goodbye");
    EXPECT_EQ(root.FindAll("child")[1]->TrimmedText(), "AB");
    EXPECT_NE(root.Find("empty"), nullptr);
    EXPECT_TRUE(root.Find("empty")->children.empty());
}

TEST(XmlReaderTest, MalformedAndUnsupportedDocumentsAreRefusedWithALocation)
{
    EXPECT_THROW((void)CNA::Internal::ParseXml("<a><b></a>", "memory"),
                 CNA::Internal::XmlParseException);
    EXPECT_THROW((void)CNA::Internal::ParseXml("<a>", "memory"),
                 CNA::Internal::XmlParseException);
    EXPECT_THROW((void)CNA::Internal::ParseXml("", "memory"),
                 CNA::Internal::XmlParseException);
    // An external entity is the classic way to make a parser read a file it was never given.
    // This one has no mechanism for it and says so rather than ignoring the declaration.
    EXPECT_THROW((void)CNA::Internal::ParseXml(
                     "<!DOCTYPE a [<!ENTITY x SYSTEM \"/etc/passwd\">]><a>&x;</a>", "memory"),
                 CNA::Internal::XmlParseException);
    EXPECT_THROW((void)CNA::Internal::ParseXml("<a>&unknown;</a>", "memory"),
                 CNA::Internal::XmlParseException);

    try
    {
        (void)CNA::Internal::ParseXml("<a>\n  <b></c>\n</a>", "Console.spritefont");
        FAIL() << "mismatched tags must be refused";
    }
    catch (const CNA::Internal::XmlParseException& error)
    {
        EXPECT_NE(std::string(error.what()).find("Console.spritefont(2,"), std::string::npos)
            << error.what();
    }
}

// -- the description (XNAP-50) -----------------------------------------------------------------

TEST(FontDescriptionTest, EveryAuthoredFieldIsRead)
{
    const Pipeline::FontDescription description = Pipeline::ParseFontDescription(
        SpriteFontXml("Segoe UI Mono", "18", "Bold, Italic", "false"), "Console.spritefont");

    EXPECT_EQ(description.fontName, "Segoe UI Mono");
    EXPECT_FLOAT_EQ(description.size, 18.0f);
    EXPECT_FLOAT_EQ(description.spacing, 0.0f);
    EXPECT_FALSE(description.useKerning);
    EXPECT_EQ(description.style, Pipeline::FontDescriptionStyle::BoldItalic);
    ASSERT_TRUE(description.defaultCharacter.has_value());
    EXPECT_EQ(*description.defaultCharacter, u'*');
    ASSERT_EQ(description.characterRegions.size(), 1u);
    EXPECT_EQ(description.characterRegions[0].start, u' ');
    EXPECT_EQ(description.characterRegions[0].end, u'~');
}

TEST(FontDescriptionTest, ADuplicatedFieldIsRefusedRatherThanSilentlyResolved)
{
    std::string xml = SpriteFontXml("LiberationMono-Regular");
    const std::size_t style = xml.find("    <Style>Regular</Style>\n");
    ASSERT_NE(style, std::string::npos);
    xml.insert(style, "    <Style>Bold</Style>\n");

    try
    {
        (void)Pipeline::ParseFontDescription(xml, "Console.spritefont");
        FAIL() << "a field declared twice must be refused";
    }
    catch (const std::runtime_error& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("<Style> appears 2 times"), std::string::npos) << message;
    }
}

TEST(FontDescriptionTest, CharacterRegionsExpandAscendingAndDeduplicated)
{
    Pipeline::FontDescription description;
    description.characterRegions = {{u'a', u'c'}, {u'b', u'd'}, {u'A', u'A'}};
    const std::vector<SharpRuntime::charcs> characters =
        Pipeline::ExpandCharacterRegions(description);
    const std::vector<SharpRuntime::charcs> expected{u'A', u'a', u'b', u'c', u'd'};
    EXPECT_EQ(characters, expected);
}

// A <DefaultCharacter> the regions do not cover joins them, which is what XNA does: measured on
// font_regions.spritefont, whose two regions cover 42 characters and whose built font carries 43
// (plans/plan_xnapipeline_parity.md XNAPP-182). Refusing was this pipeline's own idea, and the
// reason it gave -- that such a font could never draw its own fallback -- was true only because it
// declined to put the glyph there.
TEST(FontDescriptionTest, ADefaultCharacterOutsideTheRegionsJoinsThem)
{
    Pipeline::FontDescription description;
    description.characterRegions = {{u'a', u'c'}};
    description.defaultCharacter = u'?';
    const std::vector<SharpRuntime::charcs> characters =
        Pipeline::ExpandCharacterRegions(description);
    // Sorted, so the fallback lands where its code point belongs rather than at either end.
    EXPECT_EQ(characters, (std::vector<SharpRuntime::charcs>{u'?', u'a', u'b', u'c'}));
}

TEST(FontDescriptionTest, ADefaultCharacterAlreadyInTheRegionsIsNotDuplicated)
{
    Pipeline::FontDescription description;
    description.characterRegions = {{u'a', u'c'}};
    description.defaultCharacter = u'b';
    EXPECT_EQ(Pipeline::ExpandCharacterRegions(description).size(), 3u);
}

TEST(FontDescriptionTest, AMalformedDescriptionNamesTheOffendingElement)
{
    const auto expectMessage = [](const std::string& xml, const std::string& fragment)
    {
        try
        {
            (void)Pipeline::ParseFontDescription(xml, "Console.spritefont");
            FAIL() << "expected a refusal mentioning " << fragment;
        }
        catch (const std::exception& error)
        {
            EXPECT_NE(std::string(error.what()).find(fragment), std::string::npos)
                << error.what();
        }
    };

    expectMessage(R"(<XnaContent><Asset Type="Graphics:FontDescription"><Size>10</Size>
        <CharacterRegions><CharacterRegion><Start>a</Start><End>b</End></CharacterRegion>
        </CharacterRegions></Asset></XnaContent>)",
                  "<FontName>");
    expectMessage(R"(<XnaContent><Asset Type="Graphics:FontDescription"><FontName>x</FontName>
        <Size>0</Size><CharacterRegions><CharacterRegion><Start>a</Start><End>b</End>
        </CharacterRegion></CharacterRegions></Asset></XnaContent>)",
                  "<Size>");
    expectMessage(R"(<XnaContent><Asset Type="Graphics:FontDescription"><FontName>x</FontName>
        <Size>10</Size></Asset></XnaContent>)",
                  "<CharacterRegions>");
    expectMessage(R"(<XnaContent><Asset Type="Graphics:Texture2DContent"/></XnaContent>)",
                  "not a font description");
    expectMessage(R"(<XnaContent><Asset Type="Graphics:FontDescription"><FontName>x</FontName>
        <Size>10</Size><CharacterRegions><CharacterRegion><Start>z</Start><End>a</End>
        </CharacterRegion></CharacterRegions></Asset></XnaContent>)",
                  "ends before it starts");
}

// -- rasterization and the end-to-end route (XNAP-51) ------------------------------------------

TEST(SpriteFontSourceRouteTest, ASpriteFontDescriptionBuildsToSpriteFontXnb)
{
    if (!Pipeline::IsFontRasterizationAvailable())
    {
        GTEST_SKIP() << "this build has no font rasterizer";
    }
    ScratchDirectory scratch("xnb");
    if (!StageFontProject(scratch, SpriteFontXml("LiberationMono-Regular")))
    {
        GTEST_SKIP() << "the vendored test font is missing";
    }

    const Pipeline::ContentBuildResult result =
        BuildFont(scratch, Pipeline::ContentOutputFormat::Xnb);
    EXPECT_EQ(result.importer.name, "CNA.FontDescriptionImporter");
    EXPECT_EQ(result.processor.name, "CNA.FontDescriptionProcessor");
    EXPECT_EQ(result.writer.name, "CNA.XnbSpriteFontWriter");

    // The font file is a build input, so replacing it rebuilds the font.
    bool dependsOnFont = false;
    for (const Pipeline::ContentDependency& dependency : result.dependencies)
    {
        if (dependency.identity.find("LiberationMono-Regular.ttf") != std::string::npos)
        {
            dependsOnFont = true;
        }
    }
    EXPECT_TRUE(dependsOnFont);

    const std::filesystem::path path = scratch.Path() / "Console.xnb";
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(result.output.bytes.data()),
                     static_cast<std::streamsize>(result.output.bytes.size()));
    }
    const Xnb::XnbCanonicalAsset asset = Xnb::DecodeXnbCanonicalAsset(path);
    EXPECT_EQ(asset.rootReader, "Microsoft.Xna.Framework.Content.SpriteFontReader");
    const auto& font = std::get<Xnb::XnbSpriteFontData>(asset.value);

    EXPECT_EQ(font.characters.size(), 95u);
    EXPECT_EQ(font.characters.front(), u' ');
    EXPECT_EQ(font.characters.back(), u'~');
    EXPECT_EQ(font.glyphs.size(), font.characters.size());
    EXPECT_EQ(font.cropping.size(), font.characters.size());
    EXPECT_EQ(font.kerning.size(), font.characters.size());
    EXPECT_GT(font.lineSpacing, 0);
    ASSERT_TRUE(font.defaultCharacter.has_value());
    EXPECT_EQ(*font.defaultCharacter, u'*');
    EXPECT_GT(font.atlas.width, 0u);
    EXPECT_EQ(font.atlas.mipCount, 1u);
}

TEST(SpriteFontSourceRouteTest, GlyphMetricsHaveTheShapeSpriteBatchConsumes)
{
    if (!Pipeline::IsFontRasterizationAvailable())
    {
        GTEST_SKIP() << "this build has no font rasterizer";
    }
    ScratchDirectory scratch("metrics");
    if (!StageFontProject(scratch, SpriteFontXml("LiberationMono-Regular")))
    {
        GTEST_SKIP() << "the vendored test font is missing";
    }

    Pipeline::FontDescription description =
        Pipeline::ParseFontDescription(SpriteFontXml("LiberationMono-Regular"), "memory");
    description.resolvedFontFile = scratch.Path() / "LiberationMono-Regular.ttf";
    std::vector<std::string> warnings;
    const Cnb::CnbSpriteFontData font =
        Pipeline::RasterizeFontDescription(description, warnings);
    EXPECT_TRUE(warnings.empty()) << warnings.front();

    ASSERT_EQ(font.glyphBounds.size(), font.characters.size());
    const int cellHeight = font.cropping.front().Height;
    for (std::size_t index = 0u; index < font.characters.size(); ++index)
    {
        const Microsoft::Xna::Framework::Rectangle& glyph = font.glyphBounds[index];
        const Microsoft::Xna::Framework::Rectangle& cropping = font.cropping[index];
        const Microsoft::Xna::Framework::Vector3& kerning = font.kerning[index];

        // Every glyph rectangle lies inside the atlas.
        EXPECT_GE(glyph.X, 0);
        EXPECT_GE(glyph.Y, 0);
        EXPECT_GT(glyph.Width, 0);
        EXPECT_GT(glyph.Height, 0);
        EXPECT_LE(static_cast<std::uint32_t>(glyph.X + glyph.Width), font.atlas.width);
        EXPECT_LE(static_cast<std::uint32_t>(glyph.Y + glyph.Height), font.atlas.height);

        // Kerning is the ABC triple SpriteBatch advances by: B is the ink width the glyph
        // rectangle also states, and the three sum to a non-negative advance.
        EXPECT_FLOAT_EQ(kerning.Y, static_cast<float>(glyph.Width));
        EXPECT_GE(kerning.X + kerning.Y + kerning.Z, 0.0f);

        // Cropping's height is the font-wide cell every glyph shares -- MeasureString takes a
        // line's height from it -- and its width is the ink width.
        EXPECT_EQ(cropping.Height, cellHeight);
        EXPECT_EQ(cropping.Width, glyph.Width);
        EXPECT_GE(cropping.Y, 0);
    }

    // A space has no ink, so it gets a single transparent texel rather than a zero-width
    // rectangle no runtime could sample.
    const Microsoft::Xna::Framework::Rectangle& space = font.glyphBounds.front();
    EXPECT_EQ(space.Width, 1);
    EXPECT_EQ(space.Height, 1);
    EXPECT_GT(font.kerning.front().Z, 0.0f);
}

TEST(SpriteFontSourceRouteTest, DisablingKerningFoldsTheBearingsIntoTheAdvance)
{
    if (!Pipeline::IsFontRasterizationAvailable())
    {
        GTEST_SKIP() << "this build has no font rasterizer";
    }
    ScratchDirectory scratch("nokerning");
    if (!StageFontProject(scratch, SpriteFontXml("LiberationMono-Regular")))
    {
        GTEST_SKIP() << "the vendored test font is missing";
    }

    Pipeline::FontDescription description = Pipeline::ParseFontDescription(
        SpriteFontXml("LiberationMono-Regular", "14", "Regular", "false"), "memory");
    description.resolvedFontFile = scratch.Path() / "LiberationMono-Regular.ttf";
    std::vector<std::string> warnings;
    const Cnb::CnbSpriteFontData font =
        Pipeline::RasterizeFontDescription(description, warnings);

    for (const Microsoft::Xna::Framework::Vector3& kerning : font.kerning)
    {
        EXPECT_FLOAT_EQ(kerning.X, 0.0f);
        EXPECT_FLOAT_EQ(kerning.Z, 0.0f);
        EXPECT_GE(kerning.Y, 0.0f);
    }
}

TEST(SpriteFontSourceRouteTest, TheSameDescriptionAlsoBuildsToCnb)
{
    if (!Pipeline::IsFontRasterizationAvailable())
    {
        GTEST_SKIP() << "this build has no font rasterizer";
    }
    ScratchDirectory scratch("cnb");
    if (!StageFontProject(scratch, SpriteFontXml("LiberationMono-Regular")))
    {
        GTEST_SKIP() << "the vendored test font is missing";
    }

    const Pipeline::ContentBuildResult cnb =
        BuildFont(scratch, Pipeline::ContentOutputFormat::Cnb);
    EXPECT_EQ(cnb.writer.name, "CNA.SpriteFontContentWriter");
    ASSERT_GE(cnb.output.bytes.size(), 3u);
    EXPECT_EQ(cnb.output.bytes[0], 'C');
}

TEST(SpriteFontSourceRouteTest, RasterizationIsDeterministic)
{
    if (!Pipeline::IsFontRasterizationAvailable())
    {
        GTEST_SKIP() << "this build has no font rasterizer";
    }
    ScratchDirectory scratch("deterministic");
    if (!StageFontProject(scratch, SpriteFontXml("LiberationMono-Regular")))
    {
        GTEST_SKIP() << "the vendored test font is missing";
    }
    EXPECT_EQ(BuildFont(scratch, Pipeline::ContentOutputFormat::Xnb).output.bytes,
              BuildFont(scratch, Pipeline::ContentOutputFormat::Xnb).output.bytes);
}

TEST(SpriteFontSourceRouteTest, AFontFileThatIsNotThereIsRefusedWithTheAttemptsListed)
{
    ScratchDirectory scratch("missing");
    WriteText(scratch.Path() / "Console.spritefont", SpriteFontXml("NoSuchTypefaceAnywhere"));

    try
    {
        (void)BuildFont(scratch, Pipeline::ContentOutputFormat::Xnb);
        FAIL() << "an unresolvable font must be refused";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("NoSuchTypefaceAnywhere"), std::string::npos) << message;
        EXPECT_NE(message.find("NoSuchTypefaceAnywhere.ttf"), std::string::npos) << message;
        EXPECT_NE(message.find("NoSuchTypefaceAnywhere.otf"), std::string::npos) << message;
        EXPECT_NE(message.find("NoSuchTypefaceAnywhere.ttc"), std::string::npos) << message;
        // The diagnostic has to explain the difference from XNA, not just report a missing file:
        // XNA names a Windows font family, CNA names a file beside the description.
        EXPECT_NE(message.find("same bytes on every machine"), std::string::npos) << message;
    }
}

TEST(SpriteFontSourceRouteTest, AFontMissingARequestedGlyphIsRefusedByCodePoint)
{
    if (!Pipeline::IsFontRasterizationAvailable())
    {
        GTEST_SKIP() << "this build has no font rasterizer";
    }
    ScratchDirectory scratch("missing_glyph");
    if (!StageFontProject(scratch, SpriteFontXml("LiberationMono-Regular")))
    {
        GTEST_SKIP() << "the vendored test font is missing";
    }

    Pipeline::FontDescription description;
    description.fontName = "LiberationMono-Regular";
    description.size = 12.0f;
    // U+4E00, a CJK ideograph a Latin monospace face does not carry.
    description.characterRegions = {{static_cast<SharpRuntime::charcs>(0x4E00),
                                     static_cast<SharpRuntime::charcs>(0x4E00)}};
    description.resolvedFontFile = scratch.Path() / "LiberationMono-Regular.ttf";

    std::vector<std::string> warnings;
    try
    {
        (void)Pipeline::RasterizeFontDescription(description, warnings);
        FAIL() << "a missing glyph must be refused";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string(error.what()).find("U+4E00"), std::string::npos) << error.what();
    }
}

TEST(SpriteFontSourceRouteTest, ASynthesizedStyleIsReportedRatherThanPassedOffAsTheRealFace)
{
    if (!Pipeline::IsFontRasterizationAvailable())
    {
        GTEST_SKIP() << "this build has no font rasterizer";
    }
    ScratchDirectory scratch("style");
    if (!StageFontProject(scratch, SpriteFontXml("LiberationMono-Regular")))
    {
        GTEST_SKIP() << "the vendored test font is missing";
    }

    Pipeline::FontDescription description = Pipeline::ParseFontDescription(
        SpriteFontXml("LiberationMono-Regular", "14", "Bold"), "memory");
    description.resolvedFontFile = scratch.Path() / "LiberationMono-Regular.ttf";
    std::vector<std::string> warnings;
    const Cnb::CnbSpriteFontData font =
        Pipeline::RasterizeFontDescription(description, warnings);
    EXPECT_FALSE(font.characters.empty());
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings.front().find("synthesized"), std::string::npos) << warnings.front();
}

TEST(SpriteFontSourceRouteTest, AnAtlasThatCannotFitIsRefusedWithTheSizeThatCausedIt)
{
    if (!Pipeline::IsFontRasterizationAvailable())
    {
        GTEST_SKIP() << "this build has no font rasterizer";
    }
    ScratchDirectory scratch("too_big");
    if (!StageFontProject(scratch, SpriteFontXml("LiberationMono-Regular")))
    {
        GTEST_SKIP() << "the vendored test font is missing";
    }

    Pipeline::FontDescription description;
    description.fontName = "LiberationMono-Regular";
    description.size = 900.0f;
    description.characterRegions = {{u' ', u'~'}};
    description.resolvedFontFile = scratch.Path() / "LiberationMono-Regular.ttf";

    std::vector<std::string> warnings;
    try
    {
        (void)Pipeline::RasterizeFontDescription(description, warnings);
        FAIL() << "an oversized atlas must be refused";
    }
    catch (const std::runtime_error& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("2048"), std::string::npos) << message;
        EXPECT_NE(message.find("<Size>"), std::string::npos) << message;
    }
}
