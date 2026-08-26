// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-081/CNBF-082/CNBF-083 (Phase G tests): ContentManager's `.cnb` tier.
//
// The centrepiece is the resolution-order suite. `misc/cnj.md`'s "core rule" fixes an existing,
// dated, deliberately-chosen precedence (.xnb outranks everything, .cnj outranks native), and
// this file pins where `.cnb` lands in it -- immediately below `.xnb`, above everything CNA can
// compile a `.cnb` FROM. Rather than asserting on one pair at a time, the fixture puts all five
// candidate files on disk at once and removes them one at a time, so each assertion is about the
// whole ordering rather than a single comparison.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbLoaderRegistry.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Content/LooseFileContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using CNA::Content::CnbLoaderRegistry;
using CNA::Content::Cnb::CnbDocument;
using CNA::Content::Cnb::CnbWriter;
using CNA::Content::Cnb::EncodeAnimationClipToCnb;
using CNA::Content::Cnb::EncodeCurveToCnb;
using Microsoft::Xna::Framework::Curve;
using Microsoft::Xna::Framework::CurveContinuity;
using Microsoft::Xna::Framework::CurveKey;
using Microsoft::Xna::Framework::CurveLoopType;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Content::LooseFileContentTypeReader;
using Microsoft::Xna::Framework::Graphics::AnimationClipEXT;
using Microsoft::Xna::Framework::Graphics::BoneTrackEXT;
using Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT;
using Microsoft::Xna::Framework::Graphics::KeyframeEXT;

namespace CnbAssetTypeId = CNA::Content::Cnb::CnbAssetTypeId;
namespace CnbChunkFlags = CNA::Content::Cnb::CnbChunkFlags;

namespace
{
    /// The asset every resolution-order test loads. Its single field records which of the five
    /// candidate files actually won.
    struct Marker
    {
        std::string source;
    };

    const std::uint32_t kMarkerAssetTypeId =
        CNA::Content::Cnb::CnbAssetTypeIdFromName("CNA.Test.Marker");
    const CNA::Content::Cnb::CnbChunkId kMarkerChunk =
        CNA::Content::Cnb::MakeChunkId('m', 'r', 'k', 'r');

    class MarkerXnbReader : public ContentTypeReader<Marker>
    {
    public:
        MarkerXnbReader() : ContentTypeReader<Marker>("CNA.Test.Marker") {}

    protected:
        Marker Read(ContentReader& input, std::optional<Marker>) override
        {
            (void)input.ReadInt32();
            return Marker{"xnb"};
        }
    };

    /// Covers the two lowest tiers at once: ResolveAssetPath tries the literal path, then ".cnj",
    /// then this reader's own declared extension, and the reader reports which one it was handed.
    class MarkerLooseReader : public LooseFileContentTypeReader<Marker>
    {
    public:
        [[nodiscard]] std::vector<std::string> GetExtensions() const override
        {
            return {".marker"};
        }

        Marker Read(const std::string& path, ContentManager&) override
        {
            const std::string ext = std::filesystem::path(path).extension().string();
            if (ext == ".cnj") { return Marker{"cnj"}; }
            if (ext == ".marker") { return Marker{"native"}; }
            return Marker{"literal"};
        }
    };

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }

    void WriteText(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    std::vector<std::uint8_t> BuildMarkerXnb()
    {
        System::IO::MemoryStream bodyMs;
        System::IO::BinaryWriter bodyWriter(&bodyMs, true);
        bodyWriter.Write7BitEncodedInt(1);
        bodyWriter.Write(std::string("CNA.Test.MarkerReader"));
        bodyWriter.Write((int32_t)0);
        bodyWriter.Write7BitEncodedInt(0);
        bodyWriter.Write7BitEncodedInt(1);
        bodyWriter.Write((int32_t)0);
        bodyWriter.Flush();
        const auto bodyBytes = bodyMs.ToArray();

        System::IO::MemoryStream fileMs;
        System::IO::BinaryWriter fileWriter(&fileMs, true);
        fileWriter.Write((uint8_t)'X'); fileWriter.Write((uint8_t)'N'); fileWriter.Write((uint8_t)'B');
        fileWriter.Write((uint8_t)'w');
        fileWriter.Write((uint8_t)5);
        fileWriter.Write((uint8_t)0);
        fileWriter.Write((int32_t)(10 + (int32_t)bodyBytes.size()));
        fileWriter.Write(bodyBytes.data(), 0, (int32_t)bodyBytes.size());
        fileWriter.Flush();
        const auto fileBytes = fileMs.ToArray();
        return std::vector<std::uint8_t>(fileBytes.begin(), fileBytes.end());
    }

    std::vector<std::uint8_t> BuildMarkerCnb()
    {
        CnbWriter writer(kMarkerAssetTypeId, 1u);
        writer.SetMetadata("CNA.Test.Marker", "thing");
        writer.AddChunk(kMarkerChunk, {1u}, CnbChunkFlags::Mandatory, 4u);
        return writer.Build();
    }

    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path() /
                   ("cna_cnb_content_manager_test_" +
                    std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }
        ~ScratchContentRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }
        ScratchContentRoot(const ScratchContentRoot&) = delete;
        ScratchContentRoot& operator=(const ScratchContentRoot&) = delete;
        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    Curve MakeSampleCurve()
    {
        Curve curve;
        curve.setPreLoopProperty(CurveLoopType::Cycle);
        curve.setPostLoopProperty(CurveLoopType::Oscillate);
        curve.getKeysProperty().Add(CurveKey(0.0f, 4.0f, 0.5f, -0.5f, CurveContinuity::Step));
        curve.getKeysProperty().Add(CurveKey(2.0f, -1.0f, 0.0f, 0.0f, CurveContinuity::Smooth));
        return curve;
    }

    class CnbContentManagerTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::AddTypeCreator(
                "CNA.Test.MarkerReader", [] { return std::make_unique<MarkerXnbReader>(); });
            CnbLoaderRegistry::Remove(kMarkerAssetTypeId);
            CnbLoaderRegistry::Register(
                kMarkerAssetTypeId, "CNA.Test.Marker",
                [](const CnbDocument&, ContentManager&, const std::string&) -> std::any
                { return std::any(Marker{"cnb"}); });
        }

        void TearDown() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CnbLoaderRegistry::Remove(kMarkerAssetTypeId);
        }

        static ContentManager MakeManager(const ScratchContentRoot& root)
        {
            return ContentManager(nullptr, root.path().string());
        }
    };
}

// --------------------------------------------------------------------------------------------
// CNBF-083 -- resolution order: xnb > cnb > literal > cnj > native
// --------------------------------------------------------------------------------------------

TEST_F(CnbContentManagerTest, ResolutionOrderIsXnbThenCnbThenLiteralThenCnjThenNative)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "thing.xnb", BuildMarkerXnb());
    WriteBytes(root.path() / "thing.cnb", BuildMarkerCnb());
    WriteText(root.path() / "thing", "literal");
    WriteText(root.path() / "thing.cnj", "{\"cnjVersion\":1,\"type\":\"Marker\"}");
    WriteText(root.path() / "thing.marker", "native");

    {
        ContentManager cm(nullptr, root.path().string());
        cm.RegisterTypeReader<Marker>(std::make_unique<MarkerLooseReader>());
        EXPECT_EQ(cm.Load<Marker>("thing").source, "xnb");
    }

    std::filesystem::remove(root.path() / "thing.xnb");
    {
        ContentManager cm(nullptr, root.path().string());
        cm.RegisterTypeReader<Marker>(std::make_unique<MarkerLooseReader>());
        EXPECT_EQ(cm.Load<Marker>("thing").source, "cnb");
    }

    std::filesystem::remove(root.path() / "thing.cnb");
    {
        ContentManager cm(nullptr, root.path().string());
        cm.RegisterTypeReader<Marker>(std::make_unique<MarkerLooseReader>());
        EXPECT_EQ(cm.Load<Marker>("thing").source, "literal");
    }

    std::filesystem::remove(root.path() / "thing");
    {
        ContentManager cm(nullptr, root.path().string());
        cm.RegisterTypeReader<Marker>(std::make_unique<MarkerLooseReader>());
        EXPECT_EQ(cm.Load<Marker>("thing").source, "cnj");
    }

    std::filesystem::remove(root.path() / "thing.cnj");
    {
        ContentManager cm(nullptr, root.path().string());
        cm.RegisterTypeReader<Marker>(std::make_unique<MarkerLooseReader>());
        EXPECT_EQ(cm.Load<Marker>("thing").source, "native");
    }
}

TEST_F(CnbContentManagerTest, ACnbDispatchesWithNoPerTypeReaderRegisteredAtAll)
{
    // The same property `.xnb` has: a compiled file is self-describing, so it needs no
    // LooseFileContentTypeReader<T> on this ContentManager. Loading Marker here would be a hard
    // "no reader registered" error if the .cnb tier were not ahead of the reader lookup.
    ScratchContentRoot root;
    WriteBytes(root.path() / "thing.cnb", BuildMarkerCnb());

    ContentManager cm(nullptr, root.path().string());
    EXPECT_EQ(cm.Load<Marker>("thing").source, "cnb");
}

TEST_F(CnbContentManagerTest, ACnbNamedWithItsFullExtensionAlsoResolves)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "thing.cnb", BuildMarkerCnb());

    ContentManager cm(nullptr, root.path().string());
    EXPECT_EQ(cm.Load<Marker>("thing.cnb").source, "cnb");
}

TEST_F(CnbContentManagerTest, LoadingACnbTwiceReturnsTheCachedInstance)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "thing.cnb", BuildMarkerCnb());

    ContentManager cm(nullptr, root.path().string());
    EXPECT_EQ(cm.Load<Marker>("thing").source, "cnb");

    // Deleting the file proves the second call never touched the filesystem.
    std::filesystem::remove(root.path() / "thing.cnb");
    EXPECT_EQ(cm.Load<Marker>("thing").source, "cnb");
}

TEST_F(CnbContentManagerTest, AMalformedCnbIsAHardErrorAndDoesNotFallThroughToTheLowerTiers)
{
    // Matching the `.xnb` tier's own documented behaviour: a present-but-broken compiled file is
    // a failure, not an invitation to quietly load something else that happens to share the name.
    ScratchContentRoot root;
    std::vector<std::uint8_t> broken = BuildMarkerCnb();
    broken[45] ^= 0xFFu; // corrupt the header checksum field
    WriteBytes(root.path() / "thing.cnb", broken);
    WriteText(root.path() / "thing.marker", "native");

    ContentManager cm(nullptr, root.path().string());
    cm.RegisterTypeReader<Marker>(std::make_unique<MarkerLooseReader>());
    EXPECT_THROW((void)cm.Load<Marker>("thing"), ContentLoadException);
}

TEST_F(CnbContentManagerTest, ACnbHoldingAnUnregisteredAssetTypeReportsItByName)
{
    ScratchContentRoot root;
    CnbWriter writer(CNA::Content::Cnb::CnbAssetTypeIdFromName("SomeGame.Unknown"), 1u);
    writer.SetMetadata("SomeGame.Unknown", "thing");
    writer.AddChunk(kMarkerChunk, {}, CnbChunkFlags::None, 4u);
    WriteBytes(root.path() / "thing.cnb", writer.Build());

    ContentManager cm(nullptr, root.path().string());
    try
    {
        (void)cm.Load<Marker>("thing");
        FAIL() << "expected a ContentLoadException";
    }
    catch (const ContentLoadException& e)
    {
        EXPECT_NE(std::string(e.what()).find("SomeGame.Unknown"), std::string::npos)
            << e.what();
    }
}

TEST_F(CnbContentManagerTest, AskingForTheWrongTypeGivesAContentLoadExceptionNotABadAnyCast)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "thing.cnb", BuildMarkerCnb());

    ContentManager cm(nullptr, root.path().string());
    EXPECT_THROW((void)cm.Load<Curve>("thing"), ContentLoadException);
}

// --------------------------------------------------------------------------------------------
// CNBF-080/CNBF-081 -- the built-in Curve and AnimationClip loaders, end to end
// --------------------------------------------------------------------------------------------

TEST_F(CnbContentManagerTest, LoadsARealCurveCnbThroughContentManager)
{
    ScratchContentRoot root;
    const Curve original = MakeSampleCurve();
    WriteBytes(root.path() / "wobble.cnb", EncodeCurveToCnb(original, "wobble"));

    ContentManager cm(nullptr, root.path().string());
    const Curve loaded = cm.Load<Curve>("wobble");

    ASSERT_EQ(loaded.getKeysProperty().getCountProperty(), 2);
    EXPECT_EQ(loaded.getPreLoopProperty(), CurveLoopType::Cycle);
    EXPECT_EQ(loaded.getPostLoopProperty(), CurveLoopType::Oscillate);
    for (float t = -1.0f; t <= 3.0f; t += 0.5f)
    {
        EXPECT_FLOAT_EQ(loaded.Evaluate(t), original.Evaluate(t)) << "t=" << t;
    }
}

TEST_F(CnbContentManagerTest, LoadsARealAnimationClipCnbThroughContentManager)
{
    ScratchContentRoot root;

    AnimationClipEXT clip;
    clip.Duration = System::TimeSpan::FromSeconds(1.25);
    clip.TargetSpace = ClipTargetSpaceEXT::SceneNode;
    BoneTrackEXT track;
    track.BoneIndex = 4;
    KeyframeEXT key;
    key.Time = System::TimeSpan::FromSeconds(0.5);
    key.Translation = Microsoft::Xna::Framework::Vector3(1.0f, 2.0f, 3.0f);
    track.Keys.push_back(key);
    clip.Tracks.push_back(track);

    WriteBytes(root.path() / "walk.cnb", EncodeAnimationClipToCnb(clip, "walk"));

    ContentManager cm(nullptr, root.path().string());
    const AnimationClipEXT loaded = cm.Load<AnimationClipEXT>("walk");

    EXPECT_EQ(loaded.Duration.getTicksProperty(), clip.Duration.getTicksProperty());
    EXPECT_EQ(loaded.TargetSpace, ClipTargetSpaceEXT::SceneNode);
    ASSERT_EQ(loaded.Tracks.size(), 1u);
    EXPECT_EQ(loaded.Tracks[0].BoneIndex, 4);
    ASSERT_EQ(loaded.Tracks[0].Keys.size(), 1u);
    EXPECT_FLOAT_EQ(loaded.Tracks[0].Keys[0].Translation.Y, 2.0f);
}

TEST_F(CnbContentManagerTest, ACurveCnbOutranksASameNamedCurveCnj)
{
    ScratchContentRoot root;
    WriteText(root.path() / "wobble.cnj",
              "{\"cnjVersion\":1,\"type\":\"Curve\",\"keys\":["
              "{\"position\":0.0,\"value\":99.0}]}");
    WriteBytes(root.path() / "wobble.cnb", EncodeCurveToCnb(MakeSampleCurve(), "wobble"));

    ContentManager cm(nullptr, root.path().string());
    const Curve loaded = cm.Load<Curve>("wobble");
    ASSERT_EQ(loaded.getKeysProperty().getCountProperty(), 2);
    EXPECT_FLOAT_EQ(loaded.getKeysProperty()[0].getValueProperty(), 4.0f);

    // ... and with the compiled file gone, the .cnj is still perfectly loadable. CNB ranks above
    // CNJ; it does not replace it.
    std::filesystem::remove(root.path() / "wobble.cnb");
    ContentManager cm2(nullptr, root.path().string());
    const Curve fromCnj = cm2.Load<Curve>("wobble");
    ASSERT_EQ(fromCnj.getKeysProperty().getCountProperty(), 1);
    EXPECT_FLOAT_EQ(fromCnj.getKeysProperty()[0].getValueProperty(), 99.0f);
}

// --------------------------------------------------------------------------------------------
// CNBF-082 -- custom game types
// --------------------------------------------------------------------------------------------

namespace
{
    struct GameLevel
    {
        int width = 0;
        int height = 0;
    };
}

TEST_F(CnbContentManagerTest, AGameCanRegisterItsOwnCnbAssetType)
{
    const std::uint32_t levelId = CNA::Content::Cnb::CnbAssetTypeIdFromName("MyGame.Level");
    const CNA::Content::Cnb::CnbChunkId levelChunk =
        CNA::Content::Cnb::MakeChunkId('l', 'v', 'l', '0');
    CnbLoaderRegistry::Remove(levelId);

    ContentManager::RegisterCnbLoaderEXT<GameLevel>(
        levelId, "MyGame.Level",
        [levelChunk](const CnbDocument& document, ContentManager&) -> GameLevel
        {
            document.RequireAsset(CNA::Content::Cnb::CnbAssetTypeIdFromName("MyGame.Level"), 1u);
            const CNA::Content::Cnb::CnbChunkId known[] = {levelChunk};
            document.RequireMandatoryChunksUnderstood(known);
            auto reader = document.OpenChunk(document.RequireSingle(levelChunk));
            GameLevel level;
            level.width = static_cast<int>(reader.ReadU32());
            level.height = static_cast<int>(reader.ReadU32());
            reader.RequireExhausted();
            return level;
        });

    ScratchContentRoot root;
    {
        CnbWriter writer(levelId, 1u);
        writer.SetMetadata("MyGame.Level", "Levels/first");
        CNA::Content::Cnb::CnbByteWriter payload;
        payload.WriteU32(320u);
        payload.WriteU32(240u);
        writer.AddChunk(levelChunk, payload.Take(), CnbChunkFlags::Mandatory, 4u);
        WriteBytes(root.path() / "first.cnb", writer.Build());
    }

    ContentManager cm(nullptr, root.path().string());
    const GameLevel level = cm.Load<GameLevel>("first");
    EXPECT_EQ(level.width, 320);
    EXPECT_EQ(level.height, 240);

    CnbLoaderRegistry::Remove(levelId);
}

TEST_F(CnbContentManagerTest, RegisteringTwoTypesUnderOneIdentifierIsRefused)
{
    const std::uint32_t id = CNA::Content::Cnb::CnbAssetTypeIdFromName("MyGame.Collide");
    CnbLoaderRegistry::Remove(id);

    const auto loader = [](const CnbDocument&, ContentManager&, const std::string&) -> std::any
    { return std::any(GameLevel{}); };

    CnbLoaderRegistry::Register(id, "MyGame.Collide", loader);
    // A repeat under the same name is tolerated ...
    EXPECT_NO_THROW(CnbLoaderRegistry::Register(id, "MyGame.Collide", loader));
    // ... a different name under the same identifier is not, because that is exactly the
    // hash-collision case a 31-bit custom identifier space makes possible.
    EXPECT_THROW(CnbLoaderRegistry::Register(id, "MyGame.Other", loader), std::logic_error);

    EXPECT_TRUE(CnbLoaderRegistry::IsRegistered(id));
    EXPECT_EQ(CnbLoaderRegistry::RegisteredTypeName(id), "MyGame.Collide");
    EXPECT_TRUE(CnbLoaderRegistry::Remove(id));
    EXPECT_FALSE(CnbLoaderRegistry::Remove(id));
    EXPECT_FALSE(CnbLoaderRegistry::IsRegistered(id));
}

TEST_F(CnbContentManagerTest, RegistryRejectsInvalidRegistrations)
{
    const auto loader = [](const CnbDocument&, ContentManager&, const std::string&) -> std::any
    { return std::any(GameLevel{}); };

    EXPECT_THROW(CnbLoaderRegistry::Register(CnbAssetTypeId::Invalid, "X", loader),
                 std::invalid_argument);
    EXPECT_THROW(CnbLoaderRegistry::Register(0x90000001u, "", loader), std::invalid_argument);
    EXPECT_THROW(CnbLoaderRegistry::Register(0x90000001u, "X", {}), std::invalid_argument);
}

TEST_F(CnbContentManagerTest, TheBuiltInLoadersAreRegisteredByEveryContentManager)
{
    ScratchContentRoot root;
    ContentManager cm(nullptr, root.path().string());
    EXPECT_TRUE(CnbLoaderRegistry::IsRegistered(CnbAssetTypeId::Curve));
    EXPECT_TRUE(CnbLoaderRegistry::IsRegistered(CnbAssetTypeId::AnimationClip));
    EXPECT_EQ(CnbLoaderRegistry::RegisteredTypeName(CnbAssetTypeId::Curve),
              "Microsoft.Xna.Framework.Curve");
}
