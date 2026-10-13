// SPDX-License-Identifier: MS-PL
//
// A Game declares the built-in .xnb readers, and the first Load<T> of each asset family installs
// that family's readers (detail::BuiltinContent, docs/link-size-architecture.md). A real content
// pipeline's file names value readers beside the asset's own: a Song dispatches its duration
// through Int32Reader, a Video its file name, size and frame rate through StringReader, Int32Reader
// and SingleReader, and a Model's Tag may hold any serialized value -- XNA's SkinningSample keeps
// its animation clips there, whose durations and keyframe times are TimeSpans. So each family
// installs every reader its files may name, and any family loads first, with no other family's
// registration having run before it.
//
// Every test starts from exactly a Game's state -- the built-ins declared, none installed -- and
// none calls RegisterAllBuiltInXnbReaders() or loads another family first.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Media::Song;
using Microsoft::Xna::Framework::Media::Video;

namespace ContentDetail = Microsoft::Xna::Framework::Content::detail;

namespace
{
    constexpr const char* kMonoGameDir = "tests/assets/xnb/monogame/windows/uncompressed";

    constexpr const char* kInt32Reader = "Microsoft.Xna.Framework.Content.Int32Reader";
    constexpr const char* kSingleReader = "Microsoft.Xna.Framework.Content.SingleReader";
    constexpr const char* kStringReader = "Microsoft.Xna.Framework.Content.StringReader";
    constexpr const char* kMatrixReader = "Microsoft.Xna.Framework.Content.MatrixReader";
    constexpr const char* kTimeSpanReader = "Microsoft.Xna.Framework.Content.TimeSpanReader";
    constexpr const char* kDateTimeReader = "Microsoft.Xna.Framework.Content.DateTimeReader";
    constexpr const char* kSongReader = "Microsoft.Xna.Framework.Content.SongReader";
    constexpr const char* kVideoReader = "Microsoft.Xna.Framework.Content.VideoReader";
    constexpr const char* kModelReader = "Microsoft.Xna.Framework.Content.ModelReader";
    constexpr const char* kVertexBufferReader = "Microsoft.Xna.Framework.Content.VertexBufferReader";
    constexpr const char* kBasicEffectReader = "Microsoft.Xna.Framework.Content.BasicEffectReader";
    constexpr const char* kObjectDictionaryReader =
        "Microsoft.Xna.Framework.Content.DictionaryReader`2[[System.String],[System.Object]]";
    constexpr const char* kSpriteFontReader = "Microsoft.Xna.Framework.Content.SpriteFontReader";
    constexpr const char* kClipTagReader = "CNA.Test.ClipTagReader";

    class ContentManagerGamePathXnbTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            // What Game's constructor does (Game.cpp), and nothing else.
            ContentDetail::DeclareBuiltinXnbReaders();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        /// Whether a reader is installed, asked in a way that installs nothing (IsRegistered would
        /// install every declared reader first).
        static bool Installed(const char* canonicalName)
        {
            return ContentDetail::CreateRegisteredReader(canonicalName) != nullptr;
        }

        GraphicsDevice gd;
    };

    /// The shape of SkinningSample's Model.Tag: a game type, read by the game's own reader, whose
    /// fields go through the built-in readers the file's table names -- here a TimeSpan.
    class ClipTag final : public System::Object
    {
    public:
        explicit ClipTag(System::TimeSpan duration) : duration(duration) {}

        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string name = "CNA.Test.ClipTag";
            return name;
        }

        System::TimeSpan duration;
    };

    class ClipTagReader final : public ContentTypeReader<std::shared_ptr<System::Object>>
    {
    public:
        ClipTagReader() : ContentTypeReader<std::shared_ptr<System::Object>>("CNA.Test.ClipTag") {}

    protected:
        std::shared_ptr<System::Object> Read(ContentReader& input,
                                             std::optional<std::shared_ptr<System::Object>>) override
        {
            return std::make_shared<ClipTag>(input.ReadObject<System::TimeSpan>());
        }
    };

    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_xnb_game_path_test_" +
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

    /// A mesh-less model whose Tag is a ClipTag holding @p duration: the type-reader table names
    /// ModelReader, StringReader, the game's reader and TimeSpanReader, as a real pipeline's would.
    std::vector<std::uint8_t> BuildClipTaggedModelXnb(System::TimeSpan duration)
    {
        System::IO::MemoryStream bodyStream;
        System::IO::BinaryWriter writer(&bodyStream, true);

        writer.Write7BitEncodedInt(4);
        writer.Write(std::string(kModelReader));
        writer.Write(static_cast<int32_t>(0));
        writer.Write(std::string(kStringReader));
        writer.Write(static_cast<int32_t>(0));
        writer.Write(std::string(kClipTagReader));
        writer.Write(static_cast<int32_t>(0));
        writer.Write(std::string(kTimeSpanReader));
        writer.Write(static_cast<int32_t>(0));
        writer.Write7BitEncodedInt(0); // shared resources
        writer.Write7BitEncodedInt(1); // root: ModelReader

        writer.Write(static_cast<uint32_t>(1)); // one bone
        writer.Write7BitEncodedInt(2);          // its name: StringReader
        writer.Write(std::string("Root"));
        const float identity[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        };
        for (float value : identity) writer.Write(value);
        writer.Write(static_cast<uint8_t>(0));  // no parent
        writer.Write(static_cast<uint32_t>(0)); // no children
        writer.Write(static_cast<int32_t>(0));  // no meshes
        writer.Write(static_cast<uint8_t>(1));  // root bone reference
        writer.Write7BitEncodedInt(3);           // model Tag: ClipTagReader ...
        writer.Write7BitEncodedInt(4);           // ... whose duration: TimeSpanReader
        writer.Write(static_cast<int64_t>(duration.getTicksProperty()));
        writer.Flush();

        const auto body = bodyStream.ToArray();
        System::IO::MemoryStream fileStream;
        System::IO::BinaryWriter fileWriter(&fileStream, true);
        fileWriter.Write(static_cast<uint8_t>('X'));
        fileWriter.Write(static_cast<uint8_t>('N'));
        fileWriter.Write(static_cast<uint8_t>('B'));
        fileWriter.Write(static_cast<uint8_t>('w'));
        fileWriter.Write(static_cast<uint8_t>(5));
        fileWriter.Write(static_cast<uint8_t>(0));
        fileWriter.Write(static_cast<int32_t>(10 + static_cast<int32_t>(body.size())));
        fileWriter.Write(body.data(), 0, static_cast<int32_t>(body.size()));
        fileWriter.Flush();
        const auto file = fileStream.ToArray();
        return {file.begin(), file.end()};
    }

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
}

TEST_F(ContentManagerGamePathXnbTest, ASongIsTheFirstAssetAGameLoads)
{
    ASSERT_TRUE(ContentDetail::DeclaredXnbReadersPending());
    ASSERT_FALSE(Installed(kInt32Reader));

    ContentManager cm(nullptr, std::string(kMonoGameDir) + "/song");
    Song song = cm.Load<Song>("one_two_three");

    EXPECT_EQ(song.getNameProperty(), "one_two_three");
    // The duration travels through Int32Reader (ContentManagerSongXnbTests: 3005 ms).
    EXPECT_EQ(song.getDurationProperty().getTicksProperty(),
              System::TimeSpan::FromMilliseconds(3005).getTicksProperty());

    // The Song family and the value readers, and nothing more: a game that plays music still
    // links no Model reader (LinkFootprint_2DGame).
    EXPECT_TRUE(Installed(kSongReader));
    EXPECT_TRUE(Installed(kInt32Reader));
    EXPECT_FALSE(Installed(kVideoReader));
    EXPECT_FALSE(Installed(kModelReader));
    EXPECT_TRUE(ContentDetail::DeclaredXnbReadersPending());
}

TEST_F(ContentManagerGamePathXnbTest, AVideoIsTheFirstAssetAGameLoads)
{
    ASSERT_FALSE(Installed(kStringReader));

    // The committed object-referenced fixture (ContentManagerVideoXnbTests): its table names
    // StringReader, Int32Reader and SingleReader beside VideoReader.
    ContentManager cm(nullptr, "tests/assets/media/video");
    cm.setGraphicsDevice(gd);
    Video video = cm.Load<Video>("video_xnb_object_fixture");

    EXPECT_EQ(video.getWidthProperty(), 160);
    EXPECT_EQ(video.getHeightProperty(), 90);
    EXPECT_FLOAT_EQ(video.getFramesPerSecondProperty(), 25.0f);
    EXPECT_EQ(video.getDurationProperty(), System::TimeSpan::FromMilliseconds(2000));
    EXPECT_TRUE(std::filesystem::exists(video.getFileNameProperty()));

    EXPECT_TRUE(Installed(kVideoReader));
    EXPECT_TRUE(Installed(kStringReader));
    EXPECT_TRUE(Installed(kSingleReader));
    EXPECT_FALSE(Installed(kSongReader));
    EXPECT_FALSE(Installed(kModelReader));
    EXPECT_TRUE(ContentDetail::DeclaredXnbReadersPending());
}

TEST_F(ContentManagerGamePathXnbTest, AModelIsTheFirstAssetAGameLoads)
{
    // Model loading builds a real VertexBuffer, which a renderer with no 3D pipeline refuses.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ASSERT_FALSE(Installed(kStringReader));

    ContentManager cm(nullptr, kMonoGameDir);
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("BlenderDefaultCube");

    EXPECT_NE(model.getRootProperty(), nullptr);
    EXPECT_GT(model.getMeshesProperty().getCountProperty(), 0);
    EXPECT_TRUE(Installed(kModelReader));
    EXPECT_FALSE(Installed(kSongReader));
    EXPECT_FALSE(Installed(kVideoReader));
    EXPECT_TRUE(ContentDetail::DeclaredXnbReadersPending());
}

TEST_F(ContentManagerGamePathXnbTest, AModelTagMayHoldATimeSpanWhenTheModelLoadsFirst)
{
    // The game registered the reader of its Tag type, as SkinningSample's pipeline library does;
    // the TimeSpan inside it is read by CNA's own reader, which the Model family must install.
    ContentTypeReaderManager::AddTypeCreator(kClipTagReader,
                                             [] { return std::make_unique<ClipTagReader>(); });
    ASSERT_FALSE(Installed(kTimeSpanReader));

    ScratchContentRoot root;
    WriteBytes(root.path() / "clip.xnb",
               BuildClipTaggedModelXnb(System::TimeSpan::FromMilliseconds(2500)));
    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("clip");

    auto* tag = dynamic_cast<ClipTag*>(model.getTagProperty());
    ASSERT_NE(tag, nullptr);
    EXPECT_EQ(tag->duration, System::TimeSpan::FromMilliseconds(2500));
    EXPECT_TRUE(Installed(kTimeSpanReader));
    EXPECT_TRUE(ContentDetail::DeclaredXnbReadersPending());
}

TEST_F(ContentManagerGamePathXnbTest, EachFamilyInstallsTheReadersItsFilesMayName)
{
    // The registration closure of each family, without a fixture: what a real pipeline's table
    // may name for that asset is installed, and nothing of the other families is.
    ContentManager cm(nullptr, kMonoGameDir);

    ContentDetail::BuiltinContent<Song>::Register(cm);
    EXPECT_TRUE(Installed(kSongReader));
    EXPECT_TRUE(Installed(kInt32Reader));
    EXPECT_FALSE(Installed(kVideoReader));
    EXPECT_FALSE(Installed(kModelReader));
    EXPECT_FALSE(Installed(kTimeSpanReader));

    ContentDetail::BuiltinContent<Video>::Register(cm);
    EXPECT_TRUE(Installed(kVideoReader));
    EXPECT_TRUE(Installed(kStringReader));
    EXPECT_TRUE(Installed(kInt32Reader));
    EXPECT_TRUE(Installed(kSingleReader));
    EXPECT_FALSE(Installed(kModelReader));

    ContentDetail::BuiltinContent<Model>::Register(cm);
    EXPECT_TRUE(Installed(kModelReader));
    EXPECT_TRUE(Installed(kVertexBufferReader));
    EXPECT_TRUE(Installed(kBasicEffectReader));
    EXPECT_TRUE(Installed(kObjectDictionaryReader));
    EXPECT_TRUE(Installed(kStringReader));
    EXPECT_TRUE(Installed(kMatrixReader));
    EXPECT_TRUE(Installed(kTimeSpanReader));
    EXPECT_TRUE(Installed(kDateTimeReader));
#if SHARP_RUNTIME_HAS_NATIVE_INT128
    EXPECT_TRUE(Installed("Microsoft.Xna.Framework.Content.DecimalReader"));
#endif

    // None of them installed everything: fonts are their own family.
    EXPECT_FALSE(Installed(kSpriteFontReader));
    EXPECT_TRUE(ContentDetail::DeclaredXnbReadersPending());
}
