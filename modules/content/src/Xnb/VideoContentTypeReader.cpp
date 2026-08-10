// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/VideoContentTypeReader.hpp"

#include <array>
#include <filesystem>

#include "CNA/Internal/PathContainment.hpp"
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

        std::string ResolveRelativeFilePath(const std::string& contentRoot,
                                            const std::string& filePath,
                                            const std::string& relativeFile)
        {
            const auto result = CNA::Internal::ResolveContainedPathRelativeToFile(
                contentRoot, filePath, relativeFile);
            if (!result.ok)
            {
                throw ContentLoadException(
                    "VideoReader: embedded media path must be a non-empty relative path contained "
                    "within the authorized content root or explicit external bundle.");
            }
            return result.resolvedPath;
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

        namespace fs = std::filesystem;
        const std::string& contentRoot = contentManager->getRootDirectoryProperty();
        fs::path assetPath = fs::path(contentRoot) / input.getAssetNameProperty();
        assetPath += ".xnb";
        std::string path = ResolveRelativeFilePath(
            contentRoot, assetPath.string(), input.ReadString());

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
        // final selected candidate as well as the embedded spelling before constructing Video.
        const fs::path selectedRelative =
            fs::path(path).lexically_relative(assetPath.parent_path());
        path = ResolveRelativeFilePath(
            contentRoot, assetPath.string(), selectedRelative.generic_string());

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
