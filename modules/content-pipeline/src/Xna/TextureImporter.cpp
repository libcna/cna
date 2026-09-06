// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/TextureImporter.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Internal/Graphics/DdsSurfaceReader.hpp"
#include "CNA/Internal/Graphics/PfmDecoder.hpp"
#include "CNA/Internal/Graphics/RadianceHdrDecoder.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentIdentity.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/DxtBitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/PixelBitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "System/IO/FileNotFoundException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    namespace
    {
        /** @brief The whole file, as bytes. */
        [[nodiscard]] std::vector<std::uint8_t> ReadAll(const std::string& filename)
        {
            std::ifstream file(filename, std::ios::binary);
            const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
            return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
        }

        /**
         * @brief XNA's own refusal for a source its texture reader cannot read.
         *
         * The genuine importer appends the D3DX code its reader answered: a file with no bytes at
         * all is `D3DERR_INVALIDCALL`, and one whose bytes are not a picture it knows is
         * `D3DXERR_INVALIDDATA` (measured, tests/reference/xna40/graphics cases
         * textureext/empty.png, textureext/truncated.png, textureext/garbage.tga,
         * textureext/truncated.dds and textureimporter/refusals).
         *
         * @param empty Whether the source held no bytes.
         * @return The complete sentence, error code included.
         */
        [[nodiscard]] std::string UnreadableTextureMessage(bool empty)
        {
            return std::string("Can not read the texture file. The file is corrupted or invalid. "
                               "Error code: ") + (empty ? "D3DERR_INVALIDCALL" : "D3DXERR_INVALIDDATA") + ".";
        }

    }

    std::shared_ptr<Graphics::TextureContent> TextureImporter::Import(const std::string& filename,
                                                                      ContentImporterContext& context)
    {
        (void)context;
        const std::string tool(XnaTypeName);
        std::error_code error;
        if (!std::filesystem::exists(filename, error) || error)
        {
            throw System::IO::FileNotFoundException("Can not read the texture \"" + filename +
                                                    "\". The file could not be found.");
        }
        const std::vector<std::uint8_t> bytes = ReadAll(filename);
        auto texture = std::make_shared<Graphics::Texture2DContent>();
        texture->setIdentityProperty(ContentIdentity(filename, tool.substr(tool.rfind('.') + 1)));
        if (CNA::Internal::Graphics::IsRadianceHdr(bytes))
        {
            // A Radiance picture answers floats, as a portable float map does (measured,
            // textureext/probe.hdr: its bitmap is Vector4, and probe.hdr/floats gives the values).
            CNA::Internal::Graphics::DecodedRadianceHdr decoded;
            try
            {
                decoded = CNA::Internal::Graphics::DecodeRadianceHdr(bytes, filename);
            }
            catch (const std::exception&)
            {
                throw InvalidContentException(UnreadableTextureMessage(bytes.empty()));
            }
            auto bitmap = std::make_shared<Graphics::PixelBitmapContent<Vector4>>(
                static_cast<SharpRuntime::intcs>(decoded.width),
                static_cast<SharpRuntime::intcs>(decoded.height));
            for (std::uint32_t y = 0u; y < decoded.height; ++y)
            {
                for (std::uint32_t x = 0u; x < decoded.width; ++x)
                {
                    const std::size_t at = (static_cast<std::size_t>(y) * decoded.width + x) * 4u;
                    bitmap->SetPixel(static_cast<SharpRuntime::intcs>(x), static_cast<SharpRuntime::intcs>(y),
                                     Vector4(decoded.pixels[at], decoded.pixels[at + 1u],
                                             decoded.pixels[at + 2u], decoded.pixels[at + 3u]));
                }
            }
            texture->getMipmapsProperty().Add(std::move(bitmap));
            return texture;
        }
        if (CNA::Internal::Graphics::IsPfm(bytes))
        {
            // A portable float map is the one source that answers floats rather than bytes
            // (measured, textureimporter/formats: its bitmap is Vector4).
            CNA::Internal::Graphics::DecodedPfm decoded;
            try
            {
                decoded = CNA::Internal::Graphics::DecodePfm(bytes, filename);
            }
            catch (const std::exception&)
            {
                throw InvalidContentException(UnreadableTextureMessage(bytes.empty()));
            }
            auto bitmap = std::make_shared<Graphics::PixelBitmapContent<Vector4>>(
                static_cast<SharpRuntime::intcs>(decoded.width),
                static_cast<SharpRuntime::intcs>(decoded.height));
            for (std::uint32_t y = 0u; y < decoded.height; ++y)
            {
                for (std::uint32_t x = 0u; x < decoded.width; ++x)
                {
                    const std::size_t at = (static_cast<std::size_t>(y) * decoded.width + x) * 4u;
                    bitmap->SetPixel(static_cast<SharpRuntime::intcs>(x), static_cast<SharpRuntime::intcs>(y),
                                     Vector4(decoded.pixels[at], decoded.pixels[at + 1u],
                                             decoded.pixels[at + 2u], decoded.pixels[at + 3u]));
                }
            }
            texture->getMipmapsProperty().Add(std::move(bitmap));
            return texture;
        }
        if (CNA::Internal::Graphics::IsDds(bytes))
        {
            // A DDS keeps whatever it stores: its compressed blocks reach an `.xnb` compressed,
            // its cube becomes a TextureCubeContent and its volume a Texture3DContent (measured,
            // textureimporter/dds_variants).
            CNA::Internal::Graphics::DdsSurfaces surfaces;
            try
            {
                surfaces = CNA::Internal::Graphics::ReadDdsSurfaces(bytes, filename);
            }
            catch (const std::exception&)
            {
                throw InvalidContentException(UnreadableTextureMessage(bytes.empty()));
            }
            std::shared_ptr<Graphics::TextureContent> content;
            if (surfaces.isCube)
            {
                content = std::make_shared<Graphics::TextureCubeContent>();
            }
            else if (surfaces.isVolume)
            {
                auto volume = std::make_shared<Graphics::Texture3DContent>();
                for (std::size_t slice = 0; slice < surfaces.surfaces.size(); ++slice)
                {
                    volume->getFacesProperty().Add(std::make_shared<Graphics::MipmapChain>());
                }
                content = volume;
            }
            else
            {
                content = std::make_shared<Graphics::Texture2DContent>();
            }
            content->setIdentityProperty(ContentIdentity(filename, tool.substr(tool.rfind('.') + 1)));
            for (std::size_t face = 0; face < surfaces.surfaces.size(); ++face)
            {
                for (std::size_t level = 0; level < surfaces.surfaces[face].size(); ++level)
                {
                    const auto width = static_cast<SharpRuntime::intcs>(
                        std::max<std::uint32_t>(1u, surfaces.width >> level));
                    const auto height = static_cast<SharpRuntime::intcs>(
                        std::max<std::uint32_t>(1u, surfaces.height >> level));
                    std::shared_ptr<Graphics::BitmapContent> bitmap;
                    if (surfaces.format == CNA::Internal::Graphics::DdsSurfaceFormat::Color)
                    {
                        bitmap = std::make_shared<Graphics::PixelBitmapContent<Color>>(width, height);
                    }
                    else if (surfaces.format == CNA::Internal::Graphics::DdsSurfaceFormat::Dxt1)
                    {
                        bitmap = std::make_shared<Graphics::Dxt1BitmapContent>(width, height);
                    }
                    else if (surfaces.format == CNA::Internal::Graphics::DdsSurfaceFormat::Dxt3)
                    {
                        bitmap = std::make_shared<Graphics::Dxt3BitmapContent>(width, height);
                    }
                    else
                    {
                        bitmap = std::make_shared<Graphics::Dxt5BitmapContent>(width, height);
                    }
                    const std::vector<std::uint8_t>& payload = surfaces.surfaces[face][level];
                    bitmap->SetPixelData(std::vector<SharpRuntime::bytecs>(payload.begin(), payload.end()));
                    const std::shared_ptr<Graphics::MipmapChain>& chain =
                        content->getFacesProperty()[static_cast<SharpRuntime::intcs>(face)];
                    chain->Add(std::move(bitmap));
                }
            }
            return content;
        }
        // Everything else goes through the one image decoder this repository has, so a texture
        // built here holds the pixels the runtime would have loaded from the same file.
        CNA::Content::Cnb::CnbTextureData decoded;
        try
        {
            // A `.dib` reaches this the same way a `.bmp` does: the shared decoder puts the
            // missing file header back itself, so there is no second decoder and no temporary
            // file here (measured, textureimporter/formats accepts one).
            decoded = CNA::Content::Cnb::ImportImageAsCnbTexture2D(filename);
        }
        catch (const std::exception&)
        {
            throw InvalidContentException(UnreadableTextureMessage(bytes.empty()));
        }
        if (decoded.representations.empty() || decoded.representations[0].levels.empty())
        {
            throw InvalidContentException(UnreadableTextureMessage(bytes.empty()));
        }
        auto bitmap = std::make_shared<Graphics::PixelBitmapContent<Color>>(
            static_cast<SharpRuntime::intcs>(decoded.width), static_cast<SharpRuntime::intcs>(decoded.height));
        bitmap->SetPixelData(std::vector<SharpRuntime::bytecs>(decoded.representations[0].levels[0].begin(),
                                                               decoded.representations[0].levels[0].end()));
        texture->getMipmapsProperty().Add(std::move(bitmap));
        return texture;
    }

    ContentImporterAttribute TextureImporter::Attribute()
    {
        ContentImporterAttribute attribute(
            {".bmp", ".dds", ".dib", ".hdr", ".jpg", ".pfm", ".png", ".ppm", ".tga"});
        attribute.setDefaultProcessorProperty("SpriteTextureProcessor");
        attribute.setDisplayNameProperty("Texture - XNA Framework");
        return attribute;
    }

    const std::string& TextureImporter::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
