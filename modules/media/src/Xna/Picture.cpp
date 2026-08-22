// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/Picture.hpp"

#include <vector>

#include "CNA/Internal/Media/ThumbnailGenerator.hpp"
#include "System/IO/MemoryStream.hpp"

#include <functional>
#include <utility>

#include "System/IO/FileStream.hpp"

namespace Microsoft::Xna::Framework::Media
{
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
        return album_;
    }

    std::chrono::system_clock::time_point Picture::getDateProperty() const
    {
        return date_;
    }

    SharpRuntime::intcs Picture::getHeightProperty() const
    {
        return height_;
    }

    bool Picture::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    std::string Picture::getNameProperty() const
    {
        return name_;
    }

    SharpRuntime::intcs Picture::getWidthProperty() const
    {
        return width_;
    }

    System::IO::Stream* Picture::GetImage()
    {
        return new System::IO::FileStream(path_);
    }

    System::IO::Stream* Picture::GetThumbnail()
    {
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
        return other != nullptr && path_ == other->path_;
    }

    bool Picture::Equals(const System::Object* obj) const
    {
        return Equals(dynamic_cast<const Picture*>(obj));
    }

    int Picture::GetHashCode() const
    {
        return static_cast<int>(std::hash<std::string>{}(path_));
    }

    std::string Picture::ToString() const
    {
        return name_;
    }

    const std::string& Picture::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Picture";
        return typeName;
    }

    bool operator==(const Picture& lhs, const Picture& rhs)
    {
        return lhs.Equals(&rhs);
    }

    bool operator!=(const Picture& lhs, const Picture& rhs)
    {
        return !(lhs == rhs);
    }
}
