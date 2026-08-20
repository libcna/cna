// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Media/Album.hpp"
#include "Microsoft/Xna/Framework/Media/AlbumCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Artist.hpp"
#include "Microsoft/Xna/Framework/Media/ArtistCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Genre.hpp"
#include "Microsoft/Xna/Framework/Media/GenreCollection.hpp"
#include "Microsoft/Xna/Framework/Media/MediaSource.hpp"
#include "Microsoft/Xna/Framework/Media/Picture.hpp"
#include "Microsoft/Xna/Framework/Media/PictureAlbum.hpp"
#include "Microsoft/Xna/Framework/Media/PictureAlbumCollection.hpp"
#include "Microsoft/Xna/Framework/Media/PictureCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Playlist.hpp"
#include "Microsoft/Xna/Framework/Media/PlaylistCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"
#include "System/IDisposable.hpp"
#include "System/IO/Stream.hpp"
#include "System/Object.hpp"

namespace CNA::Internal::Media
{
    class PictureLibraryIndex;
}

namespace Microsoft::Xna::Framework::Media
{
    /** @brief Provides access to the media library catalog on the current device. */
    class MediaLibrary final : public System::Object, public System::IDisposable
    {
    public:
        /** @brief Constructs a MediaLibrary using the default media source. */
        MediaLibrary();

        /**
         * @brief Constructs a MediaLibrary using the specified media source.
         *
         * @param mediaSource The media source to enumerate.
         */
        explicit MediaLibrary(MediaSource* mediaSource);

        /** @brief Releases the resources used by this media library. */
        void Dispose() override;

        /**
         * @brief Gets the collection of all albums in this library.
         *
         * @return Pointer to the AlbumCollection.
         */
        [[nodiscard]] AlbumCollection* getAlbumsProperty() const;

        /**
         * @brief Gets the collection of all artists in this library.
         *
         * @return Pointer to the ArtistCollection.
         */
        [[nodiscard]] ArtistCollection* getArtistsProperty() const;

        /**
         * @brief Gets the collection of all genres in this library.
         *
         * @return Pointer to the GenreCollection.
         */
        [[nodiscard]] GenreCollection* getGenresProperty() const;

        /**
         * @brief Gets whether this library has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets the media source this library was opened with.
         *
         * @return Pointer to the MediaSource.
         */
        [[nodiscard]] MediaSource* getMediaSourceProperty() const;

        /**
         * @brief Gets the collection of pictures in this library.
         *
         * @return Pointer to the PictureCollection.
         */
        [[nodiscard]] PictureCollection* getPicturesProperty() const;

        /**
         * @brief Gets the collection of playlists in this library.
         *
         * @return Pointer to the PlaylistCollection.
         */
        [[nodiscard]] PlaylistCollection* getPlaylistsProperty() const;

        /**
         * @brief Gets the root picture album for this library.
         *
         * @return Pointer to the root PictureAlbum.
         */
        [[nodiscard]] PictureAlbum* getRootPictureAlbumProperty() const;

        /**
         * @brief Gets the collection of pictures saved by the application.
         *
         * @return Pointer to the saved PictureCollection.
         */
        [[nodiscard]] PictureCollection* getSavedPicturesProperty() const;

        /**
         * @brief Gets the collection of all songs in this library.
         *
         * @return Pointer to the SongCollection.
         */
        [[nodiscard]] SongCollection* getSongsProperty() const;

        /**
         * @brief Retrieves a Picture object using an opaque library token.
         *
         * @param token Token string identifying the picture.
         * @return Pointer to the matching Picture, or nullptr.
         */
        Picture* GetPictureFromToken(std::string token);

        /**
         * @brief Saves an image buffer as a new picture in the media library.
         *
         * @param name        Display name for the picture.
         * @param imageBuffer Raw image data bytes.
         * @return Pointer to the newly created Picture.
         */
        Picture* SavePicture(std::string name, const std::vector<uint8_t>& imageBuffer);

        /**
         * @brief Saves a picture from a stream into the media library.
         *
         * @param name   Display name for the picture.
         * @param source Stream containing the image data.
         * @return Pointer to the newly created Picture.
         */
        Picture* SavePicture(std::string name, System::IO::Stream* source);

        /** @brief Returns the fully-qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        void BuildFromRoots(const std::string& musicRoot, const std::string& pictureRoot);
        PictureAlbum* BuildPictureAlbumTree(const CNA::Internal::Media::PictureLibraryIndex& pictureIndex,
                                             const std::string& nodePath, PictureAlbum* parent);

        // Lazily creates the real "Saved Pictures" PictureAlbum tree node (and its child
        // PictureAlbumCollection registration under rootPictureAlbum_) the first time a picture
        // is actually saved, if it didn't already exist from a previous session's scan. Idempotent
        // -- a no-op if savedPicturesAlbum_ is already set. Returns the (possibly newly-created)
        // album, or rootPictureAlbum_ if there is nowhere to attach a new node (plans/plan_media.md
        // MEDIA-59/D7, a real gap found by external code review: SavePicture() used to silently
        // fall back to rootPictureAlbum_ forever instead of ever creating this node).
        PictureAlbum* EnsureSavedPicturesAlbum();

        bool isDisposed_ = false;
        std::string pictureRoot_;

        std::unique_ptr<MediaSource> mediaSource_;

        std::vector<std::unique_ptr<Song>> ownedSongs_;
        std::vector<std::unique_ptr<Genre>> ownedGenres_;
        std::vector<std::unique_ptr<Artist>> ownedArtists_;
        std::vector<std::unique_ptr<Album>> ownedAlbums_;
        std::vector<std::unique_ptr<Picture>> ownedPictures_;
        std::vector<std::unique_ptr<PictureAlbum>> ownedPictureAlbums_;
        std::vector<std::unique_ptr<Playlist>> ownedPlaylists_;

        // Per-genre/per-artist AlbumCollections (Genre::Albums/Artist::Albums each need their own).
        std::vector<std::unique_ptr<AlbumCollection>> ownedGroupAlbumCollections_;
        // Per-genre/per-artist/per-album/per-playlist SongCollections (each needs its own).
        std::vector<std::unique_ptr<SongCollection>> ownedGroupSongCollections_;
        // Per-PictureAlbum-node child collections (every tree node has its own).
        std::vector<std::unique_ptr<PictureAlbumCollection>> ownedPictureAlbumChildCollections_;
        std::vector<std::unique_ptr<PictureCollection>> ownedPictureAlbumPictureCollections_;

        std::unordered_map<std::string, Song*> songByPath_;
        std::unordered_map<std::string, PictureAlbum*> pictureAlbumByPath_;
        PictureAlbum* savedPicturesAlbum_ = nullptr;

        std::unique_ptr<SongCollection> songsCollection_;
        std::unique_ptr<AlbumCollection> albumsCollection_;
        std::unique_ptr<ArtistCollection> artistsCollection_;
        std::unique_ptr<GenreCollection> genresCollection_;
        std::unique_ptr<PictureCollection> picturesCollection_;
        std::unique_ptr<PictureCollection> savedPicturesCollection_;
        std::unique_ptr<PlaylistCollection> playlistsCollection_;

        PictureAlbum* rootPictureAlbum_ = nullptr; // points into ownedPictureAlbums_
    };
}
