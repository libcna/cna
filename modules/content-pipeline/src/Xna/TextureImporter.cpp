// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/TextureImporter.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
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
            // A block-compressed surface whose dimensions are not a whole number of blocks is
            // answered at the size its blocks cover, and only the *declared* image is inside it:
            // a 5x5 DXT1 is a 2x2 block grid, and the genuine importer answers an 8x8 bitmap
            // whose first five rows and columns are the file's image and whose remaining pixels
            // are black. A mip chain's lower levels are that rounded level 0 halved rather than
            // each level rounded on its own -- a 6x10 chain answers 8x12, 4x6, 2x3, 1x1, where
            // rounding each level would give 4x8 at level 1. Measured over eleven committed
            // fixtures, `tests/reference/xna40/texture/dds-block-oracle.json`
            // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-224`).
            const bool compressed =
                surfaces.format != CNA::Internal::Graphics::DdsSurfaceFormat::Color;
            const auto blockRound = [](const std::uint32_t pixels)
            { return ((pixels + 3u) / 4u) * 4u; };
            const std::uint32_t baseWidth = compressed ? blockRound(surfaces.width) : surfaces.width;
            const std::uint32_t baseHeight =
                compressed ? blockRound(surfaces.height) : surfaces.height;
            const auto makeBitmap = [&surfaces](const SharpRuntime::intcs width,
                                                const SharpRuntime::intcs height)
                -> std::shared_ptr<Graphics::BitmapContent>
            {
                switch (surfaces.format)
                {
                    case CNA::Internal::Graphics::DdsSurfaceFormat::Color:
                        return std::make_shared<Graphics::PixelBitmapContent<Color>>(width, height);
                    case CNA::Internal::Graphics::DdsSurfaceFormat::Dxt1:
                        return std::make_shared<Graphics::Dxt1BitmapContent>(width, height);
                    case CNA::Internal::Graphics::DdsSurfaceFormat::Dxt3:
                        return std::make_shared<Graphics::Dxt3BitmapContent>(width, height);
                    default:
                        return std::make_shared<Graphics::Dxt5BitmapContent>(width, height);
                }
            };
            for (std::size_t face = 0; face < surfaces.surfaces.size(); ++face)
            {
                for (std::size_t level = 0; level < surfaces.surfaces[face].size(); ++level)
                {
                    const auto width =
                        static_cast<SharpRuntime::intcs>(std::max<std::uint32_t>(1u, baseWidth >> level));
                    const auto height =
                        static_cast<SharpRuntime::intcs>(std::max<std::uint32_t>(1u, baseHeight >> level));
                    // What the file itself declares at this level, which is what the blocks hold.
                    const auto declaredWidth = static_cast<SharpRuntime::intcs>(
                        std::max<std::uint32_t>(1u, surfaces.width >> level));
                    const auto declaredHeight = static_cast<SharpRuntime::intcs>(
                        std::max<std::uint32_t>(1u, surfaces.height >> level));
                    const auto storedWidth = static_cast<SharpRuntime::intcs>(
                        compressed ? blockRound(static_cast<std::uint32_t>(declaredWidth))
                                   : static_cast<std::uint32_t>(declaredWidth));
                    const auto storedHeight = static_cast<SharpRuntime::intcs>(
                        compressed ? blockRound(static_cast<std::uint32_t>(declaredHeight))
                                   : static_cast<std::uint32_t>(declaredHeight));
                    std::shared_ptr<Graphics::BitmapContent> stored =
                        makeBitmap(storedWidth, storedHeight);
                    const std::vector<std::uint8_t>& payload = surfaces.surfaces[face][level];
                    stored->SetPixelData(std::vector<SharpRuntime::bytecs>(payload.begin(), payload.end()));
                    std::shared_ptr<Graphics::BitmapContent> bitmap = stored;
                    if (compressed &&
                        (storedWidth != width || storedHeight != height ||
                         declaredWidth != storedWidth || declaredHeight != storedHeight))
                    {
                        // The declared image in the corner of a black bitmap of the answered size,
                        // re-encoded. The blocks this writes are CNA's compressor's rather than
                        // XNA's, which is the difference 263 of the corpus's references already
                        // carry.
                        auto surface = std::make_shared<Graphics::PixelBitmapContent<Color>>(width, height);
                        const SharpRuntime::intcs keepWidth = std::min(declaredWidth, width);
                        const SharpRuntime::intcs keepHeight = std::min(declaredHeight, height);
                        if (keepWidth > 0 && keepHeight > 0)
                        {
                            Graphics::BitmapContent::Copy(
                                stored, Rectangle(0, 0, keepWidth, keepHeight), surface,
                                Rectangle(0, 0, keepWidth, keepHeight));
                        }
                        bitmap = makeBitmap(width, height);
                        Graphics::BitmapContent::Copy(surface, bitmap);
                    }
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
        // The same rule the canonical route applies: GDI+ honours a PNG's own `gAMA`, and the
        // genuine importer is GDI+ (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-109`).
        CNA::Content::Pipeline::ApplyPngFileGammaEXT(bytes, decoded.representations[0].levels[0]);
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
