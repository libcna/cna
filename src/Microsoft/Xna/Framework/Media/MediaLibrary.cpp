#include "Microsoft/Xna/Framework/Media/MediaLibrary.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Media
{
    MediaLibrary::MediaLibrary()
    {
    }

    MediaLibrary::MediaLibrary(MediaSource* mediaSource)
    {
        // TODO: implement media library catalog integration
        throw std::runtime_error("not implemented");
    }

    void MediaLibrary::Dispose()
    {
        // TODO: implement media library catalog integration
        throw std::runtime_error("not implemented");
    }

    AlbumCollection* MediaLibrary::getAlbumsProperty() const
    {
        // TODO: implement media library catalog integration
        throw std::runtime_error("not implemented");
    }

    ArtistCollection* MediaLibrary::getArtistsProperty() const
    {
        // TODO: implement media library catalog integration
        throw std::runtime_error("not implemented");
    }

    GenreCollection* MediaLibrary::getGenresProperty() const
    {
        // TODO: implement media library catalog integration
        throw std::runtime_error("not implemented");
    }

    bool MediaLibrary::getIsDisposedProperty() const
    {
        // TODO: implement media library catalog integration
        throw std::runtime_error("not implemented");
    }

    MediaSource* MediaLibrary::getMediaSourceProperty() const
    {
        // TODO: implement media library catalog integration
        throw std::runtime_error("not implemented");
    }

    PictureCollection* MediaLibrary::getPicturesProperty() const
    {
        // TODO: implement media library catalog integration
        throw std::runtime_error("not implemented");
    }

    PlaylistCollection* MediaLibrary::getPlaylistsProperty() const
    {
        // TODO: implement media library catalog integration
        throw std::runtime_error("not implemented");
    }

    PictureAlbum* MediaLibrary::getRootPictureAlbumProperty() const
    {
        // TODO: implement media library catalog integration
        throw std::runtime_error("not implemented");
    }

    PictureCollection* MediaLibrary::getSavedPicturesProperty() const
    {
        // TODO: implement media library catalog integration
        throw std::runtime_error("not implemented");
    }

    SongCollection* MediaLibrary::getSongsProperty() const
    {
        // TODO: implement media library catalog integration
        throw std::runtime_error("not implemented");
    }

    Picture* MediaLibrary::GetPictureFromToken(std::string token)
    {
        // TODO: implement media library catalog integration
        throw std::runtime_error("not implemented");
    }

    Picture* MediaLibrary::SavePicture(std::string name, const std::vector<uint8_t>& imageBuffer)
    {
        // TODO: implement media library catalog integration
        throw std::runtime_error("not implemented");
    }

    const std::string& MediaLibrary::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.MediaLibrary";
        return typeName;
    }
}
