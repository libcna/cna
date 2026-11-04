// SPDX-License-Identifier: MS-PL
#include <functional>
#include <typeinfo>

#include "MediaLibraryTestFixture.hpp"
#include "Microsoft/Xna/Framework/Media/VisualizationData.hpp"
#include "System/ObjectDisposedException.hpp"

using namespace Microsoft::Xna::Framework::Media;
using Microsoft::Xna::Framework::Media::Test::MediaLibraryTestFixture;

namespace
{
    template<class C>
    auto Named(C* collection, const std::string& name) -> decltype((*collection)[0])
    {
        for (auto* item : *collection) if (item->getNameProperty() == name) return item;
        return nullptr;
    }

    template<class T>
    void CheckEntity(T* value, T* peer, T* different, const char* name, const char* typeName)
    {
        SCOPED_TRACE(typeName);
        ASSERT_NE(value, nullptr);
        ASSERT_NE(peer, nullptr);
        ASSERT_NE(different, nullptr);
        ASSERT_NE(value, peer);
        // Same fixture identity in two live libraries: this tests value comparison, not
        // ReferenceEquals. Native MediaItem_IsEqual's identity key is opaque to managed IL;
        // retaining CNA's catalog mapping here is NOT a native-XNA identity qualification.
        EXPECT_TRUE(value->Equals(peer));
        EXPECT_TRUE(peer->Equals(value));
        EXPECT_TRUE(*value == *peer);
        EXPECT_FALSE(*value != *peer);
        EXPECT_FALSE(value->Equals(different));
        EXPECT_FALSE(*value == *different);
        EXPECT_TRUE(*value != *different);
        EXPECT_TRUE(value->Equals(static_cast<const System::Object*>(peer)));
        EXPECT_FALSE(value->Equals(static_cast<const System::Object*>(different)));
        EXPECT_FALSE(value->Equals(static_cast<const T*>(nullptr)));
        EXPECT_FALSE(value->Equals(static_cast<const System::Object*>(nullptr)));
        VisualizationData wrongType;
        EXPECT_FALSE(value->Equals(&wrongType));

        // Refresh <Type>.ToString IL_0000..000c delegates to Name.
        EXPECT_EQ(value->getNameProperty(), name);
        EXPECT_EQ(value->ToString(), name);
        // Refresh <Type>.GetHashCode IL_000f..001b hashes Name only. The native C++
        // string hash is the existing port mapping; do not claim Windows hash bit parity.
        const int expectedHash = static_cast<int>(std::hash<std::string>{}(name));
        EXPECT_EQ(value->GetHashCode(), expectedHash);
        EXPECT_EQ(peer->GetHashCode(), expectedHash);
        EXPECT_FALSE(value->getIsDisposedProperty());
        value->Dispose();
        value->Dispose();
        EXPECT_TRUE(value->getIsDisposedProperty());
        EXPECT_FALSE(peer->getIsDisposedProperty());
        EXPECT_EQ(peer->ToString(), name);
        EXPECT_EQ(peer->GetHashCode(), expectedHash);
        // Refresh operators call Object.Equals (IL_0000..0007), which short-circuits self.
        EXPECT_TRUE(*value == *value);
        EXPECT_FALSE(*value != *value);
        auto disposed = [&](auto operation)
        {
            try { operation(); FAIL() << "Expected ObjectDisposedException"; }
            catch (const System::ObjectDisposedException& error)
            {
                EXPECT_EQ(typeid(error), typeid(System::ObjectDisposedException));
                EXPECT_EQ(error.getObjectNameProperty(), typeName);
            }
        };
        // Instance Equals checks disposal BEFORE self/null/type tests, unlike operators.
        disposed([&] { (void)value->Equals(value); });
        disposed([&] { (void)value->Equals(static_cast<const T*>(nullptr)); });
        disposed([&] { (void)value->Equals(static_cast<const System::Object*>(value)); });
        disposed([&] { (void)value->Equals(&wrongType); });
        disposed([&] { (void)value->GetHashCode(); });
        disposed([&] { (void)value->ToString(); });
        disposed([&] { (void)value->getNameProperty(); });
    }
}

TEST_F(MediaLibraryTestFixture, Tq052EntityEqualityTextHashAndDisposedPrecedence)
{
    MediaLibrary peer;
    CheckEntity(Named(library->getAlbumsProperty(), "Album Alpha"),
                Named(peer.getAlbumsProperty(), "Album Alpha"),
                Named(peer.getAlbumsProperty(), "Album Beta"), "Album Alpha", "Album");
    CheckEntity(Named(library->getArtistsProperty(), "Artist One"),
                Named(peer.getArtistsProperty(), "Artist One"),
                Named(peer.getArtistsProperty(), "Artist Two"), "Artist One", "Artist");
    CheckEntity(Named(library->getGenresProperty(), "Rock"),
                Named(peer.getGenresProperty(), "Rock"),
                Named(peer.getGenresProperty(), "Electronic"), "Rock", "Genre");
    CheckEntity(Named(library->getPlaylistsProperty(), "Favorites"),
                Named(peer.getPlaylistsProperty(), "Favorites"),
                Named(peer.getPlaylistsProperty(), "International"), "Favorites", "Playlist");
    CheckEntity(Named(library->getPicturesProperty(), "beach"),
                Named(peer.getPicturesProperty(), "beach"),
                Named(peer.getPicturesProperty(), "portrait"), "beach", "Picture");
    CheckEntity(Named(library->getRootPictureAlbumProperty()->getAlbumsProperty(), "Vacation"),
                Named(peer.getRootPictureAlbumProperty()->getAlbumsProperty(), "Vacation"),
                Named(peer.getRootPictureAlbumProperty()->getAlbumsProperty(), "Family"),
                "Vacation", "PictureAlbum");
}

TEST_F(MediaLibraryTestFixture, Tq052LibraryDisposalNamesTheLibraryAndLeavesViewsAlive)
{
    auto* albums = library->getAlbumsProperty();
    auto* artists = library->getArtistsProperty();
    auto* genres = library->getGenresProperty();
    auto* playlists = library->getPlaylistsProperty();
    auto* songs = library->getSongsProperty();
    auto* pictures = library->getPicturesProperty();
    auto* saved = library->getSavedPicturesProperty();
    auto* root = library->getRootPictureAlbumProperty();
    // Refresh MediaLibrary inherits Object.ToString/Equals; no value identity override.
    EXPECT_EQ(library->ToString(), "Microsoft.Xna.Framework.Media.MediaLibrary");
    EXPECT_TRUE(library->Equals(library.get()));
    EXPECT_FALSE(library->Equals(static_cast<const System::Object*>(nullptr)));
    library->Dispose();
    library->Dispose();
    EXPECT_TRUE(library->getIsDisposedProperty());
    auto check = [](auto operation)
    {
        try { operation(); FAIL() << "Expected ObjectDisposedException"; }
        catch (const System::ObjectDisposedException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ObjectDisposedException));
            EXPECT_EQ(error.getObjectNameProperty(), "MediaLibrary");
        }
    };
    // Refresh all nine getters / SavePicture overloads / GetPictureFromToken:
    // IL_0000..001d refuses disposal before reading arguments or returning fields.
    check([&] { (void)library->getAlbumsProperty(); });
    check([&] { (void)library->getArtistsProperty(); });
    check([&] { (void)library->getGenresProperty(); });
    check([&] { (void)library->getPlaylistsProperty(); });
    check([&] { (void)library->getSongsProperty(); });
    check([&] { (void)library->getPicturesProperty(); });
    check([&] { (void)library->getSavedPicturesProperty(); });
    check([&] { (void)library->getRootPictureAlbumProperty(); });
    check([&] { (void)library->getMediaSourceProperty(); });
    check([&] { (void)library->GetPictureFromToken(""); });
    check([&] { (void)library->SavePicture("", std::vector<uint8_t>{}); });
    check([&] { (void)library->SavePicture("", static_cast<System::IO::Stream*>(nullptr)); });
    // Dispose(bool) IL_0000..000f changes only the flag; all preexisting views survive.
    EXPECT_FALSE(albums->getIsDisposedProperty());
    EXPECT_FALSE(artists->getIsDisposedProperty());
    EXPECT_FALSE(genres->getIsDisposedProperty());
    EXPECT_FALSE(playlists->getIsDisposedProperty());
    EXPECT_FALSE(songs->getIsDisposedProperty());
    EXPECT_FALSE(pictures->getIsDisposedProperty());
    EXPECT_FALSE(saved->getIsDisposedProperty());
    ASSERT_NE(root, nullptr);
    EXPECT_FALSE(root->getIsDisposedProperty());
    EXPECT_EQ(playlists->getCountProperty(), 2); // fixture M3U files
    EXPECT_EQ(pictures->getCountProperty(), 3); // fixture manifest
}
