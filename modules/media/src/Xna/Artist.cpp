// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/Artist.hpp"

#include <functional>
#include <utility>

namespace Microsoft::Xna::Framework::Media
{
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
        return albums_;
    }

    bool Artist::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    std::string Artist::getNameProperty() const
    {
        return name_;
    }

    SongCollection* Artist::getSongsProperty() const
    {
        return songs_;
    }

    bool Artist::Equals(const Artist* other) const
    {
        return other != nullptr && name_ == other->name_;
    }

    bool Artist::Equals(const System::Object* obj) const
    {
        return Equals(dynamic_cast<const Artist*>(obj));
    }

    int Artist::GetHashCode() const
    {
        return static_cast<int>(std::hash<std::string>{}(name_));
    }

    std::string Artist::ToString() const
    {
        return name_;
    }

    const std::string& Artist::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Artist";
        return typeName;
    }

    bool operator==(const Artist& lhs, const Artist& rhs)
    {
        return lhs.Equals(&rhs);
    }

    bool operator!=(const Artist& lhs, const Artist& rhs)
    {
        return !(lhs == rhs);
    }
}
