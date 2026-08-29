// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/TextureCubeContentTypeReader.hpp"

#include <algorithm>
#include <cstdint>

#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
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
        const CNA::Content::Cnb::CnbTextureData rgba =
            ConvertXnbTextureToCnbRgba8(decoded, true);
        const int32_t size = static_cast<int32_t>(decoded.width);
        const int32_t levels = static_cast<int32_t>(decoded.mipCount);

        TextureCube textureCube = existingInstance.has_value()
            ? std::move(*existingInstance)
            : TextureCube(RequireGraphicsDevice(input), size, levels > 1, SurfaceFormat::Color);

        for (int32_t face = 0; face < 6; ++face)
        {
            int32_t faceSize = size;
            for (int32_t level = 0; level < levels; ++level)
            {
                const std::vector<uint8_t>& bytes = rgba.representations.front().levels[
                    static_cast<std::size_t>(face) * decoded.mipCount +
                    static_cast<std::size_t>(level)];
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
