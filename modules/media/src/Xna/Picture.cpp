// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/Picture.hpp"

#include <vector>

#include "CNA/Internal/Media/MediaObjectDisposal.hpp"
#include "CNA/Internal/Media/ThumbnailGenerator.hpp"
#include "System/IO/MemoryStream.hpp"

#include <functional>
#include <utility>

#include "System/IO/FileStream.hpp"

namespace Microsoft::Xna::Framework::Media
{
    using CNA::Internal::Media::ThrowIfDisposed;

    Picture::Picture(std::string name, PictureAlbum* album, SharpRuntime::intcs width,
                      SharpRuntime::intcs height, std::chrono::system_clock::time_point date,
                      std::string path)
        : name_(std::move(name)), album_(album), width_(width), height_(height), date_(date),
          path_(std::move(path))
    {
    }

    void Picture::Dispose()
    {
        // A Picture is a non-owning view into MediaLibrary's real data -- nothing picture-specific
        // to release, only the disposed flag itself.
        isDisposed_ = true;
    }

    PictureAlbum* Picture::getAlbumProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Picture");
        return album_;
    }

    std::chrono::system_clock::time_point Picture::getDateProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Picture");
        return date_;
    }

    SharpRuntime::intcs Picture::getHeightProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Picture");
        return height_;
    }

    bool Picture::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    std::string Picture::getNameProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Picture");
        return name_;
    }

    SharpRuntime::intcs Picture::getWidthProperty() const
    {
        ThrowIfDisposed(isDisposed_, "Picture");
        return width_;
    }

    System::IO::Stream* Picture::GetImage()
    {
        ThrowIfDisposed(isDisposed_, "Picture");
        return new System::IO::FileStream(path_);
    }

    System::IO::Stream* Picture::GetThumbnail()
    {
        ThrowIfDisposed(isDisposed_, "Picture");
        // Same fix as Album::GetThumbnail -- this was a synonym for GetImage() returning the
        // full-size picture, sharing one downscaler rather than duplicating it (MEDIA-210).
        std::vector<uint8_t> png;
        if (CNA::Internal::Media::ThumbnailGenerator::CreatePngThumbnail(path_, png))
        {
            return new System::IO::MemoryStream(
                png.data(), static_cast<SharpRuntime::intcs>(png.size()));
        }
        return new System::IO::FileStream(path_);
    }

    bool Picture::Equals(const Picture* other) const
    {
        ThrowIfDisposed(isDisposed_, "Picture");
        // A disposed Picture has released its handle, so it equals only itself, as in XNA.
        return other == this || (other != nullptr && !other->isDisposed_ && path_ == other->path_);
    }

    bool Picture::Equals(const System::Object* obj) const
    {
        ThrowIfDisposed(isDisposed_, "Picture");
        return Equals(dynamic_cast<const Picture*>(obj));
    }

    int Picture::GetHashCode() const
    {
        ThrowIfDisposed(isDisposed_, "Picture");
        return static_cast<int>(std::hash<std::string>{}(path_));
    }

    std::string Picture::ToString() const
    {
        ThrowIfDisposed(isDisposed_, "Picture");
        return name_;
    }

    const std::string& Picture::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Picture";
        return typeName;
    }

    bool operator==(const Picture& lhs, const Picture& rhs)
    {
        // object.Equals(first, second): the same object is equal without asking it anything.
        return &lhs == &rhs || lhs.Equals(&rhs);
    }

    bool operator!=(const Picture& lhs, const Picture& rhs)
    {
        return !(lhs == rhs);
    }
}
