// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/Playlist.hpp"

#include <functional>
#include <utility>

namespace Microsoft::Xna::Framework::Media
{
    Playlist::Playlist(std::string name, SongCollection* songs, System::TimeSpan duration)
        : name_(std::move(name)), songs_(songs), duration_(duration)
    {
    }

    void Playlist::Dispose()
    {
        // A Playlist is a non-owning view into MediaLibrary's real data -- nothing
        // playlist-specific to release, only the disposed flag itself.
        isDisposed_ = true;
    }

    System::TimeSpan Playlist::getDurationProperty() const
    {
        return duration_;
    }

    bool Playlist::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    std::string Playlist::getNameProperty() const
    {
        return name_;
    }

    SongCollection* Playlist::getSongsProperty() const
    {
        return songs_;
    }

    bool Playlist::Equals(const Playlist* other) const
    {
        return other != nullptr && name_ == other->name_;
    }

    bool Playlist::Equals(const System::Object* obj) const
    {
        return Equals(dynamic_cast<const Playlist*>(obj));
    }

    int Playlist::GetHashCode() const
    {
        return static_cast<int>(std::hash<std::string>{}(name_));
    }

    std::string Playlist::ToString() const
    {
        return name_;
    }

    const std::string& Playlist::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Playlist";
        return typeName;
    }

    // FNA's real operator== has a ReferenceEquals-then-Equals null-check shape, since its
    // parameters are nullable C# object references; CNA's operator overloads across this whole
    // namespace instead take C++ references (see Genre/Artist/Album/Picture/PictureAlbum), which
    // cannot represent "null" the same way -- Equals() is called directly, matching that
    // project-wide convention rather than reintroducing null-handling a reference can't express.
    bool operator==(const Playlist& lhs, const Playlist& rhs)
    {
        return lhs.Equals(&rhs);
    }

    bool operator!=(const Playlist& lhs, const Playlist& rhs)
    {
        return !(lhs == rhs);
    }
}
