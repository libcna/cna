// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Media/SavedPictureStore.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>

#include "CNA/Internal/PathUtf8.hpp"

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

            const std::optional<std::filesystem::path> native = TryPathFromUtf8(normalized);
            if (!native)
            {
                return "picture";
            }
            std::string result = PathToUtf8(native->filename());
            if (result.empty() || result == "." || result == "..")
            {
                return "picture";
            }
            return result;
        }

        // The native form of GetSavedPicturesDirectory(), so SavePicture() can join onto the
        // directory it just created without narrowing and re-parsing it. Returns an empty path on
        // failure, which is what the public string form reports as an empty string.
        std::filesystem::path ResolveSavedPicturesDirectory(const std::string& picturesRoot)
        {
            if (picturesRoot.empty())
            {
                return {};
            }
            const std::optional<std::filesystem::path> root = TryPathFromUtf8(picturesRoot);
            if (!root)
            {
                return {};
            }
            std::filesystem::path dir = *root / "Saved Pictures";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            if (ec && !std::filesystem::exists(dir))
            {
                return {};
            }
            return dir;
        }
    }

    std::string SavedPictureStore::GetSavedPicturesDirectory(const std::string& picturesRoot)
    {
        const std::filesystem::path dir = ResolveSavedPicturesDirectory(picturesRoot);
        if (dir.empty())
        {
            return {};
        }
        return PathToUtf8(dir);
    }

    std::string SavedPictureStore::SavePicture(const std::string& picturesRoot,
                                                const std::string& name,
                                                const std::vector<uint8_t>& data)
    {
        const std::filesystem::path dir = ResolveSavedPicturesDirectory(picturesRoot);
        if (dir.empty())
        {
            return {};
        }

        // One conversion, of the sanitized leaf name only: the directory is already native.
        const std::optional<std::filesystem::path> leaf =
            TryPathFromUtf8(SanitizePictureName(name) + SniffImageExtension(data));
        if (!leaf)
        {
            return {};
        }

        std::filesystem::path outPath = dir / *leaf;
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
        return PathToUtf8(outPath);
    }
}
