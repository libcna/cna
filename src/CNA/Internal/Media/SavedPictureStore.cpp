// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Media/SavedPictureStore.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace CNA::Internal::Media
{
    namespace
    {
        std::string SniffImageExtension(const std::vector<uint8_t>& data)
        {
            static const uint8_t kPngMagic[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
            if (data.size() >= 8 && std::equal(std::begin(kPngMagic), std::end(kPngMagic), data.begin()))
            {
                return ".png";
            }
            if (data.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
            {
                return ".jpg";
            }
            if (data.size() >= 2 && data[0] == 'B' && data[1] == 'M')
            {
                return ".bmp";
            }
            return ".png"; // unrecognized -- default to a supported, rescan-visible extension
        }

        // `name` is caller-supplied and untrusted -- reduces it to a single, safe path *segment*
        // (no directory traversal, no absolute-path escape) before it's ever used to build a real
        // filesystem path. A caller passing "../../etc/passwd" or an absolute path like
        // "/etc/cron.d/evil" must not be able to write outside the Saved Pictures directory.
        // Normalizes backslashes to forward slashes first (Windows-style separators are also a
        // real traversal vector there, even though std::filesystem::path only treats '/' as a
        // separator by default on this platform), keeps only the last path segment, and rejects
        // "."/".."/empty results in favor of a safe fallback name.
        std::string SanitizePictureName(const std::string& name)
        {
            std::string normalized = name;
            std::replace(normalized.begin(), normalized.end(), '\\', '/');

            std::filesystem::path segment = std::filesystem::path(normalized).filename();
            std::string result = segment.string();
            if (result.empty() || result == "." || result == "..")
            {
                return "picture";
            }
            return result;
        }
    }

    std::string SavedPictureStore::GetSavedPicturesDirectory(const std::string& picturesRoot)
    {
        if (picturesRoot.empty())
        {
            return {};
        }
        std::filesystem::path dir = std::filesystem::path(picturesRoot) / "Saved Pictures";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec && !std::filesystem::exists(dir))
        {
            return {};
        }
        return dir.string();
    }

    std::string SavedPictureStore::SavePicture(const std::string& picturesRoot,
                                                const std::string& name,
                                                const std::vector<uint8_t>& data)
    {
        std::string dir = GetSavedPicturesDirectory(picturesRoot);
        if (dir.empty())
        {
            return {};
        }

        std::filesystem::path outPath =
            std::filesystem::path(dir) / (SanitizePictureName(name) + SniffImageExtension(data));
        std::ofstream out(outPath, std::ios::binary);
        if (!out.is_open())
        {
            return {};
        }
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out.good())
        {
            return {};
        }
        return outPath.string();
    }
}
