// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/PictureAlbumCollection.hpp"

namespace Microsoft::Xna::Framework::Media
{
    PictureAlbumCollection::PictureAlbumCollection(std::vector<PictureAlbum*> albums)
        : base_(std::move(albums))
    {
    }

    void PictureAlbumCollection::Dispose()
    {
        base_.Dispose();
    }

    SharpRuntime::intcs PictureAlbumCollection::getCountProperty() const
    {
        return base_.Count();
    }

    bool PictureAlbumCollection::getIsDisposedProperty() const
    {
        return base_.IsDisposed();
    }

    PictureAlbum* PictureAlbumCollection::operator[](SharpRuntime::intcs index) const
    {
        return base_.At(index);
    }

    PictureAlbumCollection::iterator PictureAlbumCollection::begin()
    {
        return base_.begin();
    }

    PictureAlbumCollection::iterator PictureAlbumCollection::end()
    {
        return base_.end();
    }

    PictureAlbumCollection::const_iterator PictureAlbumCollection::begin() const
    {
        return base_.begin();
    }

    PictureAlbumCollection::const_iterator PictureAlbumCollection::end() const
    {
        return base_.end();
    }

    const std::string& PictureAlbumCollection::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.PictureAlbumCollection";
        return typeName;
    }
}
