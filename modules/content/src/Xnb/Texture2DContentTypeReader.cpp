// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"

#include <algorithm>
#include <cstdint>

#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "CNA/Internal/Xnb/XnbArithmetic.hpp"
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
        SurfaceFormat ReadSurfaceFormat(ContentReader& input)
        {
            if (input.getVersionProperty() < 5)
            {
                // FNA's own legacy mapping: these int values are XNA's pre-4.0 SurfaceFormat
                // enum ordinals, unrelated to the current enum's own numeric values.
                const int32_t legacyFormat = input.ReadInt32();
                switch (legacyFormat)
                {
                    case 1:  return SurfaceFormat::ColorBgraEXT;
                    case 28: return SurfaceFormat::Dxt1;
                    case 30: return SurfaceFormat::Dxt3;
                    case 32: return SurfaceFormat::Dxt5;
                    default:
                        throw ContentLoadException(
                            "Texture2DReader: unsupported legacy surface format (" +
                            std::to_string(legacyFormat) + ").");
                }
            }
            return static_cast<SurfaceFormat>(input.ReadInt32());
        }

        bool IsCompressed(SurfaceFormat format)
        {
            return format == SurfaceFormat::Dxt1 || format == SurfaceFormat::Dxt3 ||
                   format == SurfaceFormat::Dxt5;
        }

        // REMED-CONTENT-001: the maximum mip level count a real width x height texture can ever
        // have -- mirrors Texture2D.cpp's own private CalculateMipLevels() (duplicated rather than
        // exposed, since it's a pure math helper with no dependency on Texture2D's internal state).
        // A file-declared levelCount above this ceiling would make the read loop below call
        // SetData() for a mip level the actual constructed texture (whose own real level count is
        // computed the same way) does not have -- confirmed root cause of the Vulkan/WebGPU crashes
        // this task closes.
        int32_t CalculateMaxMipLevels(int32_t w, int32_t h)
        {
            int32_t levels = 1;
            while (w > 1 || h > 1)
            {
                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
                ++levels;
            }
            return levels;
        }

        std::size_t CompressedLevelByteCount(SurfaceFormat format, int32_t width, int32_t height)
        {
            const std::size_t blockWidth =
                (static_cast<std::size_t>(width) + 3u) / 4u;
            const std::size_t blockHeight =
                (static_cast<std::size_t>(height) + 3u) / 4u;
            const std::size_t bytesPerBlock = format == SurfaceFormat::Dxt1 ? 8u : 16u;
            return blockWidth * blockHeight * bytesPerBlock;
        }
    }

    Texture* TextureReader::Read(ContentReader& /*input*/, std::optional<Texture*> existingInstance)
    {
        return existingInstance.value_or(nullptr);
    }

    Texture2D Texture2DReader::Read(ContentReader& input, std::optional<Texture2D> existingInstance)
    {
        const SurfaceFormat surfaceFormat = ReadSurfaceFormat(input);
        const int32_t width = input.ReadInt32();
        const int32_t height = input.ReadInt32();
        const int32_t levelCount = input.ReadInt32();

        // Reject an adversarial/corrupt width/height before any allocation is attempted -- both
        // must be positive individually (two negatives would otherwise multiply to a
        // small-looking positive product and slip past the byte-size check below). The
        // decoded-byte-size product is computed via CheckedMultiplyOrThrow(), not raw int64_t
        // multiplication (plans/plan_xnb.md XNB-43) -- widening to int64_t alone is not sufficient: two
        // dimensions near INT32_MAX multiply to a value still representable in int64_t, but the
        // subsequent "* 4" can itself overflow int64_t, which is undefined behavior (confirmed by
        // UBSan, REMED-CONTENT-009). CheckedMultiplyOrThrow() rejects that case as a clean
        // exception before the overflow occurs.
        if (width <= 0 || height <= 0)
        {
            throw ContentLoadException("Texture2DReader: invalid width/height.");
        }
        // Always decompress DXT to Color -- see this reader's class docs for why (XNB-24's
        // fuller per-renderer capability query is deferred, not required for correctness).
        const SurfaceFormat uploadFormat = IsCompressed(surfaceFormat) ? SurfaceFormat::Color : surfaceFormat;
        if (uploadFormat != SurfaceFormat::Color &&
            uploadFormat != SurfaceFormat::NormalizedByte4 &&
            uploadFormat != SurfaceFormat::NormalizedByte2)
        {
            throw ContentLoadException(
                "Texture2DReader: SurfaceFormat is not yet supported by CNA's .xnb reader "
                "(only Color/NormalizedByte2/NormalizedByte4/Dxt1/Dxt3/Dxt5 are implemented "
                "so far).");
        }

        // Not every supported format is four bytes per pixel: NormalizedByte2 carries only the
        // X and Y channels, which is exactly why a content pipeline picks it for a 2D
        // displacement map. The decoded-byte bound has to follow the format rather than assume.
        const int32_t bytesPerPixel =
            uploadFormat == SurfaceFormat::NormalizedByte2 ? 2 : 4;
        input.CheckDecodedByteSize(
            CheckedMultiplyOrThrow({width, height, bytesPerPixel}, "Texture2DReader"),
            "Texture2DReader");

        GraphicsDevice* device = input.getContentManagerProperty()
            ? &input.getContentManagerProperty()->getGraphicsDeviceInternal()
            : nullptr;
        if (!device)
        {
            throw ContentLoadException(
                "Texture2DReader: no GraphicsDevice available (ContentManager was not set on "
                "this ContentReader).");
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
        const int32_t maxLevels = CalculateMaxMipLevels(width, height);
        if (levelCount <= 0 || levelCount > maxLevels)
        {
            throw ContentLoadException(
                "Texture2DReader: declared mip level count (" + std::to_string(levelCount) +
                ") is outside 1.." + std::to_string(maxLevels) + " for a " +
                std::to_string(width) + "x" + std::to_string(height) + " texture.");
        }
        // Texture2D's public XNA allocation is level-zero-only or the complete floor-halved
        // chain. A partial prefix would allocate additional levels that were never present in
        // the asset; Skia would then deterministically generate them. Reject that ambiguity so
        // content loading never fabricates absent mip data.
        if (levelCount != 1 && levelCount != maxLevels)
        {
            throw ContentLoadException(
                "Texture2DReader: incomplete mip chain; expected level zero only or all " +
                std::to_string(maxLevels) + " levels for a " + std::to_string(width) + "x" +
                std::to_string(height) + " texture.");
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

        for (int32_t level = 0; level < levelCount; ++level)
        {
            const int32_t byteCount = input.ReadInt32();
            std::vector<uint8_t> bytes = input.ReadBytesExactOrThrow(byteCount, "Texture2DReader");

            const int32_t levelWidth = std::max(1, width >> level);
            const int32_t levelHeight = std::max(1, height >> level);

            if (IsCompressed(surfaceFormat))
            {
                const std::size_t expectedCompressedBytes =
                    CompressedLevelByteCount(surfaceFormat, levelWidth, levelHeight);
                if (bytes.size() != expectedCompressedBytes)
                {
                    throw ContentLoadException(
                        "Texture2DReader: compressed level " + std::to_string(level) +
                        " byte count (" + std::to_string(bytes.size()) + ") does not match " +
                        std::to_string(levelWidth) + "x" + std::to_string(levelHeight) +
                        "'s required " + std::to_string(expectedCompressedBytes) + " bytes.");
                }
                switch (surfaceFormat)
                {
                    case SurfaceFormat::Dxt1:
                        bytes = CNA::Internal::Graphics::DxtUtil::DecompressDxt1(
                            bytes.data(), bytes.size(), levelWidth, levelHeight);
                        break;
                    case SurfaceFormat::Dxt3:
                        bytes = CNA::Internal::Graphics::DxtUtil::DecompressDxt3(
                            bytes.data(), bytes.size(), levelWidth, levelHeight);
                        break;
                    case SurfaceFormat::Dxt5:
                        bytes = CNA::Internal::Graphics::DxtUtil::DecompressDxt5(
                            bytes.data(), bytes.size(), levelWidth, levelHeight);
                        break;
                    default:
                        break; // unreachable: IsCompressed() only true for the three cases above
                }
            }

            const int32_t pixelCount = levelWidth * levelHeight;
            // The compressed branch above always produces exactly pixelCount*4 bytes by
            // construction; an uncompressed format can still disagree here, if the file's own
            // declared byteCount does not match levelWidth/levelHeight (a truncated/adversarial
            // file) -- catch that before indexing into bytes below. The expected size follows
            // the format's own bytes per pixel, which is 2 for NormalizedByte2.
            const std::size_t expectedLevelBytes =
                static_cast<std::size_t>(pixelCount) * static_cast<std::size_t>(bytesPerPixel);
            if (bytes.size() != expectedLevelBytes)
            {
                throw ContentLoadException(
                    "Texture2DReader: level " + std::to_string(level) + " byte count (" +
                    std::to_string(bytes.size()) + ") does not match " + std::to_string(levelWidth) +
                    "x" + std::to_string(levelHeight) + "'s required " +
                    std::to_string(expectedLevelBytes) + " bytes.");
            }
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
