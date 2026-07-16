// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"

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
    }

    Texture2D Texture2DReader::Read(ContentReader& input, std::optional<Texture2D> existingInstance)
    {
        const SurfaceFormat surfaceFormat = ReadSurfaceFormat(input);
        const int32_t width = input.ReadInt32();
        const int32_t height = input.ReadInt32();
        const int32_t levelCount = input.ReadInt32();

        // Reject an adversarial/corrupt width/height before any allocation is attempted -- both
        // must be positive individually (two negatives would otherwise multiply to a
        // small-looking positive product and slip past the byte-size check below), and the
        // decoded-byte-size product is computed in int64_t so it can't itself silently wrap back
        // into range (plan_xnb.md XNB-43).
        if (width <= 0 || height <= 0)
        {
            throw ContentLoadException("Texture2DReader: invalid width/height.");
        }
        input.CheckDecodedByteSize(
            static_cast<int64_t>(width) * static_cast<int64_t>(height) * 4, "Texture2DReader");

        // Always decompress DXT to Color -- see this reader's class docs for why (XNB-24's
        // fuller per-backend capability query is deferred, not required for correctness).
        const SurfaceFormat uploadFormat = IsCompressed(surfaceFormat) ? SurfaceFormat::Color : surfaceFormat;
        if (uploadFormat != SurfaceFormat::Color)
        {
            throw ContentLoadException(
                "Texture2DReader: SurfaceFormat is not yet supported by CNA's .xnb reader "
                "(only Color/Dxt1/Dxt3/Dxt5 are implemented so far).");
        }

        GraphicsDevice* device = input.getContentManagerProperty()
            ? &input.getContentManagerProperty()->getGraphicsDeviceInternal()
            : nullptr;
        if (!device)
        {
            throw ContentLoadException(
                "Texture2DReader: no GraphicsDevice available (ContentManager was not set on "
                "this ContentReader).");
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

            // Color is not a raw 4-byte POD -- it derives from IPackedVectorT, which has virtual
            // methods, so it carries a vtable pointer. Raw RGBA bytes must be used to construct
            // real Color values one at a time, never reinterpret_cast wholesale.
            const int32_t pixelCount = levelWidth * levelHeight;
            // The compressed branch above always produces exactly pixelCount*4 bytes by
            // construction; only the uncompressed Color branch can still disagree here, if the
            // file's own declared byteCount doesn't actually match levelWidth/levelHeight (a
            // truncated/adversarial file) -- catch that before indexing into bytes below.
            if (bytes.size() != static_cast<std::size_t>(pixelCount) * 4)
            {
                throw ContentLoadException(
                    "Texture2DReader: level " + std::to_string(level) + " byte count (" +
                    std::to_string(bytes.size()) + ") does not match " + std::to_string(levelWidth) +
                    "x" + std::to_string(levelHeight) + "'s required " +
                    std::to_string(static_cast<std::size_t>(pixelCount) * 4) + " bytes.");
            }
            std::vector<Color> colors;
            colors.reserve(static_cast<std::size_t>(pixelCount));
            for (int32_t i = 0; i < pixelCount; ++i)
            {
                const std::size_t o = static_cast<std::size_t>(i) * 4;
                colors.emplace_back(bytes[o], bytes[o + 1], bytes[o + 2], bytes[o + 3]);
            }
            texture.SetData(level, nullptr, colors.data(), 0, pixelCount);
        }

        return texture;
    }

    void RegisterTexture2DXnbReader()
    {
        Microsoft::Xna::Framework::Content::ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.Texture2DReader",
            [] { return std::make_unique<Texture2DReader>(); });
    }
}
