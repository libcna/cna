// SPDX-License-Identifier: MS-PL
// Pixel-level proof of OpenVG's claimed BlendState support matrix (P1-7): Opaque, AlphaBlend and
// NonPremultiplied against known source/destination RGBA values, deterministic rejection of
// Additive and an arbitrary custom BlendState (P1-6/P1-7), and BlendState.ColorWriteChannels
// honored via a real glColorMask (P1-5).
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

#include <cstdio>
#include <memory>
#include <stdexcept>
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

    // Raw XNA Blend/BlendFunction ordinals -- matches BlendStateToVgBlendMode's own table.
    constexpr int kOne = 0, kZero = 1, kSrcAlpha = 4, kInvSrcAlpha = 5, kAdd = 0;

    Color ReadPixel(OpenVgRenderer& renderer, int x, int y)
    {
        uint8_t rgba[4] = {0, 0, 0, 0};
        renderer.ReadBackbuffer(x, y, 1, 1, rgba);
        return Color(rgba[0], rgba[1], rgba[2], rgba[3]);
    }

    bool CloseTo(const Color& a, const Color& b, int tol = 10)
    {
        const auto d = [](int x, int y) { return x > y ? x - y : y - x; };
        return d(a.getRProperty(), b.getRProperty()) <= tol &&
               d(a.getGProperty(), b.getGProperty()) <= tol &&
               d(a.getBProperty(), b.getBProperty()) <= tol;
    }

    ImageData Solid(int w, int h, const Color& c)
    {
        ImageData data; data.width = w; data.height = h;
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
        SDL_Window* window = SDL_CreateWindow("openvg-blend-test", 100, 100, SDL_WINDOW_OPENGL);
        if (!window) throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        {
            SdlTestGlContext glContext(window);
            OpenVgRenderer renderer(SdlTestRendererArgs(
                window, &glContext, nullptr, 0, 0, CnaPresentationMode::NativeBackBuffer));

        // Source: straight (non-premultiplied) 50% red, matching this renderer's own always-
        // straight-alpha ImageData convention.
        const Color kSrc(255, 0, 0, 128);
        const Color kDst(0, 0, 255, 255); // opaque blue background
        auto tex = renderer.CreateTexture(Solid(4, 4, kSrc));
        auto sb = renderer.CreateSpriteBatch();

        const auto drawOnBlueBackground = [&]
        {
            renderer.Clear(kDst.getRProperty() / 255.0f, kDst.getGProperty() / 255.0f,
                           kDst.getBProperty() / 255.0f, 1.0f);
            sb->Begin();
            sb->Draw(*tex, Rectangle(0, 0, 40, 40), Rectangle(0, 0, 4, 4), Color::White, 0.0f,
                    Vector2(0, 0), SpriteEffects::None, 0.0f);
            sb->End();
        };

        // Opaque: ShivaVG's VG_BLEND_SRC disables blending -- the destination is fully replaced,
        // alpha ignored entirely.
        renderer.ApplyBlendState(kOne, kOne, kZero, kZero, kAdd, kAdd, BlendWriteState{});
        drawOnBlueBackground();
        Check(CloseTo(ReadPixel(renderer, 20, 20), Color(255, 0, 0, 255)),
              "Opaque: destination fully replaced by the source colour, alpha ignored");

        // NonPremultiplied: real glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) straight-over.
        // Expected = src*srcA + dst*(1-srcA), srcA=128/255~=0.502.
        renderer.ApplyBlendState(kSrcAlpha, kSrcAlpha, kInvSrcAlpha, kInvSrcAlpha, kAdd, kAdd, BlendWriteState{});
        drawOnBlueBackground();
        {
            const float a = kSrc.getAProperty() / 255.0f;
            const Color expected(
                static_cast<int>(kSrc.getRProperty() * a + kDst.getRProperty() * (1 - a)),
                static_cast<int>(kSrc.getGProperty() * a + kDst.getGProperty() * (1 - a)),
                static_cast<int>(kSrc.getBProperty() * a + kDst.getBProperty() * (1 - a)), 255);
            Check(CloseTo(ReadPixel(renderer, 20, 20), expected, /*tol=*/16),
                  "NonPremultiplied: real straight-alpha-over composite matches the computed expectation");
        }

        // AlphaBlend: mapped to the same VG_BLEND_SRC_OVER (documented straight-source caveat) --
        // produces the SAME visible pixel as NonPremultiplied for this renderer's always-straight
        // textures.
        renderer.ApplyBlendState(kOne, kOne, kInvSrcAlpha, kInvSrcAlpha, kAdd, kAdd, BlendWriteState{});
        drawOnBlueBackground();
        {
            const float a = kSrc.getAProperty() / 255.0f;
            const Color expected(
                static_cast<int>(kSrc.getRProperty() * a + kDst.getRProperty() * (1 - a)),
                static_cast<int>(kSrc.getGProperty() * a + kDst.getGProperty() * (1 - a)),
                static_cast<int>(kSrc.getBProperty() * a + kDst.getBProperty() * (1 - a)), 255);
            Check(CloseTo(ReadPixel(renderer, 20, 20), expected, /*tol=*/16),
                  "AlphaBlend: matches the same straight-over composite for this renderer's straight-alpha textures");
        }

        // Additive: declared by OpenVG, never implemented by ShivaVG -- must be rejected, not
        // silently rendered as ordinary alpha blending.
        bool additiveThrew = false;
        try { renderer.ApplyBlendState(kSrcAlpha, kSrcAlpha, kOne, kOne, kAdd, kAdd, BlendWriteState{}); }
        catch (const std::runtime_error&) { additiveThrew = true; }
        Check(additiveThrew, "Additive is rejected deterministically (ShivaVG has no real additive path)");

        // Arbitrary custom BlendState combination: no generic blend-factor/equation model exists.
        bool customThrew = false;
        try { renderer.ApplyBlendState(2, 2, 3, 3, kAdd, kAdd, BlendWriteState{}); }
        catch (const std::runtime_error&) { customThrew = true; }
        Check(customThrew, "an arbitrary custom BlendState combination is rejected deterministically");

        // Restore Opaque so the write-mask checks below aren't confounded by blending.
        renderer.ApplyBlendState(kOne, kOne, kZero, kZero, kAdd, kAdd, BlendWriteState{});

        // ColorWriteChannels (P1-5): masking out Red must leave the destination's red channel
        // untouched while green/blue still take the source's values.
        {
            BlendWriteState maskGreenBlueOnly;
            maskGreenBlueOnly.colorWriteChannels[0] = 0x6; // G|B, R excluded (bit0=R,1=G,2=B,3=A)
            renderer.ApplyBlendState(kOne, kOne, kZero, kZero, kAdd, kAdd, maskGreenBlueOnly);
            const Color kBackground(9, 9, 9, 255);
            renderer.Clear(kBackground.getRProperty() / 255.0f, kBackground.getGProperty() / 255.0f,
                           kBackground.getBProperty() / 255.0f, 1.0f);
            auto whiteTex = renderer.CreateTexture(Solid(4, 4, Color(200, 150, 90, 255)));
            sb->Begin();
            sb->Draw(*whiteTex, Rectangle(0, 0, 40, 40), Rectangle(0, 0, 4, 4), Color::White, 0.0f,
                    Vector2(0, 0), SpriteEffects::None, 0.0f);
            sb->End();
            const Color got = ReadPixel(renderer, 20, 20);
            Check(got.getRProperty() <= kBackground.getRProperty() + 6,
                  "ColorWriteChannels masking Red: the red channel keeps its pre-draw value");
            Check(got.getGProperty() >= 140 && got.getBProperty() >= 80,
                  "ColorWriteChannels masking Red: green/blue still take the source's values");
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
