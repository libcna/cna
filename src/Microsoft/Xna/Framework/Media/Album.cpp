// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/Album.hpp"

#include <functional>
#include <utility>

#include "Microsoft/Xna/Framework/Media/Artist.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/IO/FileStream.hpp"

namespace Microsoft::Xna::Framework::Media
{
    Album::Album(std::string name, Artist* artist, Genre* genre, System::TimeSpan duration,
                 std::string artPath, SongCollection* songs)
        : name_(std::move(name)), artist_(artist), genre_(genre), duration_(duration),
          artPath_(std::move(artPath)), songs_(songs)
    {
    }

    void Album::Dispose()
    {
        // An Album is a non-owning view into MediaLibrary's real data -- nothing album-specific to
        // release, only the disposed flag itself.
        isDisposed_ = true;
    }

    Artist* Album::getArtistProperty() const
    {
        return artist_;
    }

    System::TimeSpan Album::getDurationProperty() const
    {
        return duration_;
    }

    Genre* Album::getGenreProperty() const
    {
        return genre_;
    }

    bool Album::getHasArtProperty() const
    {
        return !artPath_.empty();
    }

    bool Album::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    std::string Album::getNameProperty() const
    {
        return name_;
    }

    SongCollection* Album::getSongsProperty() const
    {
        return songs_;
    }

    System::IO::Stream* Album::GetAlbumArt()
    {
        if (artPath_.empty())
        {
            throw System::InvalidOperationException("This album has no album art.");
        }
        return new System::IO::FileStream(artPath_);
    }

    System::IO::Stream* Album::GetThumbnail()
    {
        return GetAlbumArt();
    }

    bool Album::Equals(const Album* other) const
    {
        // Album names can collide across different artists -- equality is by (Name, Artist), not
        // Name alone.
        if (other == nullptr) return false;
        if (name_ != other->name_) return false;
        if (artist_ == other->artist_) return true;
        if (artist_ == nullptr || other->artist_ == nullptr) return false;
        return artist_->Equals(other->artist_);
    }

    int Album::GetHashCode() const
    {
        std::size_t h1 = std::hash<std::string>{}(name_);
        std::size_t h2 = artist_ != nullptr ? static_cast<std::size_t>(artist_->GetHashCode()) : 0;
        return static_cast<int>(h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2)));
    }

    std::string Album::ToString() const
    {
        return name_;
    }

    const std::string& Album::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Album";
        return typeName;
    }

    bool operator==(const Album& lhs, const Album& rhs)
    {
        return lhs.Equals(&rhs);
    }

    bool operator!=(const Album& lhs, const Album& rhs)
    {
        return !(lhs == rhs);
    }
}
