// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-130, 131, 137, 138: the processor enumerations, the three
// texture processors and the pass-through processor against what the genuine XNA 4.0 pipeline does
// (tests/reference/xna40/graphics/graphics-content-oracle.json, cases processor/* and
// textureprocessor/*).
//
// The defaults are read from the runtime's own objects rather than from a document, and the
// processing steps are measured against a build context of the driver's own, which is the only way
// to run a processor outside XNA's internal build engine. What they settle: the order of the steps
// (colour key, resize, premultiply, mipmaps, format) and that the compressed format is Dxt1 unless
// a pixel is partly transparent.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>

#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/PixelBitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/TextureContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/PassThroughProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/ProcessorEnums.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/TextureProcessor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgr565.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/NotSupportedException.hpp"

namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
namespace Processors = Microsoft::Xna::Framework::Content::Pipeline::Processors;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::Pipeline::ContentProcessorContext;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::PackedVector::Bgr565;
using Graphics::PixelBitmapContent;
using Graphics::Texture2DContent;
using Graphics::TextureContent;
using Graphics::TextureCubeContent;
using Processors::ModelTextureProcessor;
using Processors::SpriteTextureProcessor;
using Processors::TextureProcessor;
using Processors::TextureProcessorOutputFormat;

namespace
{
    std::filesystem::path CorpusFile()
    {
        const std::filesystem::path relative = "tests/reference/xna40/graphics/graphics-content-oracle.json";
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

    std::string Normalize(const std::string& result)
    {
        std::string text = result;
        const std::size_t parameter = text.find("Parameter name:");
        if (parameter != std::string::npos)
        {
            std::size_t cut = parameter;
            while (cut > 0 && (text[cut - 1] == '\n' || text[cut - 1] == '\r'))
            {
                --cut;
            }
            text = text.substr(0, cut);
        }
        const std::size_t core = text.find(" (Parameter '");
        if (core != std::string::npos)
        {
            const std::size_t end = text.find(')', core);
            text = text.substr(0, core) + (end == std::string::npos ? "" : text.substr(end + 1));
        }
        return text;
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
                    map[match[1]] = Unescape(match[2]);
                }
            }
            return map;
        }();
        return cases;
    }

    std::string Expected(const std::string& name)
    {
        const auto found = Oracle().find(name);
        return found == Oracle().end() ? std::string("<missing case ") + name + ">" : Normalize(found->second);
    }

    std::string Result(const std::function<std::string()>& body)
    {
        try
        {
            return Normalize(body());
        }
        catch (const System::ArgumentNullException& error)
        {
            return Normalize("throws ArgumentNullException: " + error.getMessageProperty());
        }
        catch (const System::Exception& error)
        {
            return Normalize("throws Exception: " + error.getMessageProperty());
        }
    }

    /** @brief A context that answers what a processor asks of it, as the oracle's driver does. */
    class ProbeContext final : public ContentProcessorContext
    {
    public:
        [[nodiscard]] std::string getBuildConfigurationProperty() const override { return "Debug"; }
        [[nodiscard]] std::string getIntermediateDirectoryProperty() const override { return "obj"; }
        [[nodiscard]] Microsoft::Xna::Framework::Content::Pipeline::ContentBuildLogger& getLoggerProperty()
            const override
        {
            return logger_;
        }
        [[nodiscard]] std::string getOutputDirectoryProperty() const override { return "bin"; }
        [[nodiscard]] std::string getOutputFilenameProperty() const override { return "asset.xnb"; }
        [[nodiscard]] const Microsoft::Xna::Framework::Content::Pipeline::OpaqueDataDictionary&
        getParametersProperty() const override
        {
            return parameters_;
        }
        [[nodiscard]] Microsoft::Xna::Framework::Content::Pipeline::TargetPlatform getTargetPlatformProperty()
            const override
        {
            return Microsoft::Xna::Framework::Content::Pipeline::TargetPlatform::Windows;
        }
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::GraphicsProfile getTargetProfileProperty() const override
        {
            return Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef;
        }
        void AddDependency(const std::string& filename) override { (void)filename; }
        void AddOutputFile(const std::string& filename) override { (void)filename; }

    protected:
        [[nodiscard]] Microsoft::Xna::Framework::Content::Pipeline::ContentObject BuildAndLoadAssetCore(
            const std::string&, const Microsoft::Xna::Framework::Content::Pipeline::ContentIdentity&,
            const std::string&, const Microsoft::Xna::Framework::Content::Pipeline::OpaqueDataDictionary&,
            const std::string&, const std::string&, const std::string&) override
        {
            throw System::NotSupportedException("BuildAndLoadAsset");
        }
        [[nodiscard]] std::string BuildAssetCore(
            const std::string&, const Microsoft::Xna::Framework::Content::Pipeline::ContentIdentity&,
            const std::string&, const Microsoft::Xna::Framework::Content::Pipeline::OpaqueDataDictionary&,
            const std::string&, const std::string&, const std::string&, const std::string&) override
        {
            throw System::NotSupportedException("BuildAsset");
        }
        [[nodiscard]] Microsoft::Xna::Framework::Content::Pipeline::ContentObject ConvertCore(
            const Microsoft::Xna::Framework::Content::Pipeline::ContentObject&, const std::string&,
            const Microsoft::Xna::Framework::Content::Pipeline::OpaqueDataDictionary&, const std::string&) override
        {
            throw System::NotSupportedException("Convert");
        }

    private:
        /** @brief A logger that keeps everything, as the driver's does. */
        class SilentLogger final : public Microsoft::Xna::Framework::Content::Pipeline::ContentBuildLogger
        {
        protected:
            void LogMessage(const std::string& message) override { (void)message; }
            void LogImportantMessage(const std::string& message) override { (void)message; }
            void LogWarning(const std::string& helpLink,
                            const Microsoft::Xna::Framework::Content::Pipeline::ContentIdentity& contentIdentity,
                            const std::string& message) override
            {
                (void)helpLink;
                (void)contentIdentity;
                (void)message;
            }
        };

        mutable SilentLogger logger_;
        Microsoft::Xna::Framework::Content::Pipeline::OpaqueDataDictionary parameters_;
    };

    /** @brief The oracle's own gradient bitmap: byte-for-byte the same inputs it measured. */
    std::shared_ptr<PixelBitmapContent<Color>> Gradient(int width, int height)
    {
        auto bitmap = std::make_shared<PixelBitmapContent<Color>>(width, height);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                bitmap->SetPixel(x, y,
                                 Color(static_cast<SharpRuntime::bytecs>(x * 255 / std::max(1, width - 1)),
                                       static_cast<SharpRuntime::bytecs>(y * 255 / std::max(1, height - 1)),
                                       static_cast<SharpRuntime::bytecs>(37 + 11 * x + 7 * y),
                                       static_cast<SharpRuntime::bytecs>(255 - 40 * y)));
            }
        }
        return bitmap;
    }

    std::string Hex(const std::vector<std::uint8_t>& bytes)
    {
        static const char* digits = "0123456789ABCDEF";
        std::string out;
        for (const std::uint8_t byte : bytes)
        {
            out += digits[byte >> 4];
            out += digits[byte & 15];
        }
        return out;
    }

    std::string FormatName(SurfaceFormat format)
    {
        switch (format)
        {
        case SurfaceFormat::Color:
            return "Color";
        case SurfaceFormat::Bgr565:
            return "Bgr565";
        case SurfaceFormat::Dxt1:
            return "Dxt1";
        case SurfaceFormat::Dxt3:
            return "Dxt3";
        case SurfaceFormat::Dxt5:
            return "Dxt5";
        default:
            return "other";
        }
    }

    /** @brief The oracle's DescribeTexture. */
    std::string DescribeTexture(const std::shared_ptr<TextureContent>& texture, const std::string& typeName)
    {
        std::string text = typeName + " faces=" + std::to_string(texture->getFacesProperty().getCountProperty());
        for (SharpRuntime::intcs face = 0; face < texture->getFacesProperty().getCountProperty(); ++face)
        {
            const std::shared_ptr<Graphics::MipmapChain>& chain =
                static_cast<const System::Collections::ObjectModel::Collection<
                    std::shared_ptr<Graphics::MipmapChain>>&>(texture->getFacesProperty())[face];
            text += " [";
            for (SharpRuntime::intcs level = 0; level < chain->getCountProperty(); ++level)
            {
                const std::shared_ptr<Graphics::BitmapContent>& bitmap =
                    static_cast<const System::Collections::ObjectModel::Collection<
                        std::shared_ptr<Graphics::BitmapContent>>&>(*chain)[level];
                if (level > 0)
                {
                    text += ' ';
                }
                SurfaceFormat format = SurfaceFormat::Color;
                const bool hasFormat = bitmap->TryGetFormat(format);
                text += std::to_string(bitmap->getWidthProperty()) + "x" +
                        std::to_string(bitmap->getHeightProperty()) + ":" +
                        (hasFormat ? FormatName(format) : "none");
            }
            text += ']';
        }
        if (texture->getFacesProperty().getCountProperty() == 1)
        {
            const std::shared_ptr<Graphics::MipmapChain>& chain =
                static_cast<const System::Collections::ObjectModel::Collection<
                    std::shared_ptr<Graphics::MipmapChain>>&>(texture->getFacesProperty())[0];
            if (chain->getCountProperty() > 0)
            {
                const std::shared_ptr<Graphics::BitmapContent>& first =
                    static_cast<const System::Collections::ObjectModel::Collection<
                        std::shared_ptr<Graphics::BitmapContent>>&>(*chain)[0];
                SurfaceFormat format = SurfaceFormat::Color;
                if (first->TryGetFormat(format) && format == SurfaceFormat::Color &&
                    first->getWidthProperty() * first->getHeightProperty() <= 16)
                {
                    text += " pixels=" + Hex(first->GetPixelData());
                }
            }
        }
        return text;
    }

    std::shared_ptr<Texture2DContent> ColorTexture(int width, int height)
    {
        auto texture = std::make_shared<Texture2DContent>();
        texture->getMipmapsProperty().Add(Gradient(width, height));
        return texture;
    }

    std::string RunProcessor(TextureProcessor& processor, const std::shared_ptr<TextureContent>& texture,
                             const std::string& typeName)
    {
        ProbeContext context;
        return DescribeTexture(processor.Process(texture, context), typeName);
    }
}

TEST(XnaProcessorEnums, ValuesMatchXna)
{
    EXPECT_EQ(static_cast<int>(TextureProcessorOutputFormat::NoChange), 0);
    EXPECT_EQ(static_cast<int>(TextureProcessorOutputFormat::Color), 1);
    EXPECT_EQ(static_cast<int>(TextureProcessorOutputFormat::DxtCompressed), 2);
    EXPECT_EQ(static_cast<int>(Processors::MaterialProcessorDefaultEffect::BasicEffect), 0);
    EXPECT_EQ(static_cast<int>(Processors::MaterialProcessorDefaultEffect::SkinnedEffect), 1);
    EXPECT_EQ(static_cast<int>(Processors::MaterialProcessorDefaultEffect::EnvironmentMapEffect), 2);
    EXPECT_EQ(static_cast<int>(Processors::MaterialProcessorDefaultEffect::DualTextureEffect), 3);
    EXPECT_EQ(static_cast<int>(Processors::MaterialProcessorDefaultEffect::AlphaTestEffect), 4);
    EXPECT_EQ(static_cast<int>(Processors::EffectProcessorDebugMode::Auto), 0);
    EXPECT_EQ(static_cast<int>(Processors::EffectProcessorDebugMode::Debug), 1);
    EXPECT_EQ(static_cast<int>(Processors::EffectProcessorDebugMode::Optimize), 2);
}

TEST(XnaTextureProcessor, DefaultsMatchXna)
{
    const auto describe = [](const TextureProcessor& processor)
    {
        const Color key = processor.getColorKeyColorProperty();
        static const std::map<TextureProcessorOutputFormat, std::string> formats = {
            {TextureProcessorOutputFormat::NoChange, "NoChange"},
            {TextureProcessorOutputFormat::Color, "Color"},
            {TextureProcessorOutputFormat::DxtCompressed, "DxtCompressed"}};
        return "ColorKeyColor={R:" + std::to_string(static_cast<int>(key.getRProperty())) + " G:" +
               std::to_string(static_cast<int>(key.getGProperty())) + " B:" +
               std::to_string(static_cast<int>(key.getBProperty())) + " A:" +
               std::to_string(static_cast<int>(key.getAProperty())) + "} ColorKeyEnabled=" +
               (processor.getColorKeyEnabledProperty() ? "True" : "False") + " GenerateMipmaps=" +
               (processor.getGenerateMipmapsProperty() ? "True" : "False") + " PremultiplyAlpha=" +
               (processor.getPremultiplyAlphaProperty() ? "True" : "False") + " ResizeToPowerOfTwo=" +
               (processor.getResizeToPowerOfTwoProperty() ? "True" : "False") + " TextureFormat=" +
               formats.at(processor.getTextureFormatProperty());
    };
    EXPECT_EQ(describe(TextureProcessor()), Expected("processor/TextureProcessor"));
    EXPECT_EQ(describe(SpriteTextureProcessor()), Expected("processor/SpriteTextureProcessor"));
    EXPECT_EQ(describe(ModelTextureProcessor()), Expected("processor/ModelTextureProcessor"));
}

TEST(XnaTextureProcessor, ColorKeyAndPremultiplyMatchXna)
{
    TextureProcessor keyed;
    auto texture = std::make_shared<Texture2DContent>();
    auto bitmap = std::make_shared<PixelBitmapContent<Color>>(2, 1);
    bitmap->SetPixel(0, 0, Color(255, 0, 255, 255));
    bitmap->SetPixel(1, 0, Color(10, 20, 30, 255));
    texture->getMipmapsProperty().Add(bitmap);
    EXPECT_EQ(RunProcessor(keyed, texture, "Texture2DContent"), Expected("textureprocessor/color_key"));

    TextureProcessor unkeyed;
    unkeyed.setColorKeyEnabledProperty(false);
    auto kept = std::make_shared<Texture2DContent>();
    auto keptBitmap = std::make_shared<PixelBitmapContent<Color>>(2, 1);
    keptBitmap->SetPixel(0, 0, Color(255, 0, 255, 255));
    keptBitmap->SetPixel(1, 0, Color(10, 20, 30, 255));
    kept->getMipmapsProperty().Add(keptBitmap);
    EXPECT_EQ(RunProcessor(unkeyed, kept, "Texture2DContent"), Expected("textureprocessor/color_key_disabled"));

    TextureProcessor premultiplied;
    premultiplied.setColorKeyEnabledProperty(false);
    auto transparent = std::make_shared<Texture2DContent>();
    auto transparentBitmap = std::make_shared<PixelBitmapContent<Color>>(1, 1);
    transparentBitmap->SetPixel(0, 0, Color(255, 128, 0, 128));
    transparent->getMipmapsProperty().Add(transparentBitmap);
    EXPECT_EQ(RunProcessor(premultiplied, transparent, "Texture2DContent"), Expected("textureprocessor/premultiply"));

    TextureProcessor straight;
    straight.setColorKeyEnabledProperty(false);
    straight.setPremultiplyAlphaProperty(false);
    auto untouched = std::make_shared<Texture2DContent>();
    auto untouchedBitmap = std::make_shared<PixelBitmapContent<Color>>(1, 1);
    untouchedBitmap->SetPixel(0, 0, Color(255, 128, 0, 128));
    untouched->getMipmapsProperty().Add(untouchedBitmap);
    EXPECT_EQ(RunProcessor(straight, untouched, "Texture2DContent"), Expected("textureprocessor/no_premultiply"));
}

TEST(XnaTextureProcessor, MipmapsResizeAndFormatMatchXna)
{
    TextureProcessor mipmapped;
    mipmapped.setGenerateMipmapsProperty(true);
    EXPECT_EQ(RunProcessor(mipmapped, ColorTexture(4, 4), "Texture2DContent"), Expected("textureprocessor/mipmaps"));

    TextureProcessor resized;
    resized.setResizeToPowerOfTwoProperty(true);
    EXPECT_EQ(RunProcessor(resized, ColorTexture(3, 5), "Texture2DContent"),
              Expected("textureprocessor/resize_to_power_of_two"));

    TextureProcessor unresized;
    unresized.setResizeToPowerOfTwoProperty(true);
    EXPECT_EQ(RunProcessor(unresized, ColorTexture(4, 4), "Texture2DContent"),
              Expected("textureprocessor/resize_already_power_of_two"));

    TextureProcessor compressed;
    compressed.setTextureFormatProperty(TextureProcessorOutputFormat::DxtCompressed);
    EXPECT_EQ(RunProcessor(compressed, ColorTexture(4, 4), "Texture2DContent"), Expected("textureprocessor/dxt"));

    TextureProcessor unchanged;
    unchanged.setTextureFormatProperty(TextureProcessorOutputFormat::NoChange);
    auto other = std::make_shared<Texture2DContent>();
    other->getMipmapsProperty().Add(std::make_shared<PixelBitmapContent<Bgr565>>(4, 4));
    EXPECT_EQ(RunProcessor(unchanged, other, "Texture2DContent"), Expected("textureprocessor/no_change"));
}

TEST(XnaTextureProcessor, CompressedFormatIsDxt1UnlessAlphaIsPartial)
{
    TextureProcessor opaque;
    opaque.setTextureFormatProperty(TextureProcessorOutputFormat::DxtCompressed);
    opaque.setColorKeyEnabledProperty(false);
    auto texture = std::make_shared<Texture2DContent>();
    auto bitmap = std::make_shared<PixelBitmapContent<Color>>(4, 4);
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            bitmap->SetPixel(x, y, Color(x * 60, y * 60, 30, 255));
        }
    }
    texture->getMipmapsProperty().Add(bitmap);
    EXPECT_EQ(RunProcessor(opaque, texture, "Texture2DContent"), Expected("textureprocessor/dxt_opaque"));

    TextureProcessor keyed;
    keyed.setTextureFormatProperty(TextureProcessorOutputFormat::DxtCompressed);
    auto keyedTexture = std::make_shared<Texture2DContent>();
    auto keyedBitmap = std::make_shared<PixelBitmapContent<Color>>(4, 4);
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            keyedBitmap->SetPixel(x, y, Color(x * 60, y * 60, 30, 255));
        }
    }
    keyedBitmap->SetPixel(0, 0, Color(255, 0, 255, 255));
    keyedTexture->getMipmapsProperty().Add(keyedBitmap);
    EXPECT_EQ(RunProcessor(keyed, keyedTexture, "Texture2DContent"), Expected("textureprocessor/dxt_colorkeyed_opaque"));

    TextureProcessor odd;
    odd.setTextureFormatProperty(TextureProcessorOutputFormat::DxtCompressed);
    EXPECT_EQ(RunProcessor(odd, ColorTexture(5, 3), "Texture2DContent"),
              Expected("textureprocessor/dxt_non_multiple_of_four"));
}

TEST(XnaTextureProcessor, EveryFaceIsProcessedAndNullIsRefused)
{
    TextureProcessor processor;
    auto cube = std::make_shared<TextureCubeContent>();
    for (SharpRuntime::intcs face = 0; face < 6; ++face)
    {
        const std::shared_ptr<Graphics::MipmapChain>& chain =
            static_cast<const System::Collections::ObjectModel::Collection<
                std::shared_ptr<Graphics::MipmapChain>>&>(cube->getFacesProperty())[face];
        chain->Add(Gradient(4, 4));
    }
    EXPECT_EQ(RunProcessor(processor, cube, "TextureCubeContent"), Expected("textureprocessor/cube_defaults"));

    EXPECT_EQ(Result([]
                     {
                         TextureProcessor refusing;
                         ProbeContext context;
                         return DescribeTexture(refusing.Process(nullptr, context), "Texture2DContent");
                     }),
              Expected("textureprocessor/null_input"));
}

TEST(XnaTextureProcessor, DerivedProcessorsRunTheirOwnDefaults)
{
    SpriteTextureProcessor sprite;
    EXPECT_EQ(RunProcessor(sprite, ColorTexture(4, 4), "Texture2DContent"), Expected("textureprocessor/sprite_defaults"));

    ModelTextureProcessor model;
    EXPECT_EQ(RunProcessor(model, ColorTexture(4, 4), "Texture2DContent"), Expected("textureprocessor/model_defaults"));
}

TEST(XnaPassThroughProcessor, ReturnsItsInput)
{
    Processors::PassThroughProcessor processor;
    ProbeContext context;
    const auto value = std::make_shared<Texture2DContent>();
    const Microsoft::Xna::Framework::Content::Pipeline::ContentObject boxed =
        Microsoft::Xna::Framework::Content::Pipeline::Box<std::shared_ptr<TextureContent>>(value);
    const Microsoft::Xna::Framework::Content::Pipeline::ContentObject result =
        processor.Process(boxed, context);
    EXPECT_EQ(Microsoft::Xna::Framework::Content::Pipeline::Unbox<std::shared_ptr<TextureContent>>(result), value);
    EXPECT_EQ(Expected("processor/PassThroughProcessor"), "");
}
