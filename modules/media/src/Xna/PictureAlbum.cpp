// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/PictureAlbum.hpp"

#include <functional>
#include <utility>

namespace Microsoft::Xna::Framework::Media
{
    PictureAlbum::PictureAlbum(std::string name, PictureAlbum* parent, std::string path)
        : name_(std::move(name)), parent_(parent), path_(std::move(path))
    {
    }

    void PictureAlbum::SetChildAlbumsAndPictures(PictureAlbumCollection* childAlbums,
                                                   PictureCollection* pictures)
    {
        childAlbums_ = childAlbums;
        pictures_ = pictures;
    }

    void PictureAlbum::Dispose()
    {
        // A PictureAlbum is a non-owning view into MediaLibrary's real data -- nothing
        // album-specific to release, only the disposed flag itself.
        isDisposed_ = true;
    }

    PictureAlbumCollection* PictureAlbum::getAlbumsProperty() const
    {
        return childAlbums_;
    }

    bool PictureAlbum::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    std::string PictureAlbum::getNameProperty() const
    {
        return name_;
    }

    PictureAlbum* PictureAlbum::getParentProperty() const
    {
        return parent_;
    }

    PictureCollection* PictureAlbum::getPicturesProperty() const
    {
        return pictures_;
    }

    bool PictureAlbum::Equals(const PictureAlbum* other) const
    {
        return other != nullptr && path_ == other->path_;
    }

    bool PictureAlbum::Equals(const System::Object* obj) const
    {
        return Equals(dynamic_cast<const PictureAlbum*>(obj));
    }

    int PictureAlbum::GetHashCode() const
    {
        return static_cast<int>(std::hash<std::string>{}(path_));
    }

    std::string PictureAlbum::ToString() const
    {
        return name_;
    }

    const std::string& PictureAlbum::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.PictureAlbum";
        return typeName;
    }

    bool operator==(const PictureAlbum& lhs, const PictureAlbum& rhs)
    {
        return lhs.Equals(&rhs);
    }

    bool operator!=(const PictureAlbum& lhs, const PictureAlbum& rhs)
    {
        return !(lhs == rhs);
    }
}
