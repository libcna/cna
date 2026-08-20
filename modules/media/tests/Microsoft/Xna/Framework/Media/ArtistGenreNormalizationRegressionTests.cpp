// SPDX-License-Identifier: MS-PL
//
// plans/plan_media.md MEDIA-119: a dedicated, isolated regression guard for MEDIA-54's case-insensitive
// Artist normalization fix -- deliberately separate from ArtistTests.cpp/GenreTests.cpp (which
// cover Artist/Genre's broader public API) so this one specific regression can't silently
// disappear inside a larger file's future edits. See MediaLibraryIndexTests.cpp's
// NormalizesCaseVariantArtistNamesToOneCanonicalValue for the lower-level (pre-public-API)
// coverage of the same fix; this test instead proves it end-to-end through the real public
// MediaLibrary/Artist API.

#include "MediaLibraryTestFixture.hpp"
#include "Microsoft/Xna/Framework/Media/Artist.hpp"
#include "Microsoft/Xna/Framework/Media/ArtistCollection.hpp"
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"

using Microsoft::Xna::Framework::Media::Artist;
using Microsoft::Xna::Framework::Media::Test::MediaLibraryTestFixture;

namespace
{
    Artist* FindArtist(Microsoft::Xna::Framework::Media::ArtistCollection* artists, const std::string& name)
    {
        for (Artist* a : *artists)
        {
            if (a->getNameProperty() == name) return a;
        }
        return nullptr;
    }
}

// plans/plan_media.md MEDIA-54/D10: "ARTIST ONE" (Twilight.mp3's case-variant tag) must NOT create a
// second, distinct Artist entry -- confirms the normalization applied in MediaLibraryIndex flows
// through end-to-end into the real public API.
TEST_F(MediaLibraryTestFixture, CaseVariantArtistTagDidNotCreateADuplicateArtist)
{
    EXPECT_EQ(FindArtist(library->getArtistsProperty(), "ARTIST ONE"), nullptr);
    Artist* artistOne = FindArtist(library->getArtistsProperty(), "Artist One");
    ASSERT_NE(artistOne, nullptr);
    EXPECT_EQ(artistOne->getSongsProperty()->getCountProperty(), 2); // Sunrise + Twilight
}
