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

    /// Provides access to the media library.
    /// @note Status: Stub — media library catalog integration not implemented
    class MediaLibrary final : public System::Object, public System::IDisposable
    {
    public:
        MediaLibrary();
        explicit MediaLibrary(MediaSource* mediaSource);

        void Dispose() override;

        [[nodiscard]] AlbumCollection* getAlbumsProperty() const;
        [[nodiscard]] ArtistCollection* getArtistsProperty() const;
        [[nodiscard]] GenreCollection* getGenresProperty() const;
        [[nodiscard]] bool getIsDisposedProperty() const;
        [[nodiscard]] MediaSource* getMediaSourceProperty() const;
        [[nodiscard]] PictureCollection* getPicturesProperty() const;
        [[nodiscard]] PlaylistCollection* getPlaylistsProperty() const;
        [[nodiscard]] PictureAlbum* getRootPictureAlbumProperty() const;
        [[nodiscard]] PictureCollection* getSavedPicturesProperty() const;
        [[nodiscard]] SongCollection* getSongsProperty() const;

        Picture* GetPictureFromToken(std::string token);
        Picture* SavePicture(std::string name, const std::vector<uint8_t>& imageBuffer);

        [[nodiscard]] const std::string& GetTypeName() const override;
    };
}
