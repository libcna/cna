// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/Genre.hpp"
#include "CNA/Internal/Media/MediaObjectDisposal.hpp"

#include <functional>
#include <utility>

namespace Microsoft::Xna::Framework::Media
{
    using CNA::Internal::Media::ThrowIfDisposed;

    Genre::Genre(std::string name, AlbumCollection* albums, SongCollection* songs)
        : name_(std::move(name)), albums_(albums), songs_(songs)
    {
    }

    void Genre::Dispose()
    {
        // A Genre is a non-owning view into MediaLibrary's real data (MediaLibrary owns the
        // underlying Album/Song objects and their collections) -- there is nothing genre-specific
        // to release, only the disposed flag itself.
        isDisposed_ = true;
    }

    AlbumCollection* Genre::getAlbumsProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Genre");
        return albums_;
    }

    bool Genre::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    std::string Genre::getNameProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Genre");
        return name_;
    }

    SongCollection* Genre::getSongsProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Genre");
        return songs_;
    }

    bool Genre::Equals(const Genre* other) const
    {
        ThrowIfDisposed(isDisposed_, "Genre");
        // A disposed Genre has released its handle, so it equals only itself, as in XNA.
        return other == this || (other != nullptr && !other->isDisposed_ && name_ == other->name_);
    }

    bool Genre::Equals(const System::Object* obj) const
    {
        ThrowIfDisposed(isDisposed_, "Genre");
        return Equals(dynamic_cast<const Genre*>(obj));
    }

    int Genre::GetHashCode() const
    {
        ThrowIfDisposed(isDisposed_, "Genre");
        return static_cast<int>(std::hash<std::string>{}(name_));
    }

    std::string Genre::ToString() const
    {
        ThrowIfDisposed(isDisposed_, "Genre");
        return name_;
    }

    const std::string& Genre::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Genre";
        return typeName;
    }

    bool operator==(const Genre& lhs, const Genre& rhs)
    {
        // object.Equals(first, second): the same object is equal without asking it anything.
        return &lhs == &rhs || lhs.Equals(&rhs);
    }

    bool operator!=(const Genre& lhs, const Genre& rhs)
    {
        return !(lhs == rhs);
    }
}
