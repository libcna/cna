#include "Microsoft/Xna/Framework/Media/Playlist.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Media
{
    void Playlist::Dispose()
    {
        // TODO: implement playlist resource cleanup
        throw std::runtime_error("not implemented");
    }

    System::TimeSpan Playlist::getDurationProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    bool Playlist::getIsDisposedProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    std::string Playlist::getNameProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    SongCollection* Playlist::getSongsProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    bool Playlist::Equals(const Playlist* other) const
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }

    std::string Playlist::ToString() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    const std::string& Playlist::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Playlist";
        return typeName;
    }

    bool operator==(const Playlist& lhs, const Playlist& rhs)
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }

    bool operator!=(const Playlist& lhs, const Playlist& rhs)
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }
}
