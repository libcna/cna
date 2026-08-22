// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class MediaLibrary;
    class SongCollection;

    /** @brief Represents a playlist of songs in the media library. */
    class Playlist final : public System::Object, public System::IDisposable
    {
    public:
        /** @brief Releases the resources used by this playlist. */
        void Dispose() override;

        /**
         * @brief Gets the total duration of all songs in this playlist.
         *
         * @return Duration as a TimeSpan.
         */
        [[nodiscard]] System::TimeSpan getDurationProperty() const;

        /**
         * @brief Gets whether this playlist has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets the display name of this playlist.
         *
         * @return Playlist name string.
         */
        [[nodiscard]] std::string getNameProperty() const;

        /**
         * @brief Gets the collection of songs in this playlist.
         *
         * @return Pointer to the SongCollection.
         */
        [[nodiscard]] SongCollection* getSongsProperty() const;

        /**
         * @brief Returns whether this playlist is equal to another.
         *
         * @param other Playlist to compare with.
         * @return true if equal; otherwise false.
         */
        [[nodiscard]] bool Equals(const Playlist* other) const;

        /**
         * @brief Returns whether this playlist is equal to another object.
         *
         * @param obj Object to compare with.
         * @return true if the object is an equal Playlist; otherwise false.
         */
        [[nodiscard]] bool Equals(const System::Object* obj) const override;

        /**
         * @brief Gets the hash code for this Playlist instance.
         * @return Hash code of the object.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Returns a string representation of this playlist.
         *
         * @return Playlist name string.
         */
        [[nodiscard]] std::string ToString() const;

        /** @brief Returns the fully-qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Returns whether two playlists are equal. */
        friend bool operator==(const Playlist& lhs, const Playlist& rhs);

        /** @brief Returns whether two playlists are not equal. */
        friend bool operator!=(const Playlist& lhs, const Playlist& rhs);

    private:
        friend class MediaLibrary;
        Playlist(std::string name, SongCollection* songs, System::TimeSpan duration);

        std::string name_;
        SongCollection* songs_; // non-owning -- owned by MediaLibrary
        System::TimeSpan duration_;
        bool isDisposed_ = false;
    };
}
