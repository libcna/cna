// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/AsciiPostProcessEffect.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Internal/Graphics/Ascii/AsciiFontAtlas.hpp"
#include "CNA/Internal/Graphics/Ascii/AsciiQuantizer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    namespace AsciiInternal = CNA::Internal::Graphics::Ascii;

    AsciiPostProcessEffect::AsciiPostProcessEffect(GraphicsDevice& device)
        : device_(&device)
        , fontAtlasTexture_(device, AsciiInternal::kAsciiGlyphWidth * AsciiInternal::kAsciiAtlasGlyphCount,
                            AsciiInternal::kAsciiGlyphHeight)
        , spriteBatch_(device)
        , cellWidth_(AsciiInternal::kAsciiGlyphWidth)
        , cellHeight_(AsciiInternal::kAsciiGlyphHeight)
        , mode_(AsciiInternal::ParseAsciiModeFromEnvironment())
    {
        const CNA::Internal::Graphics::ImageData image = AsciiInternal::BuildAsciiFontAtlasImageData();
        fontAtlasTexture_.SetDataRGBA(image.pixels.data(), image.width * image.height);
    }

    void AsciiPostProcessEffect::setCellSize(int width, int height)
    {
        if (width <= 0 || height <= 0)
        {
            throw std::invalid_argument("CNA AsciiPostProcessEffect: cell size must be positive");
        }
        cellWidth_ = width;
        cellHeight_ = height;
    }

    void AsciiPostProcessEffect::getCellSize(int& width, int& height) const
    {
        width = cellWidth_;
        height = cellHeight_;
    }

    AsciiQuantizeMode AsciiPostProcessEffect::getQuantizeMode() const { return mode_; }
    void AsciiPostProcessEffect::setQuantizeMode(AsciiQuantizeMode mode) { mode_ = mode; }

    void AsciiPostProcessEffect::Draw(Texture2D& source)
    {
        const auto& viewport = device_->getViewportProperty();
        Draw(source, Rectangle(0, 0, viewport.getWidthProperty(), viewport.getHeightProperty()));
    }

    void AsciiPostProcessEffect::Draw(Texture2D& source, const Rectangle& destinationRectangle)
    {
        const int srcWidth = source.getWidthProperty();
        const int srcHeight = source.getHeightProperty();

        // Color has no default constructor (matches every other CNA pixel-buffer convention in
        // this codebase), so the fill value must be given explicitly -- overwritten in full by
        // GetData() below regardless.
        std::vector<Color> pixels(static_cast<std::size_t>(srcWidth) * static_cast<std::size_t>(srcHeight),
                                  Color(static_cast<std::uint8_t>(0), static_cast<std::uint8_t>(0),
                                       static_cast<std::uint8_t>(0), static_cast<std::uint8_t>(0)));
        source.GetData(pixels.data(), static_cast<int>(pixels.size()));

        // AsciiQuantizer's QuantizeFrameToGrid() takes tightly-packed raw RGBA8 bytes (the shape
        // the former ASCII renderer's own IGraphicsRenderer::ReadBackbuffer(uint8_t*) always
        // produced) -- NOT a reinterpret_cast of the Color array above. Color publicly derives
        // from IPackedVectorT<UInt32>, a polymorphic base (see IPackedVector.hpp's own comment:
        // "Inheriting virtual methods adds a vptr"), so sizeof(Color) is larger than 4 bytes and
        // std::vector<Color> elements are not laid out as tightly-packed RGBA8 -- reinterpreting
        // them as such silently reads the vtable pointer's bytes as pixel data. Pack explicitly.
        std::vector<std::uint8_t> rgba(pixels.size() * 4);
        for (std::size_t i = 0; i < pixels.size(); ++i)
        {
            rgba[i * 4 + 0] = pixels[i].getRProperty();
            rgba[i * 4 + 1] = pixels[i].getGProperty();
            rgba[i * 4 + 2] = pixels[i].getBProperty();
            rgba[i * 4 + 3] = pixels[i].getAProperty();
        }

        const AsciiInternal::AsciiGrid grid = AsciiInternal::QuantizeFrameToGrid(
            rgba.data(), srcWidth, srcHeight, cellWidth_, cellHeight_, mode_);
        lastGridColumns_ = grid.columns;
        lastGridRows_ = grid.rows;

        const Rectangle solidSrc(AsciiInternal::kAsciiSolidGlyphIndex * AsciiInternal::kAsciiGlyphWidth, 0,
                                 AsciiInternal::kAsciiGlyphWidth, AsciiInternal::kAsciiGlyphHeight);

        // Point (nearest) sampling, not SpriteBatch::Begin()'s own LinearClamp default: every quad
        // here maps its source region 1:1 onto its destination in the common case, but adjacent
        // glyphs packed edge-to-edge in one atlas texture make bilinear filtering sample across
        // glyph boundaries at the seam -- the same reason every other pixel-exact CNA test draws
        // with PointClamp (see e.g. colorspace_midtone_contract_test.cpp's FillRect/BlitPalette).
        // BlendState::AlphaBlend is SpriteBatch::Begin()'s own default too; named explicitly here
        // since a non-default sampler forces the longer Begin() overload.
        static Microsoft::Xna::Framework::Graphics::SamplerState pointClamp =
            Microsoft::Xna::Framework::Graphics::SamplerState::PointClamp;
        spriteBatch_.Begin(Microsoft::Xna::Framework::Graphics::SpriteSortMode::Deferred,
                           Microsoft::Xna::Framework::Graphics::BlendState::AlphaBlend,
                           &pointClamp, nullptr, nullptr);
        // Every cell draws its own background quad -- BlackWhite mode's cells (hasBackground ==
        // false) draw solid black instead of skipping the quad, so the area outside each glyph's
        // "on" pixels is always painted deliberately, never left showing whatever was previously
        // drawn there. No separate full-destinationRectangle pre-clear quad: cells tile the
        // destination exactly (see the edge-matching note below), so one full-rect quad plus 64
        // per-cell quads all sampling the SAME solid atlas region would just be redundant overlap.
        const Microsoft::Xna::Framework::Color black(0, 0, 0, 255);
        for (int row = 0; row < grid.rows; ++row)
        {
            // Both edges of each cell are computed directly from row/col (not accumulated by
            // adding a per-cell width/height repeatedly), so adjacent cells' shared edge always
            // matches exactly -- no rounding-induced gaps or overlaps between cells.
            const int cellY0 = destinationRectangle.Y
                + static_cast<int>((static_cast<long long>(row) * destinationRectangle.Height) / grid.rows);
            const int cellY1 = destinationRectangle.Y
                + static_cast<int>((static_cast<long long>(row + 1) * destinationRectangle.Height) / grid.rows);
            for (int col = 0; col < grid.columns; ++col)
            {
                const int cellX0 = destinationRectangle.X
                    + static_cast<int>((static_cast<long long>(col) * destinationRectangle.Width) / grid.columns);
                const int cellX1 = destinationRectangle.X
                    + static_cast<int>((static_cast<long long>(col + 1) * destinationRectangle.Width) / grid.columns);
                const Rectangle dest(cellX0, cellY0, cellX1 - cellX0, cellY1 - cellY0);

                const AsciiInternal::AsciiCell& cell = grid.At(col, row);
                spriteBatch_.Draw(fontAtlasTexture_, dest, solidSrc, cell.hasBackground ? cell.background : black);

                const Rectangle glyphSrc(cell.glyphIndex * AsciiInternal::kAsciiGlyphWidth, 0,
                                         AsciiInternal::kAsciiGlyphWidth, AsciiInternal::kAsciiGlyphHeight);
                spriteBatch_.Draw(fontAtlasTexture_, dest, glyphSrc, cell.foreground);
            }
        }
        spriteBatch_.End();
    }

    void AsciiPostProcessEffect::GetLastGridDimensions(int& columns, int& rows) const
    {
        columns = lastGridColumns_;
        rows = lastGridRows_;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
