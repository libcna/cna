// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class MediaLibrary;
    class PictureAlbumCollection;
    class PictureCollection;

    /** @brief Represents a hierarchical album of pictures in the media library. */
    class PictureAlbum final : public System::Object, public System::IDisposable
    {
    public:
        /** @brief Releases the resources used by this picture album. */
        void Dispose() override;

        /**
         * @brief Gets the collection of child picture albums within this album.
         *
         * @return Pointer to the PictureAlbumCollection.
         */
        [[nodiscard]] PictureAlbumCollection* getAlbumsProperty() const;

        /**
         * @brief Gets whether this picture album has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets the display name of this picture album.
         *
         * @return Album name string.
         */
        [[nodiscard]] std::string getNameProperty() const;

        /**
         * @brief Gets the parent picture album, or nullptr if this is a root album.
         *
         * @return Pointer to the parent PictureAlbum, or nullptr.
         */
        [[nodiscard]] PictureAlbum* getParentProperty() const;

        /**
         * @brief Gets the collection of pictures directly in this album.
         *
         * @return Pointer to the PictureCollection.
         */
        [[nodiscard]] PictureCollection* getPicturesProperty() const;

        /**
         * @brief Returns whether this picture album is equal to another.
         *
         * @param other PictureAlbum to compare with.
         * @return true if equal; otherwise false.
         */
        [[nodiscard]] bool Equals(const PictureAlbum* other) const;

        /**
         * @brief Returns whether this picture album is equal to another object.
         *
         * @param obj Object to compare with.
         * @return true if the object is an equal PictureAlbum; otherwise false.
         */
        [[nodiscard]] bool Equals(const System::Object* obj) const override;

        /**
         * @brief Gets the hash code for this PictureAlbum instance.
         * @return Hash code of the object.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Returns a string representation of this picture album.
         *
         * @return Album name string.
         */
        [[nodiscard]] std::string ToString() const;

        /** @brief Returns the fully-qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Returns whether two picture albums are equal. */
        friend bool operator==(const PictureAlbum& lhs, const PictureAlbum& rhs);

        /** @brief Returns whether two picture albums are not equal. */
        friend bool operator!=(const PictureAlbum& lhs, const PictureAlbum& rhs);

    private:
        friend class MediaLibrary;
        PictureAlbum(std::string name, PictureAlbum* parent, std::string path);
        void SetChildAlbumsAndPictures(PictureAlbumCollection* childAlbums, PictureCollection* pictures);

        std::string name_;
        PictureAlbum* parent_; // non-owning; nullptr for the root album
        std::string path_;     // canonical path -- identity/equality key, not exposed publicly
        PictureAlbumCollection* childAlbums_ = nullptr; // non-owning -- owned by MediaLibrary
        PictureCollection* pictures_ = nullptr;         // non-owning
        bool isDisposed_ = false;
    };
}
