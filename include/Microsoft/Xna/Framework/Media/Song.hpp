// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Media
{
    /// Represents a song that can be played through MediaPlayer.
    class Song final : public System::Object, public System::IDisposable
    {
    public:
        /// Creates a song from a local file path and optional display name.
        explicit Song(std::string fileName, std::string name = {});

        /// Creates a song from a local file path, asset name and duration in milliseconds.
        Song(std::string fileName, std::string assetName, SharpRuntime::intcs durationMS);

        /// Disposes the song.
        ~Song() override;

        /// Gets the display name of the song.
        [[nodiscard]] const std::string& getNameProperty() const;

        /// Gets the song duration.
        [[nodiscard]] System::TimeSpan getDurationProperty() const;

        /// Sets the song duration. Used internally by MediaPlayer/content loading.
        void setDurationProperty(System::TimeSpan value);

        /// Gets whether the song is protected.
        [[nodiscard]] bool getIsProtectedProperty() const;

        /// Gets whether the song has a user rating.
        [[nodiscard]] bool getIsRatedProperty() const;

        /// Gets how many times the song has been played.
        [[nodiscard]] SharpRuntime::intcs getPlayCountProperty() const;

        /// Sets how many times the song has been played.
        void setPlayCountProperty(SharpRuntime::intcs value);

        /// Gets the rating value.
        [[nodiscard]] SharpRuntime::intcs getRatingProperty() const;

        /// Gets the track number.
        [[nodiscard]] SharpRuntime::intcs getTrackNumberProperty() const;

        /// Gets whether the song has been disposed.
        [[nodiscard]] bool getIsDisposedProperty() const;

        /// Disposes the song.
        void Dispose() override;

        /// Compares two songs by their backing handle/path.
        [[nodiscard]] bool Equals(const Song* song) const;

        /// Gets the backend/local file handle used by this song.
        [[nodiscard]] const std::string& getHandle() const;

        /// Constructs a song from a local URI/path string.
        static Song* FromUri(const std::string& name, const std::string& uri);

        [[nodiscard]] const std::string& GetTypeName() const override;

        friend bool operator==(const Song& song1, const Song& song2);
        friend bool operator!=(const Song& song1, const Song& song2);

    private:
        std::string name_;
        System::TimeSpan duration_;
        SharpRuntime::intcs playCount_;
        bool isDisposed_;
        std::string handle_;
    };
}
