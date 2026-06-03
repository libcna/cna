#include "Microsoft/Xna/Framework/Media/Genre.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Media
{
    void Genre::Dispose()
    {
        // TODO: implement genre resource cleanup
        throw std::runtime_error("not implemented");
    }

    AlbumCollection* Genre::getAlbumsProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    bool Genre::getIsDisposedProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    std::string Genre::getNameProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    SongCollection* Genre::getSongsProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    bool Genre::Equals(const Genre* other) const
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }

    std::string Genre::ToString() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    const std::string& Genre::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Genre";
        return typeName;
    }

    bool operator==(const Genre& lhs, const Genre& rhs)
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }

    bool operator!=(const Genre& lhs, const Genre& rhs)
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }
}
