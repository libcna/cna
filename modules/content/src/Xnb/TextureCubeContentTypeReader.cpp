// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/TextureCubeContentTypeReader.hpp"

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
            return input.getContentManagerProperty()->getGraphicsDeviceInternal();
        }
    }

    TextureCube TextureCubeReader::Read(ContentReader& input, std::optional<TextureCube> existingInstance)
    {
        const XnbTextureData decoded = DecodeTextureCubeXnbData(input);
        const int32_t size = static_cast<int32_t>(decoded.width);
        const int32_t levels = static_cast<int32_t>(decoded.mipCount);
        GraphicsDevice& device = RequireGraphicsDevice(input);
        const bool keepCompressed = IsCompressed(decoded.surfaceFormat)
            && device.GetRenderer().LoadsCompressedContentNativelyEXT()
            && device.GetRenderer().IsCompressedCubeTransferFormatEXT(
                static_cast<int>(decoded.surfaceFormat));
        const SurfaceFormat uploadFormat = keepCompressed
            ? decoded.surfaceFormat
            : (IsCompressed(decoded.surfaceFormat) ? SurfaceFormat::Color : decoded.surfaceFormat);
        if (existingInstance.has_value()
            && (existingInstance->getSizeProperty() != size
                || existingInstance->getFormatProperty() != uploadFormat
                || existingInstance->getLevelCountProperty() != levels))
        {
            throw ContentLoadException(
                "TextureCubeReader: existing texture size, format, or mip count does not match "
                "the serialized asset.");
        }

        std::vector<std::vector<uint8_t>> uploadLevels = keepCompressed
            ? decoded.levels
            : ConvertXnbTextureToCnbRgba8(decoded, true).representations.front().levels;

        TextureCube textureCube = existingInstance.has_value()
            ? std::move(*existingInstance)
            : TextureCube(device, size, levels > 1, uploadFormat);

        for (int32_t face = 0; face < 6; ++face)
        {
            int32_t faceSize = size;
            for (int32_t level = 0; level < levels; ++level)
            {
                const std::vector<uint8_t>& bytes = uploadLevels[
                    static_cast<std::size_t>(face) * decoded.mipCount +
                    static_cast<std::size_t>(level)];
                if (keepCompressed)
                {
                    textureCube.SetData(static_cast<CubeMapFace>(face), level, nullptr,
                                        bytes.data(), 0, static_cast<int>(bytes.size()));
                    faceSize = std::max(faceSize >> 1, 1);
                    continue;
                }
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
