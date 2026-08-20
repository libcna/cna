// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-42 (Phase G): proves the public extension point a real CNA game uses to load
// its own custom, non-built-in .xnb content. There is no separate wrapper API to add here --
// `ContentTypeReaderManager::AddTypeCreator()` (XNB-14/14A) is already public/CNAEXT-callable and
// generic over any `ContentTypeReader<T>` subclass a game defines outside CNA entirely, matching
// FNA's own real internal method of the same name/shape (see that method's own doc comment).
// `.cnj`'s `RegisterCnjLoader<T>()` is the equivalent extension point for CNA's own JSON-based
// format; this is the `.xnb` binary-format counterpart. This test plays the role of "a game",
// defining a custom asset type and reader entirely within the test file (nothing about
// `GameLevelData`/`GameLevelDataReader` exists anywhere in CNA itself), then loads a real,
// hand-authored `.xnb` file end-to-end through `ContentManager::Load<T>()` -- the same path any
// built-in reader uses, proving custom and built-in readers are first-class equals.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    // A stand-in for a hypothetical game's own asset type -- nothing about this struct is part
    // of CNA or the XNA API surface.
    struct GameLevelData
    {
        int32_t roomCount = 0;
        std::string levelName;
    };

    // A stand-in for a hypothetical game's own custom .xnb reader, defined and registered
    // entirely outside CNA -- the only thing it uses from CNA is the public ContentTypeReader<T>
    // base and AddTypeCreator() registration surface, exactly as a real game would.
    class GameLevelDataReader : public ContentTypeReader<GameLevelData>
    {
    public:
        GameLevelDataReader() : ContentTypeReader<GameLevelData>("MyGame.Content.GameLevelData") {}

    protected:
        GameLevelData Read(ContentReader& input, std::optional<GameLevelData> existingInstance) override
        {
            GameLevelData data = existingInstance.value_or(GameLevelData{});
            data.roomCount = input.ReadInt32();
            data.levelName = input.ReadString();
            return data;
        }
    };

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_xnb_custom_reader_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

    // Builds a full, valid, minimal-header-plus-body .xnb byte sequence for a single
    // GameLevelData root object -- exactly the shape a real content-pipeline-produced file has,
    // just authored by hand since no external tool knows about this game-specific type.
    std::vector<std::uint8_t> BuildGameLevelXnbFile(int32_t roomCount, const std::string& levelName)
    {
        System::IO::MemoryStream bodyMs;
        System::IO::BinaryWriter bodyWriter(&bodyMs, true);
        bodyWriter.Write7BitEncodedInt(1); // one type-reader table entry
        bodyWriter.Write(std::string("MyGame.Content.GameLevelDataReader"));
        bodyWriter.Write(static_cast<int32_t>(0)); // reader version
        bodyWriter.Write7BitEncodedInt(0); // zero shared resources
        bodyWriter.Write7BitEncodedInt(1); // root object -> table entry 1
        bodyWriter.Write(roomCount);
        bodyWriter.Write(levelName);
        bodyWriter.Flush();
        const auto bodyBytes = bodyMs.ToArray();

        System::IO::MemoryStream fileMs;
        System::IO::BinaryWriter fileWriter(&fileMs, true);
        fileWriter.Write(static_cast<uint8_t>('X'));
        fileWriter.Write(static_cast<uint8_t>('N'));
        fileWriter.Write(static_cast<uint8_t>('B'));
        fileWriter.Write(static_cast<uint8_t>('w')); // platform: Windows
        fileWriter.Write(static_cast<uint8_t>(5));   // version
        fileWriter.Write(static_cast<uint8_t>(0));   // flags: uncompressed
        fileWriter.Write(static_cast<int32_t>(10 + static_cast<int32_t>(bodyBytes.size())));
        fileWriter.Write(bodyBytes.data(), 0, static_cast<int32_t>(bodyBytes.size()));
        fileWriter.Flush();

        const auto fileBytes = fileMs.ToArray();
        return std::vector<std::uint8_t>(fileBytes.begin(), fileBytes.end());
    }

    class CustomContentTypeReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override { ContentTypeReaderManager::ClearTypeCreators(); }
        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
    };
}

TEST_F(CustomContentTypeReaderTest, GameCodeCanRegisterAndLoadItsOwnCustomXnbReader)
{
    // The entire registration call a game needs -- no CNA-side involvement, no special-casing.
    ContentTypeReaderManager::AddTypeCreator(
        "MyGame.Content.GameLevelDataReader", [] { return std::make_unique<GameLevelDataReader>(); });

    ScratchContentRoot root;
    WriteBytes(root.path() / "level1.xnb", BuildGameLevelXnbFile(7, "The Sunken Keep"));

    ContentManager cm(nullptr, root.path().string());
    const GameLevelData level = cm.Load<GameLevelData>("level1");

    EXPECT_EQ(level.roomCount, 7);
    EXPECT_EQ(level.levelName, "The Sunken Keep");
}

TEST_F(CustomContentTypeReaderTest, UnregisteredCustomReaderNameThrowsAClearError)
{
    // Deliberately do NOT register GameLevelDataReader -- a game that ships a .xnb file
    // referencing a reader it forgot to register should get a clear, actionable error, not a
    // crash or a silent wrong-type result.
    ScratchContentRoot root;
    WriteBytes(root.path() / "level1.xnb", BuildGameLevelXnbFile(7, "The Sunken Keep"));

    ContentManager cm(nullptr, root.path().string());
    EXPECT_THROW(cm.Load<GameLevelData>("level1"), Microsoft::Xna::Framework::Content::ContentLoadException);
}

TEST_F(CustomContentTypeReaderTest, CustomReaderRegistrationIsIndependentOfBuiltInReaders)
{
    // A custom reader and a built-in one coexist in the same process-wide registry without
    // interfering with each other.
    ContentTypeReaderManager::AddTypeCreator(
        "MyGame.Content.GameLevelDataReader", [] { return std::make_unique<GameLevelDataReader>(); });
    ContentTypeReaderManager::AddTypeCreator(
        "Microsoft.Xna.Framework.Content.Int32Reader", [] {
            class Int32OnlyReader : public ContentTypeReader<int32_t>
            {
            public:
                Int32OnlyReader() : ContentTypeReader<int32_t>("System.Int32") {}

            protected:
                int32_t Read(ContentReader& input, std::optional<int32_t> existingInstance) override
                {
                    (void)existingInstance;
                    return input.ReadInt32();
                }
            };
            return std::make_unique<Int32OnlyReader>();
        });

    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("MyGame.Content.GameLevelDataReader"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.Int32Reader"));
}
