// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/Texture3DContentTypeReader.hpp"

#include <algorithm>
#include <cstdint>

#include "CNA/Internal/Graphics/DxtUtil.hpp"
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
        bool IsCompressed(SurfaceFormat format)
        {
            return format == SurfaceFormat::Dxt1 || format == SurfaceFormat::Dxt3 ||
                   format == SurfaceFormat::Dxt5;
        }

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
        const auto surfaceFormat = static_cast<SurfaceFormat>(input.ReadInt32());
        int32_t width = input.ReadInt32();
        int32_t height = input.ReadInt32();
        int32_t depth = input.ReadInt32();
        const int32_t levelCount = input.ReadInt32();

        // Always decompress DXT to Color -- see Texture2DReader's own class docs for why.
        const SurfaceFormat uploadFormat = IsCompressed(surfaceFormat) ? SurfaceFormat::Color : surfaceFormat;
        if (uploadFormat != SurfaceFormat::Color)
        {
            throw ContentLoadException(
                "Texture3DReader: SurfaceFormat is not yet supported by CNA's .xnb reader "
                "(only Color/Dxt1/Dxt3/Dxt5 are implemented so far).");
        }

        std::shared_ptr<Texture3D> texture = existingInstance.value_or(nullptr);
        if (!texture)
        {
            texture = std::make_shared<Texture3D>(
                RequireGraphicsDevice(input), width, height, depth, levelCount > 1, uploadFormat);
        }

        for (int32_t level = 0; level < levelCount; ++level)
        {
            const int32_t byteCount = input.ReadInt32();
            std::vector<uint8_t> bytes = input.ReadBytes(byteCount);

            if (IsCompressed(surfaceFormat))
            {
                switch (surfaceFormat)
                {
                    case SurfaceFormat::Dxt1:
                        bytes = CNA::Internal::Graphics::DxtUtil::DecompressDxt1(bytes.data(), bytes.size(), width, height);
                        break;
                    case SurfaceFormat::Dxt3:
                        bytes = CNA::Internal::Graphics::DxtUtil::DecompressDxt3(bytes.data(), bytes.size(), width, height);
                        break;
                    case SurfaceFormat::Dxt5:
                        bytes = CNA::Internal::Graphics::DxtUtil::DecompressDxt5(bytes.data(), bytes.size(), width, height);
                        break;
                    default:
                        break; // unreachable: IsCompressed() only true for the three cases above
                }
            }

            // Color is not a raw 4-byte POD (it has a vtable) -- see Texture2DReader's own class
            // docs for why raw RGBA bytes must be unpacked into real Color values one at a time.
            const int32_t voxelCount = width * height * depth;
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
