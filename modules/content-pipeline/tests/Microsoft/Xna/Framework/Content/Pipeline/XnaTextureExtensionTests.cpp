// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-167: the per-extension source differential.
//
// Every extension TextureImporter declares gets a committed fixture in tests/assets/xna40/texture,
// and both the genuine XNA importer and this one read the same bytes -- which is the point. The
// earlier comparison had each side synthesize its own file, so it silently assumed the two
// encoders agreed; a JPEG makes that assumption false by construction, and a Radiance picture
// makes it false in a way that changes the answer's pixel type.
//
// The expectations are tests/reference/xna40/graphics/graphics-content-oracle.json, cases
// textureext/* (the importer and its default processors), textureprop/* (every TextureProcessor
// property over the same corpus) and textureprofile/* (the same under each target and profile).
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <functional>
#include <tuple>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/PixelBitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/TextureContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "System/NotSupportedException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/TextureProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/TextureImporter.hpp"

namespace Xna = Microsoft::Xna::Framework::Content::Pipeline;
namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector4;
using Xna::InvalidContentException;
using Xna::TextureImporter;
namespace Processors = Microsoft::Xna::Framework::Content::Pipeline::Processors;

namespace
{
    /** @brief Walks up from the working directory, then from this file, to find a repository path. */
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

    std::string Unescape(const std::string& text)
    {
        std::string out;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '\\' && i + 1 < text.size())
            {
                const char next = text[++i];
                out += next == 'n' ? '\n' : next == 'r' ? '\r' : next;
            }
            else
            {
                out += text[i];
            }
        }
        return out;
    }

    std::string Expected(const std::string& name)
    {
        static const std::map<std::string, std::string> cases = []
        {
            std::map<std::string, std::string> map;
            std::ifstream in(Locate("tests/reference/xna40/graphics/graphics-content-oracle.json"));
            std::string line;
            const std::regex pattern("\\{\"case\": \"([^\"]*)\", \"result\": \"((?:[^\"\\\\]|\\\\.)*)\"\\}");
            while (std::getline(in, line))
            {
                std::smatch match;
                if (std::regex_search(line, match, pattern))
                {
                    map[match[1]] = Unescape(match[2]);
                }
            }
            return map;
        }();
        const auto found = cases.find(name);
        return found == cases.end() ? std::string("<missing case ") + name + ">" : found->second;
    }

    std::filesystem::path Fixture(const std::string& name)
    {
        return Locate("tests/assets/xna40/texture") / name;
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

    std::string Hex(const std::vector<SharpRuntime::bytecs>& bytes)
    {
        std::ostringstream out;
        out << std::hex << std::uppercase << std::setfill('0');
        for (const SharpRuntime::bytecs value : bytes)
        {
            out << std::setw(2) << static_cast<unsigned>(value);
        }
        return out.str();
    }

    /** @brief The enum's own spelling, which is what the corpus records. */
    std::string FormatName(Microsoft::Xna::Framework::Graphics::SurfaceFormat format)
    {
        namespace G = Microsoft::Xna::Framework::Graphics;
        static const std::map<G::SurfaceFormat, std::string> names = {
            {G::SurfaceFormat::Color, "Color"},   {G::SurfaceFormat::Dxt1, "Dxt1"},
            {G::SurfaceFormat::Dxt3, "Dxt3"},     {G::SurfaceFormat::Dxt5, "Dxt5"},
            {G::SurfaceFormat::Vector4, "Vector4"}};
        const auto found = names.find(format);
        return found == names.end() ? std::to_string(static_cast<int>(format)) : found->second;
    }

    /** @brief The processor half of a context: the least a texture processor reads. */
    class ProcessorContext final : public Xna::ContentProcessorContext
    {
    public:
        [[nodiscard]] std::string getBuildConfigurationProperty() const override { return "Debug"; }
        [[nodiscard]] std::string getIntermediateDirectoryProperty() const override { return "obj"; }
        [[nodiscard]] Xna::ContentBuildLogger& getLoggerProperty() const override
        {
            return const_cast<SilentLogger&>(logger_);
        }
        [[nodiscard]] std::string getOutputDirectoryProperty() const override { return "bin"; }
        [[nodiscard]] std::string getOutputFilenameProperty() const override { return "bin/asset.xnb"; }
        [[nodiscard]] const Xna::OpaqueDataDictionary& getParametersProperty() const override
        {
            return parameters_;
        }
        [[nodiscard]] Xna::TargetPlatform getTargetPlatformProperty() const override
        {
            return platform_;
        }
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::GraphicsProfile
        getTargetProfileProperty() const override
        {
            return profile_;
        }
        void AddDependency(const std::string&) override {}
        void AddOutputFile(const std::string&) override {}

        Xna::TargetPlatform platform_ = Xna::TargetPlatform::Windows;
        Microsoft::Xna::Framework::Graphics::GraphicsProfile profile_ =
            Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef;

        // The three nested-build entry points a texture processor never reaches.
        [[nodiscard]] Xna::ContentObject BuildAndLoadAssetCore(const std::string&, const Xna::ContentIdentity&,
                                                               const std::string&, const Xna::OpaqueDataDictionary&,
                                                               const std::string&, const std::string&,
                                                               const std::string&) override
        {
            throw System::NotSupportedException("BuildAndLoadAsset");
        }
        [[nodiscard]] std::string BuildAssetCore(const std::string&, const Xna::ContentIdentity&,
                                                 const std::string&, const Xna::OpaqueDataDictionary&,
                                                 const std::string&, const std::string&, const std::string&,
                                                 const std::string&) override
        {
            throw System::NotSupportedException("BuildAsset");
        }
        [[nodiscard]] Xna::ContentObject ConvertCore(const Xna::ContentObject&, const std::string&,
                                                     const Xna::OpaqueDataDictionary&, const std::string&,
                                                     const std::string&) override
        {
            throw System::NotSupportedException("Convert");
        }

    private:
        class SilentLogger final : public Xna::ContentBuildLogger
        {
        protected:
            void LogMessage(const std::string&) override {}
            void LogImportantMessage(const std::string&) override {}
            void LogWarning(const std::string&, const Xna::ContentIdentity&, const std::string&) override {}
        };

        SilentLogger logger_;
        Xna::OpaqueDataDictionary parameters_;
    };

    /** @brief The oracle's own DescribeTexture, so the two strings are comparable verbatim. */
    std::string Describe(const std::shared_ptr<Graphics::TextureContent>& texture,
                         const ImporterContext& context)
    {
        const auto& faces = static_cast<const System::Collections::ObjectModel::Collection<
            std::shared_ptr<Graphics::MipmapChain>>&>(texture->getFacesProperty());
        std::string text = texture->GetTypeName().substr(texture->GetTypeName().rfind('.') + 1) +
                           " faces=" + std::to_string(faces.getCountProperty());
        for (SharpRuntime::intcs face = 0; face < faces.getCountProperty(); ++face)
        {
            text += " [";
            for (SharpRuntime::intcs level = 0; level < faces[face]->getCountProperty(); ++level)
            {
                const std::shared_ptr<Graphics::BitmapContent>& bitmap = (*faces[face])[level];
                if (level > 0)
                {
                    text += ' ';
                }
                Microsoft::Xna::Framework::Graphics::SurfaceFormat format{};
                const bool hasFormat = bitmap->TryGetFormat(format);
                text += std::to_string(bitmap->getWidthProperty()) + "x" +
                        std::to_string(bitmap->getHeightProperty()) + ":" +
                        (hasFormat ? FormatName(format) : std::string("none"));
            }
            text += ']';
        }
        if (faces.getCountProperty() == 1 && faces[0]->getCountProperty() > 0)
        {
            const std::shared_ptr<Graphics::BitmapContent>& first = (*faces[0])[0];
            Microsoft::Xna::Framework::Graphics::SurfaceFormat format{};
            if (first->TryGetFormat(format) &&
                format == Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color &&
                first->getWidthProperty() * first->getHeightProperty() <= 16)
            {
                text += " pixels=" + Hex(first->GetPixelData());
            }
        }
        return text + " dependencies=" + std::to_string(context.dependencies.size()) + " identity=" +
               texture->getIdentityProperty().getSourceToolProperty();
    }
}

// Every extension the attribute declares, read from the committed file the genuine importer read.
TEST(XnaTextureExtension, EveryDeclaredExtensionAnswersWhatXnaAnswers)
{
    // A JPEG is the one source two decoders may legitimately disagree about, so it is compared
    // separately and with a tolerance; every other format is exact.
    for (const std::string& name : {"probe.bmp", "probe.dds", "probe.dib", "probe.png", "probe.ppm",
                                    "probe.tga", "probe.xyz", "probe_3x2.png"})
    {
        ImporterContext context;
        TextureImporter importer;
        const std::shared_ptr<Graphics::TextureContent> texture =
            importer.Import(Fixture(name).string(), context);
        ASSERT_NE(texture, nullptr) << name;
        EXPECT_EQ(Describe(texture, context), Expected("textureext/" + name)) << name;
    }
}

// The two float sources answer a Vector4 bitmap, which is the shape and not only the values.
TEST(XnaTextureExtension, TheTwoFloatSourcesAnswerVector4AsXnaDoes)
{
    for (const std::string& name : {"probe.hdr", "probe.pfm"})
    {
        ImporterContext context;
        TextureImporter importer;
        const std::shared_ptr<Graphics::TextureContent> texture =
            importer.Import(Fixture(name).string(), context);
        ASSERT_NE(texture, nullptr) << name;
        EXPECT_EQ(Describe(texture, context), Expected("textureext/" + name)) << name;
    }
}

// The RGBE convention: (mantissa + 0.5) / 256 * 2^(exponent - 128), pinned value by value.
TEST(XnaTextureExtension, ARadiancePictureAnswersTheFloatsXnaAnswers)
{
    ImporterContext context;
    TextureImporter importer;
    const std::shared_ptr<Graphics::TextureContent> texture =
        importer.Import(Fixture("probe.hdr").string(), context);
    const auto& faces = static_cast<const System::Collections::ObjectModel::Collection<
        std::shared_ptr<Graphics::MipmapChain>>&>(texture->getFacesProperty());
    const std::shared_ptr<Graphics::BitmapContent> level = (*faces[0])[0];
    const auto bitmap = std::dynamic_pointer_cast<Graphics::PixelBitmapContent<Vector4>>(level);
    ASSERT_NE(bitmap, nullptr);

    // The corpus writes each pixel as (x,y,z,w) with round-tripping precision; parse it back
    // rather than reformatting, so the comparison is of numbers and not of two printers.
    const std::string expected = Expected("textureext/probe.hdr/floats");
    std::vector<float> wanted;
    const std::regex number("-?[0-9]+(?:\\.[0-9]+)?(?:E-?[0-9]+)?");
    for (std::sregex_iterator it(expected.begin(), expected.end(), number);
         it != std::sregex_iterator(); ++it)
    {
        wanted.push_back(std::stof(it->str()));
    }
    ASSERT_EQ(wanted.size(),
              static_cast<std::size_t>(bitmap->getWidthProperty() * bitmap->getHeightProperty() * 4));
    std::size_t at = 0;
    for (SharpRuntime::intcs y = 0; y < bitmap->getHeightProperty(); ++y)
    {
        for (SharpRuntime::intcs x = 0; x < bitmap->getWidthProperty(); ++x)
        {
            const Vector4 pixel = bitmap->GetPixel(x, y);
            EXPECT_FLOAT_EQ(pixel.X, wanted[at++]) << x << "," << y;
            EXPECT_FLOAT_EQ(pixel.Y, wanted[at++]) << x << "," << y;
            EXPECT_FLOAT_EQ(pixel.Z, wanted[at++]) << x << "," << y;
            EXPECT_FLOAT_EQ(pixel.W, wanted[at++]) << x << "," << y;
        }
    }
}

// A JPEG's values are the one place two conformant decoders differ; the shape must still match.
TEST(XnaTextureExtension, AJpegMatchesXnasShapeAndItsPixelsWithinDecoderTolerance)
{
    ImporterContext context;
    TextureImporter importer;
    const std::shared_ptr<Graphics::TextureContent> texture =
        importer.Import(Fixture("probe.jpg").string(), context);
    ASSERT_NE(texture, nullptr);
    const std::string described = Describe(texture, context);
    const std::string expected = Expected("textureext/probe.jpg");
    // Everything but the pixels is exact.
    EXPECT_EQ(described.substr(0, described.find(" pixels=")),
              expected.substr(0, expected.find(" pixels=")));
    EXPECT_EQ(described.substr(described.find(" dependencies=")),
              expected.substr(expected.find(" dependencies=")));

    const auto value = [](const std::string& text, std::size_t index)
    {
        const std::size_t at = text.find(" pixels=") + 8 + index * 2;
        return static_cast<int>(std::stoul(text.substr(at, 2), nullptr, 16));
    };
    // Sixteen bytes: four pixels, RGBA. An IDCT is specified to a tolerance, not to a value, so
    // the comparison is one too; anything outside it is a decoder defect and not a rounding.
    for (std::size_t i = 0; i < 16; ++i)
    {
        EXPECT_LE(std::abs(value(described, i) - value(expected, i)), 8)
            << "byte " << i << " of the JPEG: CNA " << value(described, i) << ", XNA "
            << value(expected, i);
    }
}

// The refusals, including the D3DX code the genuine reader appends and the rule that picks it.
TEST(XnaTextureExtension, EveryMalformedSourceIsRefusedAsXnaRefusesIt)
{
    for (const std::string& name : {"empty.png", "truncated.png", "garbage.tga", "truncated.dds"})
    {
        ImporterContext context;
        TextureImporter importer;
        const std::string expected = Expected("textureext/" + name);
        ASSERT_EQ(expected.rfind("throws InvalidContentException: ", 0), 0u) << name;
        const std::string message = expected.substr(std::string("throws InvalidContentException: ").size());
        try
        {
            (void)importer.Import(Fixture(name).string(), context);
            ADD_FAILURE() << name << " was accepted";
        }
        catch (const InvalidContentException& error)
        {
            EXPECT_EQ(error.getMessageProperty(), message) << name;
        }
    }
}

// A flat Radiance scanline is the one place CNA deliberately does not reproduce XNA: D3DX cannot
// read one and answers the second pixel first with infinities for the rest, which is a defect of
// its reader rather than a behaviour a source format has.
TEST(XnaTextureExtension, AFlatRadianceScanlineIsReadWhereXnaMisreadsIt)
{
    const std::string expected = Expected("textureext/probe_flat.hdr/floats");
    ASSERT_NE(expected.find("Infinity"), std::string::npos)
        << "the recorded divergence assumes XNA answers infinities here";

    ImporterContext context;
    TextureImporter importer;
    const std::shared_ptr<Graphics::TextureContent> texture =
        importer.Import(Fixture("probe_flat.hdr").string(), context);
    const auto& faces = static_cast<const System::Collections::ObjectModel::Collection<
        std::shared_ptr<Graphics::MipmapChain>>&>(texture->getFacesProperty());
    const std::shared_ptr<Graphics::BitmapContent> level = (*faces[0])[0];
    const auto bitmap = std::dynamic_pointer_cast<Graphics::PixelBitmapContent<Vector4>>(level);
    ASSERT_NE(bitmap, nullptr);
    // Red, green, blue, white, each at the exponent-128 scale the file carries.
    const float low = 0.5f / 256.0f;
    const float high = 255.5f / 256.0f;
    EXPECT_FLOAT_EQ(bitmap->GetPixel(0, 0).X, high);
    EXPECT_FLOAT_EQ(bitmap->GetPixel(0, 0).Y, low);
    EXPECT_FLOAT_EQ(bitmap->GetPixel(1, 0).Y, high);
    EXPECT_FLOAT_EQ(bitmap->GetPixel(0, 1).Z, high);
    EXPECT_FLOAT_EQ(bitmap->GetPixel(1, 1).X, high);
    EXPECT_FLOAT_EQ(bitmap->GetPixel(1, 1).W, 1.0f);
}

namespace
{
    /** @brief The oracle's DescribeTexture without the importer tail the processor cases omit. */
    std::string DescribeProcessed(const std::shared_ptr<Graphics::TextureContent>& texture)
    {
        const auto& faces = static_cast<const System::Collections::ObjectModel::Collection<
            std::shared_ptr<Graphics::MipmapChain>>&>(texture->getFacesProperty());
        std::string text = texture->GetTypeName().substr(texture->GetTypeName().rfind('.') + 1) +
                           " faces=" + std::to_string(faces.getCountProperty());
        for (SharpRuntime::intcs face = 0; face < faces.getCountProperty(); ++face)
        {
            text += " [";
            for (SharpRuntime::intcs level = 0; level < faces[face]->getCountProperty(); ++level)
            {
                const std::shared_ptr<Graphics::BitmapContent> bitmap = (*faces[face])[level];
                if (level > 0)
                {
                    text += ' ';
                }
                Microsoft::Xna::Framework::Graphics::SurfaceFormat format{};
                const bool hasFormat = bitmap->TryGetFormat(format);
                text += std::to_string(bitmap->getWidthProperty()) + "x" +
                        std::to_string(bitmap->getHeightProperty()) + ":" +
                        (hasFormat ? FormatName(format) : std::string("none"));
            }
            text += ']';
        }
        if (faces.getCountProperty() == 1 && faces[0]->getCountProperty() > 0)
        {
            const std::shared_ptr<Graphics::BitmapContent> first = (*faces[0])[0];
            Microsoft::Xna::Framework::Graphics::SurfaceFormat format{};
            if (first->TryGetFormat(format) &&
                format == Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color &&
                first->getWidthProperty() * first->getHeightProperty() <= 16)
            {
                text += " pixels=" + Hex(first->GetPixelData());
            }
        }
        return text;
    }

    std::shared_ptr<Graphics::TextureContent> Import(const std::string& name)
    {
        ImporterContext context;
        TextureImporter importer;
        return importer.Import(Fixture(name).string(), context);
    }
}

// The importer's own default processor, and the two the pipeline also offers, over the same
// committed corpus: what a source becomes on the way to an .xnb.
TEST(XnaTextureExtension, TheThreeTextureProcessorsAnswerWhatXnaAnswers)
{
    for (const std::string& source : {"probe.png", "probe.ppm", "probe.pfm", "probe.dds"})
    {
        {
            Processors::SpriteTextureProcessor processor;
            ProcessorContext context;
            EXPECT_EQ(DescribeProcessed(processor.Process(Import(source), context)),
                      Expected("textureext/" + source + "/spritetexture"))
                << source << " through SpriteTextureProcessor";
        }
        {
            Processors::TextureProcessor processor;
            ProcessorContext context;
            EXPECT_EQ(DescribeProcessed(processor.Process(Import(source), context)),
                      Expected("textureext/" + source + "/texture"))
                << source << " through TextureProcessor";
        }
        {
            Processors::ModelTextureProcessor processor;
            ProcessorContext context;
            EXPECT_EQ(DescribeProcessed(processor.Process(Import(source), context)),
                      Expected("textureext/" + source + "/modeltexture"))
                << source << " through ModelTextureProcessor";
        }
    }
}

// Every TextureProcessor property, moved one at a time off its default, over the corpus. The
// measurements settle what the documentation only names: colour keying replaces the matched pixel
// with transparent black and premultiplication still runs over the rest, DXT picks Dxt5 when the
// source carries alpha and Dxt1 when it does not, NoChange leaves a float source floating, and
// ResizeToPowerOfTwo rounds each dimension up on its own.
TEST(XnaTextureExtension, EveryTextureProcessorPropertyAnswersWhatXnaAnswers)
{
    using Format = Processors::TextureProcessorOutputFormat;
    using Configure = std::function<void(Processors::TextureProcessor&)>;
    const std::vector<std::tuple<std::string, std::string, Configure>> cases = {
        {"defaults", "probe.png", [](Processors::TextureProcessor&) {}},
        {"no_premultiply", "probe.png",
         [](Processors::TextureProcessor& p) { p.setPremultiplyAlphaProperty(false); }},
        {"colorkey_red_enabled", "probe.png",
         [](Processors::TextureProcessor& p) {
             p.setColorKeyEnabledProperty(true);
             p.setColorKeyColorProperty(Color(255, 0, 0, 255));
         }},
        {"colorkey_red_disabled", "probe.png",
         [](Processors::TextureProcessor& p) {
             p.setColorKeyEnabledProperty(false);
             p.setColorKeyColorProperty(Color(255, 0, 0, 255));
         }},
        {"colorkey_magenta_default", "probe.png",
         [](Processors::TextureProcessor& p) { p.setColorKeyEnabledProperty(true); }},
        {"generate_mipmaps", "probe.png",
         [](Processors::TextureProcessor& p) { p.setGenerateMipmapsProperty(true); }},
        {"resize_to_power_of_two", "probe_3x2.png",
         [](Processors::TextureProcessor& p) { p.setResizeToPowerOfTwoProperty(true); }},
        {"no_resize_3x2", "probe_3x2.png", [](Processors::TextureProcessor&) {}},
        {"mipmaps_without_resize_3x2", "probe_3x2.png",
         [](Processors::TextureProcessor& p) { p.setGenerateMipmapsProperty(true); }},
        {"format_color", "probe.png",
         [](Processors::TextureProcessor& p) { p.setTextureFormatProperty(Format::Color); }},
        {"format_dxt", "probe.png",
         [](Processors::TextureProcessor& p) { p.setTextureFormatProperty(Format::DxtCompressed); }},
        {"format_nochange", "probe.png",
         [](Processors::TextureProcessor& p) { p.setTextureFormatProperty(Format::NoChange); }},
        {"format_dxt_from_ppm", "probe.ppm",
         [](Processors::TextureProcessor& p) { p.setTextureFormatProperty(Format::DxtCompressed); }},
        {"format_nochange_from_pfm", "probe.pfm",
         [](Processors::TextureProcessor& p) { p.setTextureFormatProperty(Format::NoChange); }},
        {"format_nochange_from_dds", "probe.dds",
         [](Processors::TextureProcessor& p) { p.setTextureFormatProperty(Format::NoChange); }},
        {"dxt_from_3x2", "probe_3x2.png",
         [](Processors::TextureProcessor& p) { p.setTextureFormatProperty(Format::DxtCompressed); }},
        {"mipmaps_and_dxt", "probe.png",
         [](Processors::TextureProcessor& p) {
             p.setGenerateMipmapsProperty(true);
             p.setTextureFormatProperty(Format::DxtCompressed);
         }},
    };
    for (const auto& [label, source, configure] : cases)
    {
        Processors::TextureProcessor processor;
        configure(processor);
        ProcessorContext context;
        const std::string described = DescribeProcessed(processor.Process(Import(source), context));
        const std::string expected = Expected("textureprop/" + label);
        if (label == "resize_to_power_of_two")
        {
            // The one case whose pixels are a resampler's and not a rule's. XNA resizes through
            // D3DX's triangle filter with dithering, whose taps and noise are its own; CNA's
            // resampler is CNA's. The shape is held exactly and the pixels only to the distance
            // two filters of the same intent stay within, which the corpus measures at 37 of 255.
            EXPECT_EQ(described.substr(0, described.find(" pixels=")),
                      expected.substr(0, expected.find(" pixels=")))
                << label;
            const auto byte = [](const std::string& text, std::size_t index)
            {
                return static_cast<int>(
                    std::stoul(text.substr(text.find(" pixels=") + 8 + index * 2, 2), nullptr, 16));
            };
            const std::size_t count = (described.size() - described.find(" pixels=") - 8) / 2;
            ASSERT_EQ(count, (expected.size() - expected.find(" pixels=") - 8) / 2);
            for (std::size_t i = 0; i < count; ++i)
            {
                EXPECT_LE(std::abs(byte(described, i) - byte(expected, i)), 48)
                    << label << " byte " << i << ": CNA " << byte(described, i) << ", XNA "
                    << byte(expected, i);
            }
            continue;
        }
        EXPECT_EQ(described, expected) << label;
    }
}

// The same processor under each target and profile XNA offers.
TEST(XnaTextureExtension, EveryTargetAndProfileAnswersWhatXnaAnswers)
{
    namespace G = Microsoft::Xna::Framework::Graphics;
    const std::vector<std::tuple<std::string, Xna::TargetPlatform, G::GraphicsProfile>> legs = {
        {"Windows_Reach", Xna::TargetPlatform::Windows, G::GraphicsProfile::Reach},
        {"Windows_HiDef", Xna::TargetPlatform::Windows, G::GraphicsProfile::HiDef},
        {"Xbox360_Reach", Xna::TargetPlatform::Xbox360, G::GraphicsProfile::Reach},
        {"Xbox360_HiDef", Xna::TargetPlatform::Xbox360, G::GraphicsProfile::HiDef},
        {"WindowsPhone_Reach", Xna::TargetPlatform::WindowsPhone, G::GraphicsProfile::Reach},
    };
    for (const auto& [label, platform, profile] : legs)
    {
        Processors::TextureProcessor processor;
        processor.setTextureFormatProperty(Processors::TextureProcessorOutputFormat::DxtCompressed);
        ProcessorContext context;
        context.platform_ = platform;
        context.profile_ = profile;
        EXPECT_EQ(DescribeProcessed(processor.Process(Import("probe.png"), context)),
                  Expected("textureprofile/" + label))
            << label;
    }
}
