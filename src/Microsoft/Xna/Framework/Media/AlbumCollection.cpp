#include "Microsoft/Xna/Framework/Media/AlbumCollection.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Media
{
    void AlbumCollection::Dispose()
    {
        // TODO: implement collection cleanup
        throw std::runtime_error("not implemented");
    }

    SharpRuntime::intcs AlbumCollection::getCountProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    bool AlbumCollection::getIsDisposedProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    Album* AlbumCollection::operator[](SharpRuntime::intcs index) const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    AlbumCollection::iterator AlbumCollection::begin()
    {
        return innerList_.begin();
    }

    AlbumCollection::iterator AlbumCollection::end()
    {
        return innerList_.end();
    }

    AlbumCollection::const_iterator AlbumCollection::begin() const
    {
        return innerList_.begin();
    }

    AlbumCollection::const_iterator AlbumCollection::end() const
    {
        return innerList_.end();
    }

    const std::string& AlbumCollection::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.AlbumCollection";
        return typeName;
    }
}
