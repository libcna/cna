#include "Microsoft/Xna/Framework/Media/PlaylistCollection.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Media
{
    void PlaylistCollection::Dispose()
    {
        // TODO: implement collection cleanup
        throw std::runtime_error("not implemented");
    }

    SharpRuntime::intcs PlaylistCollection::getCountProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    bool PlaylistCollection::getIsDisposedProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    Playlist* PlaylistCollection::operator[](SharpRuntime::intcs index) const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    PlaylistCollection::iterator PlaylistCollection::begin()
    {
        return innerList_.begin();
    }

    PlaylistCollection::iterator PlaylistCollection::end()
    {
        return innerList_.end();
    }

    PlaylistCollection::const_iterator PlaylistCollection::begin() const
    {
        return innerList_.begin();
    }

    PlaylistCollection::const_iterator PlaylistCollection::end() const
    {
        return innerList_.end();
    }

    const std::string& PlaylistCollection::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.PlaylistCollection";
        return typeName;
    }
}
