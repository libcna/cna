// SPDX-License-Identifier: MS-PL
//
// TQ-033: EffectMaterialReader end to end. The material's effect is a genuine XNA 4.0 build
// (tests/reference/xna40/differential/effect_fx_initializer.xnb, from
// tests/assets/xna40/source/fx_initializer.fx: `float4x4 WorldViewProjection;` and
// `float4 Tint = float4(0.25, 0.5, 0.75, 1);`); the material itself is built byte by byte.
//
// Oracle: Microsoft.Xna.Framework.Graphics.dll 4.0 EffectMaterialReader --
//   Read IL_000e-0063: ReadExternalReference<Effect>, new EffectMaterial(effect) (a clone), then
//     ReadObject<Dictionary<string, object>> and TryToSetParameter per entry;
//   TryToSetParameter IL_0000-000e: a name the effect does not declare is skipped;
//     IL_001a-022c: the SetValue of the value's own type for int[], bool[], float[], Vector2[]..
//     Vector4[], Matrix[], int, bool, float, Vector2..Vector4, Matrix, string and Texture, and
//     nothing for any other type;
//     IL_0231-0340 (catch InvalidCastException): a Vector2/3/4 widened to Vector4 -- (x, y, 0, 1),
//     (x, y, z, 1) -- and narrowed to the column count of a vector-or-scalar parameter
//     (IsAVectorOrASingle IL_0000-005f); any other InvalidCastException rethrown (IL_033e);
//   EffectParameter.SetValue(Vector3) IL_0000-0064: InvalidCastException unless Vector 1x3;
//   Effect .ctor(Effect cloneSource) IL_0006-0018: ArgumentNullException("cloneSource") for null.

#include "CNA/TestSupport/EmptyServiceProvider.hpp"
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectMaterial.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidCastException.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::EffectMaterial;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;

namespace
{
    using Bytes = std::vector<std::uint8_t>;
    using ValueWriter = std::function<void(System::IO::BinaryWriter&)>;

    Bytes ReadDifferentialFixture(const std::string& name)
    {
        const std::string path = "tests/reference/xna40/differential/" + name + ".xnb";
        std::ifstream stream(path, std::ios::binary);
        EXPECT_TRUE(stream.good()) << path << " (the tests run from the source root)";
        return Bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    }

    struct Entry
    {
        std::string key;
        std::string valueReader;  // the value's reader, after the dictionary's own readers
        ValueWriter writeValue;
    };

    /// An EffectMaterial .xnb: readers [EffectMaterialReader, Dictionary<string, object> reader,
    /// StringReader, each entry's value reader...], the effect reference, then the dictionary.
    Bytes Material(const std::string& effectReference, const std::vector<Entry>& entries)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write7BitEncodedInt(static_cast<std::int32_t>(3 + entries.size()));
        for (const char* name : {
                 "Microsoft.Xna.Framework.Content.EffectMaterialReader",
                 "Microsoft.Xna.Framework.Content.DictionaryReader`2[[System.String],[System.Object]]",
                 "Microsoft.Xna.Framework.Content.StringReader"})
        {
            writer.Write(std::string(name));
            writer.Write(static_cast<std::int32_t>(0));
        }
        for (const Entry& entry : entries)
        {
            writer.Write(entry.valueReader);
            writer.Write(static_cast<std::int32_t>(0));
        }
        writer.Write7BitEncodedInt(0);   // shared resources
        writer.Write7BitEncodedInt(1);   // root: EffectMaterialReader
        writer.Write(effectReference);   // ReadExternalReference: a raw string, "" = null
        writer.Write7BitEncodedInt(2);   // the Dictionary<string, object>
        writer.Write(static_cast<std::int32_t>(entries.size()));
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            writer.Write7BitEncodedInt(3);
            writer.Write(entries[i].key);
            writer.Write7BitEncodedInt(static_cast<std::int32_t>(4 + i));
            entries[i].writeValue(writer);
        }
        writer.Flush();
        const auto body = stream.ToArray();
        Bytes file{'X', 'N', 'B', 'w', 5, 0, 0, 0, 0, 0};
        const auto total = static_cast<std::uint32_t>(10 + body.size());
        for (int i = 0; i < 4; ++i) { file[6 + i] = static_cast<std::uint8_t>(total >> (8 * i)); }
        file.insert(file.end(), body.begin(), body.end());
        return file;
    }

    ValueWriter Floats(std::vector<float> values)
    {
        return [values = std::move(values)](System::IO::BinaryWriter& writer)
        {
            for (const float value : values) { writer.Write(value); }
        };
    }

    class InMemoryContentManager final : public ContentManager
    {
    public:
        InMemoryContentManager() : ContentManager(CNA::TestSupport::EmptyServices(), "Content") { RouteLoadsThroughOpenStreamEXT(true); }

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

    class EffectMaterialParameterConversionTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
            manager.setGraphicsDevice(device);
            manager.files["fx"] = ReadDifferentialFixture("effect_fx_initializer");
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        /// Whether this renderer builds an Effect from XNA Effect Framework bytecode at all; the
        /// SOFTWARE renderer does only with CNA_SOFTWARE_COMPILED_EFFECTS, HEADLESS does always.
        bool CompiledEffectsAvailable()
        {
            return device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects);
        }

        Vector4 TintAfter(const std::string& asset, const std::vector<Entry>& entries)
        {
            manager.files[asset] = Material("fx", entries);
            const std::shared_ptr<Effect> loaded = manager.Load<std::shared_ptr<Effect>>(asset);
            auto material = std::dynamic_pointer_cast<EffectMaterial>(loaded);
            EXPECT_NE(material, nullptr) << "EffectMaterialReader must produce an EffectMaterial";
            if (!material) return Vector4(-1, -1, -1, -1);
            auto* tint = material->getParametersProperty()["Tint"];
            EXPECT_NE(tint, nullptr);
            return tint ? tint->GetValueVector4() : Vector4(-1, -1, -1, -1);
        }

        // effect_fx_initializer.xnb is a HiDef build (header flags 0x01).
        GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                              PresentationParameters::CreateWindowedEXT(800, 480)};
        InMemoryContentManager manager;
    };

    constexpr const char* kVector2Reader = "Microsoft.Xna.Framework.Content.Vector2Reader";
    constexpr const char* kVector3Reader = "Microsoft.Xna.Framework.Content.Vector3Reader";
    constexpr const char* kVector4Reader = "Microsoft.Xna.Framework.Content.Vector4Reader";
}

TEST_F(EffectMaterialParameterConversionTest, TheMaterialIsACloneThatStartsFromTheEffectsInitializer)
{
    if (!CompiledEffectsAvailable())
        GTEST_SKIP() << "the renderer builds no Effect from XNA bytecode (CompiledEffects is false)";
    // No entries: the clone keeps the source's Tint, the fx initializer (0.25, 0.5, 0.75, 1).
    EXPECT_EQ(TintAfter("plain", {}), Vector4(0.25f, 0.5f, 0.75f, 1.0f));
}

TEST_F(EffectMaterialParameterConversionTest, AVector4ForAFloat4ParameterIsSetAsIs)
{
    if (!CompiledEffectsAvailable())
        GTEST_SKIP() << "the renderer builds no Effect from XNA bytecode (CompiledEffects is false)";
    EXPECT_EQ(TintAfter("v4", {{"Tint", kVector4Reader, Floats({0.9f, 0.8f, 0.7f, 0.6f})}}),
              Vector4(0.9f, 0.8f, 0.7f, 0.6f));
    // The source effect itself is untouched: the material is a clone (Read IL_0016).
    auto source = manager.Load<std::shared_ptr<Effect>>("fx");
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(source->getParametersProperty()["Tint"]->GetValueVector4(), Vector4(0.25f, 0.5f, 0.75f, 1.0f));
}

TEST_F(EffectMaterialParameterConversionTest, AVector3ForAFloat4ParameterWidensWithWOne)
{
    if (!CompiledEffectsAvailable())
        GTEST_SKIP() << "the renderer builds no Effect from XNA bytecode (CompiledEffects is false)";
    // SetValue(Vector3) on a float4 is InvalidCastException; the handler sets (x, y, z, 1).
    EXPECT_EQ(TintAfter("v3", {{"Tint", kVector3Reader, Floats({0.1f, 0.2f, 0.3f})}}),
              Vector4(0.1f, 0.2f, 0.3f, 1.0f));
}

TEST_F(EffectMaterialParameterConversionTest, AVector2ForAFloat4ParameterWidensWithZeroAndOne)
{
    if (!CompiledEffectsAvailable())
        GTEST_SKIP() << "the renderer builds no Effect from XNA bytecode (CompiledEffects is false)";
    EXPECT_EQ(TintAfter("v2", {{"Tint", kVector2Reader, Floats({0.5f, 0.25f})}}),
              Vector4(0.5f, 0.25f, 0.0f, 1.0f));
}

TEST_F(EffectMaterialParameterConversionTest, ValuesOfATypeXnaDoesNotConvertAreIgnored)
{
    if (!CompiledEffectsAvailable())
        GTEST_SKIP() << "the renderer builds no Effect from XNA bytecode (CompiledEffects is false)";
    // A Quaternion and a Color have no TryToSetParameter branch: Tint keeps the initializer, and
    // a name the effect does not declare is skipped.
    const Vector4 initializer(0.25f, 0.5f, 0.75f, 1.0f);
    EXPECT_EQ(TintAfter("quaternion", {{"Tint", "Microsoft.Xna.Framework.Content.QuaternionReader",
                                        Floats({0.9f, 0.8f, 0.7f, 0.6f})}}),
              initializer);
    EXPECT_EQ(TintAfter("color", {{"Tint", "Microsoft.Xna.Framework.Content.ColorReader",
                                   [](System::IO::BinaryWriter& w) { w.Write(static_cast<std::uint32_t>(0xFF336699u)); }}}),
              initializer);
    EXPECT_EQ(TintAfter("unknown", {{"NoSuchParameter", kVector4Reader, Floats({1, 1, 1, 1})}}),
              initializer);
}

TEST_F(EffectMaterialParameterConversionTest, AVectorForAMatrixParameterIsInvalidCastException)
{
    if (!CompiledEffectsAvailable())
        GTEST_SKIP() << "the renderer builds no Effect from XNA bytecode (CompiledEffects is false)";
    // The matrix is not a vector-or-scalar parameter, so the handler rethrows.
    manager.files["matrix"] = Material("fx", {{"WorldViewProjection", kVector3Reader, Floats({1, 2, 3})}});
    EXPECT_THROW((void)manager.Load<std::shared_ptr<Effect>>("matrix"), System::InvalidCastException);
}

TEST_F(EffectMaterialParameterConversionTest, ANullEffectReferenceIsArgumentNullException)
{
    manager.files["noeffect"] = Material("", {});
    EXPECT_THROW((void)manager.Load<std::shared_ptr<Effect>>("noeffect"), System::ArgumentNullException);
}
