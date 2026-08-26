// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "System/IDisposable.hpp"
#include "System/IO/Stream.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class Artist;
    class Genre;
    class MediaLibrary;
    class SongCollection;

    /** @brief Represents a music album in the media library. */
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
         * @brief Returns a stream over the cover art image for this album.
         *
         * The FNA/XNA return type is Stream (not the previous placeholder void*, corrected here
         * to match the real API shape now that this member is genuinely implemented).
         *
         * @throws System::InvalidOperationException If HasArt is false.
         * @return Pointer to a Stream over the album art image data. Caller owns the Stream.
         */
        System::IO::Stream* GetAlbumArt();

        /**
         * @brief Returns a stream over a thumbnail image for this album.
         *
         * No separate thumbnail is generated -- returns the same image as GetAlbumArt(), matching
         * this being a from-scratch feature with no FNA behavior to differ against.
         *
         * @throws System::InvalidOperationException If HasArt is false.
         * @return Pointer to a Stream over the thumbnail image data. Caller owns the Stream.
         */
        System::IO::Stream* GetThumbnail();

        /**
         * @brief Returns whether this album is equal to another.
         *
         * @param other Album to compare with.
         * @return true if equal; otherwise false.
         */
        [[nodiscard]] bool Equals(const Album* other) const;

        /**
         * @brief Returns whether this album is equal to another object.
         *
         * @param obj Object to compare with.
         * @return true if the object is an equal Album; otherwise false.
         */
        [[nodiscard]] bool Equals(const System::Object* obj) const override;

        /**
         * @brief Gets the hash code for this Album instance.
         * @return Hash code of the object.
         */
        [[nodiscard]] int GetHashCode() const override;

        /**
         * @brief Returns a string representation of this album.
         *
         * @return Album name string.
         */
        [[nodiscard]] std::string ToString() const override;

        /** @brief Returns the fully-qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        // MediaLibrary is the only component that knows which member song carries embedded art;
        // this stays internal rather than widening the public XNA surface (plans/plan_media.md MEDIA-208).
        CNAEXT friend class MediaLibrary;

        /** @brief Returns whether two albums are equal. */
        friend bool operator==(const Album& lhs, const Album& rhs);

        /** @brief Returns whether two albums are not equal. */
        friend bool operator!=(const Album& lhs, const Album& rhs);

    private:
        friend class MediaLibrary;
        Album(std::string name, Artist* artist, Genre* genre, System::TimeSpan duration,
              std::string artPath, SongCollection* songs);

        std::string name_;
        Artist* artist_; // non-owning -- owned by MediaLibrary
        Genre* genre_;   // non-owning
        System::TimeSpan duration_;
        std::string artPath_; // file-based cover art; empty if none was found

        // Audio file carrying embedded cover art (ID3v2 APIC / FLAC PICTURE), used only when no
        // file-based art exists. Stored as a path rather than the decoded bytes so a large library
        // does not hold every album's artwork in memory; extraction happens on demand in
        // GetAlbumArt() (plans/plan_media.md MEDIA-206/207/208).
        std::string embeddedArtSourcePath_;
        SongCollection* songs_; // non-owning
        bool isDisposed_ = false;
    };
}
