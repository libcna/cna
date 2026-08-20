// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Internal::Media
{
    /// Backs MediaLibrary::SavePicture with a real "Saved Pictures" subfolder under the Pictures
    /// root (plans/plan_media.md MEDIA-59/D7) -- writes are real files, immediately visible to a
    /// PictureLibraryIndex rescan.
    class SavedPictureStore
    {
    public:
        /// Returns the "Saved Pictures" subfolder path under `picturesRoot`, creating it if it
        /// doesn't already exist. Returns an empty string if it couldn't be created.
        static std::string GetSavedPicturesDirectory(const std::string& picturesRoot);

        /// Writes `data` to a new file named "<name>.<ext>" inside the Saved Pictures directory
        /// (extension sniffed from the image's own magic bytes; defaults to ".png" if
        /// unrecognized), returning the full path written, or an empty string on failure.
        static std::string SavePicture(const std::string& picturesRoot, const std::string& name,
                                        const std::vector<uint8_t>& data);
    };
}
