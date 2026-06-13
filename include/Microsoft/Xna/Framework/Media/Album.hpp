// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class Artist;
    class Genre;
    class SongCollection;

    /**
     * @brief Represents a music album in the media library.
     *
     * @note Status: Stub — media library catalog access not implemented.
     */
    class Album final : public System::Object, public System::IDisposable
    {
    public:
        /** @brief Releases the resources used by this album. */
        void Dispose() override;

        /**
         * @brief Gets the artist associated with this album.
         *
         * @return Pointer to the Artist, or nullptr.
         */
        [[nodiscard]] Artist* getArtistProperty() const;

        /**
         * @brief Gets the total duration of this album.
         *
         * @return Duration as a TimeSpan.
         */
        [[nodiscard]] System::TimeSpan getDurationProperty() const;

        /**
         * @brief Gets the genre associated with this album.
         *
         * @return Pointer to the Genre, or nullptr.
         */
        [[nodiscard]] Genre* getGenreProperty() const;

        /**
         * @brief Gets whether this album has associated cover art.
         *
         * @return true if art is available; otherwise false.
         */
        [[nodiscard]] bool getHasArtProperty() const;

        /**
         * @brief Gets whether this album has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets the display name of this album.
         *
         * @return Album name string.
         */
        [[nodiscard]] std::string getNameProperty() const;

        /**
         * @brief Gets the collection of songs in this album.
         *
         * @return Pointer to the SongCollection.
         */
        [[nodiscard]] SongCollection* getSongsProperty() const;

        /**
         * @brief Returns the cover art image for this album, or nullptr if unavailable.
         *
         * @return Opaque pointer to image data.
         */
        void* GetAlbumArt();

        /**
         * @brief Returns a thumbnail image for this album, or nullptr if unavailable.
         *
         * @return Opaque pointer to thumbnail data.
         */
        void* GetThumbnail();

        /**
         * @brief Returns whether this album is equal to another.
         *
         * @param other Album to compare with.
         * @return true if equal; otherwise false.
         */
        [[nodiscard]] bool Equals(const Album* other) const;

        /**
         * @brief Returns a string representation of this album.
         *
         * @return Album name string.
         */
        [[nodiscard]] std::string ToString() const;

        /** @brief Returns the fully-qualified .NET type name. */
        [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Returns whether two albums are equal. */
        friend bool operator==(const Album& lhs, const Album& rhs);

        /** @brief Returns whether two albums are not equal. */
        friend bool operator!=(const Album& lhs, const Album& rhs);
    };
}
