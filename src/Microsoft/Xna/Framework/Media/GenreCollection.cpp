#include "Microsoft/Xna/Framework/Media/GenreCollection.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Media
{
    void GenreCollection::Dispose()
    {
        // TODO: implement collection cleanup
        throw std::runtime_error("not implemented");
    }

    SharpRuntime::intcs GenreCollection::getCountProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    bool GenreCollection::getIsDisposedProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    Genre* GenreCollection::operator[](SharpRuntime::intcs index) const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    GenreCollection::iterator GenreCollection::begin()
    {
        return innerList_.begin();
    }

    GenreCollection::iterator GenreCollection::end()
    {
        return innerList_.end();
    }

    GenreCollection::const_iterator GenreCollection::begin() const
    {
        return innerList_.begin();
    }

    GenreCollection::const_iterator GenreCollection::end() const
    {
        return innerList_.end();
    }

    const std::string& GenreCollection::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.GenreCollection";
        return typeName;
    }
}
