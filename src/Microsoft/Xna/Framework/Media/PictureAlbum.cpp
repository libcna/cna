// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/PictureAlbum.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Media
{
    void PictureAlbum::Dispose()
    {
        // TODO: implement picture album resource cleanup
        throw std::runtime_error("not implemented");
    }

    PictureAlbumCollection* PictureAlbum::getAlbumsProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    bool PictureAlbum::getIsDisposedProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    std::string PictureAlbum::getNameProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    PictureAlbum* PictureAlbum::getParentProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    PictureCollection* PictureAlbum::getPicturesProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    bool PictureAlbum::Equals(const PictureAlbum* other) const
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }

    std::string PictureAlbum::ToString() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    const std::string& PictureAlbum::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.PictureAlbum";
        return typeName;
    }

    bool operator==(const PictureAlbum& lhs, const PictureAlbum& rhs)
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }

    bool operator!=(const PictureAlbum& lhs, const PictureAlbum& rhs)
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }
}
