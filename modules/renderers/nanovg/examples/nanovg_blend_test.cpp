// SPDX-License-Identifier: MS-PL
// Pixel-level proof of NANOVG's claimed BlendState support matrix: Opaque, AlphaBlend,
// NonPremultiplied AND Additive against known source/destination RGBA values (NANOVG genuinely
// implements Additive, unlike OPENVG -- see BlendStateToNvgCompositeOperation's own comment),
// and deterministic rejection of both an arbitrary custom BlendState and a non-default
// BlendState.ColorWriteChannels (NanoVG's own stencil-based fill implementation unconditionally
// resets glColorMask before every color pass -- see NanoVgRenderer::ApplyBlendState's own
// comment -- so this renderer honestly rejects a write mask instead of silently ignoring it).
//
// The Opaque check uses a fully-opaque (alpha=255) source deliberately: nanovg_gl.h's own
// fragment shader always premultiplies an RGBA image's sampled colour by its own alpha
// (`if (texType == 1) color = vec4(color.xyz*color.w,color.w);`, texType==1 whenever the image
// was NOT created with NVG_IMAGE_PREMULTIPLIED, which none of NanoVgTextureRenderer's images
// are) -- independent of which nvgGlobalCompositeOperation is active. For NVG_SOURCE_OVER this is
// exactly correct (premultiplied-then-over reproduces straight-alpha-over algebraically, which is
// why the NonPremultiplied/AlphaBlend checks below use a translucent source and still pass), but
// for NVG_COPY (Opaque's real mapping) there is no compensating blend factor, so a genuinely
// translucent source would show alpha-attenuated colour rather than the full un-multiplied
// source colour a straight-alpha renderer would show -- a real, documented deviation
// (docs/nanovg-renderer.md), not exercised by this specific pixel check.
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

#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>
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
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass; else ++fail;
    }

    // Raw XNA Blend/BlendFunction ordinals -- matches BlendStateToNvgCompositeOperation's own table.
    constexpr int kOne = 0, kZero = 1, kSrcAlpha = 4, kInvSrcAlpha = 5, kAdd = 0;

    Color ReadPixel(NanoVgRenderer& renderer, int x, int y)
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
        SDL_Window* window = SDL_CreateWindow("nanovg-blend-test", 100, 100, SDL_WINDOW_OPENGL);
        if (!window) throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        {
            SdlTestGlContext glContext(window);
            NanoVgRenderer renderer(SdlTestRendererArgs(
                window, &glContext, nullptr, 0, 0, CnaPresentationMode::NativeBackBuffer));

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

        // Opaque: NVG_COPY replaces the destination entirely. Uses a SEPARATE fully-opaque source
        // texture -- see this file's own top comment for why kSrc's alpha=128 would exercise a
        // real, documented shader-premultiplication deviation here instead of proving the
        // "destination fully replaced" property this check exists for.
        auto opaqueTex = renderer.CreateTexture(Solid(4, 4, Color(255, 0, 0, 255)));
        renderer.ApplyBlendState(kOne, kOne, kZero, kZero, kAdd, kAdd, BlendWriteState{});
        renderer.Clear(kDst.getRProperty() / 255.0f, kDst.getGProperty() / 255.0f,
                       kDst.getBProperty() / 255.0f, 1.0f);
        sb->Begin();
        sb->Draw(*opaqueTex, Rectangle(0, 0, 40, 40), Rectangle(0, 0, 4, 4), Color::White, 0.0f,
                Vector2(0, 0), SpriteEffects::None, 0.0f);
        sb->End();
        Check(CloseTo(ReadPixel(renderer, 20, 20), Color(255, 0, 0, 255)),
              "Opaque: destination fully replaced by the source colour");

        // NonPremultiplied: real NVG_SOURCE_OVER -- srcRGB=GL_ONE... wait, XNA's
        // NonPremultiplied uses SourceBlend=SrcAlpha; the composite still resolves to the same
        // real straight-alpha-over pixel result for this renderer's always-straight textures.
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

        // AlphaBlend: mapped to the same NVG_SOURCE_OVER (straight-source caveat, same reasoning
        // OpenVgRenderer documents) -- produces the SAME visible pixel as NonPremultiplied here.
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

        // Additive: NVG_LIGHTER is a real (GL_ONE, GL_ONE) glBlendFuncSeparate -- a genuine
        // capability edge over OPENVG (ShivaVG declares but never implements this).
        // Expected = min(255, src*srcA + dst).
        renderer.ApplyBlendState(kSrcAlpha, kSrcAlpha, kOne, kOne, kAdd, kAdd, BlendWriteState{});
        drawOnBlueBackground();
        {
            const float a = kSrc.getAProperty() / 255.0f;
            const Color expected(
                std::min(255, static_cast<int>(kSrc.getRProperty() * a) + kDst.getRProperty()),
                std::min(255, static_cast<int>(kSrc.getGProperty() * a) + kDst.getGProperty()),
                std::min(255, static_cast<int>(kSrc.getBProperty() * a) + kDst.getBProperty()), 255);
            Check(CloseTo(ReadPixel(renderer, 20, 20), expected, /*tol=*/16),
                  "Additive: real (GL_ONE, GL_ONE) blend matches the computed expectation");
        }

        // Arbitrary custom BlendState combination: no generic blend-factor/equation model exists.
        bool customThrew = false;
        try { renderer.ApplyBlendState(2, 2, 3, 3, kAdd, kAdd, BlendWriteState{}); }
        catch (const std::runtime_error&) { customThrew = true; }
        Check(customThrew, "an arbitrary custom BlendState combination is rejected deterministically");

        // ColorWriteChannels: a non-default write mask is rejected deterministically -- NanoVG's
        // own stencil-based fill implementation unconditionally resets glColorMask to
        // all-channels-enabled before every color pass (nanovg_gl.h's glnvg__fill/
        // glnvg__convexFill), so an externally-set mask cannot survive a single draw. See
        // NanoVgRenderer::ApplyBlendState's own comment.
        {
            BlendWriteState maskGreenBlueOnly;
            maskGreenBlueOnly.colorWriteChannels[0] = 0x6; // G|B, R excluded (bit0=R,1=G,2=B,3=A)
            bool writeMaskThrew = false;
            try { renderer.ApplyBlendState(kOne, kOne, kZero, kZero, kAdd, kAdd, maskGreenBlueOnly); }
            catch (const std::runtime_error&) { writeMaskThrew = true; }
            Check(writeMaskThrew,
                  "a non-default ColorWriteChannels mask is rejected deterministically "
                  "(NanoVG always resets glColorMask before its own color pass)");
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
