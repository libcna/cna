// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-34: end-to-end test -- content.Load<Song>("fixture") against a real,
// externally-produced .xnb fixture, going through ContentManager (not a standalone parser call).

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/SongContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Media::Song;

namespace
{
    class ContentManagerSongXnbTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterPrimitiveXnbReaders();
            CNA::Internal::Xnb::RegisterSongXnbReader();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
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
                   / ("cna_song_xnb_content_manager_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

    // Builds a full, valid, minimal-header-plus-body .xnb byte sequence whose root object is a
    // SongReader entry referencing a filename that resolves to no real file on disk (no
    // .ogg/.oga/.qoa variant exists for it either), matching real MonoGame content's
    // never-actually-produced ".wma" placeholder reference.
    std::vector<std::uint8_t> BuildBrokenSongXnbFile(const std::string& reference, int32_t durationMs)
    {
        System::IO::MemoryStream bodyMs;
        System::IO::BinaryWriter bodyWriter(&bodyMs, true);
        bodyWriter.Write7BitEncodedInt(1); // one type-reader table entry
        bodyWriter.Write(std::string("Microsoft.Xna.Framework.Content.SongReader"));
        bodyWriter.Write((int32_t)0); // reader version
        bodyWriter.Write7BitEncodedInt(0); // zero shared resources
        bodyWriter.Write7BitEncodedInt(1); // root object -> table entry 1
        bodyWriter.Write(reference);
        bodyWriter.Write(durationMs);
        bodyWriter.Flush();
        auto bodyBytes = bodyMs.ToArray();

        System::IO::MemoryStream fileMs;
        System::IO::BinaryWriter fileWriter(&fileMs, true);
        fileWriter.Write((uint8_t)'X'); fileWriter.Write((uint8_t)'N'); fileWriter.Write((uint8_t)'B');
        fileWriter.Write((uint8_t)'w'); // platform: Windows
        fileWriter.Write((uint8_t)5);   // version
        fileWriter.Write((uint8_t)0);   // flags: uncompressed
        fileWriter.Write((int32_t)(10 + (int32_t)bodyBytes.size())); // total length
        fileWriter.Write(bodyBytes.data(), 0, (int32_t)bodyBytes.size());
        fileWriter.Flush();

        auto fileBytes = fileMs.ToArray();
        return std::vector<std::uint8_t>(fileBytes.begin(), fileBytes.end());
    }
}

TEST_F(ContentManagerSongXnbTest, LoadRealMonoGameFixtureEndToEnd)
{
    // Real, externally-produced fixture (MonoGame's own Tests/Assets/Audio/Song/one_two_three.xnb),
    // vendored with its real companion .ogg at tests/assets/xnb/monogame/windows/uncompressed/song/.
    ContentManager cm(nullptr, "tests/assets/xnb/monogame/windows/uncompressed/song");

    Song song = cm.Load<Song>("one_two_three");

    EXPECT_EQ(song.getHandle(), "tests/assets/xnb/monogame/windows/uncompressed/song/one_two_three.ogg");
    EXPECT_EQ(song.getDurationProperty().getTicksProperty(),
              System::TimeSpan::FromMilliseconds(769282).getTicksProperty());
}

// plans/plan_media.md MEDIA-75: re-verify that Song's corrected exception type (System::IO::
// FileNotFoundException, not a bare std::runtime_error -- see MEDIA-10) actually propagates all
// the way out of ContentManager::Load<Song>() through the full, real .xnb container-parsing path
// (header + type-reader table + object dispatch), not just out of SongReader::Read() called
// directly (which SongContentTypeReaderTests.cpp already covers) or the raw Song constructor.
TEST_F(ContentManagerSongXnbTest, UnresolvableReferenceThrowsFileNotFoundExceptionThroughLoad)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "broken_song.xnb", BuildBrokenSongXnbFile("does_not_exist.wma", 1234));

    ContentManager cm(nullptr, root.path().string());

    EXPECT_THROW(cm.Load<Song>("broken_song"), System::IO::FileNotFoundException);
}
