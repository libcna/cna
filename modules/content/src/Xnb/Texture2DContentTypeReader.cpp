// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"
#include "CNA/Internal/ContentManagerAccess.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>

#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
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

    namespace
    {
        /// The format a decoded Texture2D is created and uploaded in.
        struct Texture2DUpload
        {
            SurfaceFormat format = SurfaceFormat::Color;
            bool keepCompressed = false;
        };

        [[nodiscard]] Texture2DUpload PlanTexture2DUpload(GraphicsDevice& device,
                                                          const XnbTextureData& decoded)
        {
            const auto width = static_cast<int32_t>(decoded.width);
            const auto height = static_cast<int32_t>(decoded.height);
            const SurfaceFormat surfaceFormat = decoded.surfaceFormat;
            // WEBGPU-144 Phase 2 (XNB-24's per-renderer capability query): keep DXT content
            // compressed and upload the raw blocks when the active renderer opts in via
            // LoadsCompressedContentNativelyEXT() AND transfers this format as blocks; otherwise
            // CPU-decompress DXT to Color exactly as before (the historical default for every other
            // renderer).
            // XNA's content loader accepts authored DXT atlases with block-aligned NPOT dimensions
            // under Reach (the Platformer Hud font is Dxt3, 128x132), although the public Reach
            // Texture2D constructor refuses that shape. Use the existing lossless DXT-to-Color
            // content path for such assets; do not weaken the public construction contract.
            const bool reachNpotCompressed =
                device.getGraphicsProfileProperty() == GraphicsProfile::Reach &&
                (!std::has_single_bit(static_cast<unsigned int>(width)) ||
                 !std::has_single_bit(static_cast<unsigned int>(height)));
            Texture2DUpload upload;
            upload.keepCompressed = IsCompressed(surfaceFormat) && !reachNpotCompressed
                && device.GetRenderer().LoadsCompressedContentNativelyEXT()
                && device.GetRenderer().IsCompressedTransferFormatEXT(static_cast<int>(surfaceFormat));
            upload.format = (IsCompressed(surfaceFormat) && !upload.keepCompressed)
                ? SurfaceFormat::Color : surfaceFormat;
            return upload;
        }
    }

    void UploadTexture2DXnbLevel(Texture2D& texture, GraphicsDevice& device,
                                 const XnbTextureData& header, const std::uint32_t mip,
                                 const std::vector<std::uint8_t>& sourceBytes)
    {
        const int32_t width = static_cast<int32_t>(header.width);
        const int32_t height = static_cast<int32_t>(header.height);
        const int32_t level = static_cast<int32_t>(mip);
        const SurfaceFormat surfaceFormat = header.surfaceFormat;
        const bool keepCompressed = PlanTexture2DUpload(device, header).keepCompressed;
        std::vector<std::uint8_t> converted;
        if (IsCompressed(surfaceFormat) && !keepCompressed)
        {
            XnbTextureData oneLevel;
            oneLevel.surfaceFormat = surfaceFormat;
            oneLevel.platform = header.platform;
            oneLevel.width = std::max(1u, header.width >> mip);
            oneLevel.height = std::max(1u, header.height >> mip);
            oneLevel.depth = 1;
            oneLevel.faceCount = 1;
            oneLevel.mipCount = 1;
            oneLevel.levels = {sourceBytes};
            converted = ConvertXnbTextureToCnbRgba8(oneLevel, true)
                .representations.front().levels.front();
        }
        const auto& bytes = IsCompressed(surfaceFormat) && !keepCompressed
            ? converted : sourceBytes;
        const int32_t levelWidth = std::max(1, width >> level);
        const int32_t levelHeight = std::max(1, height >> level);

        if (IsCompressed(surfaceFormat) && keepCompressed)
        {
            // WEBGPU-144 Phase 2: upload the validated raw blocks through the compressed
            // SetData path (no CPU decode); the RGBA-pixel validation below does not apply.
            texture.SetData(level, nullptr, bytes.data(), 0, static_cast<int>(bytes.size()));
            return;
        }

        if (IsCompressed(surfaceFormat))
        {
            const int32_t pixelCount = levelWidth * levelHeight;
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
            return;
        }

        texture.SetData(level, nullptr, bytes.data(), 0, static_cast<int>(bytes.size()));
    }

    Texture2D Texture2DReader::Read(ContentReader& input, std::optional<Texture2D> /*existingInstance*/)
    {
        GraphicsDevice* device = nullptr;
        std::optional<Texture2D> constructed;
        // Refresh Texture2DReader.Read IL_0000-002f reads the header before device lookup
        // and construction. The new texture is owned here if any level subsequently fails.
        (void)DecodeTexture2DXnbData(
            input, std::numeric_limits<std::uint32_t>::max(),
            [&](const XnbTextureData& header) {
                device = &CNA::Internal::ContentManagerAccess::GraphicsDeviceFromContentReader(input);
                RequireXnbTextureFormatBeforeConstruction(input, header);
                constructed.emplace(ConstructTexture2DForXnbData(*device, header));
            },
            [&](const XnbTextureData& header, std::uint32_t, std::uint32_t level,
                const std::vector<std::uint8_t>& bytes) {
                UploadTexture2DXnbLevel(*constructed, *device, header, level, bytes);
            });
        return std::move(*constructed);
    }

    Texture2D ConstructTexture2DForXnbData(GraphicsDevice& device, const XnbTextureData& header)
    {
        return Texture2D(device, static_cast<int32_t>(header.width),
                         static_cast<int32_t>(header.height), header.mipCount > 1,
                         PlanTexture2DUpload(device, header).format);
    }

    Texture2D CreateTexture2DFromXnbData(
        ContentReader& /*input*/, GraphicsDevice& deviceReference, const XnbTextureData& decoded,
        std::optional<Texture2D> existingInstance)
    {
        const int32_t width = static_cast<int32_t>(decoded.width);
        const int32_t height = static_cast<int32_t>(decoded.height);
        const int32_t levelCount = static_cast<int32_t>(decoded.mipCount);
        GraphicsDevice* const device = &deviceReference;
        const SurfaceFormat uploadFormat = PlanTexture2DUpload(deviceReference, decoded).format;
        // Reject dimensions/mip counts the active renderer cannot actually
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
            : ConstructTexture2DForXnbData(deviceReference, decoded);
        UploadTexture2DXnbData(texture, deviceReference, decoded);
        return texture;
    }

    void UploadTexture2DXnbData(Texture2D& texture, GraphicsDevice& device,
                                const XnbTextureData& decoded)
    {
        for (std::uint32_t level = 0; level < decoded.mipCount; ++level)
        {
            UploadTexture2DXnbLevel(texture, device, decoded, level, decoded.levels.at(level));
        }
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
