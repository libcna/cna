// SPDX-License-Identifier: MS-PL
// Adversarial proof that NANOVG rasterizes SpriteBatch quads the way XNA does -- one hard
// in-or-out decision per pixel centre -- and not the way NanoVG rasterizes a vector path.
//
// NanoVG's context is created with NVG_ANTIALIAS, which makes nvgFill() inset the filled polygon
// by half a pixel and cover the missing half with a separate "fringe" triangle strip whose alpha
// ramps from 0 to 1 across roughly one pixel (nanovg.c's own nvg__expandFill). That is correct
// and desirable for vector shapes and completely wrong for a sprite quad: XNA's SpriteBatch has
// no coverage antialiasing at all unless the backbuffer is multisampled, and this renderer never
// creates a multisample-capable context (GraphicsCapability.MultiSampleAntiAliasing is false).
//
// The decisive check is a whole-frame census rather than a handful of sampled pixels: an opaque
// sprite drawn over a contrasting background under BlendState.Opaque must leave EVERY pixel in
// the frame equal to either the sprite colour or the background colour. A feathered edge writes
// partially-covered values that are neither, so the census counts them directly -- and it stays
// decisive for a rotated quad, where no individual edge pixel has a well-defined expected value.
//
// Deliberately not a 90-degree rotation: the geometry below uses ~23 and ~17 degrees, fractional
// positions, a fractional origin, a non-integer scale, a partial source rectangle, both
// SpriteEffects flips and a SetTransformMatrix, because an axis-aligned integer quad is the one
// case where NanoVG's feathering can accidentally land on whole pixels.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = SKIPPED (no GPU/display).

#include "CNA/Internal/Renderers/NanoVg/NanoVgRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgTextureRenderer.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::NanoVg;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Graphics::ImageData;
using CNA::Examples::SdlTestGlContext;
using CNA::Examples::SdlTestRendererArgs;

namespace
{
    int pass = 0, fail = 0;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++pass; else ++fail;
    }

    constexpr int kWidth = 200, kHeight = 150;
    const Color kSprite(255, 0, 0, 255);
    const Color kBackground(0, 255, 0, 255);

    /// Wide enough to absorb 8-bit round-off in a value that is meant to be exactly one of the two
    /// palette colours, narrow enough that any real coverage feathering (which lands tens to
    /// hundreds of steps away, because a partially covered pixel loses the background's own green
    /// entirely under BlendState.Opaque) is still counted.
    constexpr int kPaletteTolerance = 6;

    bool Is(const Color& c, const Color& reference)
    {
        const auto d = [](int x, int y) { return x > y ? x - y : y - x; };
        return d(c.getRProperty(), reference.getRProperty()) <= kPaletteTolerance &&
               d(c.getGProperty(), reference.getGProperty()) <= kPaletteTolerance &&
               d(c.getBProperty(), reference.getBProperty()) <= kPaletteTolerance;
    }

    ImageData SolidRgba(int w, int h, const Color& c)
    {
        ImageData data;
        data.width = w;
        data.height = h;
        data.pixels.resize(static_cast<std::size_t>(w) * h * 4);
        for (std::size_t i = 0; i < data.pixels.size(); i += 4)
        {
            data.pixels[i + 0] = static_cast<uint8_t>(c.getRProperty());
            data.pixels[i + 1] = static_cast<uint8_t>(c.getGProperty());
            data.pixels[i + 2] = static_cast<uint8_t>(c.getBProperty());
            data.pixels[i + 3] = static_cast<uint8_t>(c.getAProperty());
        }
        return data;
    }

    struct Census
    {
        int sprite = 0;
        int background = 0;
        int neither = 0;
        Color firstNeither{0, 0, 0, 0};
        int firstNeitherX = -1, firstNeitherY = -1;
    };

    Census TakeCensus(NanoVgRenderer& renderer)
    {
        std::vector<uint8_t> frame(static_cast<std::size_t>(kWidth) * kHeight * 4, 0);
        renderer.ReadBackbuffer(0, 0, kWidth, kHeight, frame.data());
        Census census;
        for (int y = 0; y < kHeight; ++y)
        {
            for (int x = 0; x < kWidth; ++x)
            {
                const std::size_t i = (static_cast<std::size_t>(y) * kWidth + x) * 4;
                const Color c(frame[i], frame[i + 1], frame[i + 2], frame[i + 3]);
                if (Is(c, kSprite)) ++census.sprite;
                else if (Is(c, kBackground)) ++census.background;
                else
                {
                    if (census.neither == 0)
                    {
                        census.firstNeither = c;
                        census.firstNeitherX = x;
                        census.firstNeitherY = y;
                    }
                    ++census.neither;
                }
            }
        }
        return census;
    }

    std::string DescribeCensus(const Census& census)
    {
        std::string text = std::to_string(census.neither) + " feathered pixel(s)";
        if (census.neither > 0)
        {
            text += " (first at " + std::to_string(census.firstNeitherX) + "," +
                    std::to_string(census.firstNeitherY) + " = " +
                    std::to_string(static_cast<int>(census.firstNeither.getRProperty())) + "," +
                    std::to_string(static_cast<int>(census.firstNeither.getGProperty())) + "," +
                    std::to_string(static_cast<int>(census.firstNeither.getBProperty())) + ")";
        }
        text += ", " + std::to_string(census.sprite) + " sprite pixel(s)";
        return text;
    }
}

int main()
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        std::printf("[SKIP] no GPU/display available: %s\n", SDL_GetError());
        return 77;
    }

    try
    {
        SDL_Window* window = SDL_CreateWindow("nanovg-sprite-rasterization", kWidth, kHeight,
                                              SDL_WINDOW_OPENGL);
        if (!window) throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        {
            SdlTestGlContext glContext(window);
            NanoVgRenderer renderer(SdlTestRendererArgs(
                window, &glContext, nullptr, 0, 0, CnaPresentationMode::NativeBackBuffer));
            // BlendState.Opaque, so a partially covered pixel cannot hide behind alpha blending:
            // whatever the fragment stage emits is written verbatim.
            renderer.ApplyBlendState(/*colorSrc*/0, /*alphaSrc*/0, /*colorDst*/1, /*alphaDst*/1,
                                     /*colorFunc*/0, /*alphaFunc*/0, BlendWriteState{});

            // A 4x4 atlas of one colour: a partial source rectangle can still be taken from it
            // while every texel remains the same value, so the census palette stays binary.
            auto texture = renderer.CreateTexture(SolidRgba(4, 4, kSprite));
            auto sprites = renderer.CreateSpriteBatch();

            const auto clearBackground = [&renderer]
            {
                renderer.Clear(kBackground.getRProperty() / 255.0f,
                               kBackground.getGProperty() / 255.0f,
                               kBackground.getBProperty() / 255.0f, 1.0f);
            };
            const auto readPixel = [&renderer](int x, int y)
            {
                uint8_t rgba[4] = {0, 0, 0, 0};
                renderer.ReadBackbuffer(x, y, 1, 1, rgba);
                return Color(rgba[0], rgba[1], rgba[2], rgba[3]);
            };

            // ---- 1. Axis-aligned, integer destination: exact edge pixels --------------------
            // XNA covers a pixel when its centre is inside the quad, so Rectangle(20,20,40,30)
            // covers columns 20..59 and rows 20..49 and nothing else.
            clearBackground();
            sprites->SetSamplerFilter(/*Point*/1);
            sprites->Begin();
            sprites->Draw(*texture, Rectangle(20, 20, 40, 30), Rectangle(0, 0, 4, 4), Color::White,
                          0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
            sprites->End();
            Check(Is(readPixel(20, 35), kSprite) && Is(readPixel(19, 35), kBackground),
                  "axis-aligned: the left edge column is fully covered and the one outside it is "
                  "untouched");
            Check(Is(readPixel(59, 35), kSprite) && Is(readPixel(60, 35), kBackground),
                  "axis-aligned: the right edge column is fully covered and the one outside it is "
                  "untouched");
            Check(Is(readPixel(35, 20), kSprite) && Is(readPixel(35, 19), kBackground),
                  "axis-aligned: the top edge row is fully covered and the one above it is "
                  "untouched");
            Check(Is(readPixel(35, 49), kSprite) && Is(readPixel(35, 50), kBackground),
                  "axis-aligned: the bottom edge row is fully covered and the one below it is "
                  "untouched");
            {
                const Census census = TakeCensus(renderer);
                Check(census.neither == 0,
                      "axis-aligned: no pixel is partially covered -- " + DescribeCensus(census));
                Check(census.sprite == 40 * 30,
                      "axis-aligned: exactly 40x30 = 1200 pixels are covered -- got " +
                          std::to_string(census.sprite));
            }

            // ---- 2. ~23 degrees, fractional position/origin/scale, partial source rectangle --
            clearBackground();
            sprites->SetSamplerFilter(/*Point*/1);
            sprites->Begin();
            sprites->Draw(*texture, Rectangle(90, 70, 53, 37), Rectangle(1, 1, 2, 3), Color::White,
                          0.401426f /* ~23 degrees */, Vector2(0.75f, 1.25f), SpriteEffects::None,
                          0.0f);
            sprites->End();
            {
                const Census census = TakeCensus(renderer);
                Check(census.neither == 0,
                      "rotated ~23deg with a fractional origin and a partial source rectangle: no "
                      "pixel is partially covered -- " + DescribeCensus(census));
                // Rotation preserves area, so the covered count must stay near 53*37 = 1961; a
                // renderer that insets the quad by half a pixel all round loses about one pixel
                // per unit of perimeter (~180 here), far outside this bound.
                Check(std::abs(census.sprite - 53 * 37) < 90,
                      "rotated ~23deg: the covered area matches the destination rectangle's own "
                      "area -- got " + std::to_string(census.sprite) + ", expected ~" +
                          std::to_string(53 * 37));
            }

            // ---- 3. ~17 degrees, both flips, and a SetTransformMatrix on top -----------------
            {
                Matrix transform = Matrix::getIdentityProperty();
                transform.M11 = 1.25f;
                transform.M22 = 1.25f;
                transform.M41 = 7.5f;
                transform.M42 = -3.25f;

                clearBackground();
                sprites->SetSamplerFilter(/*Point*/1);
                sprites->SetTransformMatrix(transform);
                sprites->Begin();
                sprites->Draw(*texture, Rectangle(30, 25, 41, 29), Rectangle(0, 0, 4, 4),
                              Color::White, 0.296706f /* ~17 degrees */, Vector2(1.5f, 2.5f),
                              SpriteEffects::FlipHorizontally, 0.0f);
                sprites->Draw(*texture, Rectangle(120, 60, 33, 45), Rectangle(0, 1, 4, 3),
                              Color::White, -0.296706f, Vector2(2.5f, 0.5f),
                              SpriteEffects::FlipVertically, 0.0f);
                sprites->End();
                const Census census = TakeCensus(renderer);
                Check(census.neither == 0,
                      "rotated +-17deg with both SpriteEffects flips under a SetTransformMatrix: "
                      "no pixel is partially covered -- " + DescribeCensus(census));
                Check(census.sprite > 3000,
                      "rotated +-17deg: both sprites actually rendered -- got " +
                          std::to_string(census.sprite) + " covered pixel(s)");
            }

            // ---- 4. Two overlapping translucent sprites: no doubled edge seam ----------------
            // With NanoVG's fringe, an edge pixel is drawn twice (once by the inset fill, once by
            // the ramped fringe strip), which double-composites a translucent sprite along its own
            // border. Drawing a half-transparent quad over a known background and reading the
            // pixel column at its left edge catches that: every column inside the quad must
            // composite to the SAME value.
            clearBackground();
            renderer.ApplyBlendState(/*colorSrc*/4, /*alphaSrc*/4, /*colorDst*/5, /*alphaDst*/5,
                                     /*colorFunc*/0, /*alphaFunc*/0, BlendWriteState{});
            {
                auto translucent = renderer.CreateTexture(SolidRgba(4, 4, Color(255, 0, 0, 128)));
                sprites->SetSamplerFilter(/*Point*/1);
                sprites->SetTransformMatrix(Matrix::getIdentityProperty());
                sprites->Begin();
                sprites->Draw(*translucent, Rectangle(60, 40, 40, 30), Rectangle(0, 0, 4, 4),
                              Color::White, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
                sprites->End();
                const Color edge = readPixel(60, 55);
                const Color interior = readPixel(80, 55);
                const auto d = [](int x, int y) { return x > y ? x - y : y - x; };
                Check(d(edge.getRProperty(), interior.getRProperty()) <= 2 &&
                          d(edge.getGProperty(), interior.getGProperty()) <= 2 &&
                          d(edge.getBProperty(), interior.getBProperty()) <= 2,
                      "a translucent sprite's edge column composites exactly once, like its "
                      "interior -- edge (" +
                          std::to_string(static_cast<int>(edge.getRProperty())) + "," +
                          std::to_string(static_cast<int>(edge.getGProperty())) + "," +
                          std::to_string(static_cast<int>(edge.getBProperty())) + ") vs interior (" +
                          std::to_string(static_cast<int>(interior.getRProperty())) + "," +
                          std::to_string(static_cast<int>(interior.getGProperty())) + "," +
                          std::to_string(static_cast<int>(interior.getBProperty())) + ")");
            }
        }
        SDL_DestroyWindow(window);
    }
    catch (const std::exception& ex)
    {
        std::printf("[FAIL] uncaught exception: %s\n", ex.what());
        ++fail;
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    std::printf("=== %d/%d PASS ===\n", pass, pass + fail);
    return fail == 0 ? 0 : 1;
}
