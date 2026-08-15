// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

namespace CNA::Internal::Media
{
    /// Resolves the real per-OS Music/Pictures roots through the ambient platform filesystem, with
    /// CNAEXT static override hooks so tests don't depend on the real user's actual folder
    /// contents -- plan_media.md MEDIA-46/D1.
    class MediaLibraryPaths
    {
    public:
        /// Returns the real (or overridden) Music root folder, no trailing separator.
        static std::string GetMusicRoot();

        /// Returns the real (or overridden) Pictures root folder, no trailing separator.
        static std::string GetPictureRoot();

        /// Test-only override for the Music root. Pass an empty string to clear it.
        static void SetMusicRootOverride(const std::string& path);

        /// Test-only override for the Pictures root. Pass an empty string to clear it.
        static void SetPictureRootOverride(const std::string& path);

    private:
        static std::string musicOverride_;
        static std::string pictureOverride_;
    };
}
