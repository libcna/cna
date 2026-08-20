// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/Album.hpp"

#include <vector>

#include "CNA/Internal/Media/AudioTagParser.hpp"
#include "CNA/Internal/Media/ThumbnailGenerator.hpp"
#include "System/IO/MemoryStream.hpp"

#include <functional>
#include <utility>

#include "Microsoft/Xna/Framework/Media/Artist.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/IO/FileStream.hpp"

namespace Microsoft::Xna::Framework::Media
{
    Album::Album(std::string name, Artist* artist, Genre* genre, System::TimeSpan duration,
                 std::string artPath, SongCollection* songs)
        : name_(std::move(name)), artist_(artist), genre_(genre), duration_(duration),
          artPath_(std::move(artPath)), songs_(songs)
    {
    }

    void Album::Dispose()
    {
        // An Album is a non-owning view into MediaLibrary's real data -- nothing album-specific to
        // release, only the disposed flag itself.
        isDisposed_ = true;
    }

    Artist* Album::getArtistProperty() const
    {
        return artist_;
    }

    System::TimeSpan Album::getDurationProperty() const
    {
        return duration_;
    }

    Genre* Album::getGenreProperty() const
    {
        return genre_;
    }

    bool Album::getHasArtProperty() const
    {
        // Must agree EXACTLY with what GetAlbumArt() can actually produce -- a HasArt==true that
        // yields nothing is precisely the "claims coverage it doesn't have" defect this plan has
        // been burned by repeatedly (plans/plan_media.md MEDIA-208).
        return !artPath_.empty() || !embeddedArtSourcePath_.empty();
    }

    bool Album::getIsDisposedProperty() const
    {
        return isDisposed_;
    }

    std::string Album::getNameProperty() const
    {
        return name_;
    }

    SongCollection* Album::getSongsProperty() const
    {
        return songs_;
    }

    System::IO::Stream* Album::GetAlbumArt()
    {
        // Precedence: file-based art wins over embedded art. An album aggregates many tracks, so a
        // folder image is album-scoped by nature, whereas embedded art is per-track and several
        // member tracks could disagree; the folder image is also cheaper (no tag parse per call).
        // This deliberately differs from the initial recommendation written into MEDIA-208, which
        // suggested embedded-first -- the album-vs-track scoping argument is the stronger one.
        if (!artPath_.empty())
        {
            return new System::IO::FileStream(artPath_);
        }

        if (!embeddedArtSourcePath_.empty())
        {
            std::vector<uint8_t> image;
            if (CNA::Internal::Media::AudioTagParser::ExtractEmbeddedArt(embeddedArtSourcePath_, image)
                && !image.empty())
            {
                return new System::IO::MemoryStream(
                    image.data(), static_cast<SharpRuntime::intcs>(image.size()));
            }
        }

        throw System::InvalidOperationException("This album has no album art.");
    }

    System::IO::Stream* Album::GetThumbnail()
    {
        if (!getHasArtProperty())
        {
            throw System::InvalidOperationException("This album has no album art.");
        }

        // Embedded art has no file path to thumbnail from, so route it through GetAlbumArt() and
        // downscale the decoded bytes instead.
        if (artPath_.empty())
        {
            return GetAlbumArt();
        }

        // This used to just call GetAlbumArt(), making GetThumbnail a synonym that returned the
        // full-size image -- not a thumbnail at all (plans/plan_media.md MEDIA-209). Now genuinely
        // downscaled and re-encoded as PNG in memory.
        std::vector<uint8_t> png;
        if (CNA::Internal::Media::ThumbnailGenerator::CreatePngThumbnail(artPath_, png))
        {
            return new System::IO::MemoryStream(
                png.data(), static_cast<SharpRuntime::intcs>(png.size()));
        }

        // Unreadable or unsupported source image: fall back to the original file rather than
        // failing outright, so a caller that only wants *some* image still gets one.
        return new System::IO::FileStream(artPath_);
    }

    bool Album::Equals(const Album* other) const
    {
        // Album names can collide across different artists -- equality is by (Name, Artist), not
        // Name alone.
        if (other == nullptr) return false;
        if (name_ != other->name_) return false;
        if (artist_ == other->artist_) return true;
        if (artist_ == nullptr || other->artist_ == nullptr) return false;
        return artist_->Equals(other->artist_);
    }

    int Album::GetHashCode() const
    {
        std::size_t h1 = std::hash<std::string>{}(name_);
        std::size_t h2 = artist_ != nullptr ? static_cast<std::size_t>(artist_->GetHashCode()) : 0;
        return static_cast<int>(h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2)));
    }

    std::string Album::ToString() const
    {
        return name_;
    }

    const std::string& Album::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Album";
        return typeName;
    }

    bool operator==(const Album& lhs, const Album& rhs)
    {
        return lhs.Equals(&rhs);
    }

    bool operator!=(const Album& lhs, const Album& rhs)
    {
        return !(lhs == rhs);
    }
}
