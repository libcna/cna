// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/PictureAlbumCollection.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Media
{
    void PictureAlbumCollection::Dispose()
    {
        // TODO: implement collection cleanup
        throw std::runtime_error("not implemented");
    }

    SharpRuntime::intcs PictureAlbumCollection::getCountProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    bool PictureAlbumCollection::getIsDisposedProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    PictureAlbum* PictureAlbumCollection::operator[](SharpRuntime::intcs index) const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    PictureAlbumCollection::iterator PictureAlbumCollection::begin()
    {
        return innerList_.begin();
    }

    PictureAlbumCollection::iterator PictureAlbumCollection::end()
    {
        return innerList_.end();
    }

    PictureAlbumCollection::const_iterator PictureAlbumCollection::begin() const
    {
        return innerList_.begin();
    }

    PictureAlbumCollection::const_iterator PictureAlbumCollection::end() const
    {
        return innerList_.end();
    }

    const std::string& PictureAlbumCollection::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.PictureAlbumCollection";
        return typeName;
    }
}
