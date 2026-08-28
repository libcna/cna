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
#include <type_traits>
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

// --------------------------------------------------------------------------------------------
// CNBF-119 -- who may claim which asset type identifier
// --------------------------------------------------------------------------------------------

TEST_F(CnbContentManagerTest, AGameExtensionCannotClaimABuiltInOrReservedIdentifier)
{
    // The defect: RegisterCnbLoaderEXT accepted ANY identifier. A game registering Curve's under
    // its canonical name was accepted, and whether its factory or CNA's ended up in the table
    // depended purely on which call ran first -- because a repeat registration under a matching
    // name retains the FIRST one. Neither outcome was reported.
    const auto factory = [](const CnbDocument&, ContentManager&) { return GameLevel{}; };

    for (const std::uint32_t builtIn :
         {CnbAssetTypeId::Texture2D, CnbAssetTypeId::Curve, CnbAssetTypeId::Model,
          CnbAssetTypeId::Song, CnbAssetTypeId::Effect})
    {
        EXPECT_THROW(ContentManager::RegisterCnbLoaderEXT<GameLevel>(
                         builtIn, CnbLoaderRegistry::RegisteredTypeName(builtIn).empty()
                                       ? "MyGame.Level"
                                       : CnbLoaderRegistry::RegisteredTypeName(builtIn),
                         factory),
                     std::invalid_argument)
            << "a game claimed built-in identifier "
            << CNA::Content::Cnb::AssetTypeIdToString(builtIn);
    }

    // The reserved range CNA has set aside for its own future types is refused for the same
    // reason: an identifier CNA has not assigned yet is still not a game's to take.
    EXPECT_THROW(ContentManager::RegisterCnbLoaderEXT<GameLevel>(
                     CnbAssetTypeId::ReservedRangeFirst, "MyGame.Level", factory),
                 std::invalid_argument);
    EXPECT_THROW(ContentManager::RegisterCnbLoaderEXT<GameLevel>(0x7FFFFFFFu, "MyGame.Level",
                                                                 factory),
                 std::invalid_argument);

    // And a real custom identifier still works, so the rule is a boundary rather than a blanket
    // refusal.
    const std::uint32_t custom = CNA::Content::Cnb::CnbAssetTypeIdFromName("MyGame.Boundary");
    CnbLoaderRegistry::Remove(custom);
    EXPECT_NO_THROW(
        ContentManager::RegisterCnbLoaderEXT<GameLevel>(custom, "MyGame.Boundary", factory));
    EXPECT_TRUE(CnbLoaderRegistry::IsRegistered(custom));
    EXPECT_TRUE(CnbLoaderRegistry::Remove(custom));
}

TEST_F(CnbContentManagerTest, ABuiltInLoaderCannotBeSilentlyReplacedOrInheritedByAnExtension)
{
    // A built-in registration and a game one are never interchangeable, whichever runs first, so
    // the repeat-registration tolerance does not span the boundary between them.
    const auto loader = [](const CnbDocument&, ContentManager&, const std::string&) -> std::any
    { return std::any(GameLevel{}); };

    // Game first, CNA second: the game's registration is refused outright, so CNA's own built-in
    // is still installed and still CNA's.
    CnbLoaderRegistry::Clear();
    EXPECT_THROW(CnbLoaderRegistry::Register(CnbAssetTypeId::Curve, "Microsoft.Xna.Framework.Curve",
                                             loader),
                 std::invalid_argument);
    CnbLoaderRegistry::RegisterBuiltIns();
    EXPECT_TRUE(CnbLoaderRegistry::IsRegistered(CnbAssetTypeId::Curve));

    // CNA first, game second: same refusal, and the built-in is untouched.
    EXPECT_THROW(CnbLoaderRegistry::Register(CnbAssetTypeId::Curve, "Microsoft.Xna.Framework.Curve",
                                             loader),
                 std::invalid_argument);
    EXPECT_EQ(CnbLoaderRegistry::RegisteredTypeName(CnbAssetTypeId::Curve),
              "Microsoft.Xna.Framework.Curve");

    // CNA cannot mint a custom identifier either -- the rule reads in both directions. There is
    // no public way to attempt it any more (CNBF-122), so this is asserted through the built-in
    // route CNA itself uses: RegisterBuiltIns() installs only non-custom identifiers, and a
    // custom one registered through Register() is a game's and stays a game's.
    const std::uint32_t custom = CNA::Content::Cnb::CnbAssetTypeIdFromName("MyGame.NotCnas");
    CnbLoaderRegistry::Remove(custom);
    EXPECT_NO_THROW(CnbLoaderRegistry::Register(custom, "MyGame.NotCnas", loader));
    EXPECT_TRUE(CnbLoaderRegistry::Remove(custom));

    // Repeating CNA's own built-in registration is still tolerated: every ContentManager
    // constructor does it.
    EXPECT_NO_THROW(CnbLoaderRegistry::RegisterBuiltIns());
    EXPECT_NO_THROW(CnbLoaderRegistry::RegisterBuiltIns());
}

// --------------------------------------------------------------------------------------------
// CNBF-122 -- the built-in registration route is not caller-selectable
// --------------------------------------------------------------------------------------------

namespace
{
    /// Detects whether `CnbLoaderRegistry::RegisterBuiltIn(u32, string, LoaderFn)` is callable
    /// from here. Access checking is part of template argument substitution, so a private member
    /// is a substitution failure rather than a hard error -- which makes the boundary something a
    /// test can assert instead of merely describe.
    template <typename T, typename = void>
    struct BuiltInRouteIsReachable : std::false_type
    {
    };

    template <typename T>
    struct BuiltInRouteIsReachable<
        T, std::void_t<decltype(T::RegisterBuiltIn(std::declval<std::uint32_t>(),
                                                    std::declval<const std::string&>(),
                                                    std::declval<typename T::LoaderFn>()))>>
        : std::true_type
    {
    };

    static_assert(!BuiltInRouteIsReachable<CnbLoaderRegistry>::value,
                  "CnbLoaderRegistry's built-in registration route must not be reachable from "
                  "outside CNA (plans/plan_cnb.md CNBF-122). Making it public would let game code "
                  "claim a built-in identifier before any ContentManager exists.");

    // The same detector, shown to have teeth: a class whose equivalent member IS public makes it
    // report true, so the assertion above is not vacuously satisfied by a typo in the signature.
    struct ForgeableRegistryShape
    {
        using LoaderFn = CnbLoaderRegistry::LoaderFn;
        static void RegisterBuiltIn(std::uint32_t, const std::string&, LoaderFn) {}
    };
    static_assert(BuiltInRouteIsReachable<ForgeableRegistryShape>::value,
                  "the detector above cannot distinguish a private member from a missing one");
}

TEST_F(CnbContentManagerTest, AGameCannotForgeABuiltInRegistrationBeforeCnaInstallsIt)
{
    // The defect CNBF-122 closes. CnbLoaderOwnership::CnaBuiltIn was a public enumerator and
    // Register() took it as a defaulted argument, so game code could register its own factory
    // under a built-in identifier's canonical name, claiming CNA's ownership, BEFORE the first
    // ContentManager was constructed. The repeat-registration rule retains the first equivalent
    // registration, so CNA's genuine loader would then never have been installed -- silently.
    //
    // The enumerator is gone and the built-in route is private, so the forgery no longer compiles
    // (asserted above). What remains reachable is the direct-registry call, and it must refuse
    // every identifier CNA owns -- with an EMPTY table, which is the moment the hijack aimed at.
    const auto hostile = [](const CnbDocument&, ContentManager&, const std::string&) -> std::any
    { return std::any(Marker{"hijacked"}); };

    CnbLoaderRegistry::Clear();
    ASSERT_FALSE(CnbLoaderRegistry::IsRegistered(CnbAssetTypeId::Curve));
    for (const std::uint32_t owned :
         {CnbAssetTypeId::Curve, CnbAssetTypeId::AnimationClip, CnbAssetTypeId::Model,
          CnbAssetTypeId::Texture2D, CnbAssetTypeId::TextureCube, CnbAssetTypeId::Texture3D,
          CnbAssetTypeId::SpriteFont, CnbAssetTypeId::SoundEffect, CnbAssetTypeId::Song,
          CnbAssetTypeId::Video, CnbAssetTypeId::Effect, CnbAssetTypeId::ReservedRangeFirst,
          0x7FFFFFFFu})
    {
        EXPECT_THROW(CnbLoaderRegistry::Register(owned, "Microsoft.Xna.Framework.Curve", hostile),
                     std::invalid_argument)
            << "an empty table accepted a game registration for "
            << CNA::Content::Cnb::AssetTypeIdToString(owned);
        EXPECT_FALSE(CnbLoaderRegistry::IsRegistered(owned))
            << CNA::Content::Cnb::AssetTypeIdToString(owned) << " was registered anyway";
    }

    // And the genuine loader still installs afterwards and is the one that decodes -- the property
    // the hijack was aiming to take away. Asserted by decoding a real Curve through the resolved
    // loader rather than by asking whether something is registered.
    CnbLoaderRegistry::RegisterBuiltIns();
    ScratchContentRoot root;
    WriteBytes(root.path() / "wobble.cnb", EncodeCurveToCnb(MakeSampleCurve(), "wobble"));
    ContentManager cm(nullptr, root.path().string());
    const Curve loaded = cm.Load<Curve>("wobble");
    EXPECT_EQ(loaded.getKeysProperty().getCountProperty(), 2);
    EXPECT_EQ(loaded.getPreLoopProperty(), CurveLoopType::Cycle);
}

TEST_F(CnbContentManagerTest, RegisterBuiltInsInstallsTwoLoadersAndAContentManagerInstallsTheRest)
{
    // RegisterBuiltIns()'s documentation used to claim "every asset type CNA itself compiles to
    // .cnb". It installs two. The other eight each construct a runtime object needing a
    // GraphicsDevice or the ContentManager itself, so they come from
    // ContentManager::RegisterBuiltinLoaders() -- which matters to anyone who calls Clear() in a
    // test and expects the table back.
    CnbLoaderRegistry::Clear();
    CnbLoaderRegistry::RegisterBuiltIns();
    EXPECT_TRUE(CnbLoaderRegistry::IsRegistered(CnbAssetTypeId::Curve));
    EXPECT_TRUE(CnbLoaderRegistry::IsRegistered(CnbAssetTypeId::AnimationClip));
    for (const std::uint32_t deviceBound :
         {CnbAssetTypeId::Model, CnbAssetTypeId::Texture2D, CnbAssetTypeId::TextureCube,
          CnbAssetTypeId::Texture3D, CnbAssetTypeId::SpriteFont, CnbAssetTypeId::SoundEffect,
          CnbAssetTypeId::Song, CnbAssetTypeId::Video})
    {
        EXPECT_FALSE(CnbLoaderRegistry::IsRegistered(deviceBound))
            << CNA::Content::Cnb::AssetTypeIdToString(deviceBound)
            << " came from RegisterBuiltIns(), which its documentation says it does not";
    }

    ScratchContentRoot root;
    ContentManager cm(nullptr, root.path().string());
    for (const std::uint32_t everyBuiltIn :
         {CnbAssetTypeId::Curve, CnbAssetTypeId::AnimationClip, CnbAssetTypeId::Model,
          CnbAssetTypeId::Texture2D, CnbAssetTypeId::TextureCube, CnbAssetTypeId::Texture3D,
          CnbAssetTypeId::SpriteFont, CnbAssetTypeId::SoundEffect, CnbAssetTypeId::Song,
          CnbAssetTypeId::Video})
    {
        EXPECT_TRUE(CnbLoaderRegistry::IsRegistered(everyBuiltIn))
            << CNA::Content::Cnb::AssetTypeIdToString(everyBuiltIn)
            << " was not installed by a ContentManager";
    }
    // Effect is the one built-in identifier with no schema, so it must have no loader either.
    EXPECT_FALSE(CnbLoaderRegistry::IsRegistered(CnbAssetTypeId::Effect));
}
