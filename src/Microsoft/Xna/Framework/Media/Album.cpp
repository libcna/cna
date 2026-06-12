// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/Album.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Media
{
    void Album::Dispose()
    {
        // TODO: implement album resource cleanup
        throw std::runtime_error("not implemented");
    }

    Artist* Album::getArtistProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    System::TimeSpan Album::getDurationProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    Genre* Album::getGenreProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    bool Album::getHasArtProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    bool Album::getIsDisposedProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    std::string Album::getNameProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    SongCollection* Album::getSongsProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    void* Album::GetAlbumArt()
    {
        // TODO: implement album art retrieval
        throw std::runtime_error("not implemented");
    }

    void* Album::GetThumbnail()
    {
        // TODO: implement thumbnail retrieval
        throw std::runtime_error("not implemented");
    }

    bool Album::Equals(const Album* other) const
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }

    std::string Album::ToString() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    const std::string& Album::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Album";
        return typeName;
    }

    bool operator==(const Album& lhs, const Album& rhs)
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }

    bool operator!=(const Album& lhs, const Album& rhs)
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }
}
