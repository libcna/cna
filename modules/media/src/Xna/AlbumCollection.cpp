// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/AlbumCollection.hpp"

namespace Microsoft::Xna::Framework::Media
{
    AlbumCollection::AlbumCollection(std::vector<Album*> albums)
        : base_(std::move(albums))
    {
    }

    void AlbumCollection::Dispose()
    {
        base_.Dispose();
    }

    SharpRuntime::intcs AlbumCollection::getCountProperty() const
    {
        return base_.Count();
    }

    bool AlbumCollection::getIsDisposedProperty() const
    {
        return base_.IsDisposed();
    }

    Album* AlbumCollection::operator[](SharpRuntime::intcs index) const
    {
        return base_.At(index);
    }

    AlbumCollection::iterator AlbumCollection::begin()
    {
        return base_.begin();
    }

    AlbumCollection::iterator AlbumCollection::end()
    {
        return base_.end();
    }

    AlbumCollection::const_iterator AlbumCollection::begin() const
    {
        return base_.begin();
    }

    AlbumCollection::const_iterator AlbumCollection::end() const
    {
        return base_.end();
    }

    const std::string& AlbumCollection::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.AlbumCollection";
        return typeName;
    }
}
