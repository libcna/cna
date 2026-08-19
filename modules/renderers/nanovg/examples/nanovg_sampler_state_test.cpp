// SPDX-License-Identifier: MS-PL
// Adversarial proof that NANOVG honours the SamplerState a SpriteBatch was begun with, rather
// than whatever filter/wrap mode its textures happened to be created with.
//
// The distinction is deliberately made unmissable: every sample point below is chosen so that
// point and linear filtering, or clamp and wrap and mirror addressing, produce DIFFERENT values
// there. A test that only samples texel centres (or only in-bounds texels) passes identically on
// a renderer that ignores SamplerState entirely, which is exactly what this renderer used to do:
// SetSamplerFilter was an empty no-op and every image was created linear-filtered and
// clamp-addressed.
//
// Sampling model the expectations are derived from (plain GL texture sampling, which is what
// nvgImagePattern's paint ultimately performs):
//   u          = destinationPixelCentre / destinationWidth   (for a full-width source rectangle)
//   point      = texel[floor(u * texelCount)]
//   linear     = lerp(texel[floor(t)], texel[floor(t)+1], frac(t)),  t = u * texelCount - 0.5,
//                with t clamped into [0, texelCount-1] under TextureAddressMode.Clamp.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = SKIPPED (no GPU/display).

#include "CNA/Internal/Renderers/NanoVg/NanoVgRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgTextureRenderer.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"

#include <SDL3/SDL.h>

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

    // Raw TextureFilter ordinals (Microsoft/Xna/Framework/Graphics/TextureFilter.hpp).
    constexpr int kLinear = 0, kPoint = 1, kAnisotropic = 2, kLinearMipPoint = 3,
                  kPointMipLinear = 4, kMinLinearMagPoint = 5, kMinPointMagLinear = 7;
    // Raw TextureAddressMode ordinals.
    constexpr int kWrap = 0, kClamp = 1, kMirror = 2;

    /// GPU linear filtering is only required to carry 8 bits of sub-texel precision, so a weight
    /// may land one 8-bit step either side of the exact value; the differences under test are
    /// 70-250 steps wide, so this cannot mask any of them.
    constexpr int kTolerance = 4;

    ImageData FromTexels(int w, int h, const std::vector<Color>& texels)
    {
        ImageData data;
        data.width = w;
        data.height = h;
        data.pixels.resize(static_cast<std::size_t>(w) * h * 4);
        for (std::size_t i = 0; i < texels.size(); ++i)
        {
            data.pixels[i * 4 + 0] = static_cast<uint8_t>(texels[i].getRProperty());
            data.pixels[i * 4 + 1] = static_cast<uint8_t>(texels[i].getGProperty());
            data.pixels[i * 4 + 2] = static_cast<uint8_t>(texels[i].getBProperty());
            data.pixels[i * 4 + 3] = static_cast<uint8_t>(texels[i].getAProperty());
        }
        return data;
    }

    std::string Describe(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + ")";
    }

    bool CloseTo(const Color& a, const Color& b, int tol)
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
        SDL_Window* window = SDL_CreateWindow("nanovg-sampler-state", 200, 120, SDL_WINDOW_OPENGL);
        if (!window) throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        {
            SdlTestGlContext glContext(window);
            NanoVgRenderer renderer(SdlTestRendererArgs(
                window, &glContext, nullptr, 0, 0, CnaPresentationMode::NativeBackBuffer));
            // Opaque: BlendState.Opaque is (One, Zero) on both channels, so every readback below
            // is the sampled texel itself, with nothing composited on top of it.
            renderer.ApplyBlendState(/*colorSrc*/0, /*alphaSrc*/0, /*colorDst*/1, /*alphaDst*/1,
                                     /*colorFunc*/0, /*alphaFunc*/0, BlendWriteState{});

            const auto readPixel = [&renderer](int x, int y)
            {
                uint8_t rgba[4] = {0, 0, 0, 0};
                renderer.ReadBackbuffer(x, y, 1, 1, rgba);
                return Color(rgba[0], rgba[1], rgba[2], rgba[3]);
            };

            auto sprites = renderer.CreateSpriteBatch();

            // ---- Point vs linear on a 2x1 black/white ramp ---------------------------------
            // 40px wide destination, so each texel owns 20 physical pixels and the two filters
            // disagree by 70-100 steps at the sample points below.
            const Color kBlack(0, 0, 0, 255), kWhite(255, 255, 255, 255);
            auto ramp = renderer.CreateTexture(FromTexels(2, 1, {kBlack, kWhite}));

            const auto drawRamp = [&](int filter)
            {
                renderer.Clear(0.0f, 0.25f, 0.0f, 1.0f);
                sprites->SetSamplerFilter(filter);
                sprites->SetSamplerAddressMode(kClamp, kClamp);
                sprites->Begin();
                sprites->Draw(*ramp, Rectangle(0, 0, 40, 20), Rectangle(0, 0, 2, 1), Color::White,
                              0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
                sprites->End();
            };

            // Point: two flat plateaus with a hard step at the texel boundary (x = 20).
            drawRamp(kPoint);
            const Color point5 = readPixel(5, 10), point15 = readPixel(15, 10);
            const Color point25 = readPixel(25, 10), point35 = readPixel(35, 10);
            Check(CloseTo(point5, kBlack, kTolerance) && CloseTo(point15, kBlack, kTolerance),
                  "PointClamp: the whole left half is the nearest texel, flat -- got " +
                      Describe(point5) + " and " + Describe(point15));
            Check(CloseTo(point25, kWhite, kTolerance) && CloseTo(point35, kWhite, kTolerance),
                  "PointClamp: the whole right half is the nearest texel, flat -- got " +
                      Describe(point25) + " and " + Describe(point35));
            Check(CloseTo(readPixel(19, 10), kBlack, kTolerance) &&
                      CloseTo(readPixel(20, 10), kWhite, kTolerance),
                  "PointClamp: the texel boundary is a hard step, not a ramp");

            // Linear: t = (x+0.5)/20 - 0.5, clamped; value = 255*clamp(t,0,1).
            drawRamp(kLinear);
            const Color linear5 = readPixel(5, 10), linear15 = readPixel(15, 10);
            const Color linear25 = readPixel(25, 10), linear35 = readPixel(35, 10);
            Check(CloseTo(linear5, kBlack, kTolerance),
                  "LinearClamp: outside the first texel centre the clamp is flat -- got " +
                      Describe(linear5));
            Check(CloseTo(linear15, Color(70, 70, 70, 255), kTolerance),
                  "LinearClamp: interpolates between texel centres (expected ~70) -- got " +
                      Describe(linear15));
            Check(CloseTo(linear25, Color(197, 197, 197, 255), kTolerance),
                  "LinearClamp: interpolates between texel centres (expected ~197) -- got " +
                      Describe(linear25));
            Check(CloseTo(linear35, kWhite, kTolerance),
                  "LinearClamp: past the last texel centre the clamp is flat -- got " +
                      Describe(linear35));
            Check(!CloseTo(point15, linear15, 20) && !CloseTo(point25, linear25, 20),
                  "PointClamp and LinearClamp produce observably different images");

            // Separate minification and magnification components really are separate: GL stores
            // GL_TEXTURE_MIN_FILTER and GL_TEXTURE_MAG_FILTER independently, and the draw above
            // magnifies, so a MinLinearMagPoint request must look like Point and a
            // MinPointMagLinear request must look like Linear.
            drawRamp(kMinLinearMagPoint);
            Check(CloseTo(readPixel(15, 10), kBlack, kTolerance),
                  "MinLinearMagPointMipLinear magnifies with point sampling");
            drawRamp(kMinPointMagLinear);
            Check(CloseTo(readPixel(15, 10), Color(70, 70, 70, 255), kTolerance),
                  "MinPointMagLinearMipLinear magnifies with linear sampling");
            drawRamp(kLinearMipPoint);
            Check(CloseTo(readPixel(15, 10), Color(70, 70, 70, 255), kTolerance),
                  "LinearMipPoint magnifies linearly (the mip component is inert: one level)");
            drawRamp(kPointMipLinear);
            Check(CloseTo(readPixel(15, 10), kBlack, kTolerance),
                  "PointMipLinear magnifies with point sampling (the mip component is inert)");

            // ---- The same texture, two consecutive batches, two different filters -----------
            // A design that baked the filter into the image at creation time, or that mutated
            // shared texture state without re-asserting it per batch, fails here.
            drawRamp(kPoint);
            const Color firstPoint = readPixel(15, 10);
            drawRamp(kLinear);
            const Color thenLinear = readPixel(15, 10);
            drawRamp(kPoint);
            const Color pointAgain = readPixel(15, 10);
            Check(CloseTo(firstPoint, kBlack, kTolerance) &&
                      CloseTo(thenLinear, Color(70, 70, 70, 255), kTolerance) &&
                      CloseTo(pointAgain, kBlack, kTolerance),
                  "the SAME texture drawn Point -> Linear -> Point follows the batch, not the "
                  "texture -- got " + Describe(firstPoint) + ", " + Describe(thenLinear) + ", " +
                      Describe(pointAgain));

            // Two textures in ONE batch both receive that batch's sampler state.
            {
                auto secondRamp = renderer.CreateTexture(FromTexels(2, 1, {kWhite, kBlack}));
                renderer.Clear(0.0f, 0.25f, 0.0f, 1.0f);
                sprites->SetSamplerFilter(kPoint);
                sprites->SetSamplerAddressMode(kClamp, kClamp);
                sprites->Begin();
                sprites->Draw(*ramp, Rectangle(0, 0, 40, 20), Rectangle(0, 0, 2, 1), Color::White,
                              0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
                sprites->Draw(*secondRamp, Rectangle(0, 20, 40, 20), Rectangle(0, 0, 2, 1),
                              Color::White, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
                sprites->End();
                Check(CloseTo(readPixel(15, 10), kBlack, kTolerance) &&
                          CloseTo(readPixel(15, 30), kWhite, kTolerance),
                      "two different textures in one batch both use that batch's Point filter");
            }

            // ---- Address modes on an out-of-bounds source rectangle -------------------------
            // Source rectangle (0,0,6,1) over a 3-texel texture: destination columns 3..5 are past
            // the right edge, so each address mode resolves them differently. Point-filtered so a
            // seam cannot smear one expectation into another.
            const Color kR(255, 0, 0, 255), kG(0, 255, 0, 255), kB(0, 0, 255, 255);
            auto rgb = renderer.CreateTexture(FromTexels(3, 1, {kR, kG, kB}));

            const auto drawWide = [&](int addressU)
            {
                renderer.Clear(0.1f, 0.1f, 0.1f, 1.0f);
                sprites->SetSamplerFilter(kPoint);
                sprites->SetSamplerAddressMode(addressU, kClamp);
                sprites->Begin();
                sprites->Draw(*rgb, Rectangle(0, 0, 60, 20), Rectangle(0, 0, 6, 1), Color::White,
                              0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
                sprites->End();
            };

            drawWide(kClamp);
            Check(CloseTo(readPixel(5, 10), kR, kTolerance) &&
                      CloseTo(readPixel(15, 10), kG, kTolerance) &&
                      CloseTo(readPixel(25, 10), kB, kTolerance) &&
                      CloseTo(readPixel(35, 10), kB, kTolerance) &&
                      CloseTo(readPixel(55, 10), kB, kTolerance),
                  "TextureAddressMode.Clamp repeats the edge texel past the source rectangle");

            drawWide(kWrap);
            Check(CloseTo(readPixel(35, 10), kR, kTolerance) &&
                      CloseTo(readPixel(45, 10), kG, kTolerance) &&
                      CloseTo(readPixel(55, 10), kB, kTolerance),
                  "TextureAddressMode.Wrap tiles the texture past the source rectangle -- got " +
                      Describe(readPixel(35, 10)) + ", " + Describe(readPixel(45, 10)) + ", " +
                      Describe(readPixel(55, 10)));

            drawWide(kMirror);
            Check(CloseTo(readPixel(35, 10), kB, kTolerance) &&
                      CloseTo(readPixel(45, 10), kG, kTolerance) &&
                      CloseTo(readPixel(55, 10), kR, kTolerance),
                  "TextureAddressMode.Mirror flips each repeat past the source rectangle -- got " +
                      Describe(readPixel(35, 10)) + ", " + Describe(readPixel(45, 10)) + ", " +
                      Describe(readPixel(55, 10)));

            // U and V address modes are independent: wrap horizontally, clamp vertically.
            {
                auto column = renderer.CreateTexture(FromTexels(1, 2, {kR, kG}));
                renderer.Clear(0.1f, 0.1f, 0.1f, 1.0f);
                sprites->SetSamplerFilter(kPoint);
                sprites->SetSamplerAddressMode(kWrap, kClamp);
                sprites->Begin();
                sprites->Draw(*column, Rectangle(0, 0, 20, 80), Rectangle(0, 0, 1, 4),
                              Color::White, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
                sprites->End();
                Check(CloseTo(readPixel(10, 10), kR, kTolerance) &&
                          CloseTo(readPixel(10, 30), kG, kTolerance) &&
                          CloseTo(readPixel(10, 50), kG, kTolerance) &&
                          CloseTo(readPixel(10, 70), kG, kTolerance),
                      "addressV=Clamp is honoured independently of addressU=Wrap");
            }

            // ---- Deterministic rejection of what cannot be represented ----------------------
            const auto expectRejected = [&](const std::string& label, const std::string& fragment,
                                            auto&& call)
            {
                bool threw = false;
                std::string message;
                try { call(); }
                catch (const std::runtime_error& ex) { threw = true; message = ex.what(); }
                Check(threw && message.find(fragment) != std::string::npos,
                      label + (threw ? (": rejected with \"" + message + "\"") : ": NOT rejected"));
            };

            expectRejected("TextureFilter.Anisotropic", "Anisotropic",
                           [&] { sprites->SetSamplerFilter(kAnisotropic); });
            expectRejected("an out-of-range TextureFilter ordinal", "TextureFilter",
                           [&] { sprites->SetSamplerFilter(99); });
            expectRejected("an out-of-range TextureAddressMode ordinal", "TextureAddressMode",
                           [&] { sprites->SetSamplerAddressMode(kClamp, 7); });

            // A rejected sampler request must leave the previously accepted one intact.
            drawRamp(kPoint);
            Check(CloseTo(readPixel(15, 10), kBlack, kTolerance),
                  "a rejected SamplerState does not corrupt the next accepted batch");
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
