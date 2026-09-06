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
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <vector>
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
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Compiler/ContentCompiler.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/PixelBitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/TextureContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MeshBuilder.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/StockMaterials.hpp"
#include <optional>
#include <cstdlib>
#include "CNA/Content/Pipeline/EffectCompilerService.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/EffectImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/EffectProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannelNames.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/FontProcessors.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/ModelProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/MaterialProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/PassThroughProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/ProcessorEnums.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/TextureProcessor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgr565.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/DateTime.hpp"
#include "System/NotSupportedException.hpp"

namespace Xna = Microsoft::Xna::Framework::Content::Pipeline;
namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
namespace Processors = Microsoft::Xna::Framework::Content::Pipeline::Processors;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
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
        // A message the runtime composed with Environment.NewLine carries the host's line ending,
        // not XNA's; CNA writes "\n" on this one.
        std::string unwrapped;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
            {
                continue;
            }
            unwrapped += text[i];
        }
        text = unwrapped;
        // The runtime resolved every relative reference against its own working directory; that
        // drive letter is a property of the host, not of XNA.
        static const std::regex windowsPath("[A-Za-z]:\\\\[^ <\"\\n]*");
        std::string reduced;
        std::size_t copied = 0;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), windowsPath); it != std::sregex_iterator();
             ++it)
        {
            reduced += text.substr(copied, static_cast<std::size_t>(it->position()) - copied);
            const std::string path = it->str();
            const std::size_t slash = path.find_last_of("/\\\\");
            reduced += slash == std::string::npos ? path : path.substr(slash + 1);
            copied = static_cast<std::size_t>(it->position() + it->length());
        }
        reduced += text.substr(copied);
        return reduced;
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
        catch (const Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException& error)
        {
            return Normalize("throws InvalidContentException: " + error.getMessageProperty());
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
            const Microsoft::Xna::Framework::Content::Pipeline::OpaqueDataDictionary&, const std::string&,
            const std::string&) override
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

    /**
     * @brief A context that records what it is asked to build, as the oracle's driver does.
     */
    class RecordingContext : public ContentProcessorContext
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
            return platform_;
        }
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::GraphicsProfile getTargetProfileProperty() const override
        {
            return profile_;
        }

        /** @brief The target this context builds for; Windows/HiDef unless a test says otherwise. */
        Microsoft::Xna::Framework::Content::Pipeline::TargetPlatform platform_ =
            Microsoft::Xna::Framework::Content::Pipeline::TargetPlatform::Windows;

        /** @brief The graphics profile this context builds for. */
        Microsoft::Xna::Framework::Graphics::GraphicsProfile profile_ =
            Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef;
        void AddDependency(const std::string& filename) override { (void)filename; }
        void AddOutputFile(const std::string& filename) override { (void)filename; }

        /** @brief What was built, in the oracle's own wording. */
        [[nodiscard]] std::string Built() const { return "[" + built_ + "]"; }

    protected:
        [[nodiscard]] Microsoft::Xna::Framework::Content::Pipeline::ContentObject BuildAndLoadAssetCore(
            const std::string&, const Microsoft::Xna::Framework::Content::Pipeline::ContentIdentity&,
            const std::string&, const Microsoft::Xna::Framework::Content::Pipeline::OpaqueDataDictionary&,
            const std::string&, const std::string&, const std::string&) override
        {
            throw System::NotSupportedException("BuildAndLoadAsset");
        }

        [[nodiscard]] std::string BuildAssetCore(
            const std::string& sourceFilename,
            const Microsoft::Xna::Framework::Content::Pipeline::ContentIdentity&, const std::string& processorName,
            const Microsoft::Xna::Framework::Content::Pipeline::OpaqueDataDictionary& processorParameters,
            const std::string& importerName, const std::string& assetName, const std::string&,
            const std::string& outputTypeName) override
        {
            if (!built_.empty())
            {
                built_ += ' ';
            }
            built_ += std::filesystem::path(sourceFilename).filename().string() + "->" + processorName + "(" +
                      Describe(processorParameters) + ") importer=" + (importerName.empty() ? "null" : importerName) +
                      " asset=" + (assetName.empty() ? "null" : assetName) + " out=" +
                      outputTypeName.substr(outputTypeName.rfind('.') + 1);
            return sourceFilename + ".xnb";
        }

        /** @brief What the driver answers a Convert with: the input, recorded on the way through. */
        [[nodiscard]] Microsoft::Xna::Framework::Content::Pipeline::ContentObject ConvertCore(
            const Microsoft::Xna::Framework::Content::Pipeline::ContentObject& input,
            const std::string& processorName,
            const Microsoft::Xna::Framework::Content::Pipeline::OpaqueDataDictionary& processorParameters,
            const std::string& inputTypeName, const std::string& outputTypeName) override
        {
            if (!built_.empty())
            {
                built_ += ' ';
            }
            built_ += "convert:" + inputTypeName.substr(inputTypeName.rfind('.') + 1) + "->" + processorName + "(" +
                      Describe(processorParameters) + ")->" + outputTypeName.substr(outputTypeName.rfind('.') + 1);
            return input;
        }

    private:
        class SilentLogger final : public Microsoft::Xna::Framework::Content::Pipeline::ContentBuildLogger
        {
        protected:
            void LogMessage(const std::string&) override {}
            void LogImportantMessage(const std::string&) override {}
            void LogWarning(const std::string&,
                            const Microsoft::Xna::Framework::Content::Pipeline::ContentIdentity&,
                            const std::string&) override
            {
            }
        };

        /** @brief The parameters as the oracle prints them: key=value in key order. */
        static std::string Describe(
            const Microsoft::Xna::Framework::Content::Pipeline::OpaqueDataDictionary& parameters)
        {
            if (parameters.getCountProperty() == 0)
            {
                return "null";
            }
            std::vector<std::string> keys = parameters.getKeysProperty();
            std::sort(keys.begin(), keys.end());
            std::string text;
            for (const std::string& key : keys)
            {
                if (!text.empty())
                {
                    text += ',';
                }
                Microsoft::Xna::Framework::Content::Pipeline::ContentObject value;
                parameters.TryGetValue(key, value);
                text += key + "=" + DescribeValue(value);
            }
            return text;
        }

        static std::string DescribeValue(const Microsoft::Xna::Framework::Content::Pipeline::ContentObject& value)
        {
            using Microsoft::Xna::Framework::Content::Pipeline::Holds;
            using Microsoft::Xna::Framework::Content::Pipeline::Unbox;
            if (Holds<bool>(value))
            {
                return Unbox<bool>(value) ? "True" : "False";
            }
            if (Holds<Color>(value))
            {
                const Color color = Unbox<Color>(value);
                return "{R:" + std::to_string(static_cast<int>(color.getRProperty())) + " G:" +
                       std::to_string(static_cast<int>(color.getGProperty())) + " B:" +
                       std::to_string(static_cast<int>(color.getBProperty())) + " A:" +
                       std::to_string(static_cast<int>(color.getAProperty())) + "}";
            }
            if (Holds<TextureProcessorOutputFormat>(value))
            {
                static const std::map<TextureProcessorOutputFormat, std::string> names = {
                    {TextureProcessorOutputFormat::NoChange, "NoChange"},
                    {TextureProcessorOutputFormat::Color, "Color"},
                    {TextureProcessorOutputFormat::DxtCompressed, "DxtCompressed"}};
                return names.at(Unbox<TextureProcessorOutputFormat>(value));
            }
            if (Holds<Processors::MaterialProcessorDefaultEffect>(value))
            {
                static const std::map<Processors::MaterialProcessorDefaultEffect, std::string> names = {
                    {Processors::MaterialProcessorDefaultEffect::BasicEffect, "BasicEffect"},
                    {Processors::MaterialProcessorDefaultEffect::SkinnedEffect, "SkinnedEffect"},
                    {Processors::MaterialProcessorDefaultEffect::EnvironmentMapEffect, "EnvironmentMapEffect"},
                    {Processors::MaterialProcessorDefaultEffect::DualTextureEffect, "DualTextureEffect"}};
                return names.at(Unbox<Processors::MaterialProcessorDefaultEffect>(value));
            }
            return value.StableType();
        }

        mutable SilentLogger logger_;
        Microsoft::Xna::Framework::Content::Pipeline::OpaqueDataDictionary parameters_;
        std::string built_;
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

    /** @brief .NET's "R" format for a float: the shortest text that reads back exactly. */
    std::string Number(float value)
    {
        if (std::isnan(value))
        {
            return "NaN";
        }
        // .NET Framework's "R" tries 7 significant digits and falls back to 9, never 8, and
        // spells the exponent with a capital E.
        std::string spelled;
        for (const int digits : {7, 9})
        {
            std::ostringstream text;
            text.imbue(std::locale::classic());
            text.precision(digits);
            text << value;
            spelled = text.str();
            if (std::stof(spelled) == value)
            {
                break;
            }
        }
        const std::size_t exponent = spelled.find('e');
        if (exponent != std::string::npos)
        {
            spelled[exponent] = 'E';
        }
        return spelled;
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

namespace
{
    /** @brief The oracle's DescribeMaterial, reduced to what these cases vary. */
    std::string DescribeMaterial(const std::shared_ptr<Graphics::MaterialContent>& material,
                                 const std::string& typeName)
    {
        std::string text = typeName + " opaque={";
        bool first = true;
        for (const std::string& key : material->getOpaqueDataProperty().getKeysProperty())
        {
            if (!first)
            {
                text += ' ';
            }
            first = false;
            Microsoft::Xna::Framework::Content::Pipeline::ContentObject value;
            material->getOpaqueDataProperty().TryGetValue(key, value);
            text += key + "=" + value.StableType() + ":ExternalReference`1";
        }
        text += "} textures={";
        first = true;
        for (const std::string& key : material->getTexturesProperty().getKeysProperty())
        {
            if (!first)
            {
                text += ' ';
            }
            first = false;
            std::shared_ptr<Microsoft::Xna::Framework::Content::Pipeline::ExternalReference<TextureContent>> reference;
            material->getTexturesProperty().TryGetValue(key, reference);
            text += key + "=" +
                    (reference == nullptr
                         ? "null"
                         : std::filesystem::path(reference->getFilenameProperty()).filename().string());
        }
        return text + "}";
    }
}

TEST(XnaMaterialProcessor, DefaultsMatchXna)
{
    const Processors::MaterialProcessor processor;
    const Color key = processor.getColorKeyColorProperty();
    static const std::map<TextureProcessorOutputFormat, std::string> formats = {
        {TextureProcessorOutputFormat::NoChange, "NoChange"},
        {TextureProcessorOutputFormat::Color, "Color"},
        {TextureProcessorOutputFormat::DxtCompressed, "DxtCompressed"}};
    static const std::map<Processors::MaterialProcessorDefaultEffect, std::string> effects = {
        {Processors::MaterialProcessorDefaultEffect::BasicEffect, "BasicEffect"},
        {Processors::MaterialProcessorDefaultEffect::SkinnedEffect, "SkinnedEffect"},
        {Processors::MaterialProcessorDefaultEffect::EnvironmentMapEffect, "EnvironmentMapEffect"},
        {Processors::MaterialProcessorDefaultEffect::DualTextureEffect, "DualTextureEffect"},
        {Processors::MaterialProcessorDefaultEffect::AlphaTestEffect, "AlphaTestEffect"}};
    EXPECT_EQ("ColorKeyColor={R:" + std::to_string(static_cast<int>(key.getRProperty())) + " G:" +
                  std::to_string(static_cast<int>(key.getGProperty())) + " B:" +
                  std::to_string(static_cast<int>(key.getBProperty())) + " A:" +
                  std::to_string(static_cast<int>(key.getAProperty())) + "} ColorKeyEnabled=" +
                  (processor.getColorKeyEnabledProperty() ? "True" : "False") + " DefaultEffect=" +
                  effects.at(processor.getDefaultEffectProperty()) + " GenerateMipmaps=" +
                  (processor.getGenerateMipmapsProperty() ? "True" : "False") + " PremultiplyTextureAlpha=" +
                  (processor.getPremultiplyTextureAlphaProperty() ? "True" : "False") +
                  " ResizeTexturesToPowerOfTwo=" +
                  (processor.getResizeTexturesToPowerOfTwoProperty() ? "True" : "False") + " TextureFormat=" +
                  formats.at(processor.getTextureFormatProperty()),
              Expected("processor/MaterialProcessor"));
}

TEST(XnaMaterialProcessor, BuildsEveryTextureItNames)
{
    Processors::MaterialProcessor processor;
    auto material = std::make_shared<Graphics::BasicMaterialContent>();
    material->setTextureProperty(
        std::make_shared<Microsoft::Xna::Framework::Content::Pipeline::ExternalReference<TextureContent>>("cat.tga"));
    RecordingContext context;
    const std::shared_ptr<Graphics::MaterialContent> result = processor.Process(material, context);
    const std::string basicResult = DescribeMaterial(result, "BasicMaterialContent");
    EXPECT_EQ(basicResult + " built=" + context.Built() + " same=" + (result == material ? "True" : "False"),
              Expected("materialprocessor/basic_with_texture"));

    Processors::MaterialProcessor untextured;
    RecordingContext empty;
    // Process first, then read what was built: the two are separate arguments of one expression,
    // whose evaluation order C++ leaves unspecified.
    const std::string untexturedResult =
        DescribeMaterial(untextured.Process(std::make_shared<Graphics::BasicMaterialContent>(), empty),
                         "BasicMaterialContent");
    EXPECT_EQ(untexturedResult + " built=" + empty.Built(), Expected("materialprocessor/no_texture"));

    Processors::MaterialProcessor dual;
    auto twoTextures = std::make_shared<Graphics::DualTextureMaterialContent>();
    twoTextures->setTextureProperty(
        std::make_shared<Microsoft::Xna::Framework::Content::Pipeline::ExternalReference<TextureContent>>("one.tga"));
    twoTextures->setTexture2Property(
        std::make_shared<Microsoft::Xna::Framework::Content::Pipeline::ExternalReference<TextureContent>>("two.tga"));
    RecordingContext both;
    const std::string dualResult = DescribeMaterial(dual.Process(twoTextures, both), "DualTextureMaterialContent");
    EXPECT_EQ(dualResult + " built=" + both.Built(), Expected("materialprocessor/two_textures"));

    Processors::MaterialProcessor plain;
    RecordingContext none;
    const std::string plainResult =
        DescribeMaterial(plain.Process(std::make_shared<Graphics::MaterialContent>(), none), "MaterialContent");
    EXPECT_EQ(plainResult + " built=" + none.Built(), Expected("materialprocessor/base_material"));
}

TEST(XnaMaterialProcessor, ForwardsItsPropertiesAndBuildsTheEffect)
{
    Processors::MaterialProcessor processor;
    processor.setColorKeyColorProperty(Color(1, 2, 3, 4));
    processor.setColorKeyEnabledProperty(false);
    processor.setGenerateMipmapsProperty(false);
    processor.setPremultiplyTextureAlphaProperty(false);
    processor.setResizeTexturesToPowerOfTwoProperty(true);
    processor.setTextureFormatProperty(TextureProcessorOutputFormat::NoChange);
    auto material = std::make_shared<Graphics::BasicMaterialContent>();
    material->setTextureProperty(
        std::make_shared<Microsoft::Xna::Framework::Content::Pipeline::ExternalReference<TextureContent>>("cat.tga"));
    RecordingContext context;
    (void)processor.Process(material, context);
    EXPECT_EQ(context.Built(), Expected("materialprocessor/properties_forwarded"));

    Processors::MaterialProcessor effects;
    auto effectMaterial = std::make_shared<Graphics::EffectMaterialContent>();
    effectMaterial->setEffectProperty(
        std::make_shared<Microsoft::Xna::Framework::Content::Pipeline::ExternalReference<Graphics::EffectContent>>(
            "shader.fx"));
    RecordingContext effectContext;
    const std::shared_ptr<Graphics::MaterialContent> built = effects.Process(effectMaterial, effectContext);
    EXPECT_NE(effectMaterial->getCompiledEffectProperty(), nullptr);
    EXPECT_EQ(effectContext.Built(),
              Expected("materialprocessor/effect_material")
                  .substr(Expected("materialprocessor/effect_material").find("built=") + 6));
    EXPECT_EQ(built, effectMaterial);
}

TEST(XnaMaterialProcessor, RefusesANullMaterial)
{
    EXPECT_EQ(Result([]
                     {
                         Processors::MaterialProcessor processor;
                         RecordingContext context;
                         return DescribeMaterial(processor.Process(nullptr, context), "MaterialContent");
                     }),
              Expected("materialprocessor/null_input"));
}

namespace
{
    /** @brief A compiler that answers what a test tells it to, so the processor can be measured. */
    class ScriptedCompiler final : public CNA::Content::Pipeline::EffectCompilerService
    {
    public:
        /** @brief What the next compile answers. */
        CNA::Content::Pipeline::EffectCompileResult result;

        /** @brief What the last compile was asked for. */
        mutable CNA::Content::Pipeline::EffectCompileRequest request;

        /** @brief Whether the compiler reports itself available. */
        bool available = true;

        [[nodiscard]] CNA::Content::Pipeline::EffectCompilerIdentity Identity() const override { return {}; }
        [[nodiscard]] bool Available() const override { return available; }
        [[nodiscard]] std::string UnavailableReason() const override
        {
            return available ? std::string() : "no effect compiler was found";
        }
        [[nodiscard]] CNA::Content::Pipeline::EffectCompileResult Compile(
            const CNA::Content::Pipeline::EffectCompileRequest& compileRequest) const override
        {
            request = compileRequest;
            return result;
        }
    };
}

TEST(XnaEffectProcessor, DefaultsMatchXna)
{
    const auto compiler = std::make_shared<ScriptedCompiler>();
    const Processors::EffectProcessor processor(compiler);
    static const std::map<Processors::EffectProcessorDebugMode, std::string> modes = {
        {Processors::EffectProcessorDebugMode::Auto, "Auto"},
        {Processors::EffectProcessorDebugMode::Debug, "Debug"},
        {Processors::EffectProcessorDebugMode::Optimize, "Optimize"}};
    EXPECT_EQ("DebugMode=" + modes.at(processor.getDebugModeProperty()) + " Defines=" +
                  (processor.getDefinesProperty().empty() ? "null" : processor.getDefinesProperty()),
              Expected("processor/EffectProcessor"));

    Processors::EffectProcessor defined(compiler);
    defined.setDefinesProperty("A=1;B");
    EXPECT_EQ("defines=" + defined.getDefinesProperty() + " debug=" + modes.at(defined.getDebugModeProperty()),
              Expected("processor/effect_defines"));
}

TEST(XnaEffectProcessor, CompilesThroughTheCanonicalCompiler)
{
    const auto compiler = std::make_shared<ScriptedCompiler>();
    compiler->result.succeeded = true;
    compiler->result.bytecode = {0xCF, 0x0B, 0xF0, 0xBC, 0x01};
    Processors::EffectProcessor processor(compiler);
    processor.setDefinesProperty("TINT=float4(0,1,0,1);FLAG");
    auto effect = std::make_shared<Graphics::EffectContent>();
    effect->setEffectCodeProperty("technique T { }");
    effect->setIdentityProperty(
        Microsoft::Xna::Framework::Content::Pipeline::ContentIdentity("shader.fx"));
    RecordingContext context;
    const std::shared_ptr<Processors::CompiledEffectContent> compiled = processor.Process(effect, context);
    ASSERT_NE(compiled, nullptr);
    EXPECT_EQ(compiled->GetEffectCode(), compiler->result.bytecode);
    EXPECT_EQ(compiler->request.defines.at("TINT"), "float4(0,1,0,1)");
    EXPECT_EQ(compiler->request.defines.at("FLAG"), "");
    EXPECT_EQ(compiler->request.source.filename().string(), "shader.fx");
    // The context says HiDef and Debug, so the compile follows both.
    EXPECT_EQ(compiler->request.profile, CNA::Content::Pipeline::EffectSourceProfile::HiDef);
    EXPECT_TRUE(compiler->request.debugInformation);
}

TEST(XnaEffectProcessor, RefusalsMatchXna)
{
    const auto compiler = std::make_shared<ScriptedCompiler>();
    compiler->result.succeeded = false;
    CNA::Content::Pipeline::EffectCompilerDiagnostic diagnostic;
    diagnostic.file = "bad.fx";
    diagnostic.line = 1;
    diagnostic.column = 6;
    diagnostic.code = "X3000";
    diagnostic.message = "invalid target or usage string";
    compiler->result.diagnostics.push_back(diagnostic);
    Processors::EffectProcessor processor(compiler);
    auto effect = std::make_shared<Graphics::EffectContent>();
    effect->setEffectCodeProperty("this is not an effect");
    effect->setIdentityProperty(Microsoft::Xna::Framework::Content::Pipeline::ContentIdentity("bad.fx"));
    RecordingContext context;
    EXPECT_EQ(Result([&] { return std::string(processor.Process(effect, context) == nullptr ? "null" : "compiled"); }),
              Expected("effectprocessor/compile_error"));

    Processors::EffectProcessor refusing(compiler);
    EXPECT_EQ(Result([&]
                     {
                         RecordingContext none;
                         return std::string(refusing.Process(nullptr, none) == nullptr ? "null" : "compiled");
                     }),
              Expected("effectprocessor/null_input"));
}

TEST(XnaFontProcessors, DefaultsMatchXna)
{
    const Processors::FontTextureProcessor processor;
    static const std::map<TextureProcessorOutputFormat, std::string> formats = {
        {TextureProcessorOutputFormat::NoChange, "NoChange"},
        {TextureProcessorOutputFormat::Color, "Color"},
        {TextureProcessorOutputFormat::DxtCompressed, "DxtCompressed"}};
    // The oracle prints the character itself, which for the default is a space.
    EXPECT_EQ(std::string("FirstCharacter=") +
                  static_cast<char>(processor.getFirstCharacterProperty()) + " PremultiplyAlpha=" +
                  (processor.getPremultiplyAlphaProperty() ? "True" : "False") + " TextureFormat=" +
                  formats.at(processor.getTextureFormatProperty()),
              Expected("processor/FontTextureProcessor"));

    EXPECT_EQ("first=U+" + [&]
              {
                  std::ostringstream text;
                  text << std::uppercase << std::hex << std::setfill('0') << std::setw(4)
                       << static_cast<int>(processor.getFirstCharacterProperty());
                  return text.str();
              }(),
              Expected("processor/font_texture_first_character"));

    EXPECT_EQ("", Expected("processor/FontDescriptionProcessor"));
}

TEST(XnaFontProcessors, GlyphIndicesFollowFirstCharacter)
{
    /** @brief A processor that exposes the protected mapping, as a derived processor would. */
    class Probe : public Processors::FontTextureProcessor
    {
    public:
        using FontTextureProcessor::GetCharacterForIndex;
    };
    const auto codePoint = [](SharpRuntime::charcs value)
    {
        std::ostringstream text;
        text << std::uppercase << std::hex << std::setfill('0') << std::setw(4) << static_cast<int>(value);
        return "U+" + text.str();
    };
    Probe probe;
    EXPECT_EQ("0=" + codePoint(probe.GetCharacterForIndex(0)) + " 1=" + codePoint(probe.GetCharacterForIndex(1)) +
                  " 5=" + codePoint(probe.GetCharacterForIndex(5)),
              Expected("fontprocessor/texture_character_for_index"));

    Probe moved;
    moved.setFirstCharacterProperty(u'a');
    EXPECT_EQ("0=" + codePoint(moved.GetCharacterForIndex(0)) + " 3=" + codePoint(moved.GetCharacterForIndex(3)),
              Expected("fontprocessor/texture_first_character_set"));
}

TEST(XnaFontProcessors, RefusalsMatchXna)
{
    EXPECT_EQ(Result([]
                     {
                         Processors::FontDescriptionProcessor processor;
                         RecordingContext context;
                         return std::string(processor.Process(nullptr, context) == nullptr ? "null" : "built");
                     }),
              Expected("fontprocessor/description_null"));

    EXPECT_EQ(Result([]
                     {
                         Processors::FontTextureProcessor processor;
                         RecordingContext context;
                         return std::string(processor.Process(nullptr, context) == nullptr ? "null" : "built");
                     }),
              Expected("fontprocessor/texture_null"));

    EXPECT_EQ(Result([]
                     {
                         Processors::FontDescriptionProcessor processor;
                         RecordingContext context;
                         auto description = std::make_shared<Graphics::FontDescription>("Arial", 12.0f, 0.0f);
                         (void)processor.Process(description, context);
                         return std::string("accepted");
                     }),
              Expected("fontprocessor/description_no_characters"));

    EXPECT_EQ(Result([]
                     {
                         Processors::FontDescriptionProcessor processor;
                         RecordingContext context;
                         auto description =
                             std::make_shared<Graphics::FontDescription>("No Such Font At All", 12.0f, 0.0f);
                         description->getCharactersProperty().insert(u'A');
                         (void)processor.Process(description, context);
                         return std::string("accepted");
                     }),
              Expected("fontprocessor/description_missing_font"));

    EXPECT_EQ(Result([]
                     {
                         Processors::FontTextureProcessor processor;
                         RecordingContext context;
                         auto texture = std::make_shared<Texture2DContent>();
                         texture->getMipmapsProperty().Add(std::make_shared<PixelBitmapContent<Color>>(4, 4));
                         (void)processor.Process(texture, context);
                         return std::string("accepted");
                     }),
              Expected("fontprocessor/texture_empty"));
}

TEST(XnaFontProcessors, ATextureStripBecomesOneGlyphPerRun)
{
    Processors::FontTextureProcessor processor;
    auto texture = std::make_shared<Texture2DContent>();
    auto bitmap = std::make_shared<PixelBitmapContent<Color>>(8, 4);
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            bitmap->SetPixel(x, y, Color(255, 0, 255, 255));
        }
    }
    for (int y = 1; y < 3; ++y)
    {
        bitmap->SetPixel(1, y, Color(255, 255, 255, 255));
        bitmap->SetPixel(2, y, Color(255, 255, 255, 255));
        bitmap->SetPixel(5, y, Color(255, 255, 255, 255));
    }
    texture->getMipmapsProperty().Add(bitmap);
    RecordingContext context;
    const std::shared_ptr<Processors::SpriteFontContent> font = processor.Process(texture, context);
    ASSERT_NE(font, nullptr);
    EXPECT_EQ("type=SpriteFontContent", Expected("fontprocessor/texture_strip"));
    // What XNA answered is a SpriteFontContent with nothing public to compare, so the two runs of
    // non-border columns are asserted here rather than against the corpus.
    EXPECT_EQ(font->Data().characters.size(), 2u);
    EXPECT_EQ(font->Data().characters[0], processor.getFirstCharacterProperty());
    EXPECT_EQ(font->Data().glyphBounds.size(), 2u);
}

namespace
{
    /** @brief The oracle's own triangle mesh: the same positions, indices and channels. */
    std::shared_ptr<Graphics::MeshContent> TriangleMesh()
    {
        auto mesh = std::make_shared<Graphics::MeshContent>();
        mesh->setNameProperty("Mesh");
        mesh->getPositionsProperty().Add(Vector3(0, 0, 0));
        mesh->getPositionsProperty().Add(Vector3(1, 0, 0));
        mesh->getPositionsProperty().Add(Vector3(0, 1, 0));
        auto geometry = std::make_shared<Graphics::GeometryContent>();
        mesh->getGeometryProperty().Add(geometry);
        geometry->getVerticesProperty().AddRange({0, 1, 2});
        geometry->getIndicesProperty().AddRange({0, 1, 2});
        geometry->getVerticesProperty().getChannelsProperty().Add<Vector3>(
            Graphics::VertexChannelNames::Normal(), {Vector3(0, 0, 1), Vector3(0, 0, 1), Vector3(0, 0, 1)});
        geometry->getVerticesProperty().getChannelsProperty().Add<Vector2>(
            Graphics::VertexChannelNames::TextureCoordinate(0),
            {Vector2(0, 0), Vector2(1, 0), Vector2(0, 1)});
        return mesh;
    }

    std::shared_ptr<Graphics::NodeContent> TriangleScene()
    {
        auto root = std::make_shared<Graphics::NodeContent>();
        root->setNameProperty("Root");
        root->getChildrenProperty().Add(TriangleMesh());
        return root;
    }

    /** @brief The oracle's DescribeModel, reproduced. */
    std::string DescribeModel(const std::shared_ptr<Processors::ModelContent>& model)
    {
        const auto translation = [](const Matrix& matrix)
        { return "(" + Number(matrix.M41) + "," + Number(matrix.M42) + "," + Number(matrix.M43) + ")"; };
        std::string text = "bones=" + std::to_string(model->getBonesProperty().size()) + " meshes=" +
                           std::to_string(model->getMeshesProperty().size()) + " root=" +
                           (model->getRootProperty() == nullptr ? "null" : model->getRootProperty()->getNameProperty()) +
                           " tag=" + (model->getTagProperty().Empty() ? "null" : "set");
        for (const std::shared_ptr<Processors::ModelBoneContent>& bone : model->getBonesProperty())
        {
            text += " bone[" + std::to_string(bone->getIndexProperty()) + "]=" +
                    (bone->getNameProperty().empty() ? "null" : bone->getNameProperty()) + ":" +
                    translation(bone->getTransformProperty()) + ":parent=" +
                    (bone->getParentProperty() == nullptr
                         ? "null"
                         : std::to_string(bone->getParentProperty()->getIndexProperty())) +
                    ":children=" + std::to_string(bone->getChildrenProperty().size());
        }
        for (const std::shared_ptr<Processors::ModelMeshContent>& mesh : model->getMeshesProperty())
        {
            text += " mesh=" + (mesh->getNameProperty().empty() ? "null" : mesh->getNameProperty()) + ":parts=" +
                    std::to_string(mesh->getMeshPartsProperty().size()) + ":bone=" +
                    (mesh->getParentBoneProperty() == nullptr
                         ? "null"
                         : std::to_string(mesh->getParentBoneProperty()->getIndexProperty())) +
                    ":sphere=" + Number(mesh->getBoundingSphereProperty().Radius) + ":source=" +
                    (mesh->getSourceMeshProperty() == nullptr ? "null"
                                                              : mesh->getSourceMeshProperty()->getNameProperty());
            for (const std::shared_ptr<Processors::ModelMeshPartContent>& part : mesh->getMeshPartsProperty())
            {
                const std::shared_ptr<Processors::VertexBufferContent>& buffer = part->getVertexBufferProperty();
                text += " part=" + std::to_string(part->getNumVerticesProperty()) + "v/" +
                        std::to_string(part->getPrimitiveCountProperty()) + "p/start=" +
                        std::to_string(part->getStartIndexProperty()) + "/offset=" +
                        std::to_string(part->getVertexOffsetProperty()) + "/indices=" +
                        (part->getIndexBufferProperty() == nullptr
                             ? "null"
                             : std::to_string(part->getIndexBufferProperty()->getCountProperty())) +
                        "/material=" +
                        (part->getMaterialProperty() == nullptr
                             ? "null"
                             : std::filesystem::path(part->getMaterialProperty()->GetTypeName()).string().substr(
                                   part->getMaterialProperty()->GetTypeName().rfind('.') + 1)) +
                        "/stride=" +
                        (buffer == nullptr || !buffer->getVertexDeclarationProperty()->getVertexStrideProperty()
                             ? "null"
                             : std::to_string(*buffer->getVertexDeclarationProperty()->getVertexStrideProperty())) +
                        "/elements=" +
                        (buffer == nullptr
                             ? "null"
                             : std::to_string(
                                   buffer->getVertexDeclarationProperty()->getVertexElementsProperty()
                                       .getCountProperty())) +
                        "/bytes=" +
                        (buffer == nullptr ? "null" : std::to_string(buffer->getVertexDataProperty().size()));
                if (buffer != nullptr)
                {
                    using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
                    using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
                    static const std::map<VertexElementUsage, std::string> usages = {
                        {VertexElementUsage::Position, "Position"},
                        {VertexElementUsage::Normal, "Normal"},
                        {VertexElementUsage::TextureCoordinate, "TextureCoordinate"},
                        {VertexElementUsage::Color, "Color"},
                        {VertexElementUsage::Tangent, "Tangent"},
                        {VertexElementUsage::Binormal, "Binormal"},
                        {VertexElementUsage::BlendIndices, "BlendIndices"},
                        {VertexElementUsage::BlendWeight, "BlendWeight"}};
                    static const std::map<VertexElementFormat, std::string> formats = {
                        {VertexElementFormat::Single, "Single"},
                        {VertexElementFormat::Vector2, "Vector2"},
                        {VertexElementFormat::Vector3, "Vector3"},
                        {VertexElementFormat::Vector4, "Vector4"},
                        {VertexElementFormat::Color, "Color"}};
                    const auto& elements = buffer->getVertexDeclarationProperty()->getVertexElementsProperty();
                    for (SharpRuntime::intcs i = 0; i < elements.getCountProperty(); ++i)
                    {
                        const Microsoft::Xna::Framework::Graphics::VertexElement& element = elements[i];
                        text += " element=" + usages.at(element.getVertexElementUsageProperty()) +
                                std::to_string(element.getUsageIndexProperty()) + ":" +
                                formats.at(element.getVertexElementFormatProperty()) + "@" +
                                std::to_string(element.getOffsetProperty());
                    }
                }
            }
        }
        return text;
    }

    /** @brief The oracle's DescribeModelFull: every bone matrix and the vertex data itself. */
    std::string DescribeModelFull(const std::shared_ptr<Processors::ModelContent>& model)
    {
        std::string text = DescribeModel(model);
        const auto matrix = [](const Matrix& m)
        {
            const std::array<SharpRuntime::Single, 16> values = {m.M11, m.M12, m.M13, m.M14, m.M21, m.M22,
                                                                 m.M23, m.M24, m.M31, m.M32, m.M33, m.M34,
                                                                 m.M41, m.M42, m.M43, m.M44};
            std::string out = "[";
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                out += (i == 0 ? "" : ",") + Number(values[i]);
            }
            return out + "]";
        };
        for (const std::shared_ptr<Processors::ModelBoneContent>& bone : model->getBonesProperty())
        {
            text += " matrix[" + std::to_string(bone->getIndexProperty()) + "]=" +
                    matrix(bone->getTransformProperty());
        }
        for (const std::shared_ptr<Processors::ModelMeshContent>& mesh : model->getMeshesProperty())
        {
            for (const std::shared_ptr<Processors::ModelMeshPartContent>& part : mesh->getMeshPartsProperty())
            {
                if (part->getVertexBufferProperty() != nullptr)
                {
                    text += " data=" + Hex(part->getVertexBufferProperty()->getVertexDataProperty());
                }
                if (part->getIndexBufferProperty() != nullptr)
                {
                    text += " indices=";
                    const auto& indices = static_cast<const System::Collections::ObjectModel::Collection<
                        SharpRuntime::intcs>&>(*part->getIndexBufferProperty());
                    for (SharpRuntime::intcs i = 0; i < indices.getCountProperty(); ++i)
                    {
                        text += (i == 0 ? "" : ",") + std::to_string(indices[i]);
                    }
                }
            }
        }
        return text;
    }

    /** @brief The X of every position, as the oracle's Positions prints them. */
    std::string PositionsText(const Graphics::MeshContent& mesh)
    {
        const auto& positions =
            static_cast<const System::Collections::ObjectModel::Collection<Vector3>&>(mesh.getPositionsProperty());
        std::string text;
        for (SharpRuntime::intcs i = 0; i < positions.getCountProperty(); ++i)
        {
            text += (text.empty() ? "" : " ") + Number(positions[i].X);
        }
        return "[" + text + "]";
    }

    /** @brief A triangle carrying one colour channel, as the colour cases build it. */
    std::shared_ptr<Graphics::MeshContent> ColouredTriangle(const std::vector<Color>& colours)
    {
        std::shared_ptr<Graphics::MeshContent> mesh = TriangleMesh();
        const auto& geometry = static_cast<const System::Collections::ObjectModel::Collection<
            std::shared_ptr<Graphics::GeometryContent>>&>(mesh->getGeometryProperty())[0];
        geometry->getVerticesProperty().getChannelsProperty().Add<Color>(Graphics::VertexChannelNames::Color(0),
                                                                          colours);
        return mesh;
    }

    /** @brief The floats a vertex-data hex string spells, in order. */
    std::vector<float> FloatsOf(const std::string& hex)
    {
        std::vector<float> values;
        for (std::size_t i = 0; i + 8 <= hex.size(); i += 8)
        {
            std::uint32_t bits = 0;
            for (int byte = 3; byte >= 0; --byte)
            {
                bits = (bits << 8) | static_cast<std::uint32_t>(std::stoul(
                                         hex.substr(i + static_cast<std::size_t>(byte) * 2, 2), nullptr, 16));
            }
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            values.push_back(value);
        }
        return values;
    }

    /** @brief The ` data=` field of a description, or an empty string when it has none. */
    std::string DataOf(const std::string& described)
    {
        const std::size_t start = described.find(" data=");
        if (start == std::string::npos)
        {
            return {};
        }
        const std::size_t end = described.find(' ', start + 6);
        return described.substr(start + 6, end == std::string::npos ? end : end - start - 6);
    }

    /** @brief A scene of one named root over one mesh. */
    std::shared_ptr<Graphics::NodeContent> SceneOf(const std::shared_ptr<Graphics::MeshContent>& mesh)
    {
        auto root = std::make_shared<Graphics::NodeContent>();
        root->setNameProperty("Root");
        root->getChildrenProperty().Add(mesh);
        return root;
    }
}

TEST(XnaModelProcessor, DefaultsMatchXna)
{
    const Processors::ModelProcessor processor;
    static const std::map<TextureProcessorOutputFormat, std::string> formats = {
        {TextureProcessorOutputFormat::NoChange, "NoChange"},
        {TextureProcessorOutputFormat::Color, "Color"},
        {TextureProcessorOutputFormat::DxtCompressed, "DxtCompressed"}};
    const Color key = processor.getColorKeyColorProperty();
    EXPECT_EQ("ColorKeyColor={R:" + std::to_string(static_cast<int>(key.getRProperty())) + " G:" +
                  std::to_string(static_cast<int>(key.getGProperty())) + " B:" +
                  std::to_string(static_cast<int>(key.getBProperty())) + " A:" +
                  std::to_string(static_cast<int>(key.getAProperty())) + "} ColorKeyEnabled=" +
                  (processor.getColorKeyEnabledProperty() ? "True" : "False") +
                  " DefaultEffect=BasicEffect GenerateMipmaps=" +
                  (processor.getGenerateMipmapsProperty() ? "True" : "False") + " GenerateTangentFrames=" +
                  (processor.getGenerateTangentFramesProperty() ? "True" : "False") + " PremultiplyTextureAlpha=" +
                  (processor.getPremultiplyTextureAlphaProperty() ? "True" : "False") +
                  " PremultiplyVertexColors=" +
                  (processor.getPremultiplyVertexColorsProperty() ? "True" : "False") +
                  " ResizeTexturesToPowerOfTwo=" +
                  (processor.getResizeTexturesToPowerOfTwoProperty() ? "True" : "False") + " RotationX=" +
                  Number(processor.getRotationXProperty()) + " RotationY=" +
                  Number(processor.getRotationYProperty()) + " RotationZ=" +
                  Number(processor.getRotationZProperty()) + " Scale=" + Number(processor.getScaleProperty()) +
                  " SwapWindingOrder=" + (processor.getSwapWindingOrderProperty() ? "True" : "False") +
                  " TextureFormat=" + formats.at(processor.getTextureFormatProperty()),
              Expected("processor/ModelProcessor"));
}

TEST(XnaModelProcessor, ProcessesASceneAsXnaDoes)
{
    Processors::ModelProcessor processor;
    RecordingContext context;
    const std::shared_ptr<Processors::ModelContent> model = processor.Process(TriangleScene(), context);
    const std::string described = DescribeModel(model);
    EXPECT_EQ(described + " built=" + context.Built(), Expected("modelprocessor/triangle"));

    Processors::ModelProcessor hierarchy;
    auto root = std::make_shared<Graphics::NodeContent>();
    root->setNameProperty("Root");
    root->setTransformProperty(Matrix::CreateTranslation(1, 0, 0));
    auto bone = std::make_shared<Graphics::BoneContent>();
    bone->setNameProperty("Bone");
    bone->setTransformProperty(Matrix::CreateTranslation(0, 2, 0));
    root->getChildrenProperty().Add(bone);
    bone->getChildrenProperty().Add(TriangleMesh());
    RecordingContext hierarchyContext;
    EXPECT_EQ(DescribeModel(hierarchy.Process(root, hierarchyContext)),
              Expected("modelprocessor/bone_hierarchy"));

    Processors::ModelProcessor swapped;
    swapped.setSwapWindingOrderProperty(true);
    RecordingContext swappedContext;
    EXPECT_EQ(DescribeModel(swapped.Process(TriangleScene(), swappedContext)),
              Expected("modelprocessor/swap_winding"));

    // Swapping reverses the whole triangle and the cache optimization then renumbers its vertices,
    // which is why the index buffer still reads 0,1,2 and the vertices came out reversed.
    Processors::ModelProcessor swappedDetail;
    swappedDetail.setSwapWindingOrderProperty(true);
    RecordingContext swappedDetailContext;
    EXPECT_EQ(DescribeModelFull(swappedDetail.Process(TriangleScene(), swappedDetailContext)),
              Expected("modelprocessor/swap_winding_detail"));

    Processors::ModelProcessor empty;
    RecordingContext emptyContext;
    EXPECT_EQ(DescribeModel(empty.Process(std::make_shared<Graphics::NodeContent>(), emptyContext)),
              Expected("modelprocessor/empty_node"));
}

TEST(XnaModelProcessor, TrianglesAndVerticesComeOutInTheOrderXnaGivesThem)
{
    // Two triangles through the processor: their order and their vertices say that the cache
    // ordering runs here, reversing the triangles and renumbering the vertices.
    const std::shared_ptr<Graphics::MeshBuilder> builder = Graphics::MeshBuilder::StartMesh("Quad");
    builder->setMergeDuplicatePositionsProperty(true);
    const SharpRuntime::intcs normals =
        builder->CreateVertexChannel<Vector3>(Graphics::VertexChannelNames::Normal());
    const SharpRuntime::intcs coords =
        builder->CreateVertexChannel<Vector2>(Graphics::VertexChannelNames::TextureCoordinate(0));
    const SharpRuntime::intcs a = builder->CreatePosition(0, 0, 0);
    const SharpRuntime::intcs b = builder->CreatePosition(1, 0, 0);
    const SharpRuntime::intcs c = builder->CreatePosition(1, 1, 0);
    const SharpRuntime::intcs d = builder->CreatePosition(0, 1, 0);
    const std::vector<SharpRuntime::intcs> corners = {a, b, c, a, c, d};
    const std::vector<Vector2> uv = {Vector2(0, 0), Vector2(1, 0), Vector2(1, 1),
                                     Vector2(0, 0), Vector2(1, 1), Vector2(0, 1)};
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        builder->SetVertexChannelData(normals,
                                      Microsoft::Xna::Framework::Content::Pipeline::Box<Vector3>(Vector3(0, 0, 1)));
        builder->SetVertexChannelData(coords,
                                      Microsoft::Xna::Framework::Content::Pipeline::Box<Vector2>(uv[i]));
        builder->AddTriangleVertex(corners[i]);
    }
    auto root = std::make_shared<Graphics::NodeContent>();
    root->setNameProperty("Root");
    root->getChildrenProperty().Add(builder->FinishMesh());
    Processors::ModelProcessor processor;
    RecordingContext context;
    EXPECT_EQ(DescribeModelFull(processor.Process(root, context)), Expected("modelprocessor/quad_ordering"));
}

TEST(XnaModelProcessor, RefusalsMatchXna)
{
    EXPECT_EQ(Result([]
                     {
                         Processors::ModelProcessor processor;
                         RecordingContext context;
                         return std::string(processor.Process(nullptr, context) == nullptr ? "null" : "built");
                     }),
              Expected("modelprocessor/null_input"));

    EXPECT_EQ(Result([]
                     {
                         Processors::ModelProcessor processor;
                         processor.setDefaultEffectProperty(
                             Processors::MaterialProcessorDefaultEffect::SkinnedEffect);
                         RecordingContext context;
                         (void)processor.Process(TriangleScene(), context);
                         return std::string("built");
                     }),
              Expected("modelprocessor/default_effect_skinned"));
}

TEST(XnaModelProcessor, ScaleAndRotationAreBakedIntoTheScene)
{
    Processors::ModelProcessor processor;
    processor.setScaleProperty(2.0f);
    processor.setRotationYProperty(90.0f);
    auto root = std::make_shared<Graphics::NodeContent>();
    root->setNameProperty("Root");
    root->setTransformProperty(Matrix::CreateTranslation(1, 0, 0));
    auto bone = std::make_shared<Graphics::BoneContent>();
    bone->setNameProperty("Bone");
    bone->setTransformProperty(Matrix::CreateTranslation(0, 3, 0));
    root->getChildrenProperty().Add(bone);
    const std::shared_ptr<Graphics::MeshContent> mesh = TriangleMesh();
    bone->getChildrenProperty().Add(mesh);
    RecordingContext context;
    const std::shared_ptr<Processors::ModelContent> model = processor.Process(root, context);
    // The source mesh is transformed in place, which is how the same scene answers twice over.
    EXPECT_EQ(DescribeModelFull(model) + " source=" + PositionsText(*mesh),
              Expected("modelprocessor/scale_rotation_detail"));

    Processors::ModelProcessor plain;
    const std::shared_ptr<Graphics::MeshContent> untouched = TriangleMesh();
    RecordingContext plainContext;
    EXPECT_EQ(DescribeModelFull(plain.Process(SceneOf(untouched), plainContext)) + " source=" +
                  PositionsText(*untouched),
              Expected("modelprocessor/identity_detail"));
}

TEST(XnaModelProcessor, ThreeRotationsComposeAsXnaComposesThem)
{
    // The three rotations at once carry float error XNA's own matrix inversion decides, so this
    // one case is compared as numbers within a tolerance rather than as the corpus's own text.
    Processors::ModelProcessor processor;
    processor.setRotationXProperty(30.0f);
    processor.setRotationYProperty(45.0f);
    processor.setRotationZProperty(60.0f);
    auto root = std::make_shared<Graphics::NodeContent>();
    root->setNameProperty("Root");
    root->setTransformProperty(Matrix::CreateTranslation(1, 2, 3));
    root->getChildrenProperty().Add(TriangleMesh());
    RecordingContext context;
    const std::shared_ptr<Processors::ModelContent> model = processor.Process(root, context);
    const std::string expected = Expected("modelprocessor/rotation_order");
    const std::regex translation(R"(bone\[0\]=Root:\(([^,]+),([^,]+),([^)]+)\))");
    std::smatch match;
    ASSERT_TRUE(std::regex_search(expected, match, translation));
    const Matrix& measured = model->getBonesProperty()[0]->getTransformProperty();
    EXPECT_NEAR(measured.M41, std::stof(match[1].str()), 1e-5f);
    EXPECT_NEAR(measured.M42, std::stof(match[2].str()), 1e-5f);
    EXPECT_NEAR(measured.M43, std::stof(match[3].str()), 1e-5f);
    // The geometry is compared as numbers, not as text: this case is the one whose matrix
    // products carry the extended-precision intermediates the x86 .NET Framework evaluated the
    // measurement in, which leaves one position component three units in the last place away
    // from what strict single-precision arithmetic answers.
    const std::vector<float> measuredData = FloatsOf(DataOf(DescribeModelFull(model)));
    const std::vector<float> expectedData = FloatsOf(DataOf(expected));
    ASSERT_EQ(measuredData.size(), expectedData.size());
    for (std::size_t i = 0; i < expectedData.size(); ++i)
    {
        EXPECT_NEAR(measuredData[i], expectedData[i], 1e-6f) << "float " << i;
    }
}

TEST(XnaModelProcessor, VertexColorsArePremultipliedAsXnaDoes)
{
    const std::vector<Color> colours = {Color(255, 128, 64, 128), Color(255, 255, 255, 255), Color(0, 0, 0, 0)};
    Processors::ModelProcessor processor;
    RecordingContext context;
    EXPECT_EQ(DescribeModelFull(processor.Process(SceneOf(ColouredTriangle(colours)), context)),
              Expected("modelprocessor/vertex_colors"));

    Processors::ModelProcessor straight;
    straight.setPremultiplyVertexColorsProperty(false);
    RecordingContext straightContext;
    EXPECT_EQ(DescribeModelFull(straight.Process(SceneOf(ColouredTriangle(colours)), straightContext)),
              Expected("modelprocessor/vertex_colors_unpremultiplied"));

    // 129 at alpha 3 answers 1, which is what says the remainder is dropped and not rounded.
    Processors::ModelProcessor rounding;
    RecordingContext roundingContext;
    EXPECT_EQ(DescribeModelFull(rounding.Process(
                  SceneOf(ColouredTriangle({Color(1, 3, 5, 128), Color(255, 254, 253, 1),
                                            Color(127, 129, 191, 3)})),
                  roundingContext)),
              Expected("modelprocessor/vertex_colors_rounding"));
}

TEST(XnaModelProcessor, TangentFramesAreGeneratedAndRefusedAsXnaDoes)
{
    Processors::ModelProcessor processor;
    processor.setGenerateTangentFramesProperty(true);
    RecordingContext context;
    const std::shared_ptr<Processors::ModelContent> model = processor.Process(TriangleScene(), context);
    const std::string described = DescribeModelFull(model);
    const std::string expected = Expected("modelprocessor/generate_tangent_frames");
    (void)expected;
    // The declaration is compared as XNA wrote it; the frame's own numbers follow, within the
    // tolerance XNA's own orthogonalization leaves (its binormal carries a 4.4e-08 X where the
    // cross product answers zero).
    EXPECT_EQ(described.substr(0, described.find(" matrix[")), expected);
    const std::vector<float> measuredData = FloatsOf(DataOf(described));
    ASSERT_EQ(measuredData.size(), 3u * 14u);
    const std::vector<float> reference = FloatsOf(DataOf(Expected("modelprocessor/tangent_frames_detail")));
    ASSERT_EQ(reference.size(), measuredData.size());
    for (std::size_t i = 0; i < reference.size(); ++i)
    {
        EXPECT_NEAR(measuredData[i], reference[i], 1e-6f) << "float " << i;
    }

    EXPECT_EQ(Result(
                  []
                  {
                      Processors::ModelProcessor bare;
                      bare.setGenerateTangentFramesProperty(true);
                      auto mesh = std::make_shared<Graphics::MeshContent>();
                      mesh->setNameProperty("Mesh");
                      mesh->getPositionsProperty().Add(Vector3(0, 0, 0));
                      mesh->getPositionsProperty().Add(Vector3(1, 0, 0));
                      mesh->getPositionsProperty().Add(Vector3(0, 1, 0));
                      auto geometry = std::make_shared<Graphics::GeometryContent>();
                      mesh->getGeometryProperty().Add(geometry);
                      geometry->getVerticesProperty().AddRange({0, 1, 2});
                      geometry->getIndicesProperty().AddRange({0, 1, 2});
                      geometry->getVerticesProperty().getChannelsProperty().Add<Vector3>(
                          Graphics::VertexChannelNames::Normal(),
                          {Vector3(0, 0, 1), Vector3(0, 0, 1), Vector3(0, 0, 1)});
                      RecordingContext bareContext;
                      (void)bare.Process(SceneOf(mesh), bareContext);
                      return std::string("built");
                  }),
              Expected("modelprocessor/tangent_frames_no_texcoords"));
}

TEST(XnaVertexBufferContent, WritesAndSizesAsXnaDoes)
{
    Processors::VertexBufferContent buffer(24);
    auto declaration = std::make_shared<Processors::VertexDeclarationContent>();
    declaration->getVertexElementsProperty().Add(Microsoft::Xna::Framework::Graphics::VertexElement(
        0, Microsoft::Xna::Framework::Graphics::VertexElementFormat::Vector3,
        Microsoft::Xna::Framework::Graphics::VertexElementUsage::Position, 0));
    buffer.setVertexDeclarationProperty(declaration);
    buffer.Write<Vector3>(0, 12, {Vector3(1, 2, 3), Vector3(4, 5, 6)});
    EXPECT_EQ("bytes=" + std::to_string(buffer.getVertexDataProperty().size()) + " stride=" +
                  (declaration->getVertexStrideProperty() ? std::to_string(*declaration->getVertexStrideProperty())
                                                          : "null") +
                  " elements=" + std::to_string(declaration->getVertexElementsProperty().getCountProperty()) +
                  " data=" + Hex(buffer.getVertexDataProperty()) + " sizeof=" +
                  std::to_string(Processors::VertexBufferContent::SizeOf(
                      System::Type::From<Vector3>())),
              Expected("modelprocessor/vertex_buffer_content"));

    const Processors::VertexBufferContent fresh;
    EXPECT_EQ("bytes=" + std::to_string(fresh.getVertexDataProperty().size()) + " declaration=" +
                  (fresh.getVertexDeclarationProperty() == nullptr ? "null" : "set") + " name=\"" +
                  fresh.getNameProperty() + "\"",
              Expected("modelprocessor/vertex_buffer_defaults"));

    const Processors::VertexDeclarationContent bare;
    EXPECT_EQ("elements=" + std::to_string(bare.getVertexElementsProperty().getCountProperty()) + " stride=" +
                  (bare.getVertexStrideProperty() ? std::to_string(*bare.getVertexStrideProperty()) : "null"),
              Expected("modelprocessor/vertex_declaration_defaults"));
}

TEST(XnaVertexBufferContent, TheUntypedWriteMatchesXna)
{
    Processors::VertexBufferContent buffer(24);
    buffer.Write(0, 12, System::Type::From<Vector3>(),
                 {Microsoft::Xna::Framework::Content::Pipeline::Box<Vector3>(Vector3(1, 2, 3)), Microsoft::Xna::Framework::Content::Pipeline::Box<Vector3>(Vector3(4, 5, 6))});
    const auto refusal = [](const std::function<void()>& body)
    {
        try
        {
            body();
            return std::string("accepted");
        }
        catch (const System::ArgumentException& error)
        {
            (void)error;
            return std::string("ArgumentException");
        }
        catch (const System::NotSupportedException& error)
        {
            (void)error;
            return std::string("NotSupportedException");
        }
    };
    EXPECT_EQ("data=" + Hex(buffer.getVertexDataProperty()) + " wrongType=" +
                  refusal(
                      []
                      {
                          Processors::VertexBufferContent other(24);
                          other.Write(0, 12, System::Type::From<Vector3>(),
                                      {Microsoft::Xna::Framework::Content::Pipeline::Box<Vector2>(Vector2(1, 2)), Microsoft::Xna::Framework::Content::Pipeline::Box<Vector2>(Vector2(3, 4))});
                      }) +
                  " unsupported=" + refusal(
                                        []
                                        {
                                            Processors::VertexBufferContent other(24);
                                            other.Write(0, 4, System::Type::From<std::string>(),
                                                        {Microsoft::Xna::Framework::Content::Pipeline::Box<std::string>(std::string("a"))});
                                        }),
              Expected("modelprocessor/vertex_buffer_write_untyped"));
}

TEST(XnaVertexBufferContent, SizeOfAnswersWhatXnaAnswers)
{
    const auto size = [](System::Type type, const std::string& name)
    {
        try
        {
            return name + "=" + std::to_string(Processors::VertexBufferContent::SizeOf(type));
        }
        catch (const System::ArgumentNullException&)
        {
            return name + "=ArgumentNullException";
        }
        catch (const System::NotSupportedException&)
        {
            return name + "=NotSupportedException";
        }
    };
    // Boolean answers four and Char one: the sizes .NET marshals those values to, not the sizes
    // C++ gives them.
    EXPECT_EQ(size(System::Type::From<Vector2>(), "Vector2") + " " +
                  size(System::Type::From<Vector4>(), "Vector4") + " " +
                  size(System::Type::From<Color>(), "Color") + " " +
                  size(System::Type::From<SharpRuntime::Single>(), "Single") + " " +
                  size(System::Type::From<std::string>(), "String") + " " +
                  size(System::Type::From<SharpRuntime::intcs>(), "Int32") + " " +
                  size(System::Type::From<SharpRuntime::bytecs>(), "Byte") + " " +
                  size(System::Type::From<SharpRuntime::shortcs>(), "Int16") + " " +
                  size(System::Type::From<double>(), "Double") + " " +
                  size(System::Type::From<bool>(), "Boolean") + " " +
                  size(System::Type::From<SharpRuntime::charcs>(), "Char") + " " +
                  size(System::Type::From<Matrix>(), "Matrix") + " " +
                  size(System::Type::From<Microsoft::Xna::Framework::Quaternion>(), "Quaternion") + " " +
                  size(System::Type::From<System::DateTime>(), "DateTime") + " " +
                  size(System::Type::From<SurfaceFormat>(), "SurfaceFormat") + " " +
                  size(System::Type::From<std::vector<Vector3>>(), "Vector3[]") + " " +
                  size(System::Type(), "null"),
              Expected("modelprocessor/vertex_buffer_sizeof_refusals"));
}

// ---- XNAPP-152: a processor's own scene, through ModelProcessor, to an .xnb CNA reads back ----

namespace
{
    /** @brief A processor of a game's own: it builds a scene and hands it to ModelProcessor. */
    class SpinnerProcessor final
        : public Xna::ContentProcessor<std::shared_ptr<Xna::Graphics::NodeContent>,
                                       std::shared_ptr<Processors::ModelContent>>
    {
    public:
        [[nodiscard]] std::shared_ptr<Processors::ModelContent> Process(
            const std::shared_ptr<Xna::Graphics::NodeContent>& input,
            Xna::ContentProcessorContext& context) override
        {
            (void)input;
            const std::shared_ptr<Xna::Graphics::MeshBuilder> builder =
                Xna::Graphics::MeshBuilder::StartMesh("Spinner");
            builder->setMergeDuplicatePositionsProperty(true);
            const SharpRuntime::intcs normals =
                builder->CreateVertexChannel<Vector3>(Xna::Graphics::VertexChannelNames::Normal());
            const SharpRuntime::intcs coords = builder->CreateVertexChannel<Vector2>(
                Xna::Graphics::VertexChannelNames::TextureCoordinate(0));
            const SharpRuntime::intcs a = builder->CreatePosition(0, 0, 0);
            const SharpRuntime::intcs b = builder->CreatePosition(1, 0, 0);
            const SharpRuntime::intcs c = builder->CreatePosition(1, 1, 0);
            const SharpRuntime::intcs d = builder->CreatePosition(0, 1, 0);
            const std::vector<SharpRuntime::intcs> corners = {a, b, c, a, c, d};
            const std::vector<Vector2> uv = {Vector2(0, 0), Vector2(1, 0), Vector2(1, 1),
                                             Vector2(0, 0), Vector2(1, 1), Vector2(0, 1)};
            for (std::size_t i = 0; i < corners.size(); ++i)
            {
                builder->SetVertexChannelData(normals, Xna::Box<Vector3>(Vector3(0, 0, 1)));
                builder->SetVertexChannelData(coords, Xna::Box<Vector2>(uv[i]));
                builder->AddTriangleVertex(corners[i]);
            }
            auto material = std::make_shared<Xna::Graphics::BasicMaterialContent>();
            material->setAlphaProperty(0.25f);
            material->setDiffuseColorProperty(Vector3(0.5f, 0.25f, 0.125f));
            const std::shared_ptr<Xna::Graphics::MeshContent> mesh = builder->FinishMesh();
            const auto& batches = static_cast<const System::Collections::ObjectModel::Collection<
                std::shared_ptr<Xna::Graphics::GeometryContent>>&>(mesh->getGeometryProperty());
            batches[0]->setMaterialProperty(material);
            auto root = std::make_shared<Xna::Graphics::NodeContent>();
            root->setNameProperty("Scene");
            root->getChildrenProperty().Add(mesh);
            Processors::ModelProcessor model;
            return model.Process(root, context);
        }
    };

    /** @brief A directory the test writes its one `.xnb` into and removes afterwards. */
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_xnapp152_" + tag + "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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
}

TEST(XnaModelPipeline, AProcessorsOwnSceneBecomesAnXnbCnaReadsBack)
{
    RecordingContext context;
    SpinnerProcessor processor;
    const std::shared_ptr<Processors::ModelContent> model =
        processor.Process(std::make_shared<Graphics::NodeContent>(), context);
    ASSERT_NE(model, nullptr);

    Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler::ContentCompiler compiler;
    Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler::CompileOptions options;
    options.assetName = "Models/spinner";
    options.outputDirectory = "/out";
    const CNA::Internal::Xnb::XnbAssetWriteResult written =
        compiler.Compile<Processors::ModelContent>(model, options);
    EXPECT_EQ(written.rootReaderName,
              "Microsoft.Xna.Framework.Content.ModelReader, Microsoft.Xna.Framework.Graphics, "
              "Version=4.0.0.0, Culture=neutral, PublicKeyToken=842cf8be1de50553");

    ScratchDirectory scratch("model");
    const std::filesystem::path path = scratch.Path() / "spinner.xnb";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(written.bytes.data()),
                  static_cast<std::streamsize>(written.bytes.size()));
    }
    // Read back through CNA's own reader, which is what a game would do with the file.
    const CNA::Internal::Xnb::XnbCanonicalAsset asset = CNA::Internal::Xnb::DecodeXnbCanonicalAsset(path);
    const auto& read = std::get<CNA::Internal::Xnb::XnbModelData>(asset.value);

    ASSERT_EQ(read.bones.size(), 2u);
    EXPECT_EQ(read.bones[0].name, "Scene");
    EXPECT_EQ(read.bones[0].parent, -1);
    EXPECT_EQ(read.bones[0].children, std::vector<std::int32_t>{1});
    EXPECT_EQ(read.bones[1].name, "Spinner");
    EXPECT_EQ(read.bones[1].parent, 0);
    EXPECT_EQ(read.rootBone, 0);

    ASSERT_EQ(read.meshes.size(), 1u);
    EXPECT_EQ(read.meshes[0].name, "Spinner");
    EXPECT_EQ(read.meshes[0].parentBone, 1);
    ASSERT_EQ(read.meshes[0].parts.size(), 1u);
    const CNA::Internal::Xnb::XnbModelPartData& part = read.meshes[0].parts[0];
    EXPECT_EQ(part.vertexCount, 4);
    EXPECT_EQ(part.primitiveCount, 2);
    EXPECT_EQ(part.startIndex, 0);
    EXPECT_EQ(part.vertexOffset, 0);
    ASSERT_GE(part.vertexBufferResource, 0);
    ASSERT_GE(part.indexBufferResource, 0);
    ASSERT_GE(part.effectResource, 0);

    const auto& vertices = std::get<CNA::Internal::Xnb::XnbVertexBufferData>(
        read.sharedResources[static_cast<std::size_t>(part.vertexBufferResource)].value);
    EXPECT_EQ(vertices.vertexCount, 4u);
    EXPECT_EQ(vertices.declaration.stride, 32);
    ASSERT_EQ(vertices.declaration.elements.size(), 3u);
    EXPECT_EQ(vertices.declaration.elements[0].getVertexElementUsageProperty(),
              Microsoft::Xna::Framework::Graphics::VertexElementUsage::Position);
    EXPECT_EQ(vertices.declaration.elements[2].getVertexElementUsageProperty(),
              Microsoft::Xna::Framework::Graphics::VertexElementUsage::TextureCoordinate);
    EXPECT_EQ(vertices.bytes.size(), 4u * 32u);

    const auto& indices = std::get<CNA::Internal::Xnb::XnbIndexBufferData>(
        read.sharedResources[static_cast<std::size_t>(part.indexBufferResource)].value);
    EXPECT_EQ(indices.indexElementSize, 2u);
    ASSERT_EQ(indices.bytes.size(), 6u * 2u);
    // The cache ordering the processor runs is visible in the file: the second triangle first.
    const std::vector<std::uint8_t> expected = {0, 0, 1, 0, 2, 0, 0, 0, 3, 0, 1, 0};
    EXPECT_EQ(indices.bytes, expected);

    const auto& effect = std::get<CNA::Internal::Xnb::XnbBasicEffectData>(
        read.sharedResources[static_cast<std::size_t>(part.effectResource)].value);
    EXPECT_FLOAT_EQ(effect.alpha, 0.25f);
    EXPECT_FLOAT_EQ(effect.diffuseColor.X, 0.5f);
    EXPECT_FLOAT_EQ(effect.diffuseColor.Y, 0.25f);
    EXPECT_FLOAT_EQ(effect.diffuseColor.Z, 0.125f);
}


// ---- XNAPP-190: the effect importer, and what DebugMode.Auto decides --------------------------

namespace
{
    /** @brief An importer context that records what it is told, as the oracle's driver does. */
    class ProbeImporterContext final : public Xna::ContentImporterContext
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

    /** @brief A recording context whose build configuration the caller chooses. */
    class ConfiguredContext final : public RecordingContext
    {
    public:
        explicit ConfiguredContext(std::string configuration) : configuration_(std::move(configuration)) {}
        [[nodiscard]] std::string getBuildConfigurationProperty() const override { return configuration_; }

    private:
        std::string configuration_;
    };

    /** @brief A directory the effect tests write their sources into. */
    class EffectScratch
    {
    public:
        EffectScratch()
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_xnapp190_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }
        ~EffectScratch()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }
        EffectScratch(const EffectScratch&) = delete;
        EffectScratch& operator=(const EffectScratch&) = delete;
        [[nodiscard]] std::string Write(const std::string& name, const std::string& text) const
        {
            const std::filesystem::path file = path_ / name;
            std::ofstream out(file, std::ios::binary);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            return file.string();
        }
        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    private:
        std::filesystem::path path_;
    };
}

TEST(XnaEffectImporter, ReadsTheSourceAsXnaDoes)
{
    const EffectScratch scratch;
    const std::string source =
        "float4 Main() : COLOR { return float4(1,0,0,1); }\r\ntechnique T { pass P { PixelShader = compile "
        "ps_2_0 Main(); } }\r\n";
    const std::string path = scratch.Write("simple.fx", source);
    Xna::EffectImporter importer;
    ProbeImporterContext context;
    const std::shared_ptr<Graphics::EffectContent> content = importer.Import(path, context);
    ASSERT_NE(content, nullptr);
    ASSERT_TRUE(content->getEffectCodeProperty().has_value());
    EXPECT_EQ("type=EffectContent dependencies=" + std::to_string(context.dependencies.size()) + " identity=" +
                  std::filesystem::path(content->getIdentityProperty().getSourceFilenameProperty())
                      .filename()
                      .string() +
                  "/" + content->getIdentityProperty().getSourceToolProperty() + " name=" +
                  (content->getNameProperty().empty() ? "null" : "set") + " codeIsSource=" +
                  (*content->getEffectCodeProperty() == source ? "True" : "False") + " codeLength=" +
                  std::to_string(content->getEffectCodeProperty()->size()),
              Expected("effectimporter/source"));
}

TEST(XnaEffectImporter, AnIncludeIsLeftToTheCompiler)
{
    const EffectScratch scratch;
    (void)scratch.Write("shared.fxh", "float4 Tint;\r\n");
    const std::string path =
        scratch.Write("uses_include.fx",
                      "#include \"shared.fxh\"\r\nfloat4 Main() : COLOR { return Tint; }\r\ntechnique T { pass P "
                      "{ PixelShader = compile ps_2_0 Main(); } }\r\n");
    Xna::EffectImporter importer;
    ProbeImporterContext context;
    const std::shared_ptr<Graphics::EffectContent> content = importer.Import(path, context);
    ASSERT_TRUE(content->getEffectCodeProperty().has_value());
    EXPECT_EQ("dependencies=" + std::to_string(context.dependencies.size()) + " codeStartsWithInclude=" +
                  (content->getEffectCodeProperty()->rfind("#include", 0) == 0 ? "True" : "False"),
              Expected("effectimporter/include"));
}

TEST(XnaEffectImporter, RefusalsMatchXna)
{
    const EffectScratch scratch;
    Xna::EffectImporter importer;
    ProbeImporterContext context;
    const std::string empty = scratch.Write("empty.fx", "");
    const std::string binary = scratch.Write("binary.fx", std::string("\0\1\2\3\xFF", 5));
    const auto describe = [&importer, &context](const std::string& label, const std::string& path)
    {
        try
        {
            const std::shared_ptr<Graphics::EffectContent> content = importer.Import(path, context);
            return label + "=accepted:" +
                   (content->getEffectCodeProperty().has_value()
                        ? std::to_string(content->getEffectCodeProperty()->size())
                        : std::string("null-code"));
        }
        catch (const System::IO::FileNotFoundException&)
        {
            return label + "=FileNotFoundException";
        }
    };
    const std::string expected = Expected("effectimporter/refusals");
    EXPECT_EQ(expected.substr(0, std::string("missing=FileNotFoundException").size()),
              "missing=FileNotFoundException");
    EXPECT_THROW((void)importer.Import((scratch.Path() / "no_such.fx").string(), context),
                 System::IO::FileNotFoundException);
    // An empty file and a binary one are both accepted, with exactly the bytes they hold.
    EXPECT_EQ(describe("empty", empty) + " " + describe("binary", binary),
              expected.substr(expected.find("empty=")));
}

TEST(XnaEffectProcessor, DebugModeAutoFollowsTheBuildConfiguration)
{
    // Measured, effectprocessor/debugmode: Auto answers what Debug answers when the configuration
    // is Debug and what Optimize answers otherwise, and an explicit mode wins over both.
    const std::string expected = Expected("effectprocessor/debugmode");
    const std::size_t debugBytes = expected.find("auto_debug=") + std::string("auto_debug=").size();
    const std::size_t releaseBytes = expected.find("auto_release=") + std::string("auto_release=").size();
    EXPECT_NE(expected.substr(debugBytes, 3), expected.substr(releaseBytes, 3));

    struct Case
    {
        Processors::EffectProcessorDebugMode mode;
        std::string configuration;
        bool debugInformation;
    };
    for (const Case& probe : std::vector<Case>{
             {Processors::EffectProcessorDebugMode::Auto, "Debug", true},
             {Processors::EffectProcessorDebugMode::Auto, "Release", false},
             {Processors::EffectProcessorDebugMode::Debug, "Release", true},
             {Processors::EffectProcessorDebugMode::Optimize, "Debug", false}})
    {
        const auto compiler = std::make_shared<ScriptedCompiler>();
        compiler->result.succeeded = true;
        compiler->result.bytecode = {1, 2, 3, 4};
        Processors::EffectProcessor processor(compiler);
        processor.setDebugModeProperty(probe.mode);
        auto effect = std::make_shared<Graphics::EffectContent>();
        effect->setEffectCodeProperty("technique T { }");
        effect->setIdentityProperty(Xna::ContentIdentity("shader.fx"));
        ConfiguredContext context(probe.configuration);
        (void)processor.Process(effect, context);
        EXPECT_EQ(compiler->request.debugInformation, probe.debugInformation)
            << probe.configuration << " with mode " << static_cast<int>(probe.mode);
    }
}

// plans/plan_xnapipeline_parity.md XNAPP-021, Phase 13: what a model and a font answer for each
// of XNA's three targets.
//
// The texture route's five legs were measured first, because a texture is the one asset whose
// format visibly depends on the target. These are the other two families a project can aim at a
// target, and the measurement says something worth writing down: **XNA's ModelProcessor and
// FontDescriptionProcessor answer identically on all five legs.** The target is not a no-op
// because nobody looked -- it is a no-op, measured, for these two processors. A CNA processor that
// started varying by target would fail here, which is the point of holding it.
TEST(XnaModelProcessor, EveryTargetAndProfileAnswersWhatXnaAnswers)
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
        Processors::ModelProcessor processor;
        RecordingContext context;
        context.platform_ = platform;
        context.profile_ = profile;
        const std::shared_ptr<Processors::ModelContent> model =
            processor.Process(TriangleScene(), context);
        EXPECT_EQ(DescribeModel(model) + " built=" + context.Built(),
                  Expected("modelprofile/" + label))
            << label;
    }
}

TEST(XnaFontProcessors, EveryTargetAndProfileAnswersWhatXnaAnswers)
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
        EXPECT_EQ(Result([platform = platform, profile = profile]
                         {
                             Processors::FontDescriptionProcessor processor;
                             RecordingContext context;
                             context.platform_ = platform;
                             context.profile_ = profile;
                             // A font family neither side has, deliberately: what a target leg
                             // asks is whether the target changes the answer, and a refusal is an
                             // answer. A successful rasterization is not comparable here, because
                             // XNA resolves a family through the host and the oracle's Wine prefix
                             // has a font list this one does not.
                             auto description = std::make_shared<Graphics::FontDescription>(
                                 "No Such Font At All", 12.0f, 0.0f);
                             description->getCharactersProperty().insert(u'A');
                             description->getCharactersProperty().insert(u'B');
                             description->setIdentityProperty(Xna::ContentIdentity("font.spritefont"));
                             const auto font = processor.Process(description, context);
                             // The oracle prints the runtime type and the type's own declared
                             // properties, of which SpriteFontContent has none.
                             return "type=" + std::string(font == nullptr ? "null" : "SpriteFontContent") +
                                    " properties=";
                         }),
                  Expected("fontprofile/" + label))
            << label;
    }
}

namespace
{
    /**
     * @brief A real fxc, or an empty options object when this machine has none.
     *
     * The whole `.fx` route is measured against XNA's own `EffectProcessor`, which means running
     * the compiler XNA ran: Microsoft's legacy `fxc` at `fx_2_0`. `CNA_FXC` names it when a caller
     * wants a particular one; otherwise the DirectX SDK (June 2010) copy is looked for where the
     * oracle's own tooling keeps it. On a machine with neither, the differential skips rather than
     * pretending a fake compiler measured anything.
     */
    [[nodiscard]] std::optional<CNA::Content::Pipeline::ExternalEffectCompilerOptions> RealFxc()
    {
        CNA::Content::Pipeline::ExternalEffectCompilerOptions options;
        const char* fromEnvironment = std::getenv("CNA_FXC");
        if (fromEnvironment != nullptr && *fromEnvironment != '\0')
        {
            options.executable = fromEnvironment;
        }
        else
        {
            const std::filesystem::path sdk =
                "/rv/tmp/samples/_tools/directx-sdk-june-2010/extract/DXSDK/Utilities/bin/x86/fxc.exe";
            std::error_code error;
            if (!std::filesystem::exists(sdk, error) || error) { return std::nullopt; }
            options.executable = sdk;
        }
        const char* launcher = std::getenv("CNA_FXC_LAUNCHER");
        options.launcher = launcher != nullptr && *launcher != '\0' ? launcher : "wine";
        return options;
    }
}

// plans/plan_xnapipeline_parity.md XNAPP-021, XNAPP-191/192: the `.fx` route against the genuine
// EffectProcessor, with the compiler XNA itself used.
//
// Every other `.fx` test in this tree drives a scripted compiler, which measures the plumbing and
// nothing about the bytes. This one runs Microsoft's legacy fxc through Wine and compares what
// CNA's processor answers with what XNA's answered on the same source -- the byte count and the
// container's first four bytes, which is what the oracle recorded.
TEST(XnaEffectProcessor, TheRealCompilerAnswersWhatXnaAnswered)
{
    const std::optional<CNA::Content::Pipeline::ExternalEffectCompilerOptions> options = RealFxc();
    if (!options.has_value())
    {
        GTEST_SKIP() << "no fxc on this machine; set CNA_FXC to one";
    }
    const std::shared_ptr<const CNA::Content::Pipeline::EffectCompilerService> compiler =
        CNA::Content::Pipeline::MakeExternalEffectCompiler(*options);
    if (!compiler->Available())
    {
        GTEST_SKIP() << "the effect compiler could not be started: " << compiler->UnavailableReason();
    }

    const std::string source = "float4 PS() : COLOR0 { return float4(1,0,0,1); }\n"
                               "technique T { pass P { PixelShader = compile ps_2_0 PS(); } }\n";

    Processors::EffectProcessor processor(compiler);
    auto effect = std::make_shared<Graphics::EffectContent>();
    effect->setEffectCodeProperty(source);
    effect->setIdentityProperty(Xna::ContentIdentity("shader.fx"));
    RecordingContext context;
    const auto compiled = processor.Process(effect, context);
    ASSERT_NE(compiled, nullptr);
    const std::vector<SharpRuntime::bytecs> code = compiled->GetEffectCode();
    ASSERT_GE(code.size(), 4u);
    std::ostringstream head;
    head << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t at = 0; at < 16u; ++at) { head << std::setw(2) << static_cast<int>(code[at]); }

    // The container header is XNA's, exactly: magic 0xBCF00BCF, the offset of the inner token,
    // then two zero dwords. Measured on two unrelated effects and identical in both.
    const std::string xna = Expected("effectprocessor/compile_simple_digest");
    ASSERT_NE(xna.find("head="), std::string::npos) << xna;
    const std::string xnaHead = xna.substr(xna.find("head=") + 5u, 32u);
    EXPECT_EQ(head.str().substr(0, 32), xnaHead.substr(0, 32));
    EXPECT_EQ(head.str().substr(0, 8), "CF0BF0BC");

    // What is *not* identical is the blob behind it, and the reason is measured rather than
    // guessed: XNA compiles from a memory buffer through d3dx9, and CNA runs the `fxc` command
    // line, which embeds source information the memory path does not. The two differ by sixteen
    // bytes for this effect, first at offset 50 of the inner blob, and a relative source name and
    // a working directory do not close it -- only calling D3DXCreateEffectCompiler on a buffer
    // would, which needs a native helper this build does not have (XNAPP-191/192).
    EXPECT_GT(code.size(), 476u)
        << "CNA's inner blob is the larger one; if this now matches XNA's 476 bytes, the route "
           "reached byte parity and this test should assert equality instead";

    // A source fxc rejects is refused with the compiler's own diagnostics, as XNA's is: the
    // recorded case is an InvalidContentException naming the file, the line and the error code.
    Processors::EffectProcessor failing(compiler);
    auto bad = std::make_shared<Graphics::EffectContent>();
    bad->setEffectCodeProperty("this is not an effect");
    bad->setIdentityProperty(Xna::ContentIdentity("bad.fx"));
    RecordingContext badContext;
    try
    {
        (void)failing.Process(bad, badContext);
        FAIL() << "fxc rejects this source and so must the processor";
    }
    catch (const std::exception& error)
    {
        const std::string said(error.what());
        const std::string xna = Expected("effectprocessor/compile_error");
        EXPECT_NE(said.find("bad.fx"), std::string::npos) << said;
        EXPECT_NE(said.find("X3000"), std::string::npos)
            << "XNA answered: " << xna << "\nCNA answered: " << said;
    }
}
