// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/Song.hpp"

#include <filesystem>
#include <stdexcept>
#include <utility>

namespace Microsoft::Xna::Framework::Media
{
    Song::Song(std::string fileName, std::string name)
        : name_(std::move(name)),
          duration_(System::TimeSpan::Zero),
          playCount_(0),
          isDisposed_(false),
          handle_(std::move(fileName))
    {
        if (!std::filesystem::exists(handle_))
        {
            throw std::runtime_error(handle_);
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
        return 0;
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
