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
    // Forward-declared to break the Song <-> Album/Artist/Genre include cycle: each of those types
    // exposes a SongCollection of its members, and Song points back at all three. Matches the same
    // pattern Album.hpp already uses to forward-declare SongCollection (plans/plan_media.md MEDIA-174).
    class Album;
    class Artist;
    class Genre;
    class MediaLibrary;

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
        CNAEXT explicit Song(std::string fileName, std::string name = {});

        /**
         * @brief Creates a song from a local file path, asset name, and duration in milliseconds.
         *
         * @param fileName   File path to the audio file.
         * @param assetName  Asset/display name for this song.
         * @param durationMS Song duration in milliseconds.
         */
        CNAEXT Song(std::string fileName, std::string assetName, SharpRuntime::intcs durationMS);

        /** @brief Destroys the song and releases any associated resources. */
        CNAEXT ~Song() override;

        /**
         * @brief Gets the display name of this song.
         *
         * @return Song name string.
         */
        [[nodiscard]] const std::string& getNameProperty() const;

        /**
         * @brief Gets the Album on which the Song appears.
         *
         * Only songs obtained from a MediaLibrary scan belong to an album; a song constructed
         * directly from a file path (or loaded from an XNB) has no library context and returns
         * nullptr, matching the null a C# get-only reference property would yield.
         *
         * @return Non-owning pointer to the containing Album, or nullptr if this song is not part
         *         of a media library.
         */
        [[nodiscard]] Album* getAlbumProperty() const;

        /**
         * @brief Gets the Artist of the Song.
         *
         * @return Non-owning pointer to the containing Artist, or nullptr if this song is not part
         *         of a media library.
         */
        [[nodiscard]] Artist* getArtistProperty() const;

        /**
         * @brief Gets the Genre of the Song.
         *
         * @return Non-owning pointer to the containing Genre, or nullptr if this song is not part
         *         of a media library.
         */
        [[nodiscard]] Genre* getGenreProperty() const;

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
        CNAEXT void setDurationProperty(System::TimeSpan value);

        /**
         * @brief Gets a value that indicates whether the song is DRM protected content.
         *
         * Always returns false. This is the *correct* answer for everything CNA can index rather
         * than an unimplemented stub: the library scan only accepts plain, unencrypted container
         * formats, and a DRM-wrapped file (e.g. FairPlay .m4p) is not indexable in the first
         * place, so no indexed song can ever be protected. Revisit only if a future format
         * expansion makes DRM-wrapped files reachable (plans/plan_media.md MEDIA-185).
         *
         * @return Always false.
         */
        [[nodiscard]] bool getIsProtectedProperty() const;

        /**
         * @brief Gets a value that indicates whether the song has been rated by the user.
         *
         * True only when the file carried a real rating tag (ID3v2 POPM or a Vorbis RATING
         * comment). Deliberately NOT the same as "Rating != 0": both formats reserve 0 for
         * "unrated", so an explicit zero must still report IsRated == false.
         *
         * @return true if the user has rated this song; otherwise false.
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
        CNAEXT void setPlayCountProperty(SharpRuntime::intcs value);

        /**
         * @brief Gets the user's rating for the Song, on XNA's 0-10 scale.
         *
         * Populated from the file's own tags for library songs; 0 for a standalone song or an
         * unrated file. Source scales differ per format and are converted -- see
         * CNA::Internal::Media::AudioTags::rating and CHECKLIST.md for the documented mappings.
         *
         * @return Rating from 0 to 10, or 0 if unrated.
         */
        [[nodiscard]] SharpRuntime::intcs getRatingProperty() const;

        /**
         * @brief Gets the track number of the song on the song's Album.
         *
         * Populated from the file's own tags when the song comes from a MediaLibrary scan.
         * A song constructed directly from a file path has no scanned tag data and returns 0,
         * which also matches the "unknown track number" convention used by the tag formats
         * themselves.
         *
         * @return Track number, or 0 if unknown.
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
         * @brief Returns whether this song is equal to another object.
         *
         * @param obj Object to compare with.
         * @return true if the object is an equal Song; otherwise false.
         */
        [[nodiscard]] bool Equals(const System::Object* obj) const override;

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
         * @brief Returns a String representation of the Song.
         *
         * @return Song name string.
         */
        [[nodiscard]] std::string ToString() const override;

        /**
         * @brief Gets the renderer file path or handle used by this song.
         *
         * @return Handle/path string.
         */
        CNAEXT [[nodiscard]] const std::string& getHandle() const;

        /**
         * @brief Constructs a song from a local URI/path string.
         *
         * @param name Display name for the song.
         * @param uri  File URI or local path.
         * @return Pointer to the newly created Song.
         */
        static Song* FromUri(const std::string& name, const std::string& uri);

        /** @brief Returns the fully-qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Returns whether two songs are equal. */
        friend bool operator==(const Song& song1, const Song& song2);

        /** @brief Returns whether two songs are not equal. */
        friend bool operator!=(const Song& song1, const Song& song2);

        // Album/Artist/Genre are get-only in the XNA API, so no public setter exists for them.
        // MediaLibrary is the only thing that may populate them: it owns the Album/Artist/Genre
        // objects and is the only component that knows the song-to-group mapping. Friendship keeps
        // the write path internal instead of widening the public XNA surface with a non-XNA setter
        // (plans/plan_media.md MEDIA-175) -- same approach VideoPlayer already uses to reach
        // Video::parent_.
        CNAEXT friend class MediaLibrary;

    private:
        // Non-owning back-pointers into the MediaLibrary that produced this song; null for any
        // song not created by a library scan. The library outlives the songs it owns, so these do
        // not dangle in normal use -- the same non-owning-pointer contract Album::songs_ already
        // relies on (plans/plan_media.md MEDIA-174).
        Album*  album_  = nullptr;
        Artist* artist_ = nullptr;
        Genre*  genre_  = nullptr;

        // Populated by MediaLibrary from the file's own tags (plans/plan_media.md MEDIA-181). The index
        // already parsed the track number; it simply was never handed to Song, so this property
        // returned a hardcoded 0 for every song in the library.
        SharpRuntime::intcs trackNumber_ = 0;

        // Real user rating parsed from the file's tags, 0-10 (plans/plan_media.md MEDIA-184).
        SharpRuntime::intcs rating_ = 0;
        bool isRated_ = false;

        std::string name_;
        System::TimeSpan duration_;
        SharpRuntime::intcs playCount_;
        bool isDisposed_;
        std::string handle_;
    };
}
