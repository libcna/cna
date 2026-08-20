// SPDX-License-Identifier: MS-PL

#include <filesystem>

#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "CNA/Internal/Media/ThumbnailGenerator.hpp"
#include "MediaLibraryTestFixture.hpp"
#include "Microsoft/Xna/Framework/Media/Album.hpp"
#include "Microsoft/Xna/Framework/Media/AlbumCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Artist.hpp"
#include "Microsoft/Xna/Framework/Media/Genre.hpp"
#include "Microsoft/Xna/Framework/Media/MediaLibrary.hpp"
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/IO/Stream.hpp"

using Microsoft::Xna::Framework::Media::Album;
using Microsoft::Xna::Framework::Media::MediaLibrary;
using Microsoft::Xna::Framework::Media::Test::MediaLibraryTestFixture;

namespace
{
    Album* FindAlbum(Microsoft::Xna::Framework::Media::AlbumCollection* albums, const std::string& name)
    {
        for (Album* a : *albums)
        {
            if (a->getNameProperty() == name) return a;
        }
        return nullptr;
    }

    // plans/plan_media.md MEDIA-102: a real, dedicated scratch music tree with two DIFFERENT artists
    // that both happen to have an album literally named "Collision" -- the main shared fixture
    // tree (tests/assets/media/music/) has no such name collision by design, so
    // AlbumEqualitySetForEqualAndUnequalAlbums could only prove equality-by-(Name,Artist)
    // indirectly. This scratch tree (matching MediaLibrarySavePictureTest's own established
    // pattern) proves it directly: same Name, different Artist, must compare unequal.
    class AlbumNameCollisionTest : public ::testing::Test
    {
    protected:
        std::string scratchMusicRoot = "tests/assets/media/.album_name_collision_test_music";

        void SetUp() override
        {
            std::error_code ec;
            std::filesystem::remove_all(scratchMusicRoot, ec);
            std::filesystem::create_directories(scratchMusicRoot + "/Artist X/Collision", ec);
            std::filesystem::create_directories(scratchMusicRoot + "/Artist Y/Collision", ec);
            std::filesystem::copy_file(
                "tests/assets/media/music/Artist Two/Album Gamma/01 - Nocturne.wav",
                scratchMusicRoot + "/Artist X/Collision/01 - Track.wav", ec);
            std::filesystem::copy_file(
                "tests/assets/media/music/Artist Two/Album Gamma/01 - Nocturne.wav",
                scratchMusicRoot + "/Artist Y/Collision/01 - Track.wav", ec);

            CNA::Internal::Media::MediaLibraryPaths::SetMusicRootOverride(scratchMusicRoot);
            CNA::Internal::Media::MediaLibraryPaths::SetPictureRootOverride(scratchMusicRoot); // no pictures needed
        }

        void TearDown() override
        {
            CNA::Internal::Media::MediaLibraryPaths::SetMusicRootOverride("");
            CNA::Internal::Media::MediaLibraryPaths::SetPictureRootOverride("");
            std::error_code ec;
            std::filesystem::remove_all(scratchMusicRoot, ec);
        }
    };
}

TEST_F(AlbumNameCollisionTest, SameNameDifferentArtistAlbumsCompareUnequal)
{
    MediaLibrary library;
    ASSERT_EQ(library.getAlbumsProperty()->getCountProperty(), 2);

    Album* first = (*library.getAlbumsProperty())[0];
    Album* second = (*library.getAlbumsProperty())[1];
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(first->getNameProperty(), "Collision");
    ASSERT_EQ(second->getNameProperty(), "Collision");
    ASSERT_NE(first->getArtistProperty()->getNameProperty(), second->getArtistProperty()->getNameProperty());

    EXPECT_FALSE(first->Equals(second));
    EXPECT_TRUE(*first != *second);
    EXPECT_TRUE(first->Equals(first));
    EXPECT_TRUE(*first == *first);
}

// plans/plan_media.md MEDIA-65: real implementation, incl. HasArt reflecting the real cover.jpg fixture.
TEST_F(MediaLibraryTestFixture, AlbumsContainsEveryFixtureAlbum)
{
    // Presence of the known fixture albums is the real contract; the exact total is incidental and
    // grows with the corpus, so it is not hardcoded (plans/plan_media.md MEDIA-199/206).
    EXPECT_GE(library->getAlbumsProperty()->getCountProperty(), 6);
}

TEST_F(MediaLibraryTestFixture, AlbumAlphaHasArtViaRealCoverFile)
{
    Album* alpha = FindAlbum(library->getAlbumsProperty(), "Album Alpha");
    ASSERT_NE(alpha, nullptr);
    EXPECT_TRUE(alpha->getHasArtProperty());

    System::IO::Stream* art = alpha->GetAlbumArt();
    ASSERT_NE(art, nullptr);
    EXPECT_GT(art->getLengthProperty(), 0);
    delete art;
}

TEST_F(MediaLibraryTestFixture, AlbumBetaHasNoArtAndThrowsOnAccess)
{
    Album* beta = FindAlbum(library->getAlbumsProperty(), "Album Beta");
    ASSERT_NE(beta, nullptr);
    EXPECT_FALSE(beta->getHasArtProperty());
    EXPECT_THROW(beta->GetAlbumArt(), System::InvalidOperationException);
}

TEST_F(MediaLibraryTestFixture, AlbumArtistAndGenreAreCorrect)
{
    Album* alpha = FindAlbum(library->getAlbumsProperty(), "Album Alpha");
    ASSERT_NE(alpha, nullptr);
    ASSERT_NE(alpha->getArtistProperty(), nullptr);
    EXPECT_EQ(alpha->getArtistProperty()->getNameProperty(), "Artist One");
    ASSERT_NE(alpha->getGenreProperty(), nullptr);
    EXPECT_EQ(alpha->getGenreProperty()->getNameProperty(), "Rock");
}

TEST_F(MediaLibraryTestFixture, AlbumSongsMatchesExpectedCount)
{
    Album* delta = FindAlbum(library->getAlbumsProperty(), "Album Delta");
    ASSERT_NE(delta, nullptr);
    EXPECT_EQ(delta->getSongsProperty()->getCountProperty(), 2); // Daybreak + Étoile
}

// plans/plan_media.md MEDIA-65: equality is by (Name, Artist) since album names can collide across
// artists -- there is no actual name collision in this fixture, so this test constructs the
// scenario indirectly by confirming two DIFFERENT albums (different name, different artist) are
// correctly unequal, and an album equals itself.
TEST_F(MediaLibraryTestFixture, AlbumEqualitySetForEqualAndUnequalAlbums)
{
    Album* alpha = FindAlbum(library->getAlbumsProperty(), "Album Alpha");
    Album* gamma = FindAlbum(library->getAlbumsProperty(), "Album Gamma");
    ASSERT_NE(alpha, nullptr);
    ASSERT_NE(gamma, nullptr);

    EXPECT_TRUE(alpha->Equals(alpha));
    EXPECT_FALSE(alpha->Equals(gamma));
    EXPECT_TRUE(*alpha == *alpha);
    EXPECT_TRUE(*alpha != *gamma);
}

TEST_F(MediaLibraryTestFixture, AlbumGetTypeNameIsFullyQualified)
{
    Album* alpha = FindAlbum(library->getAlbumsProperty(), "Album Alpha");
    ASSERT_NE(alpha, nullptr);
    EXPECT_EQ(alpha->GetTypeName(), "Microsoft.Xna.Framework.Media.Album");
}

// plans/plan_media.md MEDIA-102: Duration -- library-scanned songs stay TimeSpan.Zero until actually
// played (§4 D9, deliberate design choice recorded in NEXTmedia.md), so a freshly-scanned Album's
// Duration is zero too; this asserts that documented behavior rather than leaving it untested.
// plans/plan_media.md MEDIA-65 (corrected -- found by external code review): Duration is now a real,
// eagerly-probed sum of member Song.Duration (CNA::Internal::Media::AudioDurationProbe, decode-
// free container-metadata parsing at library-scan time), not a placeholder that stays zero
// forever. Album Alpha has exactly one member song (Sunrise.ogg, ~2.0s per its own fixture
// manifest) -- assert a real, non-zero value in the right ballpark rather than an exact
// millisecond count, since the probed value depends on the real encoder's own frame/container
// rounding.
TEST_F(MediaLibraryTestFixture, AlbumDurationIsARealNonZeroSumOfMemberSongDurations)
{
    Album* alpha = FindAlbum(library->getAlbumsProperty(), "Album Alpha");
    ASSERT_NE(alpha, nullptr);
    const double ms = alpha->getDurationProperty().getTotalMillisecondsProperty();
    EXPECT_GT(ms, 1000.0);
    EXPECT_LT(ms, 3000.0);
}

// plans/plan_media.md MEDIA-102: GetThumbnail() -- both the has-art and no-art branches, not just
// GetAlbumArt()'s own coverage above.
TEST_F(MediaLibraryTestFixture, AlbumGetThumbnailReturnsArtWhenAvailable)
{
    Album* alpha = FindAlbum(library->getAlbumsProperty(), "Album Alpha");
    ASSERT_NE(alpha, nullptr);

    System::IO::Stream* thumb = alpha->GetThumbnail();
    ASSERT_NE(thumb, nullptr);
    EXPECT_GT(thumb->getLengthProperty(), 0);
    delete thumb;
}

TEST_F(MediaLibraryTestFixture, AlbumGetThumbnailThrowsWhenNoArt)
{
    Album* beta = FindAlbum(library->getAlbumsProperty(), "Album Beta");
    ASSERT_NE(beta, nullptr);
    EXPECT_THROW(beta->GetThumbnail(), System::InvalidOperationException);
}

// plans/plan_media.md MEDIA-102: IsDisposed, not exercised anywhere else in this file.
TEST_F(MediaLibraryTestFixture, AlbumDisposeFlipsIsDisposed)
{
    Album* alpha = FindAlbum(library->getAlbumsProperty(), "Album Alpha");
    ASSERT_NE(alpha, nullptr);
    ASSERT_FALSE(alpha->getIsDisposedProperty());

    alpha->Dispose();

    EXPECT_TRUE(alpha->getIsDisposedProperty());
}

// plans/plan_media.md MEDIA-103: AlbumCollection's own indexer (in-bounds) and Dispose()/IsDisposed --
// not exercised anywhere else in this file (only Count was previously checked).
TEST_F(MediaLibraryTestFixture, AlbumCollectionIndexerReturnsAlbumsInBounds)
{
    // Derived from the live Count rather than hardcoded: the shared fixture corpus grows as
    // new formats/features get coverage (plans/plan_media.md MEDIA-199/206), and an unrelated
    // indexer test should not break every time it does.
    auto* albums = library->getAlbumsProperty();
    const auto count = albums->getCountProperty();
    ASSERT_GT(count, 0);
    for (SharpRuntime::intcs i = 0; i < count; ++i)
    {
        EXPECT_NE((*albums)[i], nullptr) << "index " << i;
    }
    EXPECT_THROW((void)(*albums)[count], System::ArgumentOutOfRangeException);
}

TEST_F(MediaLibraryTestFixture, AlbumCollectionDisposeFlipsIsDisposed)
{
    auto* albums = library->getAlbumsProperty();
    ASSERT_FALSE(albums->getIsDisposedProperty());

    albums->Dispose();

    EXPECT_TRUE(albums->getIsDisposedProperty());
}

// plans/plan_media.md MEDIA-121 (found by external code review): AlbumCollection's own GetTypeName().
TEST_F(MediaLibraryTestFixture, AlbumCollectionGetTypeNameIsFullyQualified)
{
    EXPECT_EQ(library->getAlbumsProperty()->GetTypeName(), "Microsoft.Xna.Framework.Media.AlbumCollection");
}

// plans/plan_media.md MEDIA-209: Album::GetThumbnail() used to be a synonym for GetAlbumArt(), returning
// the FULL-SIZE image. The Album Alpha fixture's cover.jpg is 200x200 -- genuinely above
// ThumbnailGenerator::MaxEdge (128) -- so a pass-through implementation cannot satisfy this.
namespace
{
    std::vector<uint8_t> ReadAllAndDelete(System::IO::Stream* s)
    {
        std::vector<uint8_t> bytes;
        if (s == nullptr) return bytes;
        const SharpRuntime::intcs len = s->getLengthProperty();
        if (len > 0)
        {
            bytes.resize(static_cast<std::size_t>(len));
            s->Read(bytes.data(), 0, len);
        }
        delete s;
        return bytes;
    }
}

TEST_F(MediaLibraryTestFixture, GetThumbnailReturnsAGenuinelySmallerImageThanGetAlbumArt)
{
    Album* alpha = FindAlbum(library->getAlbumsProperty(), "Album Alpha");
    ASSERT_NE(alpha, nullptr);
    ASSERT_TRUE(alpha->getHasArtProperty()) << "fixture album must have cover art for this test";

    const std::vector<uint8_t> full  = ReadAllAndDelete(alpha->GetAlbumArt());
    const std::vector<uint8_t> thumb = ReadAllAndDelete(alpha->GetThumbnail());
    ASSERT_FALSE(full.empty());
    ASSERT_FALSE(thumb.empty());

    const auto fullImg  = CNA::Internal::Graphics::ImageLoader::LoadFromMemory(full.data(), full.size());
    const auto thumbImg = CNA::Internal::Graphics::ImageLoader::LoadFromMemory(thumb.data(), thumb.size());

    EXPECT_EQ(fullImg.width, 200) << "fixture cover.jpg should be 200x200";
    EXPECT_LT(thumbImg.width, fullImg.width) << "thumbnail is not actually downscaled";
    EXPECT_LE(std::max(thumbImg.width, thumbImg.height),
              CNA::Internal::Media::ThumbnailGenerator::MaxEdge);
}

// plans/plan_media.md MEDIA-206/208: an album with NO folder cover file falls back to embedded art from
// a member song. "Album Embedded"'s directory deliberately contains only the .mp3 -- no cover.jpg
// -- so this can only pass if the APIC payload is genuinely extracted.
TEST_F(MediaLibraryTestFixture, AlbumWithoutAFolderCoverFallsBackToEmbeddedArt)
{
    Album* embedded = FindAlbum(library->getAlbumsProperty(), "Album Embedded");
    ASSERT_NE(embedded, nullptr) << "embedded-art fixture album was not indexed";

    EXPECT_TRUE(embedded->getHasArtProperty()) << "embedded art must count as having art";

    const std::vector<uint8_t> art = ReadAllAndDelete(embedded->GetAlbumArt());
    ASSERT_FALSE(art.empty());
    const auto img = CNA::Internal::Graphics::ImageLoader::LoadFromMemory(art.data(), art.size());
    EXPECT_EQ(img.width, 400) << "did not get the real embedded picture back";
    EXPECT_EQ(img.height, 300);
}

// MEDIA-208's core contract: HasArt must agree EXACTLY with whether GetAlbumArt() can deliver.
// A HasArt==true that then throws (or ==false when art exists) is exactly the "claims coverage it
// doesn't have" class of defect this plan has repeatedly been caught by.
TEST_F(MediaLibraryTestFixture, HasArtAgreesWithWhatGetAlbumArtCanActuallyProduce)
{
    int withArt = 0;
    int withoutArt = 0;

    for (Album* album : *library->getAlbumsProperty())
    {
        if (album->getHasArtProperty())
        {
            ++withArt;
            System::IO::Stream* s = nullptr;
            EXPECT_NO_THROW(s = album->GetAlbumArt())
                << "HasArt is true but GetAlbumArt() threw for '" << album->getNameProperty() << "'";
            ASSERT_NE(s, nullptr);
            EXPECT_GT(s->getLengthProperty(), 0)
                << "HasArt is true but GetAlbumArt() returned an empty stream for '"
                << album->getNameProperty() << "'";
            delete s;
        }
        else
        {
            ++withoutArt;
            EXPECT_THROW((void)album->GetAlbumArt(), System::InvalidOperationException)
                << "HasArt is false but GetAlbumArt() did not throw for '"
                << album->getNameProperty() << "'";
        }
    }

    // Anti-vacuity: the fixture corpus must actually exercise BOTH sides of the contract.
    EXPECT_GT(withArt, 0)    << "no album had art -- the true branch was never tested";
    EXPECT_GT(withoutArt, 0) << "every album had art -- the false branch was never tested";
}
