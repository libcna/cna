// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "System/IDisposable.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class AlbumCollection;
    class ArtistCollection;
    class GenreCollection;
    class MediaSource;
    class Picture;
    class PictureAlbum;
    class PictureCollection;
    class PlaylistCollection;
    class SongCollection;

    /**
     * @brief Provides access to the media library catalog on the current device.
     *
     * @note Status: Stub — media library catalog integration not implemented.
     */
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

        /** @brief Returns the fully-qualified .NET type name. */
        [[nodiscard]] const std::string& GetTypeName() const override;
    };
}
