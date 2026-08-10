// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/PlaylistCollection.hpp"

namespace Microsoft::Xna::Framework::Media
{
    PlaylistCollection::PlaylistCollection(std::vector<Playlist*> playlists)
        : base_(std::move(playlists))
    {
    }

    void PlaylistCollection::Dispose()
    {
        base_.Dispose();
    }

    SharpRuntime::intcs PlaylistCollection::getCountProperty() const
    {
        return base_.Count();
    }

    bool PlaylistCollection::getIsDisposedProperty() const
    {
        return base_.IsDisposed();
    }

    Playlist* PlaylistCollection::operator[](SharpRuntime::intcs index) const
    {
        return base_.At(index);
    }

    PlaylistCollection::iterator PlaylistCollection::begin()
    {
        return base_.begin();
    }

    PlaylistCollection::iterator PlaylistCollection::end()
    {
        return base_.end();
    }

    PlaylistCollection::const_iterator PlaylistCollection::begin() const
    {
        return base_.begin();
    }

    PlaylistCollection::const_iterator PlaylistCollection::end() const
    {
        return base_.end();
    }

    const std::string& PlaylistCollection::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.PlaylistCollection";
        return typeName;
    }
}
