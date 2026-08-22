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

    /** @brief Represents a music genre in the media library. */
    class Genre final : public System::Object, public System::IDisposable
    {
    public:
        /** @brief Releases the resources used by this genre. */
        void Dispose() override;

        /**
         * @brief Gets the collection of albums in this genre.
         *
         * @return Pointer to the AlbumCollection.
         */
        [[nodiscard]] AlbumCollection* getAlbumsProperty() const;

        /**
         * @brief Gets whether this genre has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets the display name of this genre.
         *
         * @return Genre name string.
         */
        [[nodiscard]] std::string getNameProperty() const;

        /**
         * @brief Gets the collection of songs in this genre.
         *
         * @return Pointer to the SongCollection.
         */
        [[nodiscard]] SongCollection* getSongsProperty() const;

        /**
         * @brief Returns whether this genre is equal to another.
         *
         * @param other Genre to compare with.
         * @return true if equal; otherwise false.
         */
        [[nodiscard]] bool Equals(const Genre* other) const;

        /**
         * @brief Returns whether this genre is equal to another object.
         *
         * @param obj Object to compare with.
         * @return true if the object is an equal Genre; otherwise false.
         */
        [[nodiscard]] bool Equals(const System::Object* obj) const override;

        /**
         * @brief Gets the hash code for this Genre instance.
         * @return Hash code of the object.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Returns a string representation of this genre.
         *
         * @return Genre name string.
         */
        [[nodiscard]] std::string ToString() const;

        /** @brief Returns the fully-qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Returns whether two genres are equal. */
        friend bool operator==(const Genre& lhs, const Genre& rhs);

        /** @brief Returns whether two genres are not equal. */
        friend bool operator!=(const Genre& lhs, const Genre& rhs);

    private:
        friend class MediaLibrary;
        Genre(std::string name, AlbumCollection* albums, SongCollection* songs);
        void SetAlbums(AlbumCollection* albums) { albums_ = albums; }

        std::string name_;
        AlbumCollection* albums_; // non-owning -- owned by MediaLibrary
        SongCollection* songs_;   // non-owning
        bool isDisposed_ = false;
    };
}
