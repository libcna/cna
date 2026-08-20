// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/Texture3DContentTypeReader.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "CNA/Internal/Xnb/XnbArithmetic.hpp"
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

        // Reject an adversarial/corrupt width/height/depth before any allocation is attempted --
        // see Texture2DReader's own note for why both the individual-positivity check and
        // CheckedMultiplyOrThrow() (rather than raw int64_t multiplication, which can itself
        // overflow -- REMED-CONTENT-009) are both needed (plans/plan_xnb.md XNB-43). This product has
        // one more factor than Texture2DReader's own (width*height*depth*4 vs width*height*4), so
        // it is if anything more overflow-prone, not less.
        if (width <= 0 || height <= 0 || depth <= 0)
        {
            throw ContentLoadException("Texture3DReader: invalid width/height/depth.");
        }
        input.CheckDecodedByteSize(
            CheckedMultiplyOrThrow({width, height, depth, 4}, "Texture3DReader"),
            "Texture3DReader");

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
            std::vector<uint8_t> bytes = input.ReadBytesExactOrThrow(byteCount, "Texture3DReader");

            if (IsCompressed(surfaceFormat))
            {
                // A compressed volume texture is `depth` consecutive, independently-compressed
                // 2D slices (DXT/BC is fundamentally a 2D block format) -- decompress each slice
                // separately and concatenate, rather than treating the whole level as one 2D
                // image (which would silently drop every slice past the first).
                const std::size_t sliceCompressedSize =
                    bytes.size() / std::max<std::size_t>(1, static_cast<std::size_t>(depth));
                std::vector<uint8_t> decompressed;
                decompressed.reserve(static_cast<std::size_t>(width) * height * depth * 4);
                for (int32_t slice = 0; slice < depth; ++slice)
                {
                    const uint8_t* sliceData = bytes.data() + static_cast<std::size_t>(slice) * sliceCompressedSize;
                    std::vector<uint8_t> sliceOut;
                    switch (surfaceFormat)
                    {
                        case SurfaceFormat::Dxt1:
                            sliceOut = CNA::Internal::Graphics::DxtUtil::DecompressDxt1(sliceData, sliceCompressedSize, width, height);
                            break;
                        case SurfaceFormat::Dxt3:
                            sliceOut = CNA::Internal::Graphics::DxtUtil::DecompressDxt3(sliceData, sliceCompressedSize, width, height);
                            break;
                        case SurfaceFormat::Dxt5:
                            sliceOut = CNA::Internal::Graphics::DxtUtil::DecompressDxt5(sliceData, sliceCompressedSize, width, height);
                            break;
                        default:
                            break; // unreachable: IsCompressed() only true for the three cases above
                    }
                    decompressed.insert(decompressed.end(), sliceOut.begin(), sliceOut.end());
                }
                bytes = std::move(decompressed);
            }

            // Color is not a raw 4-byte POD (it has a vtable) -- see Texture2DReader's own class
            // docs for why raw RGBA bytes must be unpacked into real Color values one at a time.
            // Found during REMED-CONTENT-009's root-cause sweep: a raw `width * height * depth`
            // int32_t product here is its own overflow risk, independent of the CheckDecodedByteSize
            // call above (that call's own input is now overflow-safe, but this is a *separate*
            // multiplication of the same three factors). Using CheckedMultiplyOrThrow() and then
            // verifying the result still fits in int32_t makes this line safe on its own terms,
            // rather than relying on a reader tracing back to prove the earlier check subsumes it.
            const int64_t voxelCount64 = CheckedMultiplyOrThrow({width, height, depth}, "Texture3DReader");
            if (voxelCount64 > std::numeric_limits<int32_t>::max())
            {
                throw ContentLoadException(
                    "Texture3DReader: voxel count (" + std::to_string(voxelCount64) +
                    ") exceeds the maximum representable value.");
            }
            const auto voxelCount = static_cast<int32_t>(voxelCount64);
            // The compressed branch above always produces exactly voxelCount*4 bytes by
            // construction; only the uncompressed Color branch can still disagree here, if the
            // file's own declared byteCount doesn't actually match width/height/depth (a
            // truncated/adversarial file) -- catch that before indexing into bytes below.
            if (bytes.size() != static_cast<std::size_t>(voxelCount) * 4)
            {
                throw ContentLoadException(
                    "Texture3DReader: level " + std::to_string(level) + " byte count (" +
                    std::to_string(bytes.size()) + ") does not match " + std::to_string(width) + "x" +
                    std::to_string(height) + "x" + std::to_string(depth) + "'s required " +
                    std::to_string(static_cast<std::size_t>(voxelCount) * 4) + " bytes.");
            }
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
