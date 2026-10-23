// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/PictureAlbum.hpp"
#include "CNA/Internal/Media/MediaObjectDisposal.hpp"

#include <functional>
#include <utility>

namespace Microsoft::Xna::Framework::Media
{
    using CNA::Internal::Media::ThrowIfDisposed;

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
        ThrowIfDisposed(isDisposed_, "PictureAlbum");
        return childAlbums_;
    }

    bool PictureAlbum::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    std::string PictureAlbum::getNameProperty() const
    {
        ThrowIfDisposed(isDisposed_, "PictureAlbum");
        return name_;
    }

    PictureAlbum* PictureAlbum::getParentProperty() const
    {
        ThrowIfDisposed(isDisposed_, "PictureAlbum");
        return parent_;
    }

    PictureCollection* PictureAlbum::getPicturesProperty() const
    {
        ThrowIfDisposed(isDisposed_, "PictureAlbum");
        return pictures_;
    }

    bool PictureAlbum::Equals(const PictureAlbum* other) const
    {
        ThrowIfDisposed(isDisposed_, "PictureAlbum");
        // A disposed PictureAlbum has released its handle, so it equals only itself, as in XNA.
        return other == this || (other != nullptr && !other->isDisposed_ && path_ == other->path_);
    }

    bool PictureAlbum::Equals(const System::Object* obj) const
    {
        ThrowIfDisposed(isDisposed_, "PictureAlbum");
        return Equals(dynamic_cast<const PictureAlbum*>(obj));
    }

    int PictureAlbum::GetHashCode() const
    {
        ThrowIfDisposed(isDisposed_, "PictureAlbum");
        return static_cast<int>(std::hash<std::string>{}(path_));
    }

    std::string PictureAlbum::ToString() const
    {
        ThrowIfDisposed(isDisposed_, "PictureAlbum");
        return name_;
    }

    const std::string& PictureAlbum::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.PictureAlbum";
        return typeName;
    }

    bool operator==(const PictureAlbum& lhs, const PictureAlbum& rhs)
    {
        // object.Equals(first, second): the same object is equal without asking it anything.
        return &lhs == &rhs || lhs.Equals(&rhs);
    }

    bool operator!=(const PictureAlbum& lhs, const PictureAlbum& rhs)
    {
        return !(lhs == rhs);
    }
}
