// SPDX-License-Identifier: MS-PL
//
// plan_xnb.md XNB-34: end-to-end test -- content.Load<Song>("fixture") against a real,
// externally-produced .xnb fixture, going through ContentManager (not a standalone parser call).

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/SongContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

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
