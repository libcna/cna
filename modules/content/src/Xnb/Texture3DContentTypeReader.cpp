// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/Texture3DContentTypeReader.hpp"
#include "CNA/Internal/ContentManagerAccess.hpp"

#include <algorithm>
#include <cstdint>
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::Texture3D;

    namespace
    {
        GraphicsDevice& RequireGraphicsDevice(ContentReader& input)
        {
            if (!input.getContentManagerProperty())
            {
                throw ContentLoadException(
                    "Texture3DReader: no GraphicsDevice available (ContentManager was not set on "
                    "this ContentReader).");
            }
            return CNA::Internal::ContentManagerAccess::GraphicsDeviceFromContentReader(input);
        }
    }

    std::shared_ptr<Texture3D> Texture3DReader::Read(
        ContentReader& input, std::optional<std::shared_ptr<Texture3D>> /*existingInstance*/)
    {
        // Refresh Texture3DReader.Read reads the header, looks up the device and constructs the
        // texture (IL_0025-0035) -- whose CreateTexture refuses a profile without volume textures,
        // Reach, before anything else about the request (IL_0050-0074) -- and only then reads the
        // levels (IL_003c-008a). Like Texture2DReader it never loads existingInstance, so a
        // supplied volume is neither written nor a way around Reach's refusal.
        std::shared_ptr<Texture3D> texture;
        (void)DecodeTexture3DXnbData(
            input, [&](const XnbTextureData& header) {
                texture = std::make_shared<Texture3D>(
                    RequireGraphicsDevice(input), static_cast<int32_t>(header.width),
                    static_cast<int32_t>(header.height), static_cast<int32_t>(header.depth),
                    header.mipCount > 1, header.surfaceFormat);
            },
            [&](const XnbTextureData& header, std::uint32_t, std::uint32_t level,
                const std::vector<std::uint8_t>& bytes) {
                texture->SetData(static_cast<int32_t>(level), 0, 0,
                    static_cast<int32_t>(std::max(1u, header.width >> level)),
                    static_cast<int32_t>(std::max(1u, header.height >> level)), 0,
                    static_cast<int32_t>(std::max(1u, header.depth >> level)),
                    bytes.data(), 0, static_cast<int>(bytes.size()));
            });

        return texture;
    }

    void RegisterTexture3DXnbReader()
    {
        Microsoft::Xna::Framework::Content::ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.Texture3DReader",
            [] { return std::make_unique<Texture3DReader>(); });
    }
}
