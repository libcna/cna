#pragma once

#include "CNA/Internal/Graphics/ImageData.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include <array>
#include <cstdint>

namespace Microsoft::Xna::Framework::Graphics { class GraphicsDevice; }

namespace CNA::Internal::Backends::Ascii
{
    /// Luminance-ordered glyph-density ramp used by the ASCII backend's BlackWhite/Color
    /// quantization modes (plan_ascii.md Phase G4) -- emptiest/darkest first, densest last.
    inline constexpr const char* kAsciiGlyphRamp = " .:-=+*#%@";
    inline constexpr int kAsciiGlyphRampLength = 10;
    inline constexpr int kAsciiGlyphWidth = 8;
    inline constexpr int kAsciiGlyphHeight = 8;
    /// The atlas texture has one extra slot past kAsciiGlyphRamp's own characters: a fully solid
    /// white 8x8 block, used only by Present()'s internal background-fill draws (Phase G5) --
    /// never exposed as a SpriteFont character, since it isn't one.
    inline constexpr int kAsciiSolidGlyphIndex = kAsciiGlyphRampLength;
    inline constexpr int kAsciiAtlasGlyphCount = kAsciiGlyphRampLength + 1;

    /**
     * @brief Builds the raw RGBA8 pixel data for the hand-authored glyph atlas -- no
     * GraphicsDevice/Texture2D dependency, usable directly via IGraphicsBackend::CreateTexture()
     * from inside a backend itself (Phase G5's Present() needs this; BuildAsciiFontAtlas() below
     * needs a real GraphicsDevice it doesn't have access to there).
     *
     * @return width = kAsciiGlyphWidth * kAsciiAtlasGlyphCount, height = kAsciiGlyphHeight.
     */
    CNA::Internal::Graphics::ImageData BuildAsciiFontAtlasImageData();

    /**
     * @brief Builds a small, hand-authored monospace glyph atlas covering exactly the characters
     * in kAsciiGlyphRamp, wrapped as a real SpriteFont for game/application code that already has
     * a GraphicsDevice (e.g. for diagnostics/demos) -- see BuildAsciiFontAtlasImageData() for the
     * lower-level version Present() itself uses.
     *
     * Design decision 4 (plan_ascii.md): a custom-authored atlas, not a vendored font file --
     * avoids a new asset/license dependency entirely. Glyph pixels are opaque white on a fully
     * transparent background so SpriteBatch's per-draw tint color paints them, exactly like any
     * other CNA texture.
     *
     * @param device The GraphicsDevice to build the atlas texture on.
     * @return A SpriteFont over the generated atlas, one glyph per kAsciiGlyphRamp character.
     */
    Microsoft::Xna::Framework::Graphics::SpriteFont BuildAsciiFontAtlas(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

    /**
     * @brief Returns the raw 8x8 1bpp bitmap for the glyph at a given position in kAsciiGlyphRamp.
     *
     * One byte per row, MSB = leftmost of the 8 columns. Exposed for testing -- pixel-count
     * ordering across the whole ramp is a real correctness property (Phase G4's quantizer assumes
     * a strictly increasing density), not just a cosmetic detail.
     *
     * @param rampIndex Index into kAsciiGlyphRamp, [0, kAsciiGlyphRampLength).
     * @return The glyph's 8 row bytes, top row first.
     */
    std::array<std::uint8_t, kAsciiGlyphHeight> GetAsciiGlyphBitmap(int rampIndex);
}
