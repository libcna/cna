// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/Artist.hpp"
#include "CNA/Internal/Media/MediaObjectDisposal.hpp"

#include <functional>
#include <utility>

namespace Microsoft::Xna::Framework::Media
{
    using CNA::Internal::Media::ThrowIfDisposed;

    Artist::Artist(std::string name, AlbumCollection* albums, SongCollection* songs)
        : name_(std::move(name)), albums_(albums), songs_(songs)
    {
    }

    void Artist::Dispose()
    {
        // An Artist is a non-owning view into MediaLibrary's real data -- nothing artist-specific
        // to release, only the disposed flag itself.
        isDisposed_ = true;
    }

    AlbumCollection* Artist::getAlbumsProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Artist");
        return albums_;
    }

    bool Artist::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    std::string Artist::getNameProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Artist");
        return name_;
    }

    SongCollection* Artist::getSongsProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Artist");
        return songs_;
    }

    bool Artist::Equals(const Artist* other) const
    {
        ThrowIfDisposed(isDisposed_, "Artist");
        // A disposed Artist has released its handle, so it equals only itself, as in XNA.
        return other == this || (other != nullptr && !other->isDisposed_ && name_ == other->name_);
    }

    bool Artist::Equals(const System::Object* obj) const
    {
        ThrowIfDisposed(isDisposed_, "Artist");
        return Equals(dynamic_cast<const Artist*>(obj));
    }

    int Artist::GetHashCode() const
    {
        ThrowIfDisposed(isDisposed_, "Artist");
        return static_cast<int>(std::hash<std::string>{}(name_));
    }

    std::string Artist::ToString() const
    {
        ThrowIfDisposed(isDisposed_, "Artist");
        return name_;
    }

    const std::string& Artist::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Artist";
        return typeName;
    }

    bool operator==(const Artist& lhs, const Artist& rhs)
    {
        // object.Equals(first, second): the same object is equal without asking it anything.
        return &lhs == &rhs || lhs.Equals(&rhs);
    }

    bool operator!=(const Artist& lhs, const Artist& rhs)
    {
        return !(lhs == rhs);
    }
}
