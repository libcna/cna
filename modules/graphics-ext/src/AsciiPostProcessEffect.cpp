// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/AsciiPostProcessEffect.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Internal/Graphics/Ascii/AsciiFontAtlas.hpp"
#include "CNA/Internal/Graphics/Ascii/AsciiQuantizer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

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

        const AsciiInternal::AsciiGrid grid = AsciiInternal::QuantizeFrameToGrid(
            reinterpret_cast<const std::uint8_t*>(pixels.data()), srcWidth, srcHeight,
            cellWidth_, cellHeight_, mode_);
        lastGridColumns_ = grid.columns;
        lastGridRows_ = grid.rows;

        const Rectangle solidSrc(AsciiInternal::kAsciiSolidGlyphIndex * AsciiInternal::kAsciiGlyphWidth, 0,
                                 AsciiInternal::kAsciiGlyphWidth, AsciiInternal::kAsciiGlyphHeight);

        // SpriteBatch::Begin() with no arguments already defaults to real premultiplied
        // BlendState::AlphaBlend (SpriteSortMode::Deferred) -- exactly the blend factors needed so
        // a glyph's transparent "off" pixels never overwrite a cell's background fill, with no
        // renderer-specific blend-state poking required.
        spriteBatch_.Begin();
        // Fills the destination rectangle with black first, matching the former ASCII renderer's
        // own Present() (which cleared its whole target the same way): BlackWhite mode paints no
        // per-cell background at all, so without this the area outside each glyph's "on" pixels
        // would show whatever was previously drawn there instead of a clean black field.
        spriteBatch_.Draw(fontAtlasTexture_, destinationRectangle, solidSrc,
                          Microsoft::Xna::Framework::Color(0, 0, 0, 255));
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
                if (cell.hasBackground)
                {
                    spriteBatch_.Draw(fontAtlasTexture_, dest, solidSrc, cell.background);
                }

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
