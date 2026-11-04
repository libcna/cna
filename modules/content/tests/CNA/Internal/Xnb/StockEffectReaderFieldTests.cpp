// SPDX-License-Identifier: MS-PL
//
// TQ-033: every field of the five stock effect readers, with real texture references and with a
// sentinel Int32 after the effect, so a reader that reads one field too many or too few fails
// even where the missed field equals its default. The effect is read by a test-only root reader
// through ReadObject<Effect> (its own reader index), as a Model part reads it.
//
// Oracle: Microsoft.Xna.Framework.Graphics.dll 4.0 --
//   BuiltInEffectReader`1.ReadTexture IL_0000-006f: ReadExternalReference<Texture>; null -> null;
//     not the requested texture type -> ContentLoadException (BuiltInEffectWrongTextureType);
//   BasicEffectReader.Read IL_0024-0085: texture (Texture + TextureEnabled = true only when not
//     null), DiffuseColor, EmissiveColor, SpecularColor, SpecularPower, Alpha, VertexColorEnabled;
//   DualTextureEffectReader.Read IL_0024-0061: Texture, Texture2, DiffuseColor, Alpha,
//     VertexColorEnabled;
//   EnvironmentMapEffectReader.Read IL_0024-0085: Texture, EnvironmentMap (TextureCube),
//     EnvironmentMapAmount, EnvironmentMapSpecular, FresnelFactor, DiffuseColor, EmissiveColor, Alpha;
//   SkinnedEffectReader.Read IL_0024-0079: Texture, WeightsPerVertex (set_WeightsPerVertex
//     IL_0000-001b: ArgumentOutOfRangeException unless 1, 2 or 4), DiffuseColor, EmissiveColor,
//     SpecularColor, SpecularPower, Alpha;
//   AlphaTestEffectReader.Read IL_0024-006d: Texture, AlphaFunction (Int32), ReferenceAlpha
//     (Int32), DiffuseColor, Alpha, VertexColorEnabled.

#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::AlphaTestEffect;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::CompareFunction;
using Microsoft::Xna::Framework::Graphics::DualTextureEffect;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::SkinnedEffect;

namespace
{
    using Bytes = std::vector<std::uint8_t>;
    using Body = std::function<void(System::IO::BinaryWriter&)>;

    constexpr std::int32_t kSentinel = 0x5EED1234;

    Bytes ReadDifferentialFixture(const std::string& name)
    {
        const std::string path = "tests/reference/xna40/differential/" + name + ".xnb";
        std::ifstream stream(path, std::ios::binary);
        EXPECT_TRUE(stream.good()) << path << " (the tests run from the source root)";
        return Bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    }

    struct EffectProbe
    {
        std::shared_ptr<Effect> effect;
        std::int32_t sentinel = 0;
    };

    class EffectProbeReader final : public ContentTypeReader<EffectProbe>
    {
    public:
        EffectProbeReader() : ContentTypeReader<EffectProbe>("CNA.Test.EffectProbe") {}

    protected:
        EffectProbe Read(ContentReader& input, std::optional<EffectProbe> /*existingInstance*/) override
        {
            EffectProbe probe;
            probe.effect = input.ReadObject<std::shared_ptr<Effect>>();
            probe.sentinel = input.ReadInt32();
            return probe;
        }
    };

    /// [1] the probe reader, [2] the effect reader; root = probe, then the effect (index 2), its
    /// fields written by @p fields, then the sentinel.
    Bytes Probe(const std::string& effectReader, const Body& fields)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write7BitEncodedInt(2);
        writer.Write(std::string("CNA.Test.EffectProbeReader"));
        writer.Write(static_cast<std::int32_t>(0));
        writer.Write(effectReader);
        writer.Write(static_cast<std::int32_t>(0));
        writer.Write7BitEncodedInt(0);
        writer.Write7BitEncodedInt(1);
        writer.Write7BitEncodedInt(2);
        fields(writer);
        writer.Write(kSentinel);
        writer.Flush();
        const auto body = stream.ToArray();
        Bytes file{'X', 'N', 'B', 'w', 5, 1, 0, 0, 0, 0};
        const auto total = static_cast<std::uint32_t>(10 + body.size());
        for (int i = 0; i < 4; ++i) { file[6 + i] = static_cast<std::uint8_t>(total >> (8 * i)); }
        file.insert(file.end(), body.begin(), body.end());
        return file;
    }

    void Write(System::IO::BinaryWriter& writer, const Vector3& value)
    {
        writer.Write(value.X);
        writer.Write(value.Y);
        writer.Write(value.Z);
    }

    class InMemoryContentManager final : public ContentManager
    {
    public:
        InMemoryContentManager() : ContentManager(nullptr, "Content") { RouteLoadsThroughOpenStreamEXT(true); }

        std::map<std::string, Bytes> files;

    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string& assetName) override
        {
            const auto found = files.find(assetName);
            if (found == files.end()) { throw ContentLoadException("no asset '" + assetName + "'"); }
            return std::make_unique<System::IO::MemoryStream>(
                found->second.data(), static_cast<std::int32_t>(found->second.size()), false);
        }
    };

    class StockEffectReaderFieldTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
            ContentTypeReaderManager::AddTypeCreator(
                "CNA.Test.EffectProbeReader", [] { return std::make_unique<EffectProbeReader>(); });
            manager.setGraphicsDevice(device);
            // 2x2, two levels; and the 4x4 cube -- both genuine XNA builds.
            manager.files["tex"] = ReadDifferentialFixture("texture_png_texture_mipmaps");
            manager.files["tex2"] = ReadDifferentialFixture("texture_png_texture_mipmaps");
            manager.files["cube"] = ReadDifferentialFixture("texture_dds_cube");
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        template <typename TEffect>
        std::shared_ptr<TEffect> Load(const std::string& asset, const std::string& reader, const Body& fields)
        {
            manager.files[asset] = Probe(reader, fields);
            const EffectProbe probe = manager.Load<EffectProbe>(asset);
            EXPECT_EQ(probe.sentinel, kSentinel) << "the effect reader consumed a wrong number of bytes";
            auto effect = std::dynamic_pointer_cast<TEffect>(probe.effect);
            EXPECT_NE(effect, nullptr);
            return effect;
        }

        GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                              PresentationParameters::CreateWindowedEXT(800, 480)};
        InMemoryContentManager manager;
    };
}

TEST_F(StockEffectReaderFieldTest, BasicEffectReadsEveryFieldAndEnablesItsTexture)
{
    const auto effect = Load<BasicEffect>("basic", "Microsoft.Xna.Framework.Content.BasicEffectReader",
        [](System::IO::BinaryWriter& w)
        {
            w.Write(std::string("tex"));
            Write(w, Vector3(0.1f, 0.2f, 0.3f));
            Write(w, Vector3(0.4f, 0.5f, 0.6f));
            Write(w, Vector3(0.7f, 0.8f, 0.9f));
            w.Write(12.5f);
            w.Write(0.75f);
            w.Write(static_cast<std::uint8_t>(1));
        });
    ASSERT_NE(effect, nullptr);
    EXPECT_EQ(effect->getDiffuseColorProperty(), Vector3(0.1f, 0.2f, 0.3f));
    EXPECT_EQ(effect->getEmissiveColorProperty(), Vector3(0.4f, 0.5f, 0.6f));
    EXPECT_EQ(effect->getSpecularColorProperty(), Vector3(0.7f, 0.8f, 0.9f));
    EXPECT_EQ(effect->getSpecularPowerProperty(), 12.5f);
    EXPECT_EQ(effect->getAlphaProperty(), 0.75f);
    EXPECT_TRUE(effect->getVertexColorEnabledProperty());
    ASSERT_NE(effect->getTextureProperty(), nullptr);
    EXPECT_TRUE(effect->getTextureEnabledProperty());
    EXPECT_EQ(effect->getTextureProperty()->getWidthProperty(), 2);
    EXPECT_EQ(effect->getTextureProperty()->getLevelCountProperty(), 2);
}

TEST_F(StockEffectReaderFieldTest, ATextureReferenceToACubeIsContentLoadException)
{
    if (CNA_RENDERER_IS(CNA::Testing::Renderers::Stub)) GTEST_SKIP() << "the STUB renderer stores no cube faces";
    manager.files["wrong"] = Probe("Microsoft.Xna.Framework.Content.BasicEffectReader",
        [](System::IO::BinaryWriter& w)
        {
            w.Write(std::string("cube"));
            for (int i = 0; i < 11; ++i) { w.Write(1.0f); }
            w.Write(static_cast<std::uint8_t>(0));
        });
    EXPECT_THROW((void)manager.Load<EffectProbe>("wrong"), ContentLoadException);
}

TEST_F(StockEffectReaderFieldTest, DualTextureEffectReadsBothTexturesAndItsFields)
{
    const auto effect = Load<DualTextureEffect>("dual", "Microsoft.Xna.Framework.Content.DualTextureEffectReader",
        [](System::IO::BinaryWriter& w)
        {
            w.Write(std::string("tex"));
            w.Write(std::string("tex2"));
            Write(w, Vector3(0.25f, 0.5f, 0.75f));
            w.Write(0.5f);
            w.Write(static_cast<std::uint8_t>(1));
        });
    ASSERT_NE(effect, nullptr);
    ASSERT_NE(effect->getTextureProperty(), nullptr);
    ASSERT_NE(effect->getTexture2Property(), nullptr);
    EXPECT_NE(effect->getTextureProperty(), effect->getTexture2Property());  // two assets, two textures
    EXPECT_EQ(effect->getDiffuseColorProperty(), Vector3(0.25f, 0.5f, 0.75f));
    EXPECT_EQ(effect->getAlphaProperty(), 0.5f);
    EXPECT_TRUE(effect->getVertexColorEnabledProperty());
}

TEST_F(StockEffectReaderFieldTest, EnvironmentMapEffectReadsItsCubeAndEveryField)
{
    if (CNA_RENDERER_IS(CNA::Testing::Renderers::Stub)) GTEST_SKIP() << "the STUB renderer stores no cube faces";
    const auto effect = Load<EnvironmentMapEffect>("env", "Microsoft.Xna.Framework.Content.EnvironmentMapEffectReader",
        [](System::IO::BinaryWriter& w)
        {
            w.Write(std::string("tex"));
            w.Write(std::string("cube"));
            w.Write(0.375f);
            Write(w, Vector3(0.1f, 0.2f, 0.3f));
            w.Write(0.625f);
            Write(w, Vector3(0.4f, 0.5f, 0.6f));
            Write(w, Vector3(0.7f, 0.8f, 0.9f));
            w.Write(0.875f);
        });
    ASSERT_NE(effect, nullptr);
    ASSERT_NE(effect->getTextureProperty(), nullptr);
    ASSERT_NE(effect->getEnvironmentMapProperty(), nullptr);
    EXPECT_EQ(effect->getEnvironmentMapProperty()->getSizeProperty(), 4);
    EXPECT_EQ(effect->getEnvironmentMapAmountProperty(), 0.375f);
    EXPECT_EQ(effect->getEnvironmentMapSpecularProperty(), Vector3(0.1f, 0.2f, 0.3f));
    EXPECT_EQ(effect->getFresnelFactorProperty(), 0.625f);
    EXPECT_EQ(effect->getDiffuseColorProperty(), Vector3(0.4f, 0.5f, 0.6f));
    EXPECT_EQ(effect->getEmissiveColorProperty(), Vector3(0.7f, 0.8f, 0.9f));
    EXPECT_EQ(effect->getAlphaProperty(), 0.875f);
}

TEST_F(StockEffectReaderFieldTest, SkinnedEffectReadsEveryFieldAndRefusesThreeWeights)
{
    const auto fields = [](const std::int32_t weights)
    {
        return [weights](System::IO::BinaryWriter& w)
        {
            w.Write(std::string("tex"));
            w.Write(weights);
            Write(w, Vector3(0.1f, 0.2f, 0.3f));
            Write(w, Vector3(0.4f, 0.5f, 0.6f));
            Write(w, Vector3(0.7f, 0.8f, 0.9f));
            w.Write(7.0f);
            w.Write(0.5f);
        };
    };
    const auto effect = Load<SkinnedEffect>("skinned", "Microsoft.Xna.Framework.Content.SkinnedEffectReader", fields(2));
    ASSERT_NE(effect, nullptr);
    EXPECT_NE(effect->getTextureProperty(), nullptr);
    EXPECT_EQ(effect->getWeightsPerVertexProperty(), 2);
    EXPECT_EQ(effect->getDiffuseColorProperty(), Vector3(0.1f, 0.2f, 0.3f));
    EXPECT_EQ(effect->getEmissiveColorProperty(), Vector3(0.4f, 0.5f, 0.6f));
    EXPECT_EQ(effect->getSpecularColorProperty(), Vector3(0.7f, 0.8f, 0.9f));
    EXPECT_EQ(effect->getSpecularPowerProperty(), 7.0f);
    EXPECT_EQ(effect->getAlphaProperty(), 0.5f);

    manager.files["three"] = Probe("Microsoft.Xna.Framework.Content.SkinnedEffectReader", fields(3));
    EXPECT_THROW((void)manager.Load<EffectProbe>("three"), System::ArgumentOutOfRangeException);
}

TEST_F(StockEffectReaderFieldTest, AlphaTestEffectReadsItsFunctionReferenceAndFields)
{
    const auto effect = Load<AlphaTestEffect>("alpha", "Microsoft.Xna.Framework.Content.AlphaTestEffectReader",
        [](System::IO::BinaryWriter& w)
        {
            w.Write(std::string("tex"));
            w.Write(static_cast<std::int32_t>(6));    // CompareFunction.Greater
            w.Write(static_cast<std::int32_t>(200));  // ReferenceAlpha
            Write(w, Vector3(0.3f, 0.2f, 0.1f));
            w.Write(0.25f);
            w.Write(static_cast<std::uint8_t>(1));
        });
    ASSERT_NE(effect, nullptr);
    EXPECT_NE(effect->getTextureProperty(), nullptr);
    EXPECT_EQ(effect->getAlphaFunctionProperty(), CompareFunction::Greater);
    EXPECT_EQ(effect->getReferenceAlphaProperty(), 200);
    EXPECT_EQ(effect->getDiffuseColorProperty(), Vector3(0.3f, 0.2f, 0.1f));
    EXPECT_EQ(effect->getAlphaProperty(), 0.25f);
    EXPECT_TRUE(effect->getVertexColorEnabledProperty());
}
