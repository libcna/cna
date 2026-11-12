// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/TextureCubeContentTypeReader.hpp"
#include "CNA/Internal/ContentManagerAccess.hpp"

#include <algorithm>
#include <cstdint>

#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

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
            return CNA::Internal::ContentManagerAccess::GraphicsDeviceFromContentReader(input);
        }
    }

    TextureCube TextureCubeReader::Read(ContentReader& input, std::optional<TextureCube> /*existingInstance*/)
    {
        GraphicsDevice* device = nullptr;
        bool keepCompressed = false;
        std::optional<TextureCube> constructed;
        // Refresh TextureCubeReader.Read IL_0018-0027: device lookup and constructor
        // refusals precede every level. RAII releases this new cube if decoding fails.
        (void)DecodeTextureCubeXnbData(
            input, [&](const XnbTextureData& header) {
                device = &RequireGraphicsDevice(input);
                RequireXnbTextureFormatBeforeConstruction(input, header);
                keepCompressed = IsCompressed(header.surfaceFormat)
                    && device->GetRenderer().LoadsCompressedContentNativelyEXT()
                    && device->GetRenderer().IsCompressedCubeTransferFormatEXT(
                        static_cast<int>(header.surfaceFormat));
                const SurfaceFormat uploadFormat = IsCompressed(header.surfaceFormat) && !keepCompressed
                    ? SurfaceFormat::Color : header.surfaceFormat;
                constructed.emplace(*device, static_cast<int32_t>(header.width),
                                    header.mipCount > 1, uploadFormat);
            },
            [&](const XnbTextureData& header, std::uint32_t face, std::uint32_t level,
                const std::vector<std::uint8_t>& bytes) {
                if (!IsCompressed(header.surfaceFormat) || keepCompressed)
                {
                    constructed->SetData(static_cast<CubeMapFace>(face), static_cast<int>(level),
                                         nullptr, bytes.data(), 0, static_cast<int>(bytes.size()));
                    return;
                }
                XnbTextureData oneLevel;
                oneLevel.surfaceFormat = header.surfaceFormat;
                oneLevel.platform = header.platform;
                oneLevel.width = std::max(1u, header.width >> level);
                oneLevel.height = oneLevel.width;
                oneLevel.depth = 1;
                oneLevel.faceCount = 1;
                oneLevel.mipCount = 1;
                oneLevel.levels = {bytes};
                const auto rgba = ConvertXnbTextureToCnbRgba8(oneLevel, true)
                    .representations.front().levels.front();
                const auto pixelCount = static_cast<int32_t>(oneLevel.width * oneLevel.height);
                std::vector<Color> colors;
                colors.reserve(static_cast<std::size_t>(pixelCount));
                for (int32_t i = 0; i < pixelCount; ++i)
                {
                    const std::size_t o = static_cast<std::size_t>(i) * 4;
                    colors.emplace_back(rgba[o], rgba[o + 1], rgba[o + 2], rgba[o + 3]);
                }
                constructed->SetData(static_cast<CubeMapFace>(face), static_cast<int>(level),
                                     nullptr, colors.data(), 0, pixelCount);
            });
        return std::move(*constructed);
    }

    void RegisterTextureCubeXnbReader()
    {
        Microsoft::Xna::Framework::Content::ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.TextureCubeReader",
            [] { return std::make_unique<TextureCubeReader>(); });
    }
}
