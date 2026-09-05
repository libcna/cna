// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-166: TextureImporter -- what each source format becomes --
// against what the genuine XNA 4.0 importer answers for the same four pixels
// (tests/reference/xna40/graphics/graphics-content-oracle.json, cases textureimporter/*).
//
// What the measurements settle: every integer format answers a Texture2DContent of one face and
// one level whose pixels are RGBA in that order, a portable float map answers Vector4 pixels with
// an alpha of one, the file's own bytes decide how it is read rather than its extension, the
// importer records no dependency, and the identity it stamps names TextureImporter.
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/PixelBitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/TextureImporter.hpp"
#include "System/IO/FileNotFoundException.hpp"

namespace Xna = Microsoft::Xna::Framework::Content::Pipeline;
namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector4;
using Xna::InvalidContentException;
using Xna::TextureImporter;

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

    std::string Expected(const std::string& name)
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
        const auto found = cases.find(name);
        return found == cases.end() ? std::string("<missing case ") + name + ">" : found->second;
    }

    /** @brief The four pixels every probe carries: red, green, blue, half-transparent white. */
    const std::vector<std::array<std::uint8_t, 4>>& ProbePixels()
    {
        static const std::vector<std::array<std::uint8_t, 4>> pixels = {
            {255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 128}};
        return pixels;
    }

    class Scratch
    {
    public:
        Scratch()
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_xnapp166_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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
        [[nodiscard]] std::string Write(const std::string& name, const std::vector<std::uint8_t>& bytes) const
        {
            const std::filesystem::path file = path_ / name;
            std::ofstream out(file, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return file.string();
        }
        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

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

    void AppendUInt32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
        }
    }

    /** @brief The oracle's own TGA: top-left origin, thirty-two bits. */
    std::vector<std::uint8_t> Tga()
    {
        std::vector<std::uint8_t> bytes = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        bytes.push_back(2);
        bytes.push_back(0);
        bytes.push_back(2);
        bytes.push_back(0);
        bytes.push_back(32);
        bytes.push_back(0x28);
        for (const auto& pixel : ProbePixels())
        {
            bytes.push_back(pixel[2]);
            bytes.push_back(pixel[1]);
            bytes.push_back(pixel[0]);
            bytes.push_back(pixel[3]);
        }
        return bytes;
    }

    /** @brief The oracle's own PPM. */
    std::vector<std::uint8_t> Ppm()
    {
        const std::string header = "P6\n2 2\n255\n";
        std::vector<std::uint8_t> bytes(header.begin(), header.end());
        for (const auto& pixel : ProbePixels())
        {
            bytes.push_back(pixel[0]);
            bytes.push_back(pixel[1]);
            bytes.push_back(pixel[2]);
        }
        return bytes;
    }

    /** @brief The oracle's own PFM: bottom-up rows of three little-endian floats. */
    std::vector<std::uint8_t> Pfm()
    {
        const std::string header = "PF\n2 2\n-1.0\n";
        std::vector<std::uint8_t> bytes(header.begin(), header.end());
        for (int row = 1; row >= 0; --row)
        {
            for (int column = 0; column < 2; ++column)
            {
                const auto& pixel = ProbePixels()[static_cast<std::size_t>(row * 2 + column)];
                for (int channel = 0; channel < 3; ++channel)
                {
                    const float value = static_cast<float>(pixel[static_cast<std::size_t>(channel)]) / 255.0f;
                    std::uint32_t bits = 0;
                    std::memcpy(&bits, &value, sizeof(bits));
                    AppendUInt32(bytes, bits);
                }
            }
        }
        return bytes;
    }

    /** @brief A thirty-two-bit bottom-up BMP, and the same bytes without its file header. */
    std::vector<std::uint8_t> Bitmap(bool withFileHeader)
    {
        std::vector<std::uint8_t> body;
        AppendUInt32(body, 40u);
        AppendUInt32(body, 2u);
        AppendUInt32(body, 2u);
        body.push_back(1);
        body.push_back(0);
        body.push_back(32);
        body.push_back(0);
        AppendUInt32(body, 0u);
        AppendUInt32(body, 16u);
        AppendUInt32(body, 2835u);
        AppendUInt32(body, 2835u);
        AppendUInt32(body, 0u);
        AppendUInt32(body, 0u);
        for (int row = 1; row >= 0; --row)
        {
            for (int column = 0; column < 2; ++column)
            {
                const auto& pixel = ProbePixels()[static_cast<std::size_t>(row * 2 + column)];
                body.push_back(pixel[2]);
                body.push_back(pixel[1]);
                body.push_back(pixel[0]);
                body.push_back(pixel[3]);
            }
        }
        if (!withFileHeader)
        {
            return body;
        }
        std::vector<std::uint8_t> bytes = {'B', 'M'};
        AppendUInt32(bytes, static_cast<std::uint32_t>(body.size() + 14u));
        AppendUInt32(bytes, 0u);
        AppendUInt32(bytes, 54u);
        bytes.insert(bytes.end(), body.begin(), body.end());
        return bytes;
    }

    /** @brief The oracle's DescribeTexture, for a one-level texture. */
    std::string Describe(const std::shared_ptr<Graphics::TextureContent>& texture,
                         const ImporterContext& context)
    {
        const auto& faces = static_cast<const System::Collections::ObjectModel::Collection<
            std::shared_ptr<Graphics::MipmapChain>>&>(texture->getFacesProperty());
        const std::shared_ptr<Graphics::BitmapContent> bitmap = (*faces[0])[0];
        Microsoft::Xna::Framework::Graphics::SurfaceFormat format{};
        const bool hasFormat = bitmap->TryGetFormat(format);
        std::string text = "Texture2DContent faces=" +
                           std::to_string(texture->getFacesProperty().getCountProperty()) + " [" +
                           std::to_string(bitmap->getWidthProperty()) + "x" +
                           std::to_string(bitmap->getHeightProperty()) + ":" +
                           (hasFormat ? (format == Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color
                                             ? "Color"
                                             : "Vector4")
                                      : "none") +
                           "]";
        if (hasFormat && format == Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color)
        {
            static const char* digits = "0123456789ABCDEF";
            std::string hex;
            for (const SharpRuntime::bytecs value : bitmap->GetPixelData())
            {
                hex += digits[(value >> 4) & 0xF];
                hex += digits[value & 0xF];
            }
            text += " pixels=" + hex;
        }
        return text + " dependencies=" + std::to_string(context.dependencies.size()) + " identity=" +
               texture->getIdentityProperty().getSourceToolProperty();
    }
}

TEST(XnaTextureImporter, EveryFormatAnswersWhatXnaAnswers)
{
    const Scratch scratch;
    const std::string expected = Expected("textureimporter/formats");
    TextureImporter importer;
    struct Probe
    {
        std::string label;
        std::string path;
    };
    // PNG and JPEG go through the same shared decoder as the rest, and this build can write them,
    // so they are compared too; the lossy JPEG values are the corpus's own.
    std::vector<std::uint8_t> rgba;
    for (const auto& pixel : ProbePixels())
    {
        rgba.insert(rgba.end(), pixel.begin(), pixel.end());
    }
    const std::vector<std::uint8_t> png = CNA::Internal::Graphics::ImageLoader::EncodePng(rgba.data(), 2, 2, 2, 2);
    const std::vector<Probe> probes = {{"png", scratch.Write("probe.png", png)},
                                       {"bmp", scratch.Write("probe.bmp", Bitmap(true))},
                                       {"tga", scratch.Write("probe.tga", Tga())},
                                       {"ppm", scratch.Write("probe.ppm", Ppm())},
                                       {"pfm", scratch.Write("probe.pfm", Pfm())},
                                       {"dib", scratch.Write("probe.dib", Bitmap(false))},
                                       {"wrong_extension", scratch.Write("probe.xyz", png)}};
    for (const Probe& probe : probes)
    {
        ImporterContext context;
        const std::shared_ptr<Graphics::TextureContent> texture = importer.Import(probe.path, context);
        ASSERT_NE(texture, nullptr) << probe.label;
        const std::string described = probe.label + "=[" + Describe(texture, context) + "]";
        // One probe's answer runs from its label to the `]` that closes its identity, since the
        // description itself carries brackets.
        const std::size_t at = expected.find(probe.label + "=[");
        ASSERT_NE(at, std::string::npos) << probe.label;
        const std::size_t identity = expected.find("identity=", at);
        ASSERT_NE(identity, std::string::npos) << probe.label;
        const std::size_t end = expected.find(']', identity);
        ASSERT_NE(end, std::string::npos) << probe.label;
        EXPECT_EQ(described, expected.substr(at, end + 1 - at)) << probe.label;
    }
}

TEST(XnaTextureImporter, APortableFloatMapAnswersItsOwnFloats)
{
    const Scratch scratch;
    TextureImporter importer;
    ImporterContext context;
    const std::shared_ptr<Graphics::TextureContent> texture =
        importer.Import(scratch.Write("pixels.pfm", Pfm()), context);
    const auto& faces = static_cast<const System::Collections::ObjectModel::Collection<
        std::shared_ptr<Graphics::MipmapChain>>&>(texture->getFacesProperty());
    const std::shared_ptr<Graphics::BitmapContent> level = (*faces[0])[0];
    const auto bitmap = std::dynamic_pointer_cast<Graphics::PixelBitmapContent<Vector4>>(level);
    ASSERT_NE(bitmap, nullptr);
    std::string text;
    for (SharpRuntime::intcs y = 0; y < bitmap->getHeightProperty(); ++y)
    {
        for (SharpRuntime::intcs x = 0; x < bitmap->getWidthProperty(); ++x)
        {
            const Vector4 pixel = bitmap->GetPixel(x, y);
            std::ostringstream one;
            one.imbue(std::locale::classic());
            one << "(" << pixel.X << "," << pixel.Y << "," << pixel.Z << "," << pixel.W << ")";
            text += (text.empty() ? "" : " ") + one.str();
        }
    }
    // The rows come back top to bottom, whichever way the file stored them.
    EXPECT_EQ(text, Expected("textureimporter/pfm_pixels"));
}

TEST(XnaTextureImporter, ADdsSourceIsNotReadYet)
{
    // XNA reads a DDS, and the corpus shows exactly what it answers for this one. CNA has no DDS
    // reader on this route yet -- that is XNAPP-165, whose row lists the whole DX9/DX10 surface --
    // so the importer refuses it, and this test holds that refusal until the row lands.
    const std::string expected = Expected("textureimporter/formats");
    EXPECT_NE(expected.find("dds=[Texture2DContent faces=1 [2x2:Color] pixels=FF0000FF00FF00FF0000FFFFFFFFFF80"),
              std::string::npos);
    std::vector<std::uint8_t> dds = {'D', 'D', 'S', ' '};
    AppendUInt32(dds, 124u);
    AppendUInt32(dds, 0x1u | 0x2u | 0x4u | 0x1000u | 0x8u);
    AppendUInt32(dds, 2u);
    AppendUInt32(dds, 2u);
    AppendUInt32(dds, 8u);
    AppendUInt32(dds, 0u);
    AppendUInt32(dds, 0u);
    for (int i = 0; i < 11; ++i)
    {
        AppendUInt32(dds, 0u);
    }
    AppendUInt32(dds, 32u);
    AppendUInt32(dds, 0x1u | 0x40u);
    AppendUInt32(dds, 0u);
    AppendUInt32(dds, 32u);
    AppendUInt32(dds, 0x00FF0000u);
    AppendUInt32(dds, 0x0000FF00u);
    AppendUInt32(dds, 0x000000FFu);
    AppendUInt32(dds, 0xFF000000u);
    AppendUInt32(dds, 0x1000u);
    AppendUInt32(dds, 0u);
    AppendUInt32(dds, 0u);
    AppendUInt32(dds, 0u);
    AppendUInt32(dds, 0u);
    for (const auto& pixel : ProbePixels())
    {
        dds.push_back(pixel[2]);
        dds.push_back(pixel[1]);
        dds.push_back(pixel[0]);
        dds.push_back(pixel[3]);
    }
    const Scratch scratch;
    TextureImporter importer;
    ImporterContext context;
    EXPECT_THROW((void)importer.Import(scratch.Write("probe.dds", dds), context), InvalidContentException);
}

TEST(XnaTextureImporter, RefusalsMatchXna)
{
    const Scratch scratch;
    TextureImporter importer;
    ImporterContext context;
    const std::string expected = Expected("textureimporter/refusals");
    EXPECT_NE(expected.find("missing=FileNotFoundException: Can not read the texture"), std::string::npos);
    EXPECT_THROW((void)importer.Import((scratch.Path() / "no_such.png").string(), context),
                 System::IO::FileNotFoundException);
    // XNA's corrupt-file message ends with the D3DX error code its own reader answered; CNA's
    // reader has no such code, so the sentence before it is what is held to.
    const std::string sentence = "Can not read the texture file. The file is corrupted or invalid.";
    EXPECT_NE(expected.find("garbage=InvalidContentException: " + sentence), std::string::npos);
    const std::string garbage = scratch.Write("garbage.png", {1, 2, 3, 4, 5});
    try
    {
        (void)importer.Import(garbage, context);
        ADD_FAILURE() << "a corrupt file was accepted";
    }
    catch (const InvalidContentException& error)
    {
        EXPECT_EQ(error.getMessageProperty(), sentence);
    }
    EXPECT_TRUE(context.dependencies.empty());
}
