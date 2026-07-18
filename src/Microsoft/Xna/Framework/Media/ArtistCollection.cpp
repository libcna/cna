// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/ArtistCollection.hpp"

namespace Microsoft::Xna::Framework::Media
{
    ArtistCollection::ArtistCollection(std::vector<Artist*> artists)
        : base_(std::move(artists))
    {
    }

    void ArtistCollection::Dispose()
    {
        base_.Dispose();
    }

    SharpRuntime::intcs ArtistCollection::getCountProperty() const
    {
        return base_.Count();
    }

    bool ArtistCollection::getIsDisposedProperty() const
    {
        return base_.IsDisposed();
    }

    Artist* ArtistCollection::operator[](SharpRuntime::intcs index) const
    {
        return base_.At(index);
    }

    ArtistCollection::iterator ArtistCollection::begin()
    {
        return base_.begin();
    }

    ArtistCollection::iterator ArtistCollection::end()
    {
        return base_.end();
    }

    ArtistCollection::const_iterator ArtistCollection::begin() const
    {
        return base_.begin();
    }

    ArtistCollection::const_iterator ArtistCollection::end() const
    {
        return base_.end();
    }

    const std::string& ArtistCollection::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.ArtistCollection";
        return typeName;
    }
}
