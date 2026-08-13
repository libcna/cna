// SPDX-License-Identifier: MS-PL
// Decisive pixel oracle for OpenVgTextureRenderer's upload format/orientation (P1-3) and
// OpenVgSpriteBatchRenderer's out-of-bounds Clamp source-rectangle handling (P1-4).
//
// Uses a 3x2 asymmetric texture where every texel has a distinct, unmistakable RGBA value and the
// top/bottom rows are unmistakably different -- proven correct across upload, UpdatePixels, a
// partial source rectangle, destination scaling, origin, rotation, both SpriteEffects flips, and
// alpha/tint, then a real edge-clamped out-of-bounds Clamp draw is checked pixel-by-pixel.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = SKIPPED (no GPU/display).

#include "CNA/Internal/Renderers/OpenVg/OpenVgRenderer.hpp"
#include "CNA/Internal/Renderers/OpenVg/OpenVgSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/OpenVg/OpenVgTextureRenderer.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
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
using namespace CNA::Internal::Renderers::OpenVg;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Graphics::ImageData;
using CNA::Examples::SdlTestGlContext;
using CNA::Examples::SdlTestRendererArgs;

namespace
{
    int pass = 0, fail = 0;
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass; else ++fail;
    }

    // 3x2, top-row-first RGBA: every texel distinct. All fully opaque -- this file's job is
    // proving format/orientation/addressing, not blending (BlendState.Opaque is selected below so
    // every sampled pixel is the pure, unblended texel colour; alpha/tint compositing has its own
    // dedicated blend pixel-oracle coverage in openvg_blend_test.cpp).
    //   row0 (top):    (255,0,0,255)   (0,255,0,255)   (0,0,255,255)
    //   row1 (bottom): (255,255,0,255) (0,255,255,255) (255,0,255,255)
    constexpr float kPiOver2 = 1.57079632679489661923f;
    const Color kTL(255, 0, 0, 255), kTM(0, 255, 0, 255), kTR(0, 0, 255, 255);
    const Color kBL(255, 255, 0, 255), kBM(0, 255, 255, 255), kBR(255, 0, 255, 255);

    std::vector<uint8_t> AsymmetricPixels()
    {
        const Color row0[3] = { kTL, kTM, kTR };
        const Color row1[3] = { kBL, kBM, kBR };
        std::vector<uint8_t> pixels(3 * 2 * 4);
        for (int x = 0; x < 3; ++x)
        {
            const Color& c0 = row0[x];
            pixels[(0 * 3 + x) * 4 + 0] = static_cast<uint8_t>(c0.getRProperty());
            pixels[(0 * 3 + x) * 4 + 1] = static_cast<uint8_t>(c0.getGProperty());
            pixels[(0 * 3 + x) * 4 + 2] = static_cast<uint8_t>(c0.getBProperty());
            pixels[(0 * 3 + x) * 4 + 3] = static_cast<uint8_t>(c0.getAProperty());
            const Color& c1 = row1[x];
            pixels[(1 * 3 + x) * 4 + 0] = static_cast<uint8_t>(c1.getRProperty());
            pixels[(1 * 3 + x) * 4 + 1] = static_cast<uint8_t>(c1.getGProperty());
            pixels[(1 * 3 + x) * 4 + 2] = static_cast<uint8_t>(c1.getBProperty());
            pixels[(1 * 3 + x) * 4 + 3] = static_cast<uint8_t>(c1.getAProperty());
        }
        return pixels;
    }

    Color ReadPixel(OpenVgRenderer& renderer, int x, int y)
    {
        uint8_t rgba[4] = {0, 0, 0, 0};
        renderer.ReadBackbuffer(x, y, 1, 1, rgba);
        return Color(rgba[0], rgba[1], rgba[2], rgba[3]);
    }

    bool CloseTo(const Color& a, const Color& b, int tol = 12)
    {
        const auto d = [](int x, int y) { return x > y ? x - y : y - x; };
        return d(a.getRProperty(), b.getRProperty()) <= tol &&
               d(a.getGProperty(), b.getGProperty()) <= tol &&
               d(a.getBProperty(), b.getBProperty()) <= tol;
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
        SDL_Window* window = SDL_CreateWindow("openvg-texture-orientation", 300, 200, SDL_WINDOW_OPENGL);
        if (!window) throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        {
            SdlTestGlContext glContext(window);
            OpenVgRenderer renderer(SdlTestRendererArgs(
                window, &glContext, nullptr, 0, 0, CnaPresentationMode::NativeBackBuffer));
        // Opaque (blending disabled): every readback below is the pure, unblended texel colour.
        renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, BlendWriteState{});

        ImageData img;
        img.width = 3; img.height = 2;
        img.pixels = AsymmetricPixels();
        auto tex = renderer.CreateTexture(img);
        auto sb = renderer.CreateSpriteBatch();
        // Point (nearest) filtering: this file asserts EXACT per-texel colours at specific pixel
        // coordinates, so eliminate GL_LINEAR's edge-blending between adjacent texels (a real,
        // separate, correctly-working feature -- just not what a format/orientation/addressing
        // oracle wants to measure).
        sb->SetSamplerFilter(/*Point=*/1);

        const Color kClear(20, 20, 20, 255);
        const auto clearAndDraw = [&](const Rectangle& dest, const Rectangle& src, const Color& color,
                                      float rotation, const Vector2& origin, SpriteEffects fx)
        {
            renderer.Clear(kClear.getRProperty() / 255.0f, kClear.getGProperty() / 255.0f,
                           kClear.getBProperty() / 255.0f, 1.0f);
            sb->Begin();
            sb->Draw(*tex, dest, src, color, rotation, origin, fx, 0.0f);
            sb->End();
        };

        // 1) Whole-texture upload, scaled 30x/30x (90x60 on screen) at (0,0): four corners land at
        //    predictable screen pixels, each texel is 30x30 physical pixels.
        clearAndDraw(Rectangle(0, 0, 90, 60), Rectangle(0, 0, 3, 2), Color::White, 0.0f, Vector2(0, 0), SpriteEffects::None);
        Check(CloseTo(ReadPixel(renderer, 5, 5), kTL), "upload: top-left texel (row0,col0) lands top-left on screen");
        Check(CloseTo(ReadPixel(renderer, 45, 5), kTM), "upload: top-middle texel is distinct and correctly placed");
        Check(CloseTo(ReadPixel(renderer, 85, 5), kTR), "upload: top-right texel is distinct and correctly placed");
        Check(CloseTo(ReadPixel(renderer, 5, 55), kBL), "upload: bottom-left texel lands bottom-left -- proves row orientation");
        Check(CloseTo(ReadPixel(renderer, 45, 55), kBM), "upload: bottom-middle texel is distinct and correctly placed");
        Check(CloseTo(ReadPixel(renderer, 85, 55), kBR), "upload: bottom-right texel is distinct and correctly placed");

        // 2) UpdatePixels: swap row0/row1 (vertically flip the CPU-side data) and re-check.
        {
            std::vector<uint8_t> swapped(3 * 2 * 4);
            const auto& orig = img.pixels;
            for (int i = 0; i < 3 * 4; ++i)
            {
                swapped[i] = orig[3 * 4 + i];
                swapped[3 * 4 + i] = orig[i];
            }
            tex->UpdatePixels(swapped.data(), 3 * 4);
            clearAndDraw(Rectangle(0, 0, 90, 60), Rectangle(0, 0, 3, 2), Color::White, 0.0f, Vector2(0, 0), SpriteEffects::None);
            Check(CloseTo(ReadPixel(renderer, 5, 5), kBL) && CloseTo(ReadPixel(renderer, 5, 55), kTL),
                  "UpdatePixels: a real re-upload changes what's on screen, in the correct row order");
            tex->UpdatePixels(img.pixels.data(), 3 * 4); // restore
        }

        // 3) Partial source rectangle: just the top-right texel (1x1 at (2,0)), scaled to 40x40.
        clearAndDraw(Rectangle(10, 10, 40, 40), Rectangle(2, 0, 1, 1), Color::White, 0.0f, Vector2(0, 0), SpriteEffects::None);
        Check(CloseTo(ReadPixel(renderer, 30, 30), kTR), "partial sourceRectangle: extracts exactly the requested texel");

        // 4) Alpha/tint: VG_DRAW_IMAGE_MULTIPLY multiplies the SAMPLED texel by the tint colour --
        //    proven cleanly against a solid WHITE 1x1 texture (white * tint == tint), independent
        //    of the asymmetric palette above.
        {
            ImageData whiteImg; whiteImg.width = 1; whiteImg.height = 1;
            whiteImg.pixels = {255, 255, 255, 255};
            auto whiteTex = renderer.CreateTexture(whiteImg);
            renderer.Clear(kClear.getRProperty() / 255.0f, kClear.getGProperty() / 255.0f,
                           kClear.getBProperty() / 255.0f, 1.0f);
            sb->Begin();
            sb->Draw(*whiteTex, Rectangle(0, 0, 30, 30), Rectangle(0, 0, 1, 1),
                    Color(40, 220, 60, 255), 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
            sb->End();
            Check(CloseTo(ReadPixel(renderer, 10, 10), Color(40, 220, 60, 255), /*tol=*/16),
                  "tint: VG_DRAW_IMAGE_MULTIPLY against a white texel reproduces the tint colour exactly");
        }

        // 5) Rotation (90 degrees) + origin at the sprite's own centre: top-left texel rotates to
        //    the top-right quadrant (clockwise-positive, Y-down XNA convention).
        clearAndDraw(Rectangle(100, 100, 60, 40), Rectangle(0, 0, 3, 2), Color::White,
                    kPiOver2, Vector2(1.5f, 1.0f), SpriteEffects::None);
        Check(CloseTo(ReadPixel(renderer, 112, 76), kTL),
              "rotation: top-left texel rotates 90deg clockwise around the sprite centre");

        // 6) SpriteEffects::FlipHorizontally: top-left and top-right texels swap screen columns.
        clearAndDraw(Rectangle(0, 0, 90, 60), Rectangle(0, 0, 3, 2), Color::White, 0.0f, Vector2(0, 0), SpriteEffects::FlipHorizontally);
        Check(CloseTo(ReadPixel(renderer, 5, 5), kTR) && CloseTo(ReadPixel(renderer, 85, 5), kTL),
              "SpriteEffects::FlipHorizontally swaps left/right texels");

        // 7) SpriteEffects::FlipVertically: top-left and bottom-left texels swap screen rows.
        clearAndDraw(Rectangle(0, 0, 90, 60), Rectangle(0, 0, 3, 2), Color::White, 0.0f, Vector2(0, 0), SpriteEffects::FlipVertically);
        Check(CloseTo(ReadPixel(renderer, 5, 5), kBL) && CloseTo(ReadPixel(renderer, 5, 55), kTL),
              "SpriteEffects::FlipVertically swaps top/bottom texels");

        // 8) Out-of-bounds Clamp (P1-4): sourceRectangle extends 2 texels past the right edge --
        //    those columns must repeat the RIGHTMOST valid column (kTR/kBR), not read garbage, not
        //    crop the draw smaller than requested.
        clearAndDraw(Rectangle(0, 0, 100, 40), Rectangle(0, 0, 5, 2), Color::White, 0.0f, Vector2(0, 0), SpriteEffects::None);
        // Columns: [0,1)=texel0(20px), [1,2)=texel1, [2,5)=clamped-to-texel2 (60px total).
        Check(CloseTo(ReadPixel(renderer, 10, 10), kTL), "OOB Clamp (right): in-bounds column 0 unaffected");
        Check(CloseTo(ReadPixel(renderer, 30, 10), kTM), "OOB Clamp (right): in-bounds column 1 unaffected");
        Check(CloseTo(ReadPixel(renderer, 50, 10), kTR), "OOB Clamp (right): first out-of-bounds column repeats the edge texel");
        Check(CloseTo(ReadPixel(renderer, 90, 10), kTR), "OOB Clamp (right): far out-of-bounds column still repeats the edge texel");
        Check(CloseTo(ReadPixel(renderer, 10, 30), kBL) && CloseTo(ReadPixel(renderer, 90, 30), kBR),
              "OOB Clamp (right): bottom row clamps independently, matching its own edge texel");

        // 9) Out-of-bounds Clamp on the left/top simultaneously.
        clearAndDraw(Rectangle(0, 0, 90, 60), Rectangle(-1, -1, 4, 3), Color::White, 0.0f, Vector2(0, 0), SpriteEffects::None);
        // Destination columns of width 90/4=22.5px each; row0 (dest y 0..20) repeats source row -1
        // (clamped to row0), row1 (dest y 20..40) is real row0, row2 (dest y 40..60) is real row1.
        Check(CloseTo(ReadPixel(renderer, 34, 10), kTL), "OOB Clamp (left/top): repeats the corner texel into the padded region");
        Check(CloseTo(ReadPixel(renderer, 34, 30), kTL), "OOB Clamp (left/top): the real top row is still reachable one step in");

        // 10) Wrap/Mirror + out-of-bounds must be rejected deterministically, not silently cropped.
        sb->SetSamplerAddressMode(/*Wrap=*/0, /*Wrap=*/0);
        bool threw = false;
        try
        {
            sb->Begin();
            sb->Draw(*tex, Rectangle(0, 0, 90, 60), Rectangle(0, 0, 5, 2), Color::White, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
            sb->End();
        }
        catch (const std::runtime_error&) { threw = true; }
        Check(threw, "OOB Wrap/Mirror TextureAddressMode is rejected, not silently mis-rendered");

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
