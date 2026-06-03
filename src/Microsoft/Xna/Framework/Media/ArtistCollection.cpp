#include "Microsoft/Xna/Framework/Media/ArtistCollection.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Media
{
    void ArtistCollection::Dispose()
    {
        // TODO: implement collection cleanup
        throw std::runtime_error("not implemented");
    }

    SharpRuntime::intcs ArtistCollection::getCountProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    bool ArtistCollection::getIsDisposedProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    Artist* ArtistCollection::operator[](SharpRuntime::intcs index) const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    ArtistCollection::iterator ArtistCollection::begin()
    {
        return innerList_.begin();
    }

    ArtistCollection::iterator ArtistCollection::end()
    {
        return innerList_.end();
    }

    ArtistCollection::const_iterator ArtistCollection::begin() const
    {
        return innerList_.begin();
    }

    ArtistCollection::const_iterator ArtistCollection::end() const
    {
        return innerList_.end();
    }

    const std::string& ArtistCollection::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.ArtistCollection";
        return typeName;
    }
}
