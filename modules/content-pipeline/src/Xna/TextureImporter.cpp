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

        /** @brief Tells whether the bytes are a device-independent bitmap without its file header. */
        [[nodiscard]] bool IsDeviceIndependentBitmap(const std::vector<std::uint8_t>& bytes)
        {
            if (bytes.size() < 40u || (bytes[0] == 'B' && bytes[1] == 'M'))
            {
                return false;
            }
            // A DIB begins with its own header size: 40 for BITMAPINFOHEADER, 108 or 124 for the
            // versions that follow it.
            const std::uint32_t headerSize = static_cast<std::uint32_t>(bytes[0]) |
                                             (static_cast<std::uint32_t>(bytes[1]) << 8) |
                                             (static_cast<std::uint32_t>(bytes[2]) << 16) |
                                             (static_cast<std::uint32_t>(bytes[3]) << 24);
            return headerSize == 40u || headerSize == 108u || headerSize == 124u;
        }

        /** @brief The same bitmap with the fourteen-byte file header a `.bmp` carries. */
        [[nodiscard]] std::vector<std::uint8_t> WithBitmapFileHeader(const std::vector<std::uint8_t>& body)
        {
            std::vector<std::uint8_t> bytes;
            bytes.reserve(body.size() + 14u);
            const auto word32 = [&bytes](std::uint32_t value)
            {
                for (int shift = 0; shift < 32; shift += 8)
                {
                    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
                }
            };
            bytes.push_back('B');
            bytes.push_back('M');
            word32(static_cast<std::uint32_t>(body.size() + 14u));
            word32(0u);
            const std::uint32_t headerSize = static_cast<std::uint32_t>(body[0]) |
                                             (static_cast<std::uint32_t>(body[1]) << 8) |
                                             (static_cast<std::uint32_t>(body[2]) << 16) |
                                             (static_cast<std::uint32_t>(body[3]) << 24);
            word32(14u + headerSize);
            bytes.insert(bytes.end(), body.begin(), body.end());
            return bytes;
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
                throw InvalidContentException(
                    "Can not read the texture file. The file is corrupted or invalid.");
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
                throw InvalidContentException(
                    "Can not read the texture file. The file is corrupted or invalid.");
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
            if (IsDeviceIndependentBitmap(bytes))
            {
                // A `.dib` is a bitmap without its file header; putting one back is what lets the
                // shared decoder read it (measured, textureimporter/formats accepts one).
                const std::vector<std::uint8_t> whole = WithBitmapFileHeader(bytes);
                const std::filesystem::path temporary =
                    std::filesystem::temp_directory_path() /
                    ("cna_dib_" + std::to_string(reinterpret_cast<std::uintptr_t>(bytes.data())) + ".bmp");
                {
                    std::ofstream out(temporary, std::ios::binary);
                    out.write(reinterpret_cast<const char*>(whole.data()),
                              static_cast<std::streamsize>(whole.size()));
                }
                decoded = CNA::Content::Cnb::ImportImageAsCnbTexture2D(temporary);
                std::error_code removal;
                std::filesystem::remove(temporary, removal);
            }
            else
            {
                decoded = CNA::Content::Cnb::ImportImageAsCnbTexture2D(filename);
            }
        }
        catch (const std::exception&)
        {
            throw InvalidContentException("Can not read the texture file. The file is corrupted or invalid.");
        }
        if (decoded.representations.empty() || decoded.representations[0].levels.empty())
        {
            throw InvalidContentException("Can not read the texture file. The file is corrupted or invalid.");
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
        attribute.setDisplayNameProperty("Texture Importer");
        return attribute;
    }

    const std::string& TextureImporter::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
