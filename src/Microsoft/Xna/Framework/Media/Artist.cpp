#include "Microsoft/Xna/Framework/Media/Artist.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Media
{
    void Artist::Dispose()
    {
        // TODO: implement artist resource cleanup
        throw std::runtime_error("not implemented");
    }

    AlbumCollection* Artist::getAlbumsProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    bool Artist::getIsDisposedProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    std::string Artist::getNameProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    SongCollection* Artist::getSongsProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    bool Artist::Equals(const Artist* other) const
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }

    std::string Artist::ToString() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    const std::string& Artist::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Artist";
        return typeName;
    }

    bool operator==(const Artist& lhs, const Artist& rhs)
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }

    bool operator!=(const Artist& lhs, const Artist& rhs)
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }
}
