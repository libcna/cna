// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/TextureCubeContentTypeReader.hpp"

#include <algorithm>
#include <cstdint>

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Graphics::CubeMapFace;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::TextureCube;

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
                    "TextureCubeReader: no GraphicsDevice available (ContentManager was not set "
                    "on this ContentReader).");
            }
            return input.getContentManagerProperty()->getGraphicsDeviceInternal();
        }
    }

    TextureCube TextureCubeReader::Read(ContentReader& input, std::optional<TextureCube> existingInstance)
    {
        const auto surfaceFormat = static_cast<SurfaceFormat>(input.ReadInt32());
        const int32_t size = input.ReadInt32();
        const int32_t levels = input.ReadInt32();

        // Reject an adversarial/corrupt size before any allocation is attempted -- see
        // Texture2DReader's own note for why both the positivity and the int64_t-widened-product
        // checks are needed (plan_xnb.md XNB-43).
        if (size <= 0)
        {
            throw ContentLoadException("TextureCubeReader: invalid size.");
        }
        input.CheckDecodedByteSize(
            static_cast<int64_t>(size) * static_cast<int64_t>(size) * 4, "TextureCubeReader");

        // Always decompress DXT to Color -- see Texture2DReader's own class docs for why.
        const SurfaceFormat uploadFormat = IsCompressed(surfaceFormat) ? SurfaceFormat::Color : surfaceFormat;
        if (uploadFormat != SurfaceFormat::Color)
        {
            throw ContentLoadException(
                "TextureCubeReader: SurfaceFormat is not yet supported by CNA's .xnb reader "
                "(only Color/Dxt1/Dxt3/Dxt5 are implemented so far).");
        }

        TextureCube textureCube = existingInstance.has_value()
            ? std::move(*existingInstance)
            : TextureCube(RequireGraphicsDevice(input), size, levels > 1, uploadFormat);

        for (int32_t face = 0; face < 6; ++face)
        {
            int32_t faceSize = size;
            for (int32_t level = 0; level < levels; ++level)
            {
                const int32_t byteCount = input.ReadInt32();
                std::vector<uint8_t> bytes = input.ReadBytesExactOrThrow(byteCount, "TextureCubeReader");

                if (IsCompressed(surfaceFormat))
                {
                    switch (surfaceFormat)
                    {
                        case SurfaceFormat::Dxt1:
                            bytes = CNA::Internal::Graphics::DxtUtil::DecompressDxt1(bytes.data(), bytes.size(), faceSize, faceSize);
                            break;
                        case SurfaceFormat::Dxt3:
                            bytes = CNA::Internal::Graphics::DxtUtil::DecompressDxt3(bytes.data(), bytes.size(), faceSize, faceSize);
                            break;
                        case SurfaceFormat::Dxt5:
                            bytes = CNA::Internal::Graphics::DxtUtil::DecompressDxt5(bytes.data(), bytes.size(), faceSize, faceSize);
                            break;
                        default:
                            break; // unreachable: IsCompressed() only true for the three cases above
                    }
                }

                // Color is not a raw 4-byte POD (it has a vtable) -- see Texture2DReader's own
                // class docs for why raw RGBA bytes must be unpacked into real Color values.
                const int32_t pixelCount = faceSize * faceSize;
                std::vector<Color> colors;
                colors.reserve(static_cast<std::size_t>(pixelCount));
                for (int32_t i = 0; i < pixelCount; ++i)
                {
                    const std::size_t o = static_cast<std::size_t>(i) * 4;
                    colors.emplace_back(bytes[o], bytes[o + 1], bytes[o + 2], bytes[o + 3]);
                }
                textureCube.SetData(static_cast<CubeMapFace>(face), level, nullptr, colors.data(), 0, pixelCount);

                faceSize = std::max(faceSize >> 1, 1);
            }
        }

        return textureCube;
    }

    void RegisterTextureCubeXnbReader()
    {
        Microsoft::Xna::Framework::Content::ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.TextureCubeReader",
            [] { return std::make_unique<TextureCubeReader>(); });
    }
}
