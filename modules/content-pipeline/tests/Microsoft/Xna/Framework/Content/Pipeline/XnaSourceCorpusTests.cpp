// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-021: every declared source extension reads its own
// committed file.
//
// The input-parity gate (tools/xna-pipeline-oracle/inputs_matrix.py) asks each of the eighteen
// extensions XNA declares for a fixture on disk and a test that reads it. The nine texture ones
// are XnaTextureExtensionTests; the five media ones are their importers' own tests. These are the
// four whose sources are text and whose importers were, until now, only ever handed bytes a test
// had just written -- so nothing held the committed corpus to being readable at all.
//
// Each case also pins the importer attribute against what the genuine assemblies declare
// (tests/reference/xna40/content-pipeline-api.json, read into
// tests/reference/xna40/content-pipeline-inputs.json), because a display name or a default
// processor that drifts is invisible until a host shows it to a user.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <cstddef>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/EffectImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/FontDescriptionImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/EffectContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/FontDescription.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/TextureImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/WavImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/XmlImporter.hpp"

namespace Xna = Microsoft::Xna::Framework::Content::Pipeline;
namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;

namespace
{
    std::filesystem::path Locate(const std::filesystem::path& relative)
    {
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

    class ImporterContext final : public Xna::ContentImporterContext
    {
    public:
        std::vector<std::string> dependencies;
        [[nodiscard]] std::string getIntermediateDirectoryProperty() const override { return "obj"; }
        [[nodiscard]] Xna::ContentBuildLogger& getLoggerProperty() const override
        {
            return const_cast<SilentLogger&>(logger_);
        }
        [[nodiscard]] std::string getOutputDirectoryProperty() const override { return "bin"; }
        void AddDependency(const std::string& filename) override { dependencies.push_back(filename); }

    private:
        class SilentLogger final : public Xna::ContentBuildLogger
        {
        protected:
            void LogMessage(const std::string&) override {}
            void LogImportantMessage(const std::string&) override {}
            void LogWarning(const std::string&, const Xna::ContentIdentity&, const std::string&) override {}
        };

        SilentLogger logger_;
    };
}

TEST(XnaSourceCorpus, TheCommittedSpriteFontReadsEveryElementItNames)
{
    ImporterContext context;
    Xna::FontDescriptionImporter importer;
    const std::shared_ptr<Graphics::FontDescription> description =
        importer.Import(Locate("tests/assets/xna40/source/probe.spritefont").string(), context);
    ASSERT_NE(description, nullptr);
    EXPECT_EQ(description->getFontNameProperty(), "Segoe UI Mono");
    EXPECT_FLOAT_EQ(description->getSizeProperty(), 14.0f);
    EXPECT_FLOAT_EQ(description->getSpacingProperty(), 2.0f);
    EXPECT_TRUE(description->getUseKerningProperty());
    EXPECT_EQ(description->getStyleProperty(), Graphics::FontDescriptionStyle::Bold);
    ASSERT_TRUE(description->getDefaultCharacterProperty().has_value());
    EXPECT_EQ(*description->getDefaultCharacterProperty(), u'*');
    // Two regions: the printable ASCII range and the upper half of Latin-1.
    EXPECT_EQ(description->getCharactersProperty().size(),
              static_cast<std::size_t>((126 - 32 + 1) + (255 - 161 + 1)));
    EXPECT_TRUE(context.dependencies.empty());
}

TEST(XnaSourceCorpus, TheCommittedEffectReadsAsItsOwnSource)
{
    ImporterContext context;
    Xna::EffectImporter importer;
    const std::shared_ptr<Graphics::EffectContent> effect =
        importer.Import(Locate("tests/assets/xna40/source/probe.fx").string(), context);
    ASSERT_NE(effect, nullptr);
    ASSERT_TRUE(effect->getEffectCodeProperty().has_value());
    const std::string& source = *effect->getEffectCodeProperty();
    EXPECT_NE(source.find("technique Textured"), std::string::npos);
    EXPECT_NE(source.find("compile ps_2_0 PixelMain()"), std::string::npos);
    // The importer reads the file and nothing else: the source is the file, byte for byte.
    std::ifstream file(Locate("tests/assets/xna40/source/probe.fx"), std::ios::binary);
    const std::string whole((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_EQ(source, whole);
}

TEST(XnaSourceCorpus, TheCommittedXmlBuildsWhateverItsAssetTypeNames)
{
    ImporterContext context;
    Xna::XmlImporter importer;
    const Xna::ContentObject object =
        importer.Import(Locate("tests/assets/xna40/source/probe.xml").string(), context);
    const std::vector<std::string> list = Xna::Unbox<std::vector<std::string>>(object);
    ASSERT_EQ(list.size(), 3u);
    EXPECT_EQ(list[0], "first");
    EXPECT_EQ(list[1], "second");
    EXPECT_EQ(list[2], "third");
    // XNA's XmlImporter adds no dependency (measured, intermediate corpus importer_* cases).
    EXPECT_TRUE(context.dependencies.empty());
}

TEST(XnaSourceCorpus, TheCommittedWavReadsAsTheToneItCarries)
{
    ImporterContext context;
    Xna::WavImporter importer;
    const auto audio =
        importer.Import(Locate("tests/assets/xna40/media/tone_mono_44100.wav").string(), context);
    ASSERT_NE(audio, nullptr);
    EXPECT_EQ(audio->getFormatProperty()->getChannelCountProperty(), 1);
    EXPECT_EQ(audio->getFormatProperty()->getSampleRateProperty(), 44100);
    EXPECT_EQ(audio->getFormatProperty()->getBitsPerSampleProperty(), 16);
    // Half a second of 16-bit mono at 44100 is 22050 frames, whatever the encoder wrote around it.
    EXPECT_EQ(audio->getDurationProperty().getTicksProperty(), 500 * 10000);
}

// The attribute each importer declares, against the strings the genuine assemblies carry. XNA's
// display names all end in " - XNA Framework", and a host shows them to a user, so a drift here is
// visible and was not caught by anything until this test.
TEST(XnaSourceCorpus, EveryImporterDeclaresTheAttributeXnaDeclares)
{
    const auto check = [](const Xna::ContentImporterAttribute& attribute,
                          const std::vector<std::string>& extensions, const std::string& displayName,
                          const std::string& defaultProcessor, bool cacheImportedData)
    {
        EXPECT_EQ(attribute.getFileExtensionsProperty(), extensions);
        EXPECT_EQ(attribute.getDisplayNameProperty(), displayName);
        EXPECT_EQ(attribute.getDefaultProcessorProperty(), defaultProcessor);
        EXPECT_EQ(attribute.getCacheImportedDataProperty(), cacheImportedData);
    };
    check(Xna::TextureImporter::Attribute(),
          {".bmp", ".dds", ".dib", ".hdr", ".jpg", ".pfm", ".png", ".ppm", ".tga"},
          "Texture - XNA Framework", "SpriteTextureProcessor", false);
    check(Xna::FontDescriptionImporter::Attribute(), {".spritefont"},
          "Sprite Font Description - XNA Framework", "FontDescriptionProcessor", false);
    check(Xna::EffectImporter::Attribute(), {".fx"}, "Effect - XNA Framework", "EffectProcessor", false);
    check(Xna::WavImporter::Attribute(), {".wav"}, "WAV Audio File - XNA Framework",
          "SoundEffectProcessor", false);
    // XmlImporter is the one built-in that declares no default processor at all.
    check(Xna::XmlImporter::Attribute(), {".xml"}, "XML Content - XNA Framework", "", false);
}
