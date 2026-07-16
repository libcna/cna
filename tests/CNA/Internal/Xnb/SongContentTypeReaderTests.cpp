// SPDX-License-Identifier: MS-PL
//
// plan_xnb.md XNB-34: unit tests for SongReader's own behavior (registration, extension-probing
// fallback) -- see SongContentManagerXnbTests.cpp for the full end-to-end test through
// ContentManager against a real fixture.

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

#include "CNA/Internal/Xnb/SongContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Media::Song;

namespace
{
    class SongContentTypeReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterSongXnbReader();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
    };

    Song ReadSongFields(ContentManager& cm, const std::string& assetName, const std::string& reference, int32_t durationMs)
    {
        System::IO::MemoryStream ms;
        System::IO::BinaryWriter writer(&ms, true);
        writer.Write(reference);
        writer.Write(durationMs);
        writer.Flush();
        const auto buf = ms.ToArray();

        System::IO::MemoryStream body(buf.data(), static_cast<int32_t>(buf.size()));
        ContentReader reader(&cm, &body, assetName, 5, 'w');

        auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.SongReader");
        std::any resultAny = typeReader->ReadUntyped(reader, std::any{});
        return std::any_cast<Song>(resultAny);
    }
}

TEST_F(SongContentTypeReaderTest, IsRegisteredUnderRealFnaCanonicalName)
{
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.SongReader"));
}

TEST_F(SongContentTypeReaderTest, ReferenceToNonexistentFileFallsBackToUnstrippedPathThenSongCtorRejectsIt)
{
    // No real .ogg/.oga/.qoa exists for this made-up reference -- FNA's own Normalize() would
    // return null here and the un-stripped original path gets used as-is, deferring any failure to
    // actual playback. CNA's own Song constructor is stricter than FNA (it eagerly validates the
    // path with std::filesystem::exists() and throws immediately, see Song.cpp) -- a real,
    // pre-existing CNA behavior this reader inherits rather than works around; SongReader's own
    // fallback logic still ran correctly (it did fall back to the un-stripped path, matching FNA),
    // it's just that CNA's Song rejects that fallback instead of accepting it silently.
    ContentManager cm(nullptr, "tests/assets/xnb/monogame/windows/uncompressed/song");

    EXPECT_THROW(ReadSongFields(cm, "does_not_exist_song", "does_not_exist.wma", 1234), std::runtime_error);
}

TEST_F(SongContentTypeReaderTest, ReferenceEndingInFourCharacterRealExtensionResolvesToRealFile)
{
    // Real, externally-produced fixture: MonoGame's own one_two_three.ogg, vendored alongside
    // one_two_three.xnb (see that fixture's own manifest.json). Verifies Normalize()'s
    // std::filesystem::exists() probe actually finds this real, on-disk file, not just that the
    // string arithmetic looks right.
    ContentManager cm(nullptr, "tests/assets/xnb/monogame/windows/uncompressed/song");

    Song song = ReadSongFields(cm, "one_two_three", "one_two_three.ogg", 769282);

    EXPECT_EQ(song.getHandle(), "tests/assets/xnb/monogame/windows/uncompressed/song/one_two_three.ogg");
    EXPECT_TRUE(std::filesystem::exists(song.getHandle()));
}
