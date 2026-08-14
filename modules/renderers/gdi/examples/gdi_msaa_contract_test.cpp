// SPDX-License-Identifier: MS-PL
// GDI-073: exact contract for GDI's deliberately narrow backbuffer-only 4x CPU MSAA.

#include "CNA/GraphicsRendererType.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Gdi/GdiRenderer.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <vector>

using namespace CNA::Internal::Renderers;

namespace
{
    constexpr int kWidth = 8;
    constexpr int kHeight = 8;
    using Pixel = std::array<std::uint8_t, 4>;

    bool Expect(bool condition, const char* message)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
        return condition;
    }

    bool ExpectPixel(const Pixel& actual, const Pixel& expected, const char* message,
                     int tolerance = 0)
    {
        bool equal = true;
        for (std::size_t channel = 0; channel < actual.size(); ++channel)
        {
            equal &= std::abs(static_cast<int>(actual[channel]) -
                              static_cast<int>(expected[channel])) <= tolerance;
        }
        if (!equal)
        {
            std::fprintf(stderr,
                         "%s: expected RGBA(%u,%u,%u,%u), got RGBA(%u,%u,%u,%u)\n",
                         message, expected[0], expected[1], expected[2], expected[3],
                         actual[0], actual[1], actual[2], actual[3]);
        }
        return Expect(equal, message);
    }

    [[nodiscard]] Pixel ReadPixel(IGraphicsRenderer& renderer, int x, int y)
    {
        Pixel pixel{};
        renderer.ReadBackbuffer(x, y, 1, 1, pixel.data());
        return pixel;
    }

    void SetOpaqueMask(IGraphicsRenderer& renderer, unsigned int mask)
    {
        BlendWriteState writeState;
        writeState.multiSampleMask = mask;
        renderer.ApplyBlendState(
            /*One*/ 0, /*One*/ 0, /*Zero*/ 1, /*Zero*/ 1,
            /*Add*/ 0, /*Add*/ 0, writeState);
    }

    void SetStencil(IGraphicsRenderer& renderer, int compare, int pass, int fail,
                    int reference)
    {
        renderer.ApplyDepthStencilState(
            /*depthEnable*/ false, /*depthWriteEnable*/ false, /*LessEqual*/ 3,
            /*stencilEnable*/ true, compare, pass, fail, /*depthFail Keep*/ 0,
            /*readMask*/ 0xFF, /*writeMask*/ 0xFF, reference,
            /*twoSided*/ false, 0, 0, 0, 0);
    }

    void DisableStencil(IGraphicsRenderer& renderer)
    {
        renderer.ApplyDepthStencilState(
            false, false, /*LessEqual*/ 3, false, /*Always*/ 0,
            /*Keep*/ 0, /*Keep*/ 0, /*Keep*/ 0, 0xFF, 0xFF, 0,
            false, 0, 0, 0, 0);
    }

    void Draw(IGraphicsRenderer& renderer, const ITextureRenderer& texture,
              const Rectangle& destination, const Rectangle& source,
              float rotation = 0.0f,
              const Vector2& origin = Vector2(0.0f, 0.0f))
    {
        std::unique_ptr<ISpriteBatchRenderer> sprites = renderer.CreateSpriteBatch();
        sprites->Begin();
        sprites->SetSamplerFilter(/*Point*/ 1);
        sprites->SetSamplerAddressMode(/*Clamp*/ 1, /*Clamp*/ 1);
        sprites->Draw(texture, destination, source, Color::White, rotation, origin,
                      SpriteEffects::None, 0.0f);
        sprites->End();
    }

    bool ExerciseContract(IGraphicsRenderer& renderer)
    {
        bool ok = true;
        const Pixel black{0, 0, 0, 255};
        const Pixel red{255, 0, 0, 255};
        const Pixel green{0, 255, 0, 255};
        const Pixel blue{0, 0, 255, 255};

        const ImageData atlasData{
            3, 1,
            std::vector<std::uint8_t>{
                255, 0, 0, 255,
                0, 255, 0, 255,
                0, 0, 255, 255,
            }};
        std::unique_ptr<ITextureRenderer> atlas = renderer.CreateTexture(atlasData);
        const Rectangle redSource(0, 0, 1, 1);
        const Rectangle greenSource(1, 0, 1, 1);
        const Rectangle blueSource(2, 0, 1, 1);
        const Rectangle full(0, 0, kWidth, kHeight);

        ok &= Expect(renderer.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing),
                     "GDI advertises its real but deliberately narrow backbuffer MSAA path");
        ok &= Expect(renderer.ApplyMultiSampleCount(4) == 4 &&
                         renderer.GetMultiSampleCount() == 4,
                     "exactly four CPU colour samples are active");
        std::unique_ptr<IRenderTargetRenderer> target = renderer.CreateRenderTarget2D(
            2, 2, /*DepthFormat.None*/ 0, false, false, /*requested samples*/ 4);
        ok &= Expect(target != nullptr && target->GetMultiSampleCount() == 0,
                     "GDI RenderTarget2D remains explicitly single-sampled");

        DisableStencil(renderer);
        renderer.ApplyRasterizerState(/*CullCounterClockwise*/ 2, /*Solid*/ 0, false);

        struct MaskCase
        {
            unsigned int mask;
            std::uint8_t expectedRed;
            const char* message;
        };
        constexpr MaskCase maskCases[] = {
            {0x0u, 0u, "zero MultiSampleMask suppresses every colour sample"},
            {0x1u, 63u, "one enabled sample resolves to one quarter intensity"},
            {0x3u, 127u, "two enabled samples resolve to one half intensity"},
            {0x7u, 191u, "three enabled samples resolve to three quarter intensity"},
            {0xFu, 255u, "four enabled samples resolve to full intensity"},
            {0xF0u, 0u, "sample-mask bits above the implemented four are ignored"},
        };
        for (const MaskCase& maskCase : maskCases)
        {
            renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
            SetOpaqueMask(renderer, maskCase.mask);
            Draw(renderer, *atlas, full, redSource);
            ok &= ExpectPixel(ReadPixel(renderer, 1, 5),
                              Pixel{maskCase.expectedRed, 0, 0, 255}, maskCase.message, 1);
        }

        // The same rotated edge used by the public API oracle covers exactly one of the four
        // 2x2-grid locations. Running it once per single-bit mask proves that geometric coverage
        // and MultiSampleMask are intersected rather than either one replacing the other.
        int nonzeroMasks = 0;
        int resolvedRedSum = 0;
        for (int sample = 0; sample < 4; ++sample)
        {
            renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
            SetOpaqueMask(renderer, 1u << sample);
            Draw(renderer, *atlas, Rectangle(6, 2, 4, 4), redSource,
                 0.78539816339f, Vector2(0.5f, 0.5f));
            const int resolvedRed = ReadPixel(renderer, 3, 1)[0];
            resolvedRedSum += resolvedRed;
            nonzeroMasks += resolvedRed != 0 ? 1 : 0;
        }
        ok &= Expect(nonzeroMasks == 1 && std::abs(resolvedRedSum - 63) <= 1,
                     "triangle coverage intersects the four individual sample-mask bits exactly");

        // Wireframe deliberately remains the established one-pixel DDA line rasterizer. It emits
        // every mask-enabled sample for each visited pixel; it does not claim subpixel line AA.
        renderer.ApplyRasterizerState(/*CullNone*/ 0, /*WireFrame*/ 1, false);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        SetOpaqueMask(renderer, 0xFu);
        Draw(renderer, *atlas, Rectangle(1, 1, 5, 5), redSource);
        ok &= ExpectPixel(ReadPixel(renderer, 1, 1), red,
                          "wireframe edge writes every enabled sample without claiming line AA");
        ok &= ExpectPixel(ReadPixel(renderer, 2, 4), black,
                          "wireframe still leaves the quad interior untouched");

        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        SetOpaqueMask(renderer, 0x1u);
        Draw(renderer, *atlas, Rectangle(1, 1, 5, 5), redSource);
        ok &= ExpectPixel(ReadPixel(renderer, 1, 1), Pixel{63, 0, 0, 255},
                          "wireframe pixels still honor MultiSampleMask", 1);
        renderer.ApplyRasterizerState(/*CullCounterClockwise*/ 2, /*Solid*/ 0, false);

        // Stencil storage is intentionally one byte per pixel, not four values per sample. An
        // Increment pass therefore runs once for this off-diagonal triangle fragment even though
        // it covers all four colour samples; Equal(1) passes and Equal(4) fails afterward.
        renderer.ClearStencil(0);
        SetOpaqueMask(renderer, 0xFu);
        SetStencil(renderer, /*Always*/ 0, /*Increment*/ 3, /*Keep*/ 0, 0);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(renderer, *atlas, full, redSource);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        SetStencil(renderer, /*Equal*/ 4, /*Keep*/ 0, /*Keep*/ 0, 1);
        Draw(renderer, *atlas, full, greenSource);
        ok &= ExpectPixel(ReadPixel(renderer, 1, 5), green,
                          "4x coverage applies one per-pixel stencil operation, not four");

        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        SetStencil(renderer, /*Equal*/ 4, /*Keep*/ 0, /*Keep*/ 0, 4);
        Draw(renderer, *atlas, full, blueSource);
        ok &= ExpectPixel(ReadPixel(renderer, 1, 5), black,
                          "the standalone stencil plane does not pretend to be per-sample");

        // Sample-mask rejection happens before stencil. With no active colour samples, a Replace
        // operation cannot mutate the pixel's stencil value.
        renderer.ClearStencil(0);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        SetOpaqueMask(renderer, 0x0u);
        SetStencil(renderer, /*Always*/ 0, /*Replace*/ 2, /*Keep*/ 0, 7);
        Draw(renderer, *atlas, full, redSource);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        SetOpaqueMask(renderer, 0xFu);
        SetStencil(renderer, /*Equal*/ 4, /*Keep*/ 0, /*Keep*/ 0, 0);
        Draw(renderer, *atlas, full, greenSource);
        ok &= ExpectPixel(ReadPixel(renderer, 1, 5), green,
                          "zero active samples suppress stencil operations as well as colour");

        // One per-pixel comparison gates the complete active colour-sample set. This is useful for
        // crisp 2D masks but intentionally cannot represent different stencil values per sample.
        renderer.ClearStencil(0);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        SetStencil(renderer, /*Always*/ 0, /*Replace*/ 2, /*Keep*/ 0, 1);
        Draw(renderer, *atlas, Rectangle(0, 0, 4, kHeight), redSource);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        SetStencil(renderer, /*Equal*/ 4, /*Keep*/ 0, /*Keep*/ 0, 1);
        Draw(renderer, *atlas, full, blueSource);
        ok &= ExpectPixel(ReadPixel(renderer, 1, 5), blue,
                          "matching per-pixel stencil admits all four active colour samples");
        ok &= ExpectPixel(ReadPixel(renderer, 6, 5), black,
                          "failing per-pixel stencil rejects all four active colour samples");

        DisableStencil(renderer);
        SetOpaqueMask(renderer, 0xFFFFFFFFu);
        ok &= Expect(renderer.ApplyMultiSampleCount(0) == 0 &&
                         renderer.GetMultiSampleCount() == 0,
                     "disabling MSAA releases the optional four-sample colour plane");
        return ok;
    }
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "CNA GDI MSAA contract", kWidth, kHeight, SDL_WINDOW_HIDDEN);
    if (window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int result = 0;
    try
    {
        GraphicsRendererCreateArgs args;
        args.window = window;
        args.virtualWidth = kWidth;
        args.virtualHeight = kHeight;
        args.presentationMode = CnaPresentationMode::Stretch;
        std::unique_ptr<IGraphicsRenderer> renderer = Gdi::CreateGraphicsRenderer(args);

        if (CNA::getCurrentGraphicsRendererType() != CNA::GraphicsRendererType::Gdi)
        {
            std::fprintf(stderr, "GDI MSAA target selected a non-GDI renderer.\n");
            result = 1;
        }
        else
        {
            result = ExerciseContract(*renderer) ? 0 : 1;
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI MSAA contract test failed: %s\n", error.what());
        result = 1;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
