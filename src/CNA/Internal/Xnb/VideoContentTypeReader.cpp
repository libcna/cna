// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/VideoContentTypeReader.hpp"

#include <algorithm>
#include <array>
#include <filesystem>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Media::Video;
    using Microsoft::Xna::Framework::Media::VideoSoundtrackType;

    namespace
    {
        // FNA's VideoReader.supportedExtensions.
        constexpr std::array<const char*, 2> kSupportedExtensions{".ogv", ".ogg"};

        // Identical in shape to SongContentTypeReader.cpp's own helper of the same name --
        // duplicated rather than shared, matching that file's own stated rationale (a real
        // filesystem path is needed here, not a ContentManager-relative logical asset name).
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

        // FNA's VideoReader.Normalize(): returns fileName unchanged if it already names a real
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

    Video VideoReader::Read(ContentReader& input, std::optional<Video> existingInstance)
    {
        (void)existingInstance; // never provided: CanDeserializeIntoExistingObject defaults false, matching FNA

        auto* contentManager = input.getContentManagerProperty();
        if (!contentManager)
        {
            throw ContentLoadException(
                "VideoReader: no ContentManager available (ContentReader has no owning ContentManager).");
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
        const int32_t width = input.ReadInt32();
        const int32_t height = input.ReadInt32();
        const float framesPerSecond = input.ReadSingle();
        const auto soundTrackType = static_cast<VideoSoundtrackType>(input.ReadInt32());

        GraphicsDevice* device = &contentManager->getGraphicsDeviceInternal();
        return Video(path, device, durationMs, width, height, framesPerSecond, soundTrackType);
    }

    void RegisterVideoXnbReader()
    {
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.VideoReader",
            [] { return std::make_unique<VideoReader>(); });
    }
}
