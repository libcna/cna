// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class AlbumCollection;
    class MediaLibrary;
    class SongCollection;

    /** @brief Represents a music artist in the media library. */
    class Artist final : public System::Object, public System::IDisposable
    {
    public:
        /** @brief Releases the resources used by this artist. */
        void Dispose() override;

        /**
         * @brief Gets the collection of albums by this artist.
         *
         * @return Pointer to the AlbumCollection.
         */
        [[nodiscard]] AlbumCollection* getAlbumsProperty() const;

        /**
         * @brief Gets whether this artist has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets the display name of this artist.
         *
         * @return Artist name string.
         */
        [[nodiscard]] std::string getNameProperty() const;

        /**
         * @brief Gets the collection of songs by this artist.
         *
         * @return Pointer to the SongCollection.
         */
        [[nodiscard]] SongCollection* getSongsProperty() const;

        /**
         * @brief Returns whether this artist is equal to another.
         *
         * @param other Artist to compare with.
         * @return true if equal; otherwise false.
         */
        [[nodiscard]] bool Equals(const Artist* other) const;

        /**
         * @brief Returns whether this artist is equal to another object.
         *
         * @param obj Object to compare with.
         * @return true if the object is an equal Artist; otherwise false.
         */
        [[nodiscard]] bool Equals(const System::Object* obj) const override;

        /**
         * @brief Gets the hash code for this Artist instance.
         * @return Hash code of the object.
         */
        [[nodiscard]] int GetHashCode() const override;

        /**
         * @brief Returns a string representation of this artist.
         *
         * @return Artist name string.
         */
        [[nodiscard]] std::string ToString() const override;

        /** @brief Returns the fully-qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Returns whether two artists are equal. */
        friend bool operator==(const Artist& lhs, const Artist& rhs);

        /** @brief Returns whether two artists are not equal. */
        friend bool operator!=(const Artist& lhs, const Artist& rhs);

    private:
        friend class MediaLibrary;
        Artist(std::string name, AlbumCollection* albums, SongCollection* songs);
        void SetAlbums(AlbumCollection* albums) { albums_ = albums; }

        std::string name_;
        AlbumCollection* albums_; // non-owning -- owned by MediaLibrary
        SongCollection* songs_;   // non-owning
        bool isDisposed_ = false;
    };
}
