// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "System/IDisposable.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class PictureAlbumCollection;
    class PictureCollection;

    /// Represents an album of pictures in the media library.
    /// @note Status: Stub — media library catalog access not implemented
    class PictureAlbum final : public System::Object, public System::IDisposable
    {
    public:
        void Dispose() override;

        [[nodiscard]] PictureAlbumCollection* getAlbumsProperty() const;
        [[nodiscard]] bool getIsDisposedProperty() const;
        [[nodiscard]] std::string getNameProperty() const;
        [[nodiscard]] PictureAlbum* getParentProperty() const;
        [[nodiscard]] PictureCollection* getPicturesProperty() const;

        [[nodiscard]] bool Equals(const PictureAlbum* other) const;
        [[nodiscard]] std::string ToString() const;

        [[nodiscard]] const std::string& GetTypeName() const override;

        friend bool operator==(const PictureAlbum& lhs, const PictureAlbum& rhs);
        friend bool operator!=(const PictureAlbum& lhs, const PictureAlbum& rhs);
    };
}
