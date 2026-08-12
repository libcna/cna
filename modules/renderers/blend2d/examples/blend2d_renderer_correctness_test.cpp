// SPDX-License-Identifier: MS-PL
// plan_blend2d.md: deep correctness/safety regression suite for the Blend2D renderer, exercising
// Blend2DRenderer/Blend2DSpriteBatchRenderer/Blend2DTextureRenderer/Blend2DRenderTargetRenderer
// DIRECTLY (no Game/GraphicsDeviceManager/SpriteBatch front-end) so every internal EXT hook
// (ApplyBlendState, SetScissorRect/ApplyRasterizerState, SetSamplerFilter/SetSamplerAddressMode,
// SetTransformMatrix, DebugSetPresentationOutputSizeEXT) can be driven precisely and
// deterministically. Needs a real SDL_Window (Blend2DRenderer's constructor creates an
// SDL_Renderer against it) but not a real display -- SDL_VIDEODRIVER=dummy provides both without
// any X11/Wayland dependency (see modules/renderers/freedirect/examples/CMakeLists.txt for the
// established precedent of this exact pattern).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "CNA/Internal/Renderers/Blend2D/Blend2DRenderer.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Blend2D;

namespace
{
    int g_pass = 0;
    int g_total = 0;

    void Check(bool ok, const std::string& label)
    {
        ++g_total;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++g_pass;
    }

    void CheckThrows(const std::string& label, const std::function<void()>& fn)
    {
        bool threw = false;
        try { fn(); }
        catch (...) { threw = true; }
        Check(threw, label + " throws");
    }

    void CheckNoThrow(const std::string& label, const std::function<void()>& fn)
    {
        bool threw = false;
        std::string what;
        try { fn(); }
        catch (const std::exception& e) { threw = true; what = e.what(); }
        catch (...) { threw = true; what = "non-std exception"; }
        Check(!threw, label + (threw ? (" (threw: " + what + ")") : ""));
    }

    SDL_Window* MakeWindow(int w, int h)
    {
        SDL_Window* window = SDL_CreateWindow("blend2d-correctness", w, h, 0);
        if (!window)
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        return window;
    }

    std::unique_ptr<Blend2DRenderer> MakeRenderer(SDL_Window* window, int w, int h,
                                                  CnaPresentationMode mode = CnaPresentationMode::Letterbox)
    {
        return std::make_unique<Blend2DRenderer>(window, w, h, mode, 0);
    }

    ImageData SolidImage(int w, int h, Color c)
    {
        ImageData data;
        data.width = w;
        data.height = h;
        data.pixels.resize(static_cast<std::size_t>(w) * h * 4);
        for (int i = 0; i < w * h; ++i)
        {
            data.pixels[static_cast<std::size_t>(i) * 4 + 0] = c.getRProperty();
            data.pixels[static_cast<std::size_t>(i) * 4 + 1] = c.getGProperty();
            data.pixels[static_cast<std::size_t>(i) * 4 + 2] = c.getBProperty();
            data.pixels[static_cast<std::size_t>(i) * 4 + 3] = c.getAProperty();
        }
        return data;
    }

    /// 2x2 texture with four distinct opaque corner colours: TL=red, TR=green, BL=blue,
    /// BR=yellow -- used to make flip/origin/rotation orientation bugs visible as a colour
    /// mismatch rather than needing sub-pixel measurement.
    ImageData QuadrantImage()
    {
        ImageData data;
        data.width = 2;
        data.height = 2;
        data.pixels = {
            255, 0, 0, 255,    0, 255, 0, 255,   // TL=red, TR=green
            0, 0, 255, 255,    255, 255, 0, 255, // BL=blue, BR=yellow
        };
        return data;
    }

    Color ReadPixel(Blend2DRenderer& renderer, int x, int y)
    {
        std::uint8_t px[4] = {0, 0, 0, 0};
        renderer.ReadBackbuffer(x, y, 1, 1, px);
        return Color(px[0], px[1], px[2], px[3]);
    }

    bool ColorEquals(const Color& a, const Color& b, int tolerance = 0)
    {
        return std::abs(static_cast<int>(a.getRProperty()) - static_cast<int>(b.getRProperty())) <= tolerance &&
               std::abs(static_cast<int>(a.getGProperty()) - static_cast<int>(b.getGProperty())) <= tolerance &&
               std::abs(static_cast<int>(a.getBProperty()) - static_cast<int>(b.getBProperty())) <= tolerance &&
               std::abs(static_cast<int>(a.getAProperty()) - static_cast<int>(b.getAProperty())) <= tolerance;
    }

    std::string ColorStr(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + "," + std::to_string(c.getAProperty()) + ")";
    }

    void CheckPixel(Blend2DRenderer& renderer, int x, int y, Color expected, const std::string& label,
                    int tolerance = 0)
    {
        const Color actual = ReadPixel(renderer, x, y);
        Check(ColorEquals(actual, expected, tolerance),
             label + " at (" + std::to_string(x) + "," + std::to_string(y) + "): expected " +
                 ColorStr(expected) + " got " + ColorStr(actual));
    }

    /// Checks only the alpha channel of a pixel, independently of its RGB channels. Used to
    /// isolate an alpha-equation regression (e.g. a wrong independent-alpha blend factor) from an
    /// RGB regression when both are being verified from separately-derived expected values.
    void CheckAlpha(Blend2DRenderer& renderer, int x, int y, int expectedAlpha, const std::string& label,
                    int tolerance = 0)
    {
        const Color actual = ReadPixel(renderer, x, y);
        const int actualAlpha = static_cast<int>(actual.getAProperty());
        Check(std::abs(actualAlpha - expectedAlpha) <= tolerance,
             label + " alpha at (" + std::to_string(x) + "," + std::to_string(y) + "): expected alpha " +
                 std::to_string(expectedAlpha) + " got " + std::to_string(actualAlpha));
    }

    // ------------------------------------------------------------------
    // Texture stride
    // ------------------------------------------------------------------
    void TestTextureStride()
    {
        SDL_Window* window = MakeWindow(16, 16);
        auto renderer = MakeRenderer(window, 16, 16);

        // 2x3 texture, three rows of distinct colours, uploaded via UpdatePixels with a stride
        // LARGER than width*4 (8 bytes of padding between rows) -- the exact REMED-GFX-blend2d
        // defect: the previous implementation always advanced by width_*4 regardless of the
        // caller's stride, silently reading the wrong bytes for row 1/2.
        auto tex = renderer->CreateTexture(SolidImage(2, 3, Color(0, 0, 0, 255)));
        auto* blend2dTex = dynamic_cast<Blend2DTextureRenderer*>(tex.get());
        Check(blend2dTex != nullptr, "CreateTexture returns a Blend2DTextureRenderer");

        constexpr int width = 2, height = 3, tightStride = width * 4, padding = 8;
        const int paddedStride = tightStride + padding;
        std::vector<std::uint8_t> padded(static_cast<std::size_t>(paddedStride) * height, 0xEE);
        const Color rowColors[3] = {Color(255, 0, 0, 255), Color(0, 255, 0, 255), Color(0, 0, 255, 255)};
        for (int row = 0; row < height; ++row)
        {
            std::uint8_t* rowBase = padded.data() + static_cast<std::size_t>(row) * paddedStride;
            for (int col = 0; col < width; ++col)
            {
                rowBase[col * 4 + 0] = rowColors[row].getRProperty();
                rowBase[col * 4 + 1] = rowColors[row].getGProperty();
                rowBase[col * 4 + 2] = rowColors[row].getBProperty();
                rowBase[col * 4 + 3] = 255;
            }
        }
        blend2dTex->UpdatePixels(padded.data(), paddedStride);

        std::vector<std::uint8_t> readback(static_cast<std::size_t>(width) * height * 4);
        Check(blend2dTex->GetData(0, 0, 0, width, height, readback.data(),
                                 static_cast<int>(readback.size())),
             "GetData succeeds after a padded-stride UpdatePixels");
        for (int row = 0; row < height; ++row)
        {
            const std::uint8_t* px = readback.data() + static_cast<std::size_t>(row) * width * 4;
            Check(px[0] == rowColors[row].getRProperty() && px[1] == rowColors[row].getGProperty() &&
                     px[2] == rowColors[row].getBProperty(),
                 "Padded-stride UpdatePixels row " + std::to_string(row) + " reads back its own colour, "
                 "not a neighbouring row's (stride respected)");
        }

        // Tightly packed (stride == width*4) must still work exactly as before.
        std::vector<std::uint8_t> tight(static_cast<std::size_t>(tightStride) * height);
        for (int row = 0; row < height; ++row)
            for (int col = 0; col < width; ++col)
            {
                std::uint8_t* p = &tight[static_cast<std::size_t>(row) * tightStride + col * 4];
                p[0] = 10 + row; p[1] = 20 + row; p[2] = 30 + row; p[3] = 255;
            }
        blend2dTex->UpdatePixels(tight.data(), tightStride);
        Check(blend2dTex->GetData(0, 0, 0, width, height, readback.data(), static_cast<int>(readback.size())),
             "GetData succeeds after a tightly-packed UpdatePixels");
        bool tightOk = true;
        for (int row = 0; row < height; ++row)
        {
            const std::uint8_t* px = readback.data() + static_cast<std::size_t>(row) * width * 4;
            if (px[0] != 10 + row || px[1] != 20 + row || px[2] != 30 + row) tightOk = false;
        }
        Check(tightOk, "Tightly-packed UpdatePixels (stride == width*4) still round-trips exactly");

        SDL_DestroyWindow(window);
    }

    // ------------------------------------------------------------------
    // Flip / origin / rotation geometry
    // ------------------------------------------------------------------
    void TestFlipOriginRotation()
    {
        SDL_Window* window = MakeWindow(32, 32);
        auto renderer = MakeRenderer(window, 32, 32);
        auto tex = renderer->CreateTexture(QuadrantImage());
        auto sb = renderer->CreateSpriteBatch();

        const Rectangle full(0, 0, 2, 2);
        const Color red(255, 0, 0, 255), green(0, 255, 0, 255), blue(0, 0, 255, 255), yellow(255, 255, 0, 255);

        // No flip, origin (0,0): destination [0,0]-[2,2). TL=red, TR=green, BL=blue, BR=yellow.
        renderer->Clear(0, 0, 0, 1);
        sb->Begin();
        sb->Draw(*tex, Rectangle(0, 0, 2, 2), full, Color::White, 0.0f, Vector2::Zero,
                SpriteEffects::None, 0.0f);
        sb->End();
        CheckPixel(*renderer, 0, 0, red, "No-flip TL");
        CheckPixel(*renderer, 1, 0, green, "No-flip TR");
        CheckPixel(*renderer, 0, 1, blue, "No-flip BL");
        CheckPixel(*renderer, 1, 1, yellow, "No-flip BR");

        // FlipHorizontally, origin (0,0): the KNOWN DEFECT case -- must stay within [0,2), not
        // move to [-2,0). After horizontal mirroring: TL=green, TR=red, BL=yellow, BR=blue.
        renderer->Clear(0, 0, 0, 1);
        sb->Begin();
        sb->Draw(*tex, Rectangle(0, 0, 2, 2), full, Color::White, 0.0f, Vector2::Zero,
                SpriteEffects::FlipHorizontally, 0.0f);
        sb->End();
        CheckPixel(*renderer, 0, 0, green, "FlipH origin=(0,0) TL stays in dest rect (REMED defect case)");
        CheckPixel(*renderer, 1, 0, red, "FlipH origin=(0,0) TR");
        CheckPixel(*renderer, 0, 1, yellow, "FlipH origin=(0,0) BL");
        CheckPixel(*renderer, 1, 1, blue, "FlipH origin=(0,0) BR");
        // Nothing drawn to the left of the destination rect.
        CheckPixel(*renderer, 31, 0, Color(0, 0, 0, 255), "FlipH origin=(0,0) draws nothing at negative-local X");

        // FlipVertically, origin (0,0): TL=blue, TR=yellow, BL=red, BR=green.
        renderer->Clear(0, 0, 0, 1);
        sb->Begin();
        sb->Draw(*tex, Rectangle(0, 0, 2, 2), full, Color::White, 0.0f, Vector2::Zero,
                SpriteEffects::FlipVertically, 0.0f);
        sb->End();
        CheckPixel(*renderer, 0, 0, blue, "FlipV origin=(0,0) TL");
        CheckPixel(*renderer, 1, 0, yellow, "FlipV origin=(0,0) TR");
        CheckPixel(*renderer, 0, 1, red, "FlipV origin=(0,0) BL");
        CheckPixel(*renderer, 1, 1, green, "FlipV origin=(0,0) BR");

        // Both flips == 180 degree rotation of content: TL=yellow, TR=blue, BL=green, BR=red.
        renderer->Clear(0, 0, 0, 1);
        sb->Begin();
        sb->Draw(*tex, Rectangle(0, 0, 2, 2), full, Color::White, 0.0f, Vector2::Zero,
                static_cast<SpriteEffects>(static_cast<int>(SpriteEffects::FlipHorizontally) |
                                           static_cast<int>(SpriteEffects::FlipVertically)),
                0.0f);
        sb->End();
        CheckPixel(*renderer, 0, 0, yellow, "Both flips TL");
        CheckPixel(*renderer, 1, 1, red, "Both flips BR");

        // FlipHorizontally with a NON-ZERO origin and a destination rect NOT anchored at the
        // window origin -- verifies the mirror-about-center fix generalizes past origin=(0,0).
        // Uses Point sampling: this is a magnified (2x) draw, and BILINEAR sampling near a
        // quadrant boundary legitimately blends adjacent texels (that is what TestSamplerFilter
        // covers) -- Point isolates pure GEOMETRY correctness from filtering.
        sb->SetSamplerFilter(1);
        renderer->Clear(0, 0, 0, 1);
        sb->Begin();
        sb->Draw(*tex, Rectangle(10, 10, 4, 4), full, Color::White, 0.0f, Vector2(1.0f, 1.0f),
                SpriteEffects::FlipHorizontally, 0.0f);
        sb->End();
        // destW=4,sw=2 -> scaleX=2; origin (1,1) -> originDest=(2,2); rect local x in [-2,2).
        // Unflipped TL(red) would be at local (-2,-2)+scale -> world (8,8)-(10,10). Flipped about
        // centerX=0 mirrors x: TL(red) ends at local (0,-2) -> world (10,8)-(12,10) i.e. the TR
        // 2x2 quadrant of the 4x4 destination becomes red instead of TL.
        CheckPixel(*renderer, 11, 9, red, "FlipH non-zero origin: red quadrant lands top-RIGHT (mirrored)");
        CheckPixel(*renderer, 9, 9, green, "FlipH non-zero origin: green quadrant lands top-LEFT (mirrored)");

        // Rotation by 90 degrees (pi/2): TL(red) rotates to TR position. Point sampling again
        // isolates geometry from any rotated-edge interpolation.
        renderer->Clear(0, 0, 0, 1);
        sb->Begin();
        sb->Draw(*tex, Rectangle(10, 10, 2, 2), full, Color::White, static_cast<float>(M_PI / 2.0),
                Vector2::Zero, SpriteEffects::None, 0.0f);
        sb->End();
        // A local point (x,y) rotates to (-y,x) at 90 degrees; TL source pixel occupies local
        // [0,1)x[0,1), which rotates to local [-1,0)x[0,1) -> world [9,10)x[10,11).
        CheckPixel(*renderer, 9, 10, red, "90-degree rotation moves TL red quadrant to expected cell");
        sb->SetSamplerFilter(0);

        // Negative destination Width == an independent mirror source (Vector2 scale overload),
        // composing (XOR) with SpriteEffects: FlipHorizontally + negative destW must cancel out
        // and render normally.
        renderer->Clear(0, 0, 0, 1);
        sb->Begin();
        sb->Draw(*tex, Rectangle(20, 0, -2, 2), full, Color::White, 0.0f, Vector2::Zero,
                SpriteEffects::FlipHorizontally, 0.0f);
        sb->End();
        // destW=-2 means the rect canonically occupies world x in [18,20). Negative-scale-flip XOR
        // SpriteEffects-flip cancel: content must NOT be mirrored (still red at true dest-local TL).
        CheckPixel(*renderer, 18, 0, red, "Negative destW XOR FlipHorizontally cancels out (TL still red)");
        CheckPixel(*renderer, 19, 0, green, "Negative destW XOR FlipHorizontally cancels out (TR still green)");

        SDL_DestroyWindow(window);
    }

    // ------------------------------------------------------------------
    // Source sub-rectangles, including hostile out-of-range ones
    // ------------------------------------------------------------------
    void TestSourceRectangles()
    {
        SDL_Window* window = MakeWindow(32, 32);
        auto renderer = MakeRenderer(window, 32, 32);
        auto tex = renderer->CreateTexture(QuadrantImage());
        auto sb = renderer->CreateSpriteBatch();

        // In-bounds sub-rectangle: sample only the top-right green texel.
        renderer->Clear(0, 0, 0, 1);
        sb->Begin();
        sb->Draw(*tex, Rectangle(0, 0, 4, 4), Rectangle(1, 0, 1, 1), Color::White);
        sb->End();
        CheckPixel(*renderer, 0, 0, Color(0, 255, 0, 255), "In-bounds 1x1 source sub-rectangle samples the right texel");

        // Hostile: negative source origin, both tinted (exercises the resolve+tint path) and
        // untinted (white) -- must not crash and must produce a deterministic Clamp-extended
        // result (SamplerState default is Clamp), not an out-of-bounds read.
        for (bool tinted : {false, true})
        {
            const Color color = tinted ? Color(128, 200, 40, 255) : Color::White;
            CheckNoThrow(std::string("Negative source rect (tinted=") + (tinted ? "true" : "false") + ")",
                        [&] {
                            renderer->Clear(0, 0, 0, 1);
                            sb->Begin();
                            sb->Draw(*tex, Rectangle(0, 0, 4, 4), Rectangle(-5, -5, 4, 4), color);
                            sb->End();
                        });
            CheckNoThrow(std::string("Oversized source rect (tinted=") + (tinted ? "true" : "false") + ")",
                        [&] {
                            renderer->Clear(0, 0, 0, 1);
                            sb->Begin();
                            sb->Draw(*tex, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 500, 500), color);
                            sb->End();
                        });
            CheckNoThrow(std::string("Source rect entirely outside texture (tinted=") + (tinted ? "true" : "false") + ")",
                        [&] {
                            renderer->Clear(0, 0, 0, 1);
                            sb->Begin();
                            sb->Draw(*tex, Rectangle(0, 0, 4, 4), Rectangle(1000, 1000, 4, 4), color);
                            sb->End();
                        });
        }

        // Deterministic Clamp semantics: a source rect that starts 1 texel before the texture
        // (x=-1) with Clamp addressing (the default) must repeat the left EDGE column (red/blue),
        // not read garbage.
        renderer->Clear(0, 0, 0, 1);
        sb->Begin();
        sb->Draw(*tex, Rectangle(0, 0, 4, 2), Rectangle(-1, 0, 1, 1), Color::White);
        sb->End();
        CheckPixel(*renderer, 0, 0, Color(255, 0, 0, 255), "Clamp-extended one-texel-negative source reproduces the edge texel (red)");

        SDL_DestroyWindow(window);
    }

    // ------------------------------------------------------------------
    // Sampler address modes (Wrap / Clamp / Mirror) with an out-of-bounds source rectangle
    // ------------------------------------------------------------------
    void TestSamplerAddressModes()
    {
        SDL_Window* window = MakeWindow(16, 16);
        auto renderer = MakeRenderer(window, 16, 16);
        // 2x1 texture: texel 0 = red, texel 1 = green.
        ImageData img;
        img.width = 2; img.height = 1;
        img.pixels = {255, 0, 0, 255,   0, 255, 0, 255};
        auto tex = renderer->CreateTexture(img);
        auto sb = renderer->CreateSpriteBatch();

        // Sample one texel starting at x=2 (one past the texture, i.e. the first "next" texel).
        // Wrap(0): wraps to texel 0 (red). Mirror(2): reflects to texel 1 (green, the mirror of
        // index 2 about a 2-wide period). Clamp(1): clamps to the last texel (green).
        struct Case { int mode; Color expected; const char* name; };
        const Case cases[] = {
            {0, Color(255, 0, 0, 255), "Wrap"},
            {1, Color(0, 255, 0, 255), "Clamp"},
            {2, Color(0, 255, 0, 255), "Mirror"},
        };
        for (const auto& c : cases)
        {
            sb->SetSamplerAddressMode(c.mode, c.mode);
            renderer->Clear(0, 0, 0, 1);
            sb->Begin();
            sb->Draw(*tex, Rectangle(0, 0, 1, 1), Rectangle(2, 0, 1, 1), Color::White);
            sb->End();
            CheckPixel(*renderer, 0, 0, c.expected, std::string("TextureAddressMode::") + c.name + " at one-texel-past-edge");
        }

        CheckThrows("SetSamplerAddressMode(-1, 1)", [&] { sb->SetSamplerAddressMode(-1, 1); });
        CheckThrows("SetSamplerAddressMode(1, 3)", [&] { sb->SetSamplerAddressMode(1, 3); });

        SDL_DestroyWindow(window);
    }

    // ------------------------------------------------------------------
    // Sampler filter (Point vs Linear) validation and 1:1 exactness
    // ------------------------------------------------------------------
    void TestSamplerFilter()
    {
        SDL_Window* window = MakeWindow(16, 16);
        auto renderer = MakeRenderer(window, 16, 16);
        auto tex = renderer->CreateTexture(SolidImage(2, 2, Color(10, 20, 30, 255)));
        auto sb = renderer->CreateSpriteBatch();

        for (int filter : {0, 1, 2, 3, 4, 5, 6, 7, 8})
        {
            CheckNoThrow("SetSamplerFilter(" + std::to_string(filter) + ")",
                        [&] { sb->SetSamplerFilter(filter); });
            renderer->Clear(0, 0, 0, 1);
            sb->Begin();
            sb->Draw(*tex, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 2, 2), Color::White);
            sb->End();
            CheckPixel(*renderer, 0, 0, Color(10, 20, 30, 255),
                      "1:1 scale draw is exact regardless of sampler filter " + std::to_string(filter));
        }
        CheckThrows("SetSamplerFilter(9) (invalid)", [&] { sb->SetSamplerFilter(9); });
        CheckThrows("SetSamplerFilter(-1) (invalid)", [&] { sb->SetSamplerFilter(-1); });

        // A 1:1 draw is exact under EVERY filter (nothing above actually exercises interpolation).
        // Prove Point and Linear genuinely differ by minifying a two-texel (red, green) source
        // into a single destination pixel -- the same oracle as
        // skia_texture_filter_minification_test.cpp (SKIA-43): an unambiguous 2:1 minification,
        // where Point/NEAREST must select exactly one stored texel (a pure red or pure green
        // result) and Linear/BILINEAR must interpolate both (a blended result with R and G both
        // in a mid-range, neither channel near 0 nor near 255).
        ImageData redGreen;
        redGreen.width = 2;
        redGreen.height = 1;
        redGreen.pixels = { 255, 0, 0, 255,   0, 255, 0, 255 };
        auto redGreenTex = renderer->CreateTexture(redGreen);

        // Family per Blend2DSpriteBatchRenderer::SamplerFilterToPatternQualityEXT: BILINEAR =
        // {Linear=0, Anisotropic=2, LinearMipPoint=3, MinPointMagLinearMipLinear=7,
        // MinPointMagLinearMipPoint=8} (Linear MAGNIFICATION component); NEAREST = {Point=1,
        // PointMipLinear=4, MinLinearMagPointMipLinear=5, MinLinearMagPointMipPoint=6} (Point
        // magnification component). Blend2D has no mip chain at all -- every "Mip*" suffix is
        // necessarily unobservable here; only the base/magnification filter family is testable.
        struct FilterCase { int filter; bool expectBlended; const char* name; };
        const FilterCase filterCases[] = {
            {0, true,  "Linear"},
            {1, false, "Point"},
            {2, true,  "Anisotropic"},
            {3, true,  "LinearMipPoint"},
            {4, false, "PointMipLinear"},
            {5, false, "MinLinearMagPointMipLinear"},
            {6, false, "MinLinearMagPointMipPoint"},
            {7, true,  "MinPointMagLinearMipLinear"},
            {8, true,  "MinPointMagLinearMipPoint"},
        };
        for (const auto& c : filterCases)
        {
            sb->SetSamplerFilter(c.filter);
            renderer->Clear(0, 0, 0, 1);
            sb->Begin();
            // Two source texels collapsed into a single destination column (width 1): an
            // unambiguous 2:1 minification. Height 4 just keeps the sampled row comfortably away
            // from any dest-rect edge.
            sb->Draw(*redGreenTex, Rectangle(1, 0, 1, 4), Rectangle(0, 0, 2, 1), Color::White);
            sb->End();
            const Color px = ReadPixel(*renderer, 1, 2);
            const bool pure = (px.getRProperty() >= 235 && px.getGProperty() <= 20) ||
                              (px.getGProperty() >= 235 && px.getRProperty() <= 20);
            const bool blended = px.getRProperty() >= 90 && px.getRProperty() <= 165 &&
                                 px.getGProperty() >= 90 && px.getGProperty() <= 165;
            if (c.expectBlended)
                Check(blended, std::string("TextureFilter::") + c.name +
                               " (BILINEAR family) blends the minified red/green source, got " + ColorStr(px));
            else
                Check(pure, std::string("TextureFilter::") + c.name +
                            " (NEAREST family) selects one pure stored texel, got " + ColorStr(px));
        }

        SDL_DestroyWindow(window);
    }

    // ------------------------------------------------------------------
    // Transform matrix
    // ------------------------------------------------------------------
    void TestTransformMatrix()
    {
        SDL_Window* window = MakeWindow(16, 16);
        auto renderer = MakeRenderer(window, 16, 16);
        auto tex = renderer->CreateTexture(SolidImage(1, 1, Color(200, 50, 50, 255)));
        auto sb = renderer->CreateSpriteBatch();

        sb->SetTransformMatrix(Matrix::CreateTranslation(3.0f, 4.0f, 0.0f));
        renderer->Clear(0, 0, 0, 1);
        sb->Begin();
        sb->Draw(*tex, 2.0f, 2.0f);
        sb->End();
        CheckPixel(*renderer, 5, 6, Color(200, 50, 50, 255), "SetTransformMatrix translation composes on top of draw position");
        CheckPixel(*renderer, 2, 2, Color(0, 0, 0, 255), "SetTransformMatrix: original untransformed position is untouched");

        // Reset for subsequent tests.
        sb->SetTransformMatrix(Matrix::getIdentityProperty());

        SDL_DestroyWindow(window);
    }

    // ------------------------------------------------------------------
    // Scissor: enabled/disabled, target-space (unaffected by the transform matrix), and does not
    // leak across a render-target switch.
    // ------------------------------------------------------------------
    void TestScissor()
    {
        SDL_Window* window = MakeWindow(16, 16);
        auto renderer = MakeRenderer(window, 16, 16);
        auto tex = renderer->CreateTexture(SolidImage(16, 16, Color(255, 255, 255, 255)));
        auto sb = renderer->CreateSpriteBatch();

        // Disabled by default: a full-window draw fills everything.
        renderer->Clear(0, 0, 0, 1);
        sb->Begin();
        sb->Draw(*tex, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 16, 16), Color::White);
        sb->End();
        CheckPixel(*renderer, 15, 15, Color(255, 255, 255, 255), "Scissor disabled by default: corner is drawn");

        // Enable a small scissor rect; only pixels inside it may be written.
        renderer->SetScissorRect(2, 2, 4, 4);
        renderer->ApplyRasterizerState(0, 0, true);
        renderer->Clear(0, 0, 0, 1);
        sb->Begin();
        sb->Draw(*tex, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 16, 16), Color::White);
        sb->End();
        CheckPixel(*renderer, 3, 3, Color(255, 255, 255, 255), "Scissor enabled: inside the rect is drawn");
        CheckPixel(*renderer, 10, 10, Color(0, 0, 0, 255), "Scissor enabled: outside the rect is clipped");

        // Disabling ScissorTestEnable must actually stop clipping (previous defect: the shared
        // IGraphicsRenderer::ApplyRasterizerState no-op default meant this flag was never read).
        renderer->ApplyRasterizerState(0, 0, false);
        renderer->Clear(0, 0, 0, 1);
        sb->Begin();
        sb->Draw(*tex, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 16, 16), Color::White);
        sb->End();
        CheckPixel(*renderer, 10, 10, Color(255, 255, 255, 255), "ScissorTestEnable=false re-enables full drawing");

        // Target space, not sprite-local: with a translating transform matrix active, the scissor
        // rectangle's coordinates must NOT be reinterpreted through that transform.
        renderer->ApplyRasterizerState(0, 0, true);
        renderer->SetScissorRect(0, 0, 4, 4);
        sb->SetTransformMatrix(Matrix::CreateTranslation(8.0f, 0.0f, 0.0f));
        renderer->Clear(0, 0, 0, 1);
        sb->Begin();
        sb->Draw(*tex, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 16, 16), Color::White);
        sb->End();
        // The translated draw covers target x in [8,16); the scissor rect [0,4) is target-space
        // and does not itself move, so nothing should be visible at all.
        CheckPixel(*renderer, 2, 2, Color(0, 0, 0, 255), "Scissor stays target-space: translated draw outside the (untranslated) scissor rect is clipped");
        sb->SetTransformMatrix(Matrix::getIdentityProperty());
        renderer->ApplyRasterizerState(0, 0, false);

        SDL_DestroyWindow(window);
    }

    // ------------------------------------------------------------------
    // Viewport: SpriteBatch coordinates are viewport-LOCAL (REMED-GFX-073), found during the
    // second independent audit pass -- IGraphicsRenderer::SetViewport's shared default is a
    // silent no-op, which the newly-added Blend2D renderer inherited unlike every other CNA 2D
    // renderer (Skia, Software) that implements this.
    // ------------------------------------------------------------------
    void TestViewport()
    {
        SDL_Window* window = MakeWindow(24, 24);
        auto renderer = MakeRenderer(window, 24, 24);
        auto tex = renderer->CreateTexture(SolidImage(2, 2, Color(255, 128, 0, 255)));
        auto sb = renderer->CreateSpriteBatch();

        // Default (no SetViewport call): sprite coordinates are true backbuffer coordinates.
        renderer->Clear(0, 0, 0, 1);
        sb->Begin();
        sb->Draw(*tex, 0.0f, 0.0f);
        sb->End();
        CheckPixel(*renderer, 0, 0, Color(255, 128, 0, 255), "No viewport set: sprite position is backbuffer-absolute");

        // A viewport with a non-zero origin (e.g. split-screen's second half) offsets where
        // SpriteBatch position (0,0) actually lands, and clips drawing to the viewport rect.
        renderer->SetViewport(4, 4, 8, 8, 0.0f, 1.0f);
        renderer->Clear(0, 0, 0, 1);
        sb->Begin();
        sb->Draw(*tex, 0.0f, 0.0f);
        sb->End();
        CheckPixel(*renderer, 4, 4, Color(255, 128, 0, 255), "Viewport(4,4,8,8): local position (0,0) lands at the viewport origin");
        CheckPixel(*renderer, 0, 0, Color(0, 0, 0, 255), "Viewport(4,4,8,8): outside the viewport is untouched");

        // A draw that would extend past the viewport's own bounds is clipped to it: local
        // (2,2)-(10,10) offsets to target (6,6)-(14,14), but the viewport rect [4,12)x[4,12)
        // only allows target x,y < 12.
        renderer->Clear(0, 0, 0, 1);
        sb->Begin();
        sb->Draw(*tex, Rectangle(2, 2, 8, 8), Rectangle(0, 0, 2, 2), Color::White);
        sb->End();
        CheckPixel(*renderer, 7, 7, Color(255, 128, 0, 255), "Viewport clips: inside the viewport bounds is drawn");
        CheckPixel(*renderer, 13, 13, Color(0, 0, 0, 255), "Viewport clips: a draw extending past the viewport rect is clipped to it");

        SDL_DestroyWindow(window);
    }

    // ------------------------------------------------------------------
    // Blend state presets, tint, and premultiplied-alpha correctness
    // ------------------------------------------------------------------
    constexpr int kBlendOne = 0, kBlendZero = 1, kBlendSourceAlpha = 4, kBlendInverseSourceAlpha = 5, kFuncAdd = 0;

    void ApplyStockBlend(Blend2DRenderer& r, int srcFactor, int dstFactor)
    {
        BlendWriteState writeState;
        r.ApplyBlendState(srcFactor, srcFactor, dstFactor, dstFactor, kFuncAdd, kFuncAdd, writeState);
    }

    void TestBlendPresets()
    {
        // Every expected value below is derived independently from BlendState.cpp's real stock
        // factor tuples (Opaque=One/Zero, AlphaBlend=One/InverseSourceAlpha,
        // NonPremultiplied=SourceAlpha/InverseSourceAlpha, Additive=SourceAlpha/One, all with
        // matching Alpha*Blend factors), NOT from Blend2D's BLCompOp behaviour. Opaque/AlphaBlend
        // evaluate colour and alpha on Blend2D's own premultiplied storage directly (their
        // ColorSourceBlend is One, i.e. the destination's OWN premultiplied byte is one of the two
        // additive terms). NonPremultiplied/Additive are different: BOTH ColorSourceBlend and
        // AlphaSourceBlend are SourceAlpha, so XNA evaluates them from the SAME straight-space
        // equation pair -- the semantic oracle is SkiaRenderer.cpp's MaskedBlendEffect() runtime
        // blender, NOT Blend2D's BLCompOp:
        //   dstColor = Da>0 ? Dca/Da : 0                       (recovered straight destination RGB)
        //   NonPremul: Cout=saturate(Sca+dstColor*(1-Sa))        Aout=saturate(Sa*Sa+Da*(1-Sa))
        //   Additive:  Cout=saturate(Sca+dstColor)               Aout=saturate(Sa*Sa+Da)
        //   stored = (Cout*Aout, Aout)
        // where Sca is the source's own premultiplied byte (Sc_straight*Sa). Critically, Cout must
        // be premultiplied against the NEW Aout, not the OLD Da -- so, unlike AlphaBlend, NEITHER
        // NonPremultiplied's NOR Additive's colour bytes are identical to what native
        // BL_COMP_OP_SRC_OVER/PLUS would produce (those premultiply against Da instead), even
        // though native compositing's OWN colour formula happens to use the same Sca term.
        SDL_Window* window = MakeWindow(8, 8);
        auto renderer = MakeRenderer(window, 8, 8);
        auto sb = renderer->CreateSpriteBatch();

        // Opaque (One,Zero): Dca'=Sc*1+Dca*0=Sc, Da'=Sa*1+Da*0=Sa -- a literal copy of the
        // source's own stored bytes, alpha included (Opaque does not force alpha to 1). Straight
        // (255,0,0,128) stores premultiplied as (128,0,0,128); the copy reproduces those bytes,
        // which unpremultiply back to straight (255,0,0,128).
        auto halfRed = renderer->CreateTexture(SolidImage(1, 1, Color(255, 0, 0, 128)));
        ApplyStockBlend(*renderer, kBlendOne, kBlendZero);
        renderer->Clear(0, 1, 0, 1); // opaque green background
        sb->Begin();
        sb->Draw(*halfRed, 0.0f, 0.0f);
        sb->End();
        CheckAlpha(*renderer, 0, 0, 128, "BlendState::Opaque: Da'=Sa*1+Da*0=Sa=128");
        CheckPixel(*renderer, 0, 0, Color(255, 0, 0, 128), "BlendState::Opaque is a literal copy of source, alpha included (not forced to 1)");

        // AlphaBlend (One,InverseSourceAlpha) against a semi-transparent destination AND a
        // semi-transparent source: Dca'=Sca+Dca*(1-Sa), Da'=Sa+Da*(1-Sa). Source premultiplies to
        // (128,0,0,128) (Sa=128/255); the destination Clear(0,1,0,64/255) premultiplies to
        // (0,64,0,64) (Da=64/255).
        //   Da'  = 128/255 + (64/255)*(1-128/255) = 0.62696 -> 160 (byte, round-half-up)
        //   Dca_r' = 128 + 0*(1-Sa) = 128           Dca_g' = 0 + 64*(1-Sa) = 31.87 -> 32
        // Unpremultiplied by Da'=160: R = (128*255+80)/160 = 204, G = (32*255+80)/160 = 51.
        ApplyStockBlend(*renderer, kBlendOne, kBlendInverseSourceAlpha);
        renderer->Clear(0, 1, 0, 64.0f / 255.0f);
        sb->Begin();
        sb->Draw(*halfRed, 0.0f, 0.0f);
        sb->End();
        CheckAlpha(*renderer, 0, 0, 160, "BlendState::AlphaBlend: Da'=Sa+Da*(1-Sa)=160", 2);
        CheckPixel(*renderer, 0, 0, Color(204, 51, 0, 160), "BlendState::AlphaBlend, semi-transparent src AND dst", 3);

        // NonPremultiplied (SourceAlpha,InverseSourceAlpha) against the SAME semi-transparent
        // green destination as AlphaBlend above, per the MaskedBlendEffect oracle:
        //   Sa=128/255=0.501961, Sca=(0.501961,0,0) (straight red * Sa)
        //   Da=64/255=0.250980, Dca=(0,0.250980,0), dstColor=Dca/Da=(0,1,0) (straight green)
        //   Aout = Sa*Sa + Da*(1-Sa) = 0.251964 + 0.250980*0.498039 = 0.376974 -> byte 96
        //   Cout = saturate(Sca + dstColor*(1-Sa)) = (0.501961, 0.498039, 0) (already in [0,1])
        //   stored = Cout*Aout = (0.189165, 0.187740, 0), i.e. premultiplied bytes ~=(48,48,0), A=96
        // Unpremultiplied by the stored A=96 on readback: R=(48*255+48)/96=128, G=(48*255+48)/96=128.
        // This is NEITHER AlphaBlend's (204,51,0,160) NOR native SRC_OVER's colour (which would
        // premultiply the same straight blend against the OLD Da=64, not the NEW Aout=96) -- the
        // colour bytes are recomputed from scratch against the new alpha, not merely alpha-corrected.
        ApplyStockBlend(*renderer, kBlendSourceAlpha, kBlendInverseSourceAlpha);
        renderer->Clear(0, 1, 0, 64.0f / 255.0f);
        sb->Begin();
        sb->Draw(*halfRed, 0.0f, 0.0f);
        sb->End();
        CheckAlpha(*renderer, 0, 0, 96,
                  "BlendState::NonPremultiplied: Aout=Sa*Sa+Da*(1-Sa)=96 (independent of AlphaBlend's Da'=160)", 2);
        CheckPixel(*renderer, 0, 0, Color(128, 128, 0, 96),
                  "BlendState::NonPremultiplied: full straight-space Skia-oracle colour+alpha recompute (NOT AlphaBlend's RGB, NOT native SRC_OVER's RGB)", 3);

        // Additive (SourceAlpha,One) against a semi-transparent RED destination:
        //   Sa=128/255=0.501961, Sca=(0,0,0.501961) (straight blue * Sa)
        //   Da=64/255=0.250980, Dca=(0.250980,0,0), dstColor=Dca/Da=(1,0,0) (straight red)
        //   Aout = Sa*Sa + Da = 0.251964 + 0.250980 = 0.502944 -> byte 128
        //   Cout = saturate(Sca + dstColor) = saturate((1,0,0.501961)) = (1,0,0.501961)
        //   stored = Cout*Aout = (0.502944, 0, 0.252471), i.e. premultiplied bytes ~=(128,0,64), A=128
        // Unpremultiplied by the stored A=128: R=(128*255+64)/128=255 (floors from 255.5),
        // G=0, B=(64*255+64)/128=128. This is NEITHER native PLUS's colour (premultiplied against
        // the OLD Da=64) NOR the previous alpha-only-corrected byte-substitution result.
        auto halfBlue = renderer->CreateTexture(SolidImage(1, 1, Color(0, 0, 255, 128)));
        ApplyStockBlend(*renderer, kBlendSourceAlpha, kBlendOne);
        renderer->Clear(1, 0, 0, 64.0f / 255.0f);
        sb->Begin();
        sb->Draw(*halfBlue, 0.0f, 0.0f);
        sb->End();
        CheckAlpha(*renderer, 0, 0, 128, "BlendState::Additive: Aout=Sa*Sa+Da=128 (not native PLUS's Sa+Da=192)", 2);
        CheckPixel(*renderer, 0, 0, Color(255, 0, 128, 128),
                  "BlendState::Additive: full straight-space Skia-oracle colour+alpha recompute", 3);

        // Unsupported / unrecognized combination must be REJECTED, not silently approximated.
        CheckThrows("Non-stock colour blend factor combination", [&] {
            BlendWriteState writeState;
            renderer->ApplyBlendState(2 /*SourceColor*/, kBlendOne, kBlendZero, kBlendZero, kFuncAdd, kFuncAdd, writeState);
        });
        CheckThrows("Independent alpha blend factor mismatching the colour factor", [&] {
            BlendWriteState writeState;
            renderer->ApplyBlendState(kBlendOne, kBlendZero /*alpha src differs*/, kBlendInverseSourceAlpha,
                                      kBlendInverseSourceAlpha, kFuncAdd, kFuncAdd, writeState);
        });
        CheckThrows("Non-Add colour BlendFunction", [&] {
            BlendWriteState writeState;
            renderer->ApplyBlendState(kBlendOne, kBlendOne, kBlendInverseSourceAlpha, kBlendInverseSourceAlpha,
                                      1 /*Subtract*/, kFuncAdd, writeState);
        });
        CheckThrows("Non-default ColorWriteChannels1 (slot 1, unimplemented MRT slot)", [&] {
            BlendWriteState writeState;
            writeState.colorWriteChannels[1] = 1;
            renderer->ApplyBlendState(kBlendOne, kBlendOne, kBlendZero, kBlendZero, kFuncAdd, kFuncAdd, writeState);
        });
        CheckThrows("Non-default MultiSampleMask", [&] {
            BlendWriteState writeState;
            writeState.multiSampleMask = 0x1;
            renderer->ApplyBlendState(kBlendOne, kBlendOne, kBlendZero, kBlendZero, kFuncAdd, kFuncAdd, writeState);
        });

        ApplyStockBlend(*renderer, kBlendOne, kBlendInverseSourceAlpha);
        SDL_DestroyWindow(window);
    }

    // ------------------------------------------------------------------
    // PRGB32 storage invariant: every renderer-owned pixel must satisfy R,G,B <= A. Reads Blend2D's
    // raw native premultiplied bytes directly (not through the straight-RGBA readback path, which
    // would hide an invariant violation behind Blend2DPixelConvert.hpp's defensive clamp) to prove
    // BlendState::NonPremultiplied/Additive's bounded correction produces coherent PRGB32 storage
    // on its own, i.e. that clamp is hardening, never load-bearing, for genuine renderer output.
    // ------------------------------------------------------------------
    void CheckPrgb32Invariant(Blend2DRenderer& renderer, const std::string& label)
    {
        BLImageData data{};
        const bool ok = renderer.ActiveSurface().Image().get_data(&data) == BL_SUCCESS;
        Check(ok, label + ": BLImage::get_data succeeds");
        if (!ok)
            return;

        const auto* base = static_cast<const std::uint8_t*>(data.pixel_data);
        bool invariantHolds = true;
        int failX = -1, failY = -1;
        std::uint8_t failB = 0, failG = 0, failR = 0, failA = 0;
        for (int y = 0; y < renderer.ActiveSurface().Height() && invariantHolds; ++y)
        {
            const std::uint8_t* row = base + static_cast<std::ptrdiff_t>(y) * data.stride;
            for (int x = 0; x < renderer.ActiveSurface().Width(); ++x)
            {
                const std::size_t idx = static_cast<std::size_t>(x) * 4;
                // BL_FORMAT_PRGB32 is BGRA byte order on this little-endian-only renderer.
                const std::uint8_t b = row[idx + 0], g = row[idx + 1], r = row[idx + 2], a = row[idx + 3];
                if (b > a || g > a || r > a)
                {
                    invariantHolds = false;
                    failX = x; failY = y; failB = b; failG = g; failR = r; failA = a;
                    break;
                }
            }
        }
        Check(invariantHolds,
             label + ": every pixel satisfies R,G,B <= A (valid PRGB32)" +
                 (invariantHolds ? "" :
                      " -- FIRST VIOLATION at (" + std::to_string(failX) + "," + std::to_string(failY) +
                          ") B=" + std::to_string(failB) + " G=" + std::to_string(failG) +
                          " R=" + std::to_string(failR) + " A=" + std::to_string(failA)));
    }

    void TestPrgb32Invariant()
    {
        SDL_Window* window = MakeWindow(8, 8);
        auto renderer = MakeRenderer(window, 8, 8);
        auto sb = renderer->CreateSpriteBatch();

        // The two scenarios from TestBlendPresets, which the P0 fix's oracle rewrite already
        // proves produce the expected RGBA bytes -- re-checked here for the raw-storage invariant
        // specifically, independent of what CheckPixel's straight-RGBA readback would show.
        auto halfRed = renderer->CreateTexture(SolidImage(1, 1, Color(255, 0, 0, 128)));
        ApplyStockBlend(*renderer, kBlendSourceAlpha, kBlendInverseSourceAlpha);
        renderer->Clear(0, 1, 0, 64.0f / 255.0f);
        sb->Begin();
        sb->Draw(*halfRed, 0.0f, 0.0f);
        sb->End();
        CheckPrgb32Invariant(*renderer, "NonPremultiplied, Sa=128/255 over Da=64/255");

        auto halfBlue = renderer->CreateTexture(SolidImage(1, 1, Color(0, 0, 255, 128)));
        ApplyStockBlend(*renderer, kBlendSourceAlpha, kBlendOne);
        renderer->Clear(1, 0, 0, 64.0f / 255.0f);
        sb->Begin();
        sb->Draw(*halfBlue, 0.0f, 0.0f);
        sb->End();
        CheckPrgb32Invariant(*renderer, "Additive, Sa=128/255 over Da=64/255");

        // A broad sweep across source alpha / destination alpha byte combinations, including the
        // boundary cases (Sa/Da at 0, 1, and 255) most likely to expose a rounding-induced
        // invariant violation -- not just the one arbitrarily chosen pair above. Additive is the
        // riskier preset (its Cout weights, Sa+1, do not sum to 1, so an unsaturated Cout can
        // exceed 1.0 -- exactly the case the saturate-before-repremultiply ordering guards).
        const int saValues[] = {1, 32, 64, 127, 128, 200, 254, 255};
        const int daValues[] = {0, 1, 64, 128, 200, 255};
        for (int saByte : saValues)
        {
            auto sweepTex = renderer->CreateTexture(
                SolidImage(1, 1, Color(200, 100, 50, saByte)));
            for (int daByte : daValues)
            {
                ApplyStockBlend(*renderer, kBlendSourceAlpha, kBlendInverseSourceAlpha);
                renderer->Clear(10.0f / 255.0f, 150.0f / 255.0f, 220.0f / 255.0f, daByte / 255.0f);
                sb->Begin();
                sb->Draw(*sweepTex, 0.0f, 0.0f);
                sb->End();
                CheckPrgb32Invariant(*renderer, "NonPremultiplied sweep Sa=" + std::to_string(saByte) +
                                              " Da=" + std::to_string(daByte));

                ApplyStockBlend(*renderer, kBlendSourceAlpha, kBlendOne);
                renderer->Clear(10.0f / 255.0f, 150.0f / 255.0f, 220.0f / 255.0f, daByte / 255.0f);
                sb->Begin();
                sb->Draw(*sweepTex, 0.0f, 0.0f);
                sb->End();
                CheckPrgb32Invariant(*renderer, "Additive sweep Sa=" + std::to_string(saByte) +
                                              " Da=" + std::to_string(daByte));
            }
        }

        ApplyStockBlend(*renderer, kBlendOne, kBlendInverseSourceAlpha);
        SDL_DestroyWindow(window);
    }

    // ------------------------------------------------------------------
    // NonPremultiplied/Additive at a partial-coverage (antialiased) edge pixel: proves the
    // corrected COLOUR route uses the SAME per-pixel effective coverage the scratch pass recovers
    // for alpha, not just the fully-covered interior. Uses a fractional, axis-aligned destination
    // scale (2.5x via SetTransformMatrix) rather than a rotation, so the analytic AA coverage at
    // the boundary column is exactly derivable (a rotated edge's exact sub-pixel coverage depends
    // on rasterizer implementation details this test should not need to reverse-engineer): a
    // 1x1 source scaled to a device-space rect spanning x in [0,2.5) covers column x=0 fully and
    // column x=2 at exactly 50% (linear axis-aligned coverage AA), verified independently against
    // BOTH TestPrgb32Invariant's invariant AND a half-effective-alpha instance of the same
    // straight-space Skia oracle TestBlendPresets uses.
    // ------------------------------------------------------------------
    void TestBlendCorrectionEdgeCoverage()
    {
        SDL_Window* window = MakeWindow(8, 8);
        auto renderer = MakeRenderer(window, 8, 8);
        auto sb = renderer->CreateSpriteBatch();
        sb->SetSamplerFilter(1); // Point: isolates AA edge coverage from bilinear sampling blur.

        auto halfRed = renderer->CreateTexture(SolidImage(1, 1, Color(255, 0, 0, 128)));

        // NonPremultiplied: full-coverage column (x=0) must match TestBlendPresets' own Sa=128/255
        // result; the half-coverage column (x=2, effective Sa'=Sa/2) is independently derived from
        // the SAME oracle with saEff substituted for Sa.
        ApplyStockBlend(*renderer, kBlendSourceAlpha, kBlendInverseSourceAlpha);
        renderer->Clear(0, 1, 0, 64.0f / 255.0f);
        sb->SetTransformMatrix(Matrix::CreateScale(2.5f, 1.0f, 1.0f));
        sb->Begin();
        sb->Draw(*halfRed, 0.0f, 0.0f);
        sb->End();
        sb->SetTransformMatrix(Matrix::getIdentityProperty());

        // Full coverage: identical to TestBlendPresets' NonPremultiplied result, (128,128,0,96).
        CheckPixel(*renderer, 0, 0, Color(128, 128, 0, 96),
                  "NonPremultiplied edge coverage: fully-covered column matches TestBlendPresets' Sa=128/255 result", 3);

        // Half coverage (x=2): saEff = 0.5 * 128/255 = 0.250980. Sca_eff = straight red * saEff =
        // (0.250980, 0, 0). Da = 64/255 = 0.250980 (unchanged, this column's own background).
        // dstColor = (0,1,0). Aout = saEff^2 + Da*(1-saEff) = 0.062991 + 0.250980*0.749020 =
        // 0.250973 -> byte round(0.250973*255) ~= 64. Cout = saturate(Sca_eff + dstColor*(1-saEff))
        // = (0.250980, 0.749020, 0). stored = Cout*Aout ~= (0.062985, 0.188008, 0) -> premultiplied
        // bytes ~=(16,48,0), A~=64. Unpremultiplied: R=(16*255+32)/64~=64, G=(48*255+32)/64~=191.
        CheckAlpha(*renderer, 2, 0, 64, "NonPremultiplied edge coverage: half-covered column alpha ~=64", 3);
        CheckPixel(*renderer, 2, 0, Color(64, 191, 0, 64),
                  "NonPremultiplied edge coverage: half-covered column colour uses the SAME effective coverage as alpha", 6);

        // Uncovered column (x=4, well past the 2.5-wide draw): must be completely untouched by
        // this draw -- the background Clear colour, unmodified.
        CheckPixel(*renderer, 4, 0, Color(0, 255, 0, 64),
                  "NonPremultiplied edge coverage: uncovered column is untouched (raw premultiplied Clear bytes)");

        CheckPrgb32Invariant(*renderer, "NonPremultiplied edge coverage draw");

        // Additive: same fractional-scale setup, different destination/blend.
        auto halfBlue = renderer->CreateTexture(SolidImage(1, 1, Color(0, 0, 255, 128)));
        ApplyStockBlend(*renderer, kBlendSourceAlpha, kBlendOne);
        renderer->Clear(1, 0, 0, 64.0f / 255.0f);
        sb->SetTransformMatrix(Matrix::CreateScale(2.5f, 1.0f, 1.0f));
        sb->Begin();
        sb->Draw(*halfBlue, 0.0f, 0.0f);
        sb->End();
        sb->SetTransformMatrix(Matrix::getIdentityProperty());

        CheckPixel(*renderer, 0, 0, Color(255, 0, 128, 128),
                  "Additive edge coverage: fully-covered column matches TestBlendPresets' Sa=128/255 result", 3);

        // Half coverage (x=2): saEff=0.250980, Sca_eff=(0,0,0.250980), Da=0.250980, dstColor=(1,0,0).
        // Aout = saEff^2 + Da = 0.062991 + 0.250980 = 0.313971 -> byte ~=80. Cout=saturate(Sca_eff+
        // dstColor)=saturate((1,0,0.250980))=(1,0,0.250980). stored=Cout*Aout~=(0.313971,0,0.078807)
        // -> bytes ~=(80,0,20), A~=80. Unpremultiplied: R=(80*255+40)/80=255, B=(20*255+40)/80~=64.
        CheckAlpha(*renderer, 2, 0, 80, "Additive edge coverage: half-covered column alpha ~=80", 3);
        CheckPixel(*renderer, 2, 0, Color(255, 0, 64, 80),
                  "Additive edge coverage: half-covered column colour uses the SAME effective coverage as alpha", 6);

        CheckPixel(*renderer, 4, 0, Color(255, 0, 0, 64),
                  "Additive edge coverage: uncovered column is untouched (raw premultiplied Clear bytes)");

        CheckPrgb32Invariant(*renderer, "Additive edge coverage draw");

        ApplyStockBlend(*renderer, kBlendOne, kBlendInverseSourceAlpha);
        SDL_DestroyWindow(window);
    }

    void TestColorWriteChannels()
    {
        SDL_Window* window = MakeWindow(4, 4);
        auto renderer = MakeRenderer(window, 4, 4);
        auto sb = renderer->CreateSpriteBatch();
        auto blue = renderer->CreateTexture(SolidImage(1, 1, Color(0, 0, 255, 255)));

        renderer->Clear(1, 0, 0, 1); // opaque red backdrop
        BlendWriteState rgbOnly;
        rgbOnly.colorWriteChannels[0] = 7; // R|G|B, no alpha
        renderer->ApplyBlendState(kBlendOne, kBlendOne, kBlendZero, kBlendZero, kFuncAdd, kFuncAdd, rgbOnly);
        sb->Begin();
        sb->Draw(*blue, 0.0f, 0.0f);
        sb->End();
        CheckPixel(*renderer, 0, 0, Color(0, 0, 255, 255), "ColorWriteChannels RGB-only: colour is written, alpha preserved from opaque backdrop");

        // Alpha-only write mask: the merge keeps the OLD premultiplied RGB bytes and only takes
        // the new draw's alpha. Backdrop straight (1,0,0,128/255) stores premultiplied R~=128;
        // keeping that premultiplied R while changing alpha to 255 changes the UNPREMULTIPLIED
        // (straight, readback) red channel too -- straight R = premulR*255/newA = 128*255/255 =
        // 128, not 255. This is the correct, well-understood interaction between ColorWriteChannels
        // and premultiplied storage (the same representation SkiaRenderer's own masked write
        // merges in), not an approximation.
        renderer->Clear(1, 0, 0, 128.0f / 255.0f); // semi-transparent red backdrop
        BlendWriteState alphaOnly;
        alphaOnly.colorWriteChannels[0] = 8; // Alpha only
        renderer->ApplyBlendState(kBlendOne, kBlendOne, kBlendZero, kBlendZero, kFuncAdd, kFuncAdd, alphaOnly);
        sb->Begin();
        sb->Draw(*blue, 0.0f, 0.0f);
        sb->End();
        CheckPixel(*renderer, 0, 0, Color(128, 0, 0, 255), "ColorWriteChannels Alpha-only: alpha overwritten to opaque, premultiplied colour bytes preserved", 2);

        ApplyStockBlend(*renderer, kBlendOne, kBlendInverseSourceAlpha);
        SDL_DestroyWindow(window);
    }

    // ------------------------------------------------------------------
    // Tint / premultiplied-alpha chain: every source-alpha x tint-alpha combination the audit
    // explicitly calls out, verified against transparent AND semi-transparent backgrounds.
    // Blend2D stores BL_FORMAT_PRGB32 (premultiplied); CNA's public transfer contract is always
    // straight RGBA8 -- these checks catch a double premultiply or a dropped conversion.
    // ------------------------------------------------------------------
    void TestTintChain()
    {
        SDL_Window* window = MakeWindow(4, 4);
        auto renderer = MakeRenderer(window, 4, 4);
        auto sb = renderer->CreateSpriteBatch();
        ApplyStockBlend(*renderer, kBlendOne, kBlendInverseSourceAlpha); // AlphaBlend

        // Opaque source + semi-transparent tint, onto a transparent background: result should be
        // exactly the source colour with the TINT's alpha (255*128/255=128 straight-tint math),
        // since compositing onto fully transparent leaves the source's own premultiplied bytes
        // unchanged before unpremultiply.
        auto opaqueRed = renderer->CreateTexture(SolidImage(1, 1, Color(255, 0, 0, 255)));
        renderer->Clear(0, 0, 0, 0);
        sb->Begin();
        sb->Draw(*opaqueRed, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color(255, 255, 255, 128));
        sb->End();
        CheckPixel(*renderer, 0, 0, Color(255, 0, 0, 128), "Opaque source + semi-transparent white tint, onto transparent bg", 1);

        // Semi-transparent source + semi-transparent tint, onto a transparent background:
        // straight-component multiply of (255,0,0,128) by (255,255,255,128) -> alpha
        // 128*128/255~=64, colour unchanged (255,0,0).
        auto halfRed = renderer->CreateTexture(SolidImage(1, 1, Color(255, 0, 0, 128)));
        renderer->Clear(0, 0, 0, 0);
        sb->Begin();
        sb->Draw(*halfRed, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color(255, 255, 255, 128));
        sb->End();
        CheckPixel(*renderer, 0, 0, Color(255, 0, 0, 64), "Semi-transparent source + semi-transparent tint, onto transparent bg", 1);

        // Colored semi-transparent source + colored semi-transparent tint, onto a transparent
        // background: independently verifies every one of R/G/B/A is tinted (not just alpha).
        auto coloredSrc = renderer->CreateTexture(SolidImage(1, 1, Color(100, 150, 200, 128)));
        renderer->Clear(0, 0, 0, 0);
        sb->Begin();
        sb->Draw(*coloredSrc, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color(200, 100, 50, 200));
        sb->End();
        CheckPixel(*renderer, 0, 0, Color(79, 59, 38, 100), "Colored semi-transparent source + colored semi-transparent tint, onto transparent bg", 2);

        // The same colored-tint draw onto a SEMI-TRANSPARENT (not transparent, not opaque)
        // destination -- exercises the full composite equation with three independently
        // meaningful alpha values (source, tint-derived, destination) simultaneously.
        renderer->Clear(0.2f, 0.6f, 0.1f, 100.0f / 255.0f);
        sb->Begin();
        sb->Draw(*coloredSrc, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color(200, 100, 50, 200));
        sb->End();
        {
            // Independently compute the expected result in this test (not just re-deriving the
            // renderer's own formula) via straight-alpha "over" math for a cross-check: tint the
            // source, composite over the destination with Porter-Duff over, done in floating point
            // straight-alpha space, then compare with a tolerance that accounts for the
            // implementation's integer premultiplied round-trip.
            const double sr = 100 / 255.0, sg = 150 / 255.0, sb_ = 200 / 255.0, sa = 128 / 255.0;
            const double tr = 200 / 255.0, tg = 100 / 255.0, tb = 50 / 255.0, ta = 200 / 255.0;
            const double cr = sr * tr, cg = sg * tg, cb = sb_ * tb, ca = sa * ta;
            const double dr = 0.2, dg = 0.6, db = 0.1, da = 100 / 255.0;
            const double outA = ca + da * (1 - ca);
            const double outR = outA > 0 ? (cr * ca + dr * da * (1 - ca)) / outA : 0;
            const double outG = outA > 0 ? (cg * ca + dg * da * (1 - ca)) / outA : 0;
            const double outB = outA > 0 ? (cb * ca + db * da * (1 - ca)) / outA : 0;
            CheckPixel(*renderer, 0, 0,
                      Color(static_cast<std::uint8_t>(outR * 255 + 0.5), static_cast<std::uint8_t>(outG * 255 + 0.5),
                            static_cast<std::uint8_t>(outB * 255 + 0.5), static_cast<std::uint8_t>(outA * 255 + 0.5)),
                      "Colored semi-transparent source + tint, onto a semi-transparent background (independent cross-check)", 3);
        }

        ApplyStockBlend(*renderer, kBlendOne, kBlendInverseSourceAlpha);
        SDL_DestroyWindow(window);
    }

    // ------------------------------------------------------------------
    // Render target contract: bind/unbind/switch/readback/self-sample/destruction.
    // ------------------------------------------------------------------
    void TestRenderTargets()
    {
        SDL_Window* window = MakeWindow(16, 16);
        auto renderer = MakeRenderer(window, 16, 16);
        auto sb = renderer->CreateSpriteBatch();

        auto rtA = renderer->CreateRenderTarget2D(4, 4, 0);
        auto rtB = renderer->CreateRenderTarget2D(4, 4, 0);

        rtA->BindAsRenderTarget();
        renderer->Clear(1, 0, 0, 1);
        rtB->BindAsRenderTarget(); // direct A -> B without an intervening unbind
        renderer->Clear(0, 1, 0, 1);
        rtB->UnbindAsRenderTarget();

        std::uint8_t px[4];
        Check(rtA->GetData(0, 0, 0, 1, 1, px, 4) && px[0] == 255 && px[1] == 0,
             "Render target A retains its own colour after switching directly to B");
        Check(rtB->GetData(0, 0, 0, 1, 1, px, 4) && px[1] == 255 && px[0] == 0,
             "Render target B has its own colour, independent of A");

        // Sampling A while B is active must read A's real contents, not B's.
        rtB->BindAsRenderTarget();
        auto sampler = renderer->CreateSpriteBatch();
        sampler->Begin();
        sampler->Draw(*rtA, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
        sampler->End();
        rtB->UnbindAsRenderTarget();
        Check(rtB->GetData(0, 0, 0, 1, 1, px, 4) && px[0] == 255 && px[1] == 0,
             "Sampling render target A while B is active reads A's real (red) contents into B");

        // Self-sampling: draw a render target onto itself while it is the active target. Must not
        // crash (aliasing safety) and, drawn 1:1 at an unchanged position with a white tint, must
        // reproduce its own prior contents exactly (proves the snapshot-before-draw is a genuine,
        // uncorrupted copy of the pre-draw state).
        rtA->BindAsRenderTarget();
        renderer->Clear(0.2f, 0.4f, 0.6f, 1.0f);
        CheckNoThrow("Self-sampling a render target while it is the active target", [&] {
            sb->Begin();
            sb->Draw(*rtA, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
            sb->End();
        });
        rtA->UnbindAsRenderTarget();
        Check(rtA->GetData(0, 0, 0, 1, 1, px, 4) &&
             std::abs(px[0] - static_cast<int>(0.2f * 255)) <= 2 &&
             std::abs(px[1] - static_cast<int>(0.4f * 255)) <= 2,
             "Self-sampled draw at an unchanged 1:1 position reproduces the pre-draw colour exactly (no corruption)");

        // Target destruction while unbound is safe (already-established path).
        rtB.reset();
        Check(true, "Render target destruction while unbound completes without incident");

        // Target destruction while BOUND must clear the renderer's active-target pointer so a
        // subsequent Clear()/draw does not touch freed memory.
        rtA->BindAsRenderTarget();
        rtA.reset();
        CheckNoThrow("Clear() after the bound render target was destroyed (must have auto-unbound)",
                    [&] { renderer->Clear(0, 0, 0, 1); });

        SDL_DestroyWindow(window);
    }

    // ------------------------------------------------------------------
    // Cross-renderer resource type safety
    // ------------------------------------------------------------------
    class FakeForeignTextureEXT final : public ITextureRenderer
    {
    public:
        int GetWidth() const override { return 1; }
        int GetHeight() const override { return 1; }

    };

    void TestCrossRendererSafety()
    {
        SDL_Window* window = MakeWindow(8, 8);
        auto renderer = MakeRenderer(window, 8, 8);
        auto sb = renderer->CreateSpriteBatch();
        FakeForeignTextureEXT foreign;

        sb->Begin();
        bool threwInvalidArgument = false;
        try { sb->Draw(foreign, 0.0f, 0.0f); }
        catch (const std::invalid_argument&) { threwInvalidArgument = true; }
        catch (...) { /* some other exception type -- still a fail below */ }
        Check(threwInvalidArgument, "Drawing a foreign (non-Blend2D) texture throws std::invalid_argument, not std::bad_cast");
        sb->End();

        SDL_DestroyWindow(window);
    }

    // ------------------------------------------------------------------
    // Begin/End state machine
    // ------------------------------------------------------------------
    void TestBeginEndStateMachine()
    {
        SDL_Window* window = MakeWindow(8, 8);
        auto renderer = MakeRenderer(window, 8, 8);
        auto sb = renderer->CreateSpriteBatch();
        auto tex = renderer->CreateTexture(SolidImage(1, 1, Color::White));

        CheckThrows("Draw() before the first Begin()", [&] { sb->Draw(*tex, 0.0f, 0.0f); });

        sb->Begin();
        CheckThrows("Begin() called twice without an intervening End()", [&] { sb->Begin(); });
        // The renderer must remain usable after the rejected re-Begin (the batch that was already
        // open is untouched).
        CheckNoThrow("Draw() still works after a rejected double-Begin", [&] { sb->Draw(*tex, 0.0f, 0.0f); });
        sb->End();

        CheckThrows("End() called twice without an intervening Begin()", [&] { sb->End(); });
        // A failed End() must not poison the next Begin()/End() cycle.
        CheckNoThrow("Begin()/End() still works after a rejected double-End", [&] {
            sb->Begin();
            sb->Draw(*tex, 0.0f, 0.0f);
            sb->End();
        });

        // An unsupported sampler filter rejected during Begin()-adjacent setup must not corrupt
        // renderer state for the next, valid Begin()/End().
        CheckThrows("Invalid SetSamplerFilter", [&] { sb->SetSamplerFilter(42); });
        CheckNoThrow("Renderer remains usable after a rejected SetSamplerFilter", [&] {
            sb->Begin();
            sb->Draw(*tex, 0.0f, 0.0f);
            sb->End();
        });

        SDL_DestroyWindow(window);
    }

    // ------------------------------------------------------------------
    // Custom effect rejection
    // ------------------------------------------------------------------
    void TestCustomEffectRejection()
    {
        SDL_Window* window = MakeWindow(8, 8);
        auto renderer = MakeRenderer(window, 8, 8);
        auto sb = renderer->CreateSpriteBatch();

        CheckNoThrow("SetCustomEffect(nullptr) is accepted", [&] { sb->SetCustomEffect(nullptr); });

        SDL_DestroyWindow(window);
    }

    // ------------------------------------------------------------------
    // Presentation modes, including FixedHeightDynamicWidth
    // ------------------------------------------------------------------
    void TestPresentationModes()
    {
        SDL_Window* window = MakeWindow(64, 64);
        for (CnaPresentationMode mode : {CnaPresentationMode::Letterbox, CnaPresentationMode::Overscan,
                                         CnaPresentationMode::Stretch, CnaPresentationMode::NativeBackBuffer,
                                         CnaPresentationMode::FixedHeightDynamicWidth})
        {
            CheckNoThrow("Construct + Present under presentation mode " + std::to_string(static_cast<int>(mode)),
                        [&] {
                            auto renderer = MakeRenderer(window, 32, 32, mode);
                            renderer->Clear(0.1f, 0.2f, 0.3f, 1.0f);
                            renderer->Present();
                        });
        }

        // FixedHeightDynamicWidth: logical width tracks the aspect-derived formula
        // round(outputWidth * requestedHeight / outputHeight), and updates when the output size
        // changes -- mirrors SkiaRenderer's own DebugSetPresentationOutputSizeEXT-driven tests.
        auto renderer = MakeRenderer(window, 100 /*ignored width*/, 40, CnaPresentationMode::FixedHeightDynamicWidth);
        renderer->DebugSetPresentationOutputSizeEXT(160, 40); // 4:1 aspect
        renderer->SetVirtualResolution(100, 40);
        int w = 0, h = 0;
        renderer->GetViewportSize(w, h);
        Check(h == 40 && w == 160, "FixedHeightDynamicWidth derives width = outputW*prefH/outputH (160*40/40 case), got " +
             std::to_string(w) + "x" + std::to_string(h));

        renderer->DebugSetPresentationOutputSizeEXT(80, 40); // 2:1 aspect now
        renderer->Clear(0, 0, 0, 1); // triggers RefreshDynamicBackbufferIfNeeded
        renderer->GetViewportSize(w, h);
        Check(h == 40 && w == 80, "FixedHeightDynamicWidth re-derives width after an output resize, got " +
             std::to_string(w) + "x" + std::to_string(h));

        // While a render target is bound, the dynamic backbuffer must not be touched -- checked
        // by querying the viewport BEFORE unbinding (GetViewportSize's own refresh call must
        // see activeRenderTarget_ still set and skip). Then, once unbound, the deferred resize
        // must catch up on the very next query (eventual consistency, not a permanent stall).
        auto rt = renderer->CreateRenderTarget2D(4, 4, 0);
        rt->BindAsRenderTarget();
        renderer->DebugSetPresentationOutputSizeEXT(160, 40);
        renderer->Clear(0, 0, 0, 1);
        renderer->GetViewportSize(w, h);
        Check(w == 80, "FixedHeightDynamicWidth does not refresh while a RenderTarget2D is bound (still 80 from before), got " +
             std::to_string(w));
        rt->UnbindAsRenderTarget();
        renderer->GetViewportSize(w, h);
        Check(w == 160, "FixedHeightDynamicWidth catches up to the pending output-size change once unbound, got " +
             std::to_string(w));

        renderer->DebugClearPresentationOutputSizeEXT();
        SDL_DestroyWindow(window);
    }
} // namespace

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    TestTextureStride();
    TestFlipOriginRotation();
    TestSourceRectangles();
    TestSamplerAddressModes();
    TestSamplerFilter();
    TestTransformMatrix();
    TestScissor();
    TestViewport();
    TestBlendPresets();
    TestPrgb32Invariant();
    TestBlendCorrectionEdgeCoverage();
    TestColorWriteChannels();
    TestTintChain();
    TestRenderTargets();
    TestCrossRendererSafety();
    TestBeginEndStateMachine();
    TestCustomEffectRejection();
    TestPresentationModes();

    SDL_Quit();

    std::printf("=== %d/%d PASS ===\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
