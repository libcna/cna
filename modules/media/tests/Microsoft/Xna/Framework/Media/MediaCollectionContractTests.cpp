// SPDX-License-Identifier: MS-PL
#include <algorithm>
#include <limits>
#include <iterator>
#include <typeinfo>
#include <vector>

#include "MediaLibraryTestFixture.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/ObjectDisposedException.hpp"

using namespace Microsoft::Xna::Framework::Media;
using Microsoft::Xna::Framework::Media::Test::MediaLibraryTestFixture;

namespace
{
    template<class F>
    void ExpectIndexRefusal(F operation)
    {
        try { operation(); FAIL() << "Expected ArgumentOutOfRangeException"; }
        catch (const System::ArgumentOutOfRangeException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentOutOfRangeException));
            EXPECT_EQ(error.getParamNameProperty(), "index");
        }
    }

    template<class F>
    void ExpectDisposed(F operation, const char* objectName)
    {
        try { operation(); FAIL() << "Expected ObjectDisposedException"; }
        catch (const System::ObjectDisposedException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ObjectDisposedException));
            EXPECT_EQ(error.getObjectNameProperty(), objectName);
        }
    }

    template<class C>
    void CheckCollection(C& collection, const char* typeName)
    {
        SCOPED_TRACE(typeName);
        // Refresh <Type>Collection.get_Item IL_0000..0012 checks disposal first;
        // MediaLibraryEnumerator<T>.get_Item IL_0000..001c names "index".
        const auto count = collection.getCountProperty();
        EXPECT_FALSE(collection.getIsDisposedProperty());
        // No ToString/Equals override exists in the Refresh collection types: Object identity.
        EXPECT_EQ(collection.ToString(), std::string("Microsoft.Xna.Framework.Media.") + typeName);
        C distinct(collection);
        EXPECT_TRUE(collection.Equals(&collection));
        EXPECT_FALSE(collection.Equals(&distinct));
        EXPECT_FALSE(collection.Equals(static_cast<const System::Object*>(nullptr)));
        for (int index : {-1, count, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
            ExpectIndexRefusal([&] { (void)collection[index]; });

        const C& view = collection;
        EXPECT_EQ(std::distance(view.begin(), view.end()), count);
        // Refresh GetEnumerator IL_000e..0019 creates independent traversal state.
        // C++ begin/end are its adapter; never dereference an end iterator.
        if (count > 1)
        {
            auto first = view.begin();
            auto second = view.begin();
            const auto original = *second;
            ++first;
            EXPECT_EQ(*second, original);
            EXPECT_NE(first, second);
        }
        collection.Dispose();
        collection.Dispose();
        EXPECT_TRUE(collection.getIsDisposedProperty());
        ExpectDisposed([&] { (void)collection.getCountProperty(); }, typeName);
        for (int index : {-1, 0, count, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
            ExpectDisposed([&] { (void)collection[index]; }, typeName);
        ExpectDisposed([&] { (void)collection.begin(); }, typeName);
        ExpectDisposed([&] { (void)collection.end(); }, typeName);
        ExpectDisposed([&] { (void)view.begin(); }, typeName);
        ExpectDisposed([&] { (void)view.end(); }, typeName);
    }

    template<class C>
    void ExpectNames(const C& collection, std::vector<std::string> expected)
    {
        // Independent fixture manifest names, checked through BOTH public access routes.
        // No XNA native sorting rule is inferred: compare multisets, retaining duplicates.
        EXPECT_EQ(collection.getCountProperty(), static_cast<int>(expected.size()));
        std::vector<std::string> indexed, enumerated;
        for (int i = 0; i < collection.getCountProperty(); ++i)
        {
            ASSERT_NE(collection[i], nullptr);
            indexed.push_back(collection[i]->getNameProperty());
        }
        for (auto* item : collection)
        {
            ASSERT_NE(item, nullptr);
            enumerated.push_back(item->getNameProperty());
        }
        std::sort(expected.begin(), expected.end());
        std::sort(indexed.begin(), indexed.end());
        std::sort(enumerated.begin(), enumerated.end());
        EXPECT_EQ(indexed, expected);
        EXPECT_EQ(enumerated, expected);
    }

    template<class C>
    auto Named(C* collection, const std::string& name) -> decltype((*collection)[0])
    {
        for (auto* item : *collection) if (item->getNameProperty() == name) return item;
        return nullptr;
    }
}

TEST_F(MediaLibraryTestFixture, Tq052EveryCollectionReportsExactRefusalsAndIndependentIterators)
{
    auto* album = (*library->getAlbumsProperty())[0];
    auto* song = (*library->getSongsProperty())[0];
    CheckCollection(*library->getAlbumsProperty(), "AlbumCollection");
    CheckCollection(*library->getArtistsProperty(), "ArtistCollection");
    CheckCollection(*library->getGenresProperty(), "GenreCollection");
    CheckCollection(*library->getPlaylistsProperty(), "PlaylistCollection");
    CheckCollection(*library->getSongsProperty(), "SongCollection");
    CheckCollection(*library->getPicturesProperty(), "PictureCollection");
    CheckCollection(*library->getRootPictureAlbumProperty()->getAlbumsProperty(), "PictureAlbumCollection");
    // Collection Dispose releases the list, never the independently obtained item.
    EXPECT_FALSE(album->getIsDisposedProperty());
    EXPECT_FALSE(song->getIsDisposedProperty());
}

TEST_F(MediaLibraryTestFixture, Tq052FixtureMembershipIsCheckedThroughIndexerAndConstEnumeration)
{
    // tests/assets/media/{music,pictures}/manifest.json, Favorites.m3u and International.m3u8.
    auto* artist = Named(library->getArtistsProperty(), "Artist One");
    auto* rock = Named(library->getGenresProperty(), "Rock");
    auto* favorites = Named(library->getPlaylistsProperty(), "Favorites");
    ASSERT_NE(artist, nullptr);
    ASSERT_NE(rock, nullptr);
    ASSERT_NE(favorites, nullptr);
    ExpectNames(*artist->getAlbumsProperty(), {"Album Alpha", "Album Beta"});
    ExpectNames(*rock->getSongsProperty(), {"Sunrise", "Daybreak", "Étoile"});
    ExpectNames(*library->getPlaylistsProperty(), {"Favorites", "International"});
    ExpectNames(*library->getPicturesProperty(), {"beach", "portrait", "sunset"});
    ExpectNames(*library->getRootPictureAlbumProperty()->getAlbumsProperty(), {"Family", "Vacation"});
    // M3U order is explicit in the input, unlike native library sort order.
    const SongCollection& songs = *favorites->getSongsProperty();
    ASSERT_EQ(songs.getCountProperty(), 3);
    EXPECT_EQ(songs[0]->getNameProperty(), "Sunrise");
    EXPECT_EQ(songs[1]->getNameProperty(), "Twilight");
    EXPECT_EQ(songs[2]->getNameProperty(), "Daybreak");
}

TEST(Tq052SongCollectionTest, ExplicitSequenceRetainsDuplicatesAndConstOrder)
{
    // CNAEXT vector constructor contract: sequence [A,B,A], not a set.
    Song a("tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg", "A");
    Song b("tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg", "B");
    SongCollection songs({&a, &b, &a});
    const SongCollection& view = songs;
    EXPECT_EQ(view.getCountProperty(), 3);
    EXPECT_EQ((std::vector<Song*>(view.begin(), view.end())), (std::vector<Song*>{&a, &b, &a}));
    EXPECT_EQ(view[0], &a);
    EXPECT_EQ(view[1], &b);
    EXPECT_EQ(view[2], &a);
    CheckCollection(songs, "SongCollection");
    EXPECT_EQ(a.getNameProperty(), "A");
    EXPECT_EQ(b.getNameProperty(), "B");
}

TEST(Tq052EmptyLibraryTest, AllEmptyCollectionsHaveNoElementsAndExactBoundaryRefusals)
{
    struct Roots
    {
        Roots()
        {
            CNA::Internal::Media::MediaLibraryPaths::SetMusicRootOverride("tests/assets/media/.tq052-no-music");
            CNA::Internal::Media::MediaLibraryPaths::SetPictureRootOverride("tests/assets/media/.tq052-no-pictures");
        }
        ~Roots()
        {
            CNA::Internal::Media::MediaLibraryPaths::SetMusicRootOverride("");
            CNA::Internal::Media::MediaLibraryPaths::SetPictureRootOverride("");
        }
    } roots;
    MediaLibrary library;
    // Refresh MediaLibrary::.ctor(source) IL_0000..0053 installs empty collections;
    // an unavailable native catalog keeps them. CNA uses deliberately absent roots.
    auto checkEmpty = [](auto* collection, const char* name)
    {
        ASSERT_NE(collection, nullptr);
        EXPECT_EQ(collection->getCountProperty(), 0);
        EXPECT_EQ(collection->begin(), collection->end());
        CheckCollection(*collection, name);
    };
    checkEmpty(library.getAlbumsProperty(), "AlbumCollection");
    checkEmpty(library.getArtistsProperty(), "ArtistCollection");
    checkEmpty(library.getGenresProperty(), "GenreCollection");
    checkEmpty(library.getPlaylistsProperty(), "PlaylistCollection");
    checkEmpty(library.getSongsProperty(), "SongCollection");
    checkEmpty(library.getPicturesProperty(), "PictureCollection");
    checkEmpty(library.getSavedPicturesProperty(), "PictureCollection");
    // PictureAlbumCollection's empty case comes from a fixture leaf below; the absent-root
    // nullptr versus Windows Empty singleton discrepancy is tracked separately in DESIGN.
}

TEST_F(MediaLibraryTestFixture, Tq052LeafAlbumCollectionHasAnEmptyEnumerableAndExactRefusals)
{
    auto* family = Named(library->getRootPictureAlbumProperty()->getAlbumsProperty(), "Family");
    ASSERT_NE(family, nullptr);
    auto* children = family->getAlbumsProperty();
    ASSERT_NE(children, nullptr);
    EXPECT_EQ(children->getCountProperty(), 0);
    EXPECT_EQ(children->begin(), children->end());
    CheckCollection(*children, "PictureAlbumCollection");
}
