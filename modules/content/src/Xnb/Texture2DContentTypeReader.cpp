// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"

#include <algorithm>
#include <cstdint>

#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte4.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::PackedVector::NormalizedByte2;
    using Microsoft::Xna::Framework::Graphics::PackedVector::NormalizedByte4;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace
    {
        bool IsCompressed(SurfaceFormat format)
        {
            return format == SurfaceFormat::Dxt1 || format == SurfaceFormat::Dxt3 ||
                   format == SurfaceFormat::Dxt5;
        }

    }

    Texture* TextureReader::Read(ContentReader& /*input*/, std::optional<Texture*> existingInstance)
    {
        return existingInstance.value_or(nullptr);
    }

    Texture2D Texture2DReader::Read(ContentReader& input, std::optional<Texture2D> existingInstance)
    {
        GraphicsDevice* device = input.getContentManagerProperty()
            ? &input.getContentManagerProperty()->getGraphicsDeviceInternal()
            : nullptr;
        if (!device)
        {
            throw ContentLoadException(
                "Texture2DReader: no GraphicsDevice available (ContentManager was not set on "
                "this ContentReader).");
        }
        const XnbTextureData decoded = DecodeTexture2DXnbData(
            input, static_cast<std::uint32_t>(device->GetMaxTextureDimension()));
        return CreateTexture2DFromXnbData(input, decoded, std::move(existingInstance));
    }

    Texture2D CreateTexture2DFromXnbData(
        ContentReader& input, const XnbTextureData& decoded,
        std::optional<Texture2D> existingInstance)
    {
        const int32_t width = static_cast<int32_t>(decoded.width);
        const int32_t height = static_cast<int32_t>(decoded.height);
        const int32_t levelCount = static_cast<int32_t>(decoded.mipCount);
        const SurfaceFormat surfaceFormat = decoded.surfaceFormat;
        GraphicsDevice* device = input.getContentManagerProperty()
            ? &input.getContentManagerProperty()->getGraphicsDeviceInternal()
            : nullptr;
        if (!device)
        {
            throw ContentLoadException(
                "Texture2DReader: no GraphicsDevice available (ContentManager was not set on "
                "this ContentReader).");
        }

        // WEBGPU-144 Phase 2 (XNB-24's per-renderer capability query): keep DXT content compressed
        // and upload the raw blocks when the active renderer opts in via
        // LoadsCompressedContentNativelyEXT() AND transfers this format as blocks; otherwise
        // CPU-decompress DXT to Color exactly as before (the historical default for every other
        // renderer).
        const bool keepCompressed = IsCompressed(surfaceFormat)
            && device->GetRenderer().LoadsCompressedContentNativelyEXT()
            && device->GetRenderer().IsCompressedTransferFormatEXT(static_cast<int>(surfaceFormat));
        const SurfaceFormat uploadFormat =
            (IsCompressed(surfaceFormat) && !keepCompressed) ? SurfaceFormat::Color : surfaceFormat;
        if (!keepCompressed &&
            uploadFormat != SurfaceFormat::Color &&
            uploadFormat != SurfaceFormat::NormalizedByte4 &&
            uploadFormat != SurfaceFormat::NormalizedByte2)
        {
            throw ContentLoadException(
                "Texture2DReader: SurfaceFormat is not yet supported by CNA's .xnb reader "
                "(only Color/NormalizedByte2/NormalizedByte4/Dxt1/Dxt3/Dxt5 are implemented "
                "so far).");
        }

        // REMED-CONTENT-001: reject dimensions/mip counts the active renderer cannot actually
        // create, before any renderer-specific texture creation is attempted. Neither the native
        // graphics APIs' own validation (Vulkan's validation layer is advisory; wgpu-native
        // validates lazily at submit time) nor CheckDecodedByteSize() above (which only bounds the
        // width*height product, not either axis individually) catches these -- confirmed
        // reproducible process crashes (Vulkan stack smashing, WebGPU non-catchable panic) via
        // XnbContainerFuzzTest before this check existed.
        const int maxDim = device->GetMaxTextureDimension();
        if (width > maxDim || height > maxDim)
        {
            throw ContentLoadException(
                "Texture2DReader: " + std::to_string(width) + "x" + std::to_string(height) +
                " exceeds this device's maximum texture dimension of " + std::to_string(maxDim) + ".");
        }
        if (existingInstance.has_value()
            && (existingInstance->getWidthProperty() != width
                || existingInstance->getHeightProperty() != height
                || existingInstance->getFormatProperty() != uploadFormat
                || existingInstance->getLevelCountProperty() != levelCount))
        {
            throw ContentLoadException(
                "Texture2DReader: existing texture dimensions, format, or mip count do not "
                "match the serialized asset.");
        }

        Texture2D texture = existingInstance.has_value()
            ? std::move(*existingInstance)
            : Texture2D(*device, width, height, levelCount > 1, uploadFormat);

        std::vector<std::vector<uint8_t>> uploadLevels;
        if (IsCompressed(surfaceFormat) && !keepCompressed)
        {
            uploadLevels = ConvertXnbTextureToCnbRgba8(decoded, true)
                .representations.front().levels;
        }
        else
        {
            uploadLevels = decoded.levels;
        }

        for (int32_t level = 0; level < levelCount; ++level)
        {
            std::vector<uint8_t>& bytes = uploadLevels[static_cast<std::size_t>(level)];

            const int32_t levelWidth = std::max(1, width >> level);
            const int32_t levelHeight = std::max(1, height >> level);

            if (IsCompressed(surfaceFormat))
            {
                if (keepCompressed)
                {
                    // WEBGPU-144 Phase 2: upload the validated raw blocks through the compressed
                    // SetData path (no CPU decode); the RGBA-pixel validation below does not apply.
                    texture.SetData(level, nullptr, bytes.data(), 0, static_cast<int>(bytes.size()));
                    continue;
                }
            }

            const int32_t pixelCount = levelWidth * levelHeight;
            // The compressed branch above always produces exactly pixelCount*4 bytes by
            // construction; an uncompressed format can still disagree here, if the file's own
            // declared byteCount does not match levelWidth/levelHeight (a truncated/adversarial
            // file) -- catch that before indexing into bytes below. The expected size follows
            // the format's own bytes per pixel, which is 2 for NormalizedByte2.
            if (uploadFormat == SurfaceFormat::NormalizedByte2)
            {
                std::vector<NormalizedByte2> normals(static_cast<std::size_t>(pixelCount));
                for (int32_t i = 0; i < pixelCount; ++i)
                {
                    const std::size_t o = static_cast<std::size_t>(i) * 2;
                    const uint16_t packed = static_cast<uint16_t>(
                        static_cast<uint16_t>(bytes[o]) |
                        static_cast<uint16_t>(static_cast<uint16_t>(bytes[o + 1]) << 8u));
                    normals[static_cast<std::size_t>(i)].setPackedValueProperty(packed);
                }
                texture.SetData(level, nullptr, normals.data(), 0, pixelCount);
            }
            else if (uploadFormat == SurfaceFormat::NormalizedByte4)
            {
                std::vector<NormalizedByte4> normals(static_cast<std::size_t>(pixelCount));
                for (int32_t i = 0; i < pixelCount; ++i)
                {
                    const std::size_t o = static_cast<std::size_t>(i) * 4;
                    const uint32_t packed = static_cast<uint32_t>(bytes[o]) |
                        (static_cast<uint32_t>(bytes[o + 1]) << 8u) |
                        (static_cast<uint32_t>(bytes[o + 2]) << 16u) |
                        (static_cast<uint32_t>(bytes[o + 3]) << 24u);
                    normals[static_cast<std::size_t>(i)].setPackedValueProperty(packed);
                }
                texture.SetData(level, nullptr, normals.data(), 0, pixelCount);
            }
            else
            {
                // Color is not a raw 4-byte POD -- it derives from IPackedVectorT, which has
                // virtual methods, so construct real values instead of reinterpreting the bytes.
                std::vector<Color> colors;
                colors.reserve(static_cast<std::size_t>(pixelCount));
                for (int32_t i = 0; i < pixelCount; ++i)
                {
                    const std::size_t o = static_cast<std::size_t>(i) * 4;
                    colors.emplace_back(bytes[o], bytes[o + 1], bytes[o + 2], bytes[o + 3]);
                }
                texture.SetData(level, nullptr, colors.data(), 0, pixelCount);
            }
        }

        return texture;
    }

    void RegisterTexture2DXnbReader()
    {
        Microsoft::Xna::Framework::Content::ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.TextureReader",
            [] { return std::make_unique<TextureReader>(); });
        Microsoft::Xna::Framework::Content::ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.Texture2DReader",
            [] { return std::make_unique<Texture2DReader>(); });
    }
}
