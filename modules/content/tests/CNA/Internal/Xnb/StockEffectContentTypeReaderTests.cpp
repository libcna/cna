// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-32: unit tests for the 5 stock-effect readers' own behavior (registration,
// field-by-field byte layout). BasicEffectReader is verified against a real, externally-produced
// fixture slice (MonoGame's own BlenderDefaultCube.xnb); ModelReader (Phase F) isn't implemented
// yet, so a full ContentManager.Load<Model> round trip isn't possible, but the exact byte range of
// that file's BasicEffect shared resource was independently located and verified by direct
// inspection (see the fixture's own manifest.json) -- this tests the reader against real FNA-
// produced bytes, not a full end-to-end load. The other 4 readers (no real standalone or embedded
// fixture found in the available library) use hand-constructed streams verified field-by-field
// against FNA's own *EffectReader.cs sources instead.

#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

#include "CNA/Internal/Xnb/StockEffectContentTypeReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::AlphaTestEffect;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::CompareFunction;
using Microsoft::Xna::Framework::Graphics::DualTextureEffect;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SkinnedEffect;

namespace
{
    std::string ReadWholeFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return {};
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    void WriteVector3(System::IO::BinaryWriter& writer, const Vector3& v)
    {
        writer.Write(v.X);
        writer.Write(v.Y);
        writer.Write(v.Z);
    }

    class StockEffectContentTypeReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterStockEffectXnbReaders();
            cm.setGraphicsDevice(gd);
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        // Every stock-effect reader erases to std::shared_ptr<Effect> (the common base), not
        // std::shared_ptr<ConcreteEffectType> -- see StockEffectContentTypeReaders.hpp's own note
        // on why (ReadSharedResource<std::shared_ptr<Effect>>() needs every reader to agree on one
        // erased type regardless of which concrete effect a given file actually used). Downcast
        // here to get back the concrete type these tests assert fields on.
        template <typename T>
        std::shared_ptr<T> ReadViaReader(const std::string& readerName, const std::string& fieldBytes)
        {
            System::IO::MemoryStream ms(
                reinterpret_cast<const uint8_t*>(fieldBytes.data()), static_cast<int32_t>(fieldBytes.size()));
            ContentReader reader(&cm, &ms, "test", 5, 'w');

            auto typeReader = ContentTypeReaderManager::CreateReader(readerName);
            if (!typeReader) return nullptr;
            std::any resultAny = typeReader->ReadUntyped(reader, std::any{});
            std::shared_ptr<Effect> effect = std::any_cast<std::shared_ptr<Effect>>(resultAny);
            return std::dynamic_pointer_cast<T>(effect);
        }

        GraphicsDevice gd;
        ContentManager cm;
    };
}

TEST_F(StockEffectContentTypeReaderTest, AllFiveReadersAreRegisteredUnderRealFnaCanonicalNames)
{
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.BasicEffectReader"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.AlphaTestEffectReader"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.DualTextureEffectReader"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.EnvironmentMapEffectReader"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.SkinnedEffectReader"));
}

TEST_F(StockEffectContentTypeReaderTest, BasicEffectReaderParsesRealFnaProducedBytes)
{
    // Real, externally-produced fixture slice: see BlenderDefaultCube.xnb.manifest.json for how
    // this exact byte range (this file's 3rd/last shared resource) was located and verified.
    const std::string fileBytes = ReadWholeFile(
        "tests/assets/xnb/monogame/windows/uncompressed/BlenderDefaultCube.xnb");
    ASSERT_EQ(fileBytes.size(), 1802u) << "Real fixture not found relative to CWD, or changed size";

    constexpr std::size_t kFieldsOffset = 1756;
    constexpr std::size_t kFieldsLength = 46;
    const std::string fields = fileBytes.substr(kFieldsOffset, kFieldsLength);

    auto effect = ReadViaReader<BasicEffect>("Microsoft.Xna.Framework.Content.BasicEffectReader", fields);
    ASSERT_NE(effect, nullptr);

    EXPECT_EQ(effect->getTextureProperty(), nullptr);
    EXPECT_FALSE(effect->getTextureEnabledProperty());
    EXPECT_NEAR(effect->getDiffuseColorProperty().X, 0.64000004529953f, 1e-6f);
    EXPECT_NEAR(effect->getDiffuseColorProperty().Y, 0.64000004529953f, 1e-6f);
    EXPECT_NEAR(effect->getDiffuseColorProperty().Z, 0.64000004529953f, 1e-6f);
    EXPECT_EQ(effect->getEmissiveColorProperty(), Vector3::Zero);
    EXPECT_NEAR(effect->getSpecularColorProperty().X, 0.25f, 1e-6f);
    EXPECT_NEAR(effect->getSpecularPowerProperty(), 9.607843399047852f, 1e-4f);
    EXPECT_FLOAT_EQ(effect->getAlphaProperty(), 1.0f);
    EXPECT_FALSE(effect->VertexColorEnabled);
}

TEST_F(StockEffectContentTypeReaderTest, AlphaTestEffectReaderParsesHandConstructedBytes)
{
    // No real fixture found in the available library for this reader; hand-constructed, verified
    // field-by-field against FNA's AlphaTestEffectReader.cs's exact read order.
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(std::string());                                       // no texture reference
    writer.Write(static_cast<int32_t>(CompareFunction::Greater));      // AlphaFunction
    writer.Write(static_cast<uint32_t>(128));                          // ReferenceAlpha
    WriteVector3(writer, Vector3(0.1f, 0.2f, 0.3f));                    // DiffuseColor
    writer.Write(0.5f);                                                 // Alpha
    writer.Write(true);                                                 // VertexColorEnabled
    writer.Flush();
    const auto buf = ms.ToArray();
    const std::string fields(reinterpret_cast<const char*>(buf.data()), buf.size());

    auto effect = ReadViaReader<AlphaTestEffect>("Microsoft.Xna.Framework.Content.AlphaTestEffectReader", fields);
    ASSERT_NE(effect, nullptr);

    EXPECT_EQ(effect->getTextureProperty(), nullptr);
    EXPECT_EQ(effect->getAlphaFunctionProperty(), CompareFunction::Greater);
    EXPECT_EQ(effect->getReferenceAlphaProperty(), 128);
    EXPECT_EQ(effect->getDiffuseColorProperty(), Vector3(0.1f, 0.2f, 0.3f));
    EXPECT_FLOAT_EQ(effect->getAlphaProperty(), 0.5f);
    EXPECT_TRUE(effect->getVertexColorEnabledProperty());
}

TEST_F(StockEffectContentTypeReaderTest, DualTextureEffectReaderParsesHandConstructedBytes)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(std::string()); // texture
    writer.Write(std::string()); // texture2
    WriteVector3(writer, Vector3(0.4f, 0.5f, 0.6f));
    writer.Write(0.8f);
    writer.Write(false);
    writer.Flush();
    const auto buf = ms.ToArray();
    const std::string fields(reinterpret_cast<const char*>(buf.data()), buf.size());

    auto effect = ReadViaReader<DualTextureEffect>("Microsoft.Xna.Framework.Content.DualTextureEffectReader", fields);
    ASSERT_NE(effect, nullptr);

    EXPECT_EQ(effect->getTextureProperty(), nullptr);
    EXPECT_EQ(effect->getTexture2Property(), nullptr);
    EXPECT_EQ(effect->getDiffuseColorProperty(), Vector3(0.4f, 0.5f, 0.6f));
    EXPECT_FLOAT_EQ(effect->getAlphaProperty(), 0.8f);
    EXPECT_FALSE(effect->getVertexColorEnabledProperty());
}

TEST_F(StockEffectContentTypeReaderTest, EnvironmentMapEffectReaderParsesHandConstructedBytes)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(std::string()); // texture
    writer.Write(std::string()); // environment map
    writer.Write(0.75f);                              // EnvironmentMapAmount
    WriteVector3(writer, Vector3(0.1f, 0.1f, 0.1f));   // EnvironmentMapSpecular
    writer.Write(0.9f);                                // FresnelFactor
    WriteVector3(writer, Vector3(0.2f, 0.3f, 0.4f));   // DiffuseColor
    WriteVector3(writer, Vector3(0.01f, 0.02f, 0.03f)); // EmissiveColor
    writer.Write(0.6f);                                // Alpha
    writer.Flush();
    const auto buf = ms.ToArray();
    const std::string fields(reinterpret_cast<const char*>(buf.data()), buf.size());

    auto effect = ReadViaReader<EnvironmentMapEffect>(
        "Microsoft.Xna.Framework.Content.EnvironmentMapEffectReader", fields);
    ASSERT_NE(effect, nullptr);

    EXPECT_EQ(effect->getTextureProperty(), nullptr);
    EXPECT_EQ(effect->getEnvironmentMapProperty(), nullptr);
    EXPECT_FLOAT_EQ(effect->getEnvironmentMapAmountProperty(), 0.75f);
    EXPECT_EQ(effect->getEnvironmentMapSpecularProperty(), Vector3(0.1f, 0.1f, 0.1f));
    EXPECT_FLOAT_EQ(effect->getFresnelFactorProperty(), 0.9f);
    EXPECT_EQ(effect->getDiffuseColorProperty(), Vector3(0.2f, 0.3f, 0.4f));
    EXPECT_EQ(effect->getEmissiveColorProperty(), Vector3(0.01f, 0.02f, 0.03f));
    EXPECT_FLOAT_EQ(effect->getAlphaProperty(), 0.6f);
}

TEST_F(StockEffectContentTypeReaderTest, SkinnedEffectReaderParsesHandConstructedBytes)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(std::string());                       // texture
    writer.Write(static_cast<int32_t>(2));              // WeightsPerVertex
    WriteVector3(writer, Vector3(0.2f, 0.3f, 0.4f));    // DiffuseColor
    WriteVector3(writer, Vector3(0.01f, 0.02f, 0.03f)); // EmissiveColor
    WriteVector3(writer, Vector3(0.5f, 0.5f, 0.5f));    // SpecularColor
    writer.Write(12.0f);                                // SpecularPower
    writer.Write(0.95f);                                // Alpha
    writer.Flush();
    const auto buf = ms.ToArray();
    const std::string fields(reinterpret_cast<const char*>(buf.data()), buf.size());

    auto effect = ReadViaReader<SkinnedEffect>("Microsoft.Xna.Framework.Content.SkinnedEffectReader", fields);
    ASSERT_NE(effect, nullptr);

    EXPECT_EQ(effect->getTextureProperty(), nullptr);
    EXPECT_EQ(effect->getWeightsPerVertexProperty(), 2);
    EXPECT_EQ(effect->getDiffuseColorProperty(), Vector3(0.2f, 0.3f, 0.4f));
    EXPECT_EQ(effect->getEmissiveColorProperty(), Vector3(0.01f, 0.02f, 0.03f));
    EXPECT_EQ(effect->getSpecularColorProperty(), Vector3(0.5f, 0.5f, 0.5f));
    EXPECT_FLOAT_EQ(effect->getSpecularPowerProperty(), 12.0f);
    EXPECT_FLOAT_EQ(effect->getAlphaProperty(), 0.95f);
}
