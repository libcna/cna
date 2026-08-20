// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Internal/Graphics/ImageData.hpp"

namespace CNA::Internal::Media
{
    /// Produces genuinely downscaled thumbnails for Album::GetThumbnail()/Picture::GetThumbnail()
    /// (plans/plan_media.md MEDIA-209/210).
    ///
    /// Both XNA methods previously returned the *full-size* image, making GetThumbnail a synonym
    /// for GetAlbumArt/GetImage rather than a thumbnail. XNA does not specify a thumbnail size, so
    /// MaxEdge below is a documented CNA choice (see CHECKLIST.md), not a claim of XNA parity.
    class ThumbnailGenerator
    {
    public:
        /// Longest edge of a generated thumbnail, in pixels. Aspect ratio is always preserved, so
        /// a non-square source yields a proportionally smaller shorter edge.
        static constexpr int MaxEdge = 128;

        /// Box-filter downscale of RGBA8 image data. Images already within MaxEdge on both axes
        /// are returned unchanged (never upscaled -- a "thumbnail" larger than its source would be
        /// pointless and lossy).
        static Graphics::ImageData Downscale(const Graphics::ImageData& source);

        /// Loads `path`, downscales it, and encodes the result as PNG.
        ///
        /// PNG is used regardless of the source format so callers get one predictable, lossless
        /// encoding; the source may be JPEG, PNG or anything else ImageLoader accepts.
        ///
        /// Returns false -- leaving `outPng` untouched -- in BOTH of these cases, because the
        /// caller's response is the same for each (serve the original file):
        ///   * the source is already within MaxEdge, so no thumbnail is needed and re-encoding it
        ///     would only lose quality and waste time;
        ///   * the source could not be loaded or encoded at all.
        ///
        /// @param path   Image file to thumbnail.
        /// @param outPng Receives the encoded PNG bytes when a real downscale was performed.
        static bool CreatePngThumbnail(const std::string& path, std::vector<uint8_t>& outPng);
    };
}
