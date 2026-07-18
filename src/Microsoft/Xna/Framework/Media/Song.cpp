// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/Song.hpp"

#include <filesystem>
#include <utility>

#include "System/IO/FileNotFoundException.hpp"

namespace Microsoft::Xna::Framework::Media
{
    Song::Song(std::string fileName, std::string name)
        : name_(std::move(name)),
          duration_(System::TimeSpan::Zero),
          playCount_(0),
          isDisposed_(false),
          handle_(std::move(fileName))
    {
        // FNA's ctor throws FileNotFoundException(fileName) directly (Song.cs); match the
        // established CNA-wide convention (SoundBank/WaveBank) of a descriptive message plus the
        // path via getFileNameProperty(), rather than a bare std::runtime_error(handle_).
        if (!std::filesystem::exists(handle_))
        {
            throw System::IO::FileNotFoundException(
                "Could not find file '" + handle_ + "'.", handle_);
        }
    }

    Song::Song(std::string fileName, std::string assetName, SharpRuntime::intcs durationMS)
        : Song(std::move(fileName), std::move(assetName))
    {
        duration_ = System::TimeSpan::FromMilliseconds(durationMS);
    }

    Song::~Song()
    {
        Dispose();
    }

    const std::string& Song::getNameProperty() const
    {
        return name_;
    }

    Album* Song::getAlbumProperty() const
    {
        return album_;
    }

    Artist* Song::getArtistProperty() const
    {
        return artist_;
    }

    Genre* Song::getGenreProperty() const
    {
        return genre_;
    }

    System::TimeSpan Song::getDurationProperty() const
    {
        return duration_;
    }

    void Song::setDurationProperty(System::TimeSpan value)
    {
        duration_ = value;
    }

    bool Song::getIsProtectedProperty() const
    {
        return false;
    }

    bool Song::getIsRatedProperty() const
    {
        return false;
    }

    SharpRuntime::intcs Song::getPlayCountProperty() const
    {
        return playCount_;
    }

    void Song::setPlayCountProperty(SharpRuntime::intcs value)
    {
        playCount_ = value;
    }

    SharpRuntime::intcs Song::getRatingProperty() const
    {
        return 0;
    }

    SharpRuntime::intcs Song::getTrackNumberProperty() const
    {
        return trackNumber_;
    }

    bool Song::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    void Song::Dispose()
    {
        isDisposed_ = true;
    }

    bool Song::Equals(const Song* song) const
    {
        return song != nullptr && handle_ == song->handle_;
    }

    int Song::GetHashCode() const
    {
        return static_cast<int>(std::hash<std::string>{}(handle_));
    }

    std::string Song::ToString() const
    {
        // The XNA reference documents only "Returns a String representation of the Song" without
        // specifying the string. Returning the name matches what Album/Artist/Genre/Playlist in
        // this same namespace already do (each returns its own name_), so Song follows the
        // established convention rather than inventing a different format -- plan_media.md
        // MEDIA-176. This is a documented inference from sibling types, NOT a behavior verified
        // against a decompiled XNA binary.
        return name_;
    }

    const std::string& Song::getHandle() const
    {
        return handle_;
    }

    Song* Song::FromUri(const std::string& name, const std::string& uri)
    {
        return new Song(uri, name);
    }

    const std::string& Song::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Song";
        return typeName;
    }

    bool operator==(const Song& song1, const Song& song2)
    {
        return song1.Equals(&song2);
    }

    bool operator!=(const Song& song1, const Song& song2)
    {
        return !(song1 == song2);
    }
}
