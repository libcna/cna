#include "CNA/Internal/Backends/Ascii/AsciiFontAtlas.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

#include <stdexcept>
#include <vector>

namespace CNA::Internal::Backends::Ascii
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::SpriteFont;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using SharpRuntime::charcs;

    namespace
    {
        // One row per byte, MSB = leftmost of the 8 columns. Hand-authored (design decision 4 --
        // not a vendored font asset) with pixel-count coverage verified strictly increasing along
        // kAsciiGlyphRamp: 0, 2, 4, 6, 12, 20, 24, 40, 48, 60 out of 64 (AsciiFontAtlasTest checks
        // this directly, popcount by popcount).
        constexpr std::uint8_t kGlyphBitmaps[kAsciiGlyphRampLength][kAsciiGlyphHeight] = {
            /* ' ' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
            /* '.' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18},
            /* ':' */ {0x00, 0x00, 0x18, 0x00, 0x00, 0x18, 0x00, 0x00},
            /* '-' */ {0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00},
            /* '=' */ {0x00, 0x00, 0x7E, 0x00, 0x00, 0x7E, 0x00, 0x00},
            /* '+' */ {0x00, 0x18, 0x18, 0x7E, 0x7E, 0x18, 0x18, 0x00},
            /* '*' */ {0x42, 0x24, 0x18, 0x7E, 0x7E, 0x18, 0x24, 0x42},
            /* '#' */ {0x66, 0x66, 0xFF, 0x66, 0x66, 0xFF, 0x66, 0x66},
            /* '%' */ {0xFF, 0x66, 0xFF, 0x66, 0xFF, 0x66, 0xFF, 0x66},
            /* '@' */ {0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E},
        };
    }

    std::array<std::uint8_t, kAsciiGlyphHeight> GetAsciiGlyphBitmap(int rampIndex)
    {
        if (rampIndex < 0 || rampIndex >= kAsciiGlyphRampLength)
        {
            throw std::out_of_range("GetAsciiGlyphBitmap: rampIndex out of range");
        }
        std::array<std::uint8_t, kAsciiGlyphHeight> rows{};
        for (int i = 0; i < kAsciiGlyphHeight; ++i)
        {
            rows[i] = kGlyphBitmaps[rampIndex][i];
        }
        return rows;
    }

    SpriteFont BuildAsciiFontAtlas(GraphicsDevice& device)
    {
        const int atlasWidth = kAsciiGlyphWidth * kAsciiGlyphRampLength;
        const int atlasHeight = kAsciiGlyphHeight;

        std::vector<Color> pixels(static_cast<std::size_t>(atlasWidth) * atlasHeight, Color(0, 0, 0, 0));
        for (int g = 0; g < kAsciiGlyphRampLength; ++g)
        {
            for (int y = 0; y < kAsciiGlyphHeight; ++y)
            {
                const std::uint8_t rowBits = kGlyphBitmaps[g][y];
                for (int x = 0; x < kAsciiGlyphWidth; ++x)
                {
                    if ((rowBits & (0x80 >> x)) != 0)
                    {
                        const int px = g * kAsciiGlyphWidth + x;
                        pixels[static_cast<std::size_t>(y) * atlasWidth + px] = Color(255, 255, 255, 255);
                    }
                }
            }
        }

        Texture2D texture(device, atlasWidth, atlasHeight);
        texture.SetData(pixels.data(), static_cast<int>(pixels.size()));

        std::vector<Rectangle> glyphBounds;
        std::vector<Rectangle> cropping;
        std::vector<charcs> characters;
        std::vector<Vector3> kerning;
        glyphBounds.reserve(kAsciiGlyphRampLength);
        cropping.reserve(kAsciiGlyphRampLength);
        characters.reserve(kAsciiGlyphRampLength);
        kerning.reserve(kAsciiGlyphRampLength);
        for (int g = 0; g < kAsciiGlyphRampLength; ++g)
        {
            glyphBounds.emplace_back(g * kAsciiGlyphWidth, 0, kAsciiGlyphWidth, kAsciiGlyphHeight);
            cropping.emplace_back(0, 0, kAsciiGlyphWidth, kAsciiGlyphHeight);
            characters.push_back(static_cast<charcs>(kAsciiGlyphRamp[g]));
            kerning.emplace_back(0.0f, static_cast<float>(kAsciiGlyphWidth), 0.0f);
        }

        return SpriteFont(texture, glyphBounds, cropping, characters,
                          /*lineSpacing=*/kAsciiGlyphHeight, /*spacing=*/0.0f,
                          kerning, /*defaultCharacter=*/static_cast<charcs>(' '));
    }
}
