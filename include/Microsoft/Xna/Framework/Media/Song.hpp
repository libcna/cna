// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Media
{
    /** @brief Represents a song that can be played through MediaPlayer. */
    class Song final : public System::Object, public System::IDisposable
    {
    public:
        /**
         * @brief Creates a song from a local file path with an optional display name.
         *
         * @param fileName File path to the audio file.
         * @param name     Optional display name; defaults to the file name.
         */
        NOXNA explicit Song(std::string fileName, std::string name = {});

        /**
         * @brief Creates a song from a local file path, asset name, and duration in milliseconds.
         *
         * @param fileName   File path to the audio file.
         * @param assetName  Asset/display name for this song.
         * @param durationMS Song duration in milliseconds.
         */
        NOXNA Song(std::string fileName, std::string assetName, SharpRuntime::intcs durationMS);

        /** @brief Destroys the song and releases any associated resources. */
        NOXNA ~Song() override;

        /**
         * @brief Gets the display name of this song.
         *
         * @return Song name string.
         */
        [[nodiscard]] const std::string& getNameProperty() const;

        /**
         * @brief Gets the playback duration of this song.
         *
         * @return Duration as a TimeSpan.
         */
        [[nodiscard]] System::TimeSpan getDurationProperty() const;

        /**
         * @brief Sets the playback duration of this song.
         *
         * @param value New duration.
         */
        NOXNA void setDurationProperty(System::TimeSpan value);

        /**
         * @brief Gets whether this song has DRM copy protection.
         *
         * Always returns false; matches FNA, which never varies this value on desktop
         * (Song.cs hardcodes the same constant, since there is no real DRM-protection source).
         *
         * @return true if protected; otherwise false.
         */
        [[nodiscard]] bool getIsProtectedProperty() const;

        /**
         * @brief Gets whether this song has a user rating.
         *
         * Always returns false; matches FNA, which never varies this value on desktop.
         *
         * @return true if a rating has been set; otherwise false.
         */
        [[nodiscard]] bool getIsRatedProperty() const;

        /**
         * @brief Gets how many times this song has been played.
         *
         * @return Play count.
         */
        [[nodiscard]] SharpRuntime::intcs getPlayCountProperty() const;

        /**
         * @brief Sets how many times this song has been played.
         *
         * @param value New play count.
         */
        NOXNA void setPlayCountProperty(SharpRuntime::intcs value);

        /**
         * @brief Gets the user rating for this song.
         *
         * Always returns 0; matches FNA, which never varies this value on desktop.
         *
         * @return Rating value.
         */
        [[nodiscard]] SharpRuntime::intcs getRatingProperty() const;

        /**
         * @brief Gets the track number of this song within its album.
         *
         * Always returns 0; matches FNA, which never varies this value on desktop.
         *
         * @return Track number.
         */
        [[nodiscard]] SharpRuntime::intcs getTrackNumberProperty() const;

        /**
         * @brief Gets whether this song has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /** @brief Releases this song and any associated resources. */
        void Dispose() override;

        /**
         * @brief Returns whether this song is equal to another by comparing their handles.
         *
         * @param song Song to compare with.
         * @return true if equal; otherwise false.
         */
        [[nodiscard]] bool Equals(const Song* song) const;

        /**
         * @brief Gets the hash code for this Song instance.
         *
         * Deliberately content-based (a hash of the resolved file handle), unlike FNA's own
         * identity-based `base.GetHashCode()` -- FNA's choice means two FNA Songs that are
         * Equals-equal (same handle) can have different hash codes, violating the usual
         * Equals/GetHashCode contract. This is a documented, beneficial deviation, not an
         * unported detail: kept as-is rather than "fixed" to replicate FNA's inconsistency.
         *
         * @return Hash code of the object.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Gets the backend file path or handle used by this song.
         *
         * @return Handle/path string.
         */
        NOXNA [[nodiscard]] const std::string& getHandle() const;

        /**
         * @brief Constructs a song from a local URI/path string.
         *
         * @param name Display name for the song.
         * @param uri  File URI or local path.
         * @return Pointer to the newly created Song.
         */
        static Song* FromUri(const std::string& name, const std::string& uri);

        /** @brief Returns the fully-qualified .NET type name. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Returns whether two songs are equal. */
        friend bool operator==(const Song& song1, const Song& song2);

        /** @brief Returns whether two songs are not equal. */
        friend bool operator!=(const Song& song1, const Song& song2);

    private:
        std::string name_;
        System::TimeSpan duration_;
        SharpRuntime::intcs playCount_;
        bool isDisposed_;
        std::string handle_;
    };
}
