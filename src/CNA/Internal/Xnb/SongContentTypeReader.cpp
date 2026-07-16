// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/SongContentTypeReader.hpp"

#include <algorithm>
#include <array>
#include <filesystem>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
    using Microsoft::Xna::Framework::Media::Song;

    namespace
    {
        // FNA's SongReader.supportedExtensions.
        constexpr std::array<const char*, 3> kSupportedExtensions{".ogg", ".oga", ".qoa"};

        // Mirrors FNA's MonoGame.Utilities.FileHelpers.ResolveRelativePath (see also
        // ContentReader.cpp's identically-purposed helper for ReadExternalReference<T>()): resolves
        // relativeFile as a sibling of filePath, then collapses "."/".." segments. Duplicated
        // (rather than shared) because ContentReader.cpp's copy is file-local and this reader
        // needs a real filesystem path (for File::exists probing), not a ContentManager-relative
        // logical asset name.
        std::string ResolveRelativeFilePath(const std::string& filePath, const std::string& relativeFile)
        {
            namespace fs = std::filesystem;
            auto normalizeSeparators = [](std::string s)
            {
                std::replace(s.begin(), s.end(), '\\', '/');
                return s;
            };
            const fs::path base = fs::path(normalizeSeparators(filePath)).parent_path();
            const fs::path combined = base / normalizeSeparators(relativeFile);
            return combined.lexically_normal().generic_string();
        }

        // FNA's SongReader.Normalize(): returns fileName unchanged if it already names a real
        // file, else the first of fileName+kSupportedExtensions that names a real file, else empty
        // (matching FNA's null -- the caller falls back to the un-stripped original path).
        std::string Normalize(const std::string& fileName)
        {
            if (std::filesystem::exists(fileName))
            {
                return fileName;
            }
            for (const char* ext : kSupportedExtensions)
            {
                const std::string candidate = fileName + ext;
                if (std::filesystem::exists(candidate))
                {
                    return candidate;
                }
            }
            return {};
        }
    }

    Song SongReader::Read(ContentReader& input, std::optional<Song> existingInstance)
    {
        (void)existingInstance; // never provided: CanDeserializeIntoExistingObject defaults false, matching FNA

        auto* contentManager = input.getContentManagerProperty();
        if (!contentManager)
        {
            throw ContentLoadException(
                "SongReader: no ContentManager available (ContentReader has no owning ContentManager).");
        }

        const std::string assetPath =
            (std::filesystem::path(contentManager->getRootDirectoryProperty()) / input.getAssetNameProperty())
                .generic_string();
        std::string path = ResolveRelativeFilePath(assetPath, input.ReadString());

        if (path.size() > 4)
        {
            const std::string withoutExtension = path.substr(0, path.size() - 4);
            const std::string normalized = Normalize(withoutExtension);
            if (!normalized.empty())
            {
                path = normalized;
            }
        }

        const int32_t durationMs = input.ReadInt32();
        return Song(path, input.getAssetNameProperty(), durationMs);
    }

    void RegisterSongXnbReader()
    {
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.SongReader",
            [] { return std::make_unique<SongReader>(); });
    }
}
