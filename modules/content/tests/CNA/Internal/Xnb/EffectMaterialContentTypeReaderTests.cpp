// SPDX-License-Identifier: MS-PL
// SAMPLE-028: the three .xnb readers an XNA Model needs when one of its materials
// references a compiled custom effect instead of a stock one. A Car.xnb built by the
// official ModelProcessor from a .x whose material names an .fx declares ten type
// readers; CNA had seven, so such a model could not be loaded at all.

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "CNA/RendererTestGate.hpp"
#include "CNA/Internal/Xnb/EffectMaterialContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/MathContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/TextureCubeContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::TextureCube;
using namespace CNA::Testing::Renderers;

namespace
{
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : path_(std::filesystem::temp_directory_path()
                    / ("cna_xnb_effect_material_test_" +
                       std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }

        ~ScratchContentRoot()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    // A file whose root object is the Dictionary<string, object> an EffectMaterial's
    // parameter table is written as, holding one float and one Vector3.
    std::vector<std::uint8_t> BuildStringObjectDictionaryXnb()
    {
        System::IO::MemoryStream bodyStream;
        System::IO::BinaryWriter writer(&bodyStream, true);

        writer.Write7BitEncodedInt(4);
        writer.Write(std::string(
            "Microsoft.Xna.Framework.Content.DictionaryReader`2"
            "[[System.String],[System.Object]]"));
        writer.Write(static_cast<std::int32_t>(0));
        writer.Write(std::string("Microsoft.Xna.Framework.Content.StringReader"));
        writer.Write(static_cast<std::int32_t>(0));
        writer.Write(std::string("Microsoft.Xna.Framework.Content.SingleReader"));
        writer.Write(static_cast<std::int32_t>(0));
        writer.Write(std::string("Microsoft.Xna.Framework.Content.Vector3Reader"));
        writer.Write(static_cast<std::int32_t>(0));

        writer.Write7BitEncodedInt(0); // shared resources
        writer.Write7BitEncodedInt(1); // root: the dictionary

        writer.Write(static_cast<std::int32_t>(2)); // two entries

        writer.Write7BitEncodedInt(2);              // key via StringReader
        writer.Write(std::string("SpecularPower"));
        writer.Write7BitEncodedInt(3);              // value via SingleReader
        writer.Write(16.0f);

        writer.Write7BitEncodedInt(2);
        writer.Write(std::string("TargetColor"));
        writer.Write7BitEncodedInt(4);              // value via Vector3Reader
        writer.Write(0.25f);
        writer.Write(0.5f);
        writer.Write(0.75f);
        writer.Flush();

        const auto body = bodyStream.ToArray();
        System::IO::MemoryStream fileStream;
        System::IO::BinaryWriter fileWriter(&fileStream, true);
        fileWriter.Write(static_cast<std::uint8_t>('X'));
        fileWriter.Write(static_cast<std::uint8_t>('N'));
        fileWriter.Write(static_cast<std::uint8_t>('B'));
        fileWriter.Write(static_cast<std::uint8_t>('w'));
        fileWriter.Write(static_cast<std::uint8_t>(5));
        fileWriter.Write(static_cast<std::uint8_t>(0));
        fileWriter.Write(static_cast<std::int32_t>(10 + static_cast<std::int32_t>(body.size())));
        fileWriter.Write(body.data(), 0, static_cast<std::int32_t>(body.size()));
        fileWriter.Flush();
        const auto file = fileStream.ToArray();
        return {file.begin(), file.end()};
    }

    std::vector<std::uint8_t> BuildExternalReferenceDictionaryXnb()
    {
        System::IO::MemoryStream bodyStream;
        System::IO::BinaryWriter writer(&bodyStream, true);

        writer.Write7BitEncodedInt(3);
        writer.Write(std::string(
            "Microsoft.Xna.Framework.Content.DictionaryReader`2"
            "[[System.String],[System.Object]]"));
        writer.Write(static_cast<std::int32_t>(0));
        writer.Write(std::string("Microsoft.Xna.Framework.Content.StringReader"));
        writer.Write(static_cast<std::int32_t>(0));
        writer.Write(std::string("Microsoft.Xna.Framework.Content.ExternalReferenceReader"));
        writer.Write(static_cast<std::int32_t>(0));

        writer.Write7BitEncodedInt(0); // shared resources
        writer.Write7BitEncodedInt(1); // root dictionary
        writer.Write(static_cast<std::int32_t>(2));
        writer.Write7BitEncodedInt(2);
        writer.Write(std::string("EnvironmentMap"));
        writer.Write7BitEncodedInt(3);
        writer.Write(std::string("cube"));
        writer.Write7BitEncodedInt(2);
        writer.Write(std::string("EnvironmentMapAgain"));
        writer.Write7BitEncodedInt(3);
        writer.Write(std::string("cube"));
        writer.Flush();

        const auto body = bodyStream.ToArray();
        System::IO::MemoryStream fileStream;
        System::IO::BinaryWriter fileWriter(&fileStream, true);
        for (char c : {'X', 'N', 'B', 'w'})
            fileWriter.Write(static_cast<std::uint8_t>(c));
        fileWriter.Write(static_cast<std::uint8_t>(5));
        fileWriter.Write(static_cast<std::uint8_t>(0));
        fileWriter.Write(static_cast<std::int32_t>(10 + static_cast<std::int32_t>(body.size())));
        fileWriter.Write(body.data(), 0, static_cast<std::int32_t>(body.size()));
        fileWriter.Flush();
        const auto file = fileStream.ToArray();
        return {file.begin(), file.end()};
    }

    [[nodiscard]] bool CubeStorageSupported()
    {
        return !CNA_RENDERER_IS(SdlRenderer, Canvas, HtmlDom, FreeDirect, Headless, Gdi, OpenVg,
                                PortableGL, TinyGL, PixiJs, NanoVg);
    }

    class EffectMaterialContentTypeReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterPrimitiveXnbReaders();
            CNA::Internal::Xnb::RegisterMathXnbReaders();
            CNA::Internal::Xnb::RegisterEffectMaterialXnbReaders();
            CNA::Internal::Xnb::RegisterTextureCubeXnbReader();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        GraphicsDevice gd;
    };
}

TEST_F(EffectMaterialContentTypeReaderTest, AllThreeAreRegisteredUnderRealFnaCanonicalNames)
{
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(
        "Microsoft.Xna.Framework.Content.EffectMaterialReader"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(
        "Microsoft.Xna.Framework.Content.DictionaryReader`2[[System.String],[System.Object]]"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(
        "Microsoft.Xna.Framework.Content.ExternalReferenceReader"));
}

TEST_F(EffectMaterialContentTypeReaderTest, ReadsAStringObjectDictionaryWithTypeErasedValues)
{
    ScratchContentRoot root;
    const auto bytes = BuildStringObjectDictionaryXnb();
    std::ofstream file(root.path() / "params.xnb", std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    file.close();

    ContentManager content(nullptr, root.path().string());
    content.setGraphicsDevice(gd);
    auto values = content.Load<std::map<std::string, std::any>>("params");

    ASSERT_EQ(values.size(), 2u);
    ASSERT_EQ(values.count("SpecularPower"), 1u);
    ASSERT_EQ(values.count("TargetColor"), 1u);

    // Each value keeps the type its own reader produced, which is what lets
    // EffectMaterialReader pick the right EffectParameter::SetValue overload.
    const float* power = std::any_cast<float>(&values.at("SpecularPower"));
    ASSERT_NE(power, nullptr);
    EXPECT_FLOAT_EQ(*power, 16.0f);

    const Vector3* colour = std::any_cast<Vector3>(&values.at("TargetColor"));
    ASSERT_NE(colour, nullptr);
    EXPECT_FLOAT_EQ(colour->X, 0.25f);
    EXPECT_FLOAT_EQ(colour->Y, 0.5f);
    EXPECT_FLOAT_EQ(colour->Z, 0.75f);
}

TEST_F(EffectMaterialContentTypeReaderTest, AnEmptyDictionaryIsReadAsEmptyNotRefused)
{
    ScratchContentRoot root;
    System::IO::MemoryStream bodyStream;
    System::IO::BinaryWriter writer(&bodyStream, true);
    writer.Write7BitEncodedInt(1);
    writer.Write(std::string(
        "Microsoft.Xna.Framework.Content.DictionaryReader`2"
        "[[System.String],[System.Object]]"));
    writer.Write(static_cast<std::int32_t>(0));
    writer.Write7BitEncodedInt(0);
    writer.Write7BitEncodedInt(1);
    writer.Write(static_cast<std::int32_t>(0));
    writer.Flush();
    const auto body = bodyStream.ToArray();

    System::IO::MemoryStream fileStream;
    System::IO::BinaryWriter fileWriter(&fileStream, true);
    for (char c : {'X', 'N', 'B', 'w'})
        fileWriter.Write(static_cast<std::uint8_t>(c));
    fileWriter.Write(static_cast<std::uint8_t>(5));
    fileWriter.Write(static_cast<std::uint8_t>(0));
    fileWriter.Write(static_cast<std::int32_t>(10 + static_cast<std::int32_t>(body.size())));
    fileWriter.Write(body.data(), 0, static_cast<std::int32_t>(body.size()));
    fileWriter.Flush();
    const auto file = fileStream.ToArray();

    std::ofstream out(root.path() / "empty.xnb", std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()),
              static_cast<std::streamsize>(file.size()));
    out.close();

    ContentManager content(nullptr, root.path().string());
    content.setGraphicsDevice(gd);
    const auto loaded = content.Load<std::map<std::string, std::any>>("empty");
    EXPECT_TRUE(loaded.empty());
}

TEST_F(EffectMaterialContentTypeReaderTest,
       ExternalReferenceReaderPreservesReferencedTextureCubeConcreteType)
{
    if (!CubeStorageSupported())
    {
        GTEST_SKIP() << "The active renderer has no cube texture storage.";
    }

    ScratchContentRoot root;
    const auto dictionary = BuildExternalReferenceDictionaryXnb();
    std::ofstream dictionaryFile(root.path() / "params.xnb", std::ios::binary);
    dictionaryFile.write(reinterpret_cast<const char*>(dictionary.data()),
                         static_cast<std::streamsize>(dictionary.size()));
    dictionaryFile.close();

    const std::filesystem::path cubeFixture =
        "tests/assets/xnb/monogame/windows/uncompressed/SampleCube64DXT1Mips.xnb";
    ASSERT_TRUE(std::filesystem::exists(cubeFixture));
    std::filesystem::copy_file(cubeFixture, root.path() / "cube.xnb");

    ContentManager content(nullptr, root.path().string());
    content.setGraphicsDevice(gd);
    const auto values = content.Load<std::map<std::string, std::any>>("params");

    ASSERT_EQ(values.count("EnvironmentMap"), 1u);
    ASSERT_EQ(values.count("EnvironmentMapAgain"), 1u);
    const TextureCube* cube = std::any_cast<TextureCube>(&values.at("EnvironmentMap"));
    const TextureCube* cubeAgain =
        std::any_cast<TextureCube>(&values.at("EnvironmentMapAgain"));
    ASSERT_NE(cube, nullptr);
    ASSERT_NE(cubeAgain, nullptr);
    EXPECT_EQ(cube->getSizeProperty(), 64);
    EXPECT_EQ(cube->getLevelCountProperty(), 7);
    EXPECT_EQ(&cube->GetRenderer(), &cubeAgain->GetRenderer());
}
