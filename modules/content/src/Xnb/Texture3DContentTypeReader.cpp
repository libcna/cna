// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/Texture3DContentTypeReader.hpp"

#include <algorithm>
#include <cstdint>
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
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
            return input.getContentManagerProperty()->getGraphicsDeviceInternal();
        }
    }

    std::shared_ptr<Texture3D> Texture3DReader::Read(
        ContentReader& input, std::optional<std::shared_ptr<Texture3D>> existingInstance)
    {
        const XnbTextureData decoded = DecodeTexture3DXnbData(input);
        const CNA::Content::Cnb::CnbTextureData rgba =
            ConvertXnbTextureToCnbRgba8(decoded, true);
        int32_t width = static_cast<int32_t>(decoded.width);
        int32_t height = static_cast<int32_t>(decoded.height);
        int32_t depth = static_cast<int32_t>(decoded.depth);
        const int32_t levelCount = static_cast<int32_t>(decoded.mipCount);

        std::shared_ptr<Texture3D> texture = existingInstance.value_or(nullptr);
        if (!texture)
        {
            texture = std::make_shared<Texture3D>(
                RequireGraphicsDevice(input), width, height, depth, levelCount > 1,
                SurfaceFormat::Color);
        }

        for (int32_t level = 0; level < levelCount; ++level)
        {
            const std::vector<uint8_t>& bytes =
                rgba.representations.front().levels[static_cast<std::size_t>(level)];
            const auto voxelCount = static_cast<int32_t>(bytes.size() / 4u);
            std::vector<Color> colors;
            colors.reserve(static_cast<std::size_t>(voxelCount));
            for (int32_t i = 0; i < voxelCount; ++i)
            {
                const std::size_t o = static_cast<std::size_t>(i) * 4;
                colors.emplace_back(bytes[o], bytes[o + 1], bytes[o + 2], bytes[o + 3]);
            }
            texture->SetData(level, 0, 0, width, height, 0, depth, colors.data(), 0, voxelCount);

            // Calculate dimensions of the next mip level, matching FNA exactly.
            width = std::max(width >> 1, 1);
            height = std::max(height >> 1, 1);
            depth = std::max(depth >> 1, 1);
        }

        return texture;
    }

    void RegisterTexture3DXnbReader()
    {
        Microsoft::Xna::Framework::Content::ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.Texture3DReader",
            [] { return std::make_unique<Texture3DReader>(); });
    }
}
