// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/SongContentTypeReader.hpp"

#include <array>
#include <filesystem>

#include "CNA/Internal/PathContainment.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
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

        std::string ResolveRelativeFilePath(const std::string& contentRoot,
                                            const std::string& filePath,
                                            const std::string& relativeFile)
        {
            const auto result = CNA::Internal::ResolveContainedPathRelativeToFile(
                contentRoot, filePath, relativeFile);
            if (!result.ok)
            {
                throw ContentLoadException(
                    "SongReader: embedded media path must be a non-empty relative path contained "
                    "within the authorized content root or explicit external bundle.");
            }
            return result.resolvedPath;
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
        const XnbSongData decoded = DecodeSongXnbData(input);

        auto* contentManager = input.getContentManagerProperty();
        if (!contentManager)
        {
            throw ContentLoadException(
                "SongReader: no ContentManager available (ContentReader has no owning ContentManager).");
        }

        namespace fs = std::filesystem;
        const std::string& contentRoot = contentManager->getRootDirectoryProperty();
        fs::path assetPath = fs::path(contentRoot) / input.getAssetNameProperty();
        assetPath += ".xnb";
        std::string path = ResolveRelativeFilePath(
            contentRoot, assetPath.string(), decoded.mediaPath);

        if (path.size() > 4)
        {
            const std::string withoutExtension = path.substr(0, path.size() - 4);
            const std::string normalized = Normalize(withoutExtension);
            if (!normalized.empty())
            {
                path = normalized;
            }
        }

        // Normalize() may select an extension-probed sibling that is a symlink, so validate the
        // final selected candidate as well as the embedded spelling before constructing Song.
        const fs::path selectedRelative =
            fs::path(path).lexically_relative(assetPath.parent_path());
        path = ResolveRelativeFilePath(
            contentRoot, assetPath.string(), selectedRelative.generic_string());

        return Song(path, input.getAssetNameProperty(), decoded.durationMs);
    }

    void RegisterSongXnbReader()
    {
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.SongReader",
            [] { return std::make_unique<SongReader>(); });
    }
}
