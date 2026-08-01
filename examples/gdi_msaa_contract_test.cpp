// SPDX-License-Identifier: MS-PL
// GDI-073: exact contract for GDI's deliberately narrow backbuffer-only 4x CPU MSAA.

#include "CNA/GraphicsBackendType.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <vector>

using namespace CNA::Internal::Backends;

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

    [[nodiscard]] Pixel ReadPixel(IGraphicsBackend& backend, int x, int y)
    {
        Pixel pixel{};
        backend.ReadBackbuffer(x, y, 1, 1, pixel.data());
        return pixel;
    }

    void SetOpaqueMask(IGraphicsBackend& backend, unsigned int mask)
    {
        BlendWriteState writeState;
        writeState.multiSampleMask = mask;
        backend.ApplyBlendState(
            /*One*/ 0, /*One*/ 0, /*Zero*/ 1, /*Zero*/ 1,
            /*Add*/ 0, /*Add*/ 0, writeState);
    }

    void SetStencil(IGraphicsBackend& backend, int compare, int pass, int fail,
                    int reference)
    {
        backend.ApplyDepthStencilState(
            /*depthEnable*/ false, /*depthWriteEnable*/ false, /*LessEqual*/ 3,
            /*stencilEnable*/ true, compare, pass, fail, /*depthFail Keep*/ 0,
            /*readMask*/ 0xFF, /*writeMask*/ 0xFF, reference,
            /*twoSided*/ false, 0, 0, 0, 0);
    }

    void DisableStencil(IGraphicsBackend& backend)
    {
        backend.ApplyDepthStencilState(
            false, false, /*LessEqual*/ 3, false, /*Always*/ 0,
            /*Keep*/ 0, /*Keep*/ 0, /*Keep*/ 0, 0xFF, 0xFF, 0,
            false, 0, 0, 0, 0);
    }

    void Draw(IGraphicsBackend& backend, const ITextureBackend& texture,
              const Rectangle& destination, const Rectangle& source,
              float rotation = 0.0f,
              const Vector2& origin = Vector2(0.0f, 0.0f))
    {
        std::unique_ptr<ISpriteBatchBackend> sprites = backend.CreateSpriteBatch();
        sprites->Begin();
        sprites->SetSamplerFilter(/*Point*/ 1);
        sprites->SetSamplerAddressMode(/*Clamp*/ 1, /*Clamp*/ 1);
        sprites->Draw(texture, destination, source, Color::White, rotation, origin,
                      SpriteEffects::None, 0.0f);
        sprites->End();
    }

    bool ExerciseContract(IGraphicsBackend& backend)
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
        std::unique_ptr<ITextureBackend> atlas = backend.CreateTexture(atlasData);
        const Rectangle redSource(0, 0, 1, 1);
        const Rectangle greenSource(1, 0, 1, 1);
        const Rectangle blueSource(2, 0, 1, 1);
        const Rectangle full(0, 0, kWidth, kHeight);

        ok &= Expect(backend.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing),
                     "GDI advertises its real but deliberately narrow backbuffer MSAA path");
        ok &= Expect(backend.ApplyMultiSampleCount(4) == 4 &&
                         backend.GetMultiSampleCount() == 4,
                     "exactly four CPU colour samples are active");
        std::unique_ptr<IRenderTargetBackend> target = backend.CreateRenderTarget2D(
            2, 2, /*DepthFormat.None*/ 0, false, false, /*requested samples*/ 4);
        ok &= Expect(target != nullptr && target->GetMultiSampleCount() == 0,
                     "GDI RenderTarget2D remains explicitly single-sampled");

        DisableStencil(backend);
        backend.ApplyRasterizerState(/*CullCounterClockwise*/ 2, /*Solid*/ 0, false);

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
            backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
            SetOpaqueMask(backend, maskCase.mask);
            Draw(backend, *atlas, full, redSource);
            ok &= ExpectPixel(ReadPixel(backend, 1, 5),
                              Pixel{maskCase.expectedRed, 0, 0, 255}, maskCase.message, 1);
        }

        // The same rotated edge used by the public API oracle covers exactly one of the four
        // 2x2-grid locations. Running it once per single-bit mask proves that geometric coverage
        // and MultiSampleMask are intersected rather than either one replacing the other.
        int nonzeroMasks = 0;
        int resolvedRedSum = 0;
        for (int sample = 0; sample < 4; ++sample)
        {
            backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
            SetOpaqueMask(backend, 1u << sample);
            Draw(backend, *atlas, Rectangle(6, 2, 4, 4), redSource,
                 0.78539816339f, Vector2(0.5f, 0.5f));
            const int resolvedRed = ReadPixel(backend, 3, 1)[0];
            resolvedRedSum += resolvedRed;
            nonzeroMasks += resolvedRed != 0 ? 1 : 0;
        }
        ok &= Expect(nonzeroMasks == 1 && std::abs(resolvedRedSum - 63) <= 1,
                     "triangle coverage intersects the four individual sample-mask bits exactly");

        // Wireframe deliberately remains the established one-pixel DDA line rasterizer. It emits
        // every mask-enabled sample for each visited pixel; it does not claim subpixel line AA.
        backend.ApplyRasterizerState(/*CullNone*/ 0, /*WireFrame*/ 1, false);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        SetOpaqueMask(backend, 0xFu);
        Draw(backend, *atlas, Rectangle(1, 1, 5, 5), redSource);
        ok &= ExpectPixel(ReadPixel(backend, 1, 1), red,
                          "wireframe edge writes every enabled sample without claiming line AA");
        ok &= ExpectPixel(ReadPixel(backend, 2, 4), black,
                          "wireframe still leaves the quad interior untouched");

        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        SetOpaqueMask(backend, 0x1u);
        Draw(backend, *atlas, Rectangle(1, 1, 5, 5), redSource);
        ok &= ExpectPixel(ReadPixel(backend, 1, 1), Pixel{63, 0, 0, 255},
                          "wireframe pixels still honor MultiSampleMask", 1);
        backend.ApplyRasterizerState(/*CullCounterClockwise*/ 2, /*Solid*/ 0, false);

        // Stencil storage is intentionally one byte per pixel, not four values per sample. An
        // Increment pass therefore runs once for this off-diagonal triangle fragment even though
        // it covers all four colour samples; Equal(1) passes and Equal(4) fails afterward.
        backend.ClearStencil(0);
        SetOpaqueMask(backend, 0xFu);
        SetStencil(backend, /*Always*/ 0, /*Increment*/ 3, /*Keep*/ 0, 0);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *atlas, full, redSource);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        SetStencil(backend, /*Equal*/ 4, /*Keep*/ 0, /*Keep*/ 0, 1);
        Draw(backend, *atlas, full, greenSource);
        ok &= ExpectPixel(ReadPixel(backend, 1, 5), green,
                          "4x coverage applies one per-pixel stencil operation, not four");

        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        SetStencil(backend, /*Equal*/ 4, /*Keep*/ 0, /*Keep*/ 0, 4);
        Draw(backend, *atlas, full, blueSource);
        ok &= ExpectPixel(ReadPixel(backend, 1, 5), black,
                          "the standalone stencil plane does not pretend to be per-sample");

        // Sample-mask rejection happens before stencil. With no active colour samples, a Replace
        // operation cannot mutate the pixel's stencil value.
        backend.ClearStencil(0);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        SetOpaqueMask(backend, 0x0u);
        SetStencil(backend, /*Always*/ 0, /*Replace*/ 2, /*Keep*/ 0, 7);
        Draw(backend, *atlas, full, redSource);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        SetOpaqueMask(backend, 0xFu);
        SetStencil(backend, /*Equal*/ 4, /*Keep*/ 0, /*Keep*/ 0, 0);
        Draw(backend, *atlas, full, greenSource);
        ok &= ExpectPixel(ReadPixel(backend, 1, 5), green,
                          "zero active samples suppress stencil operations as well as colour");

        // One per-pixel comparison gates the complete active colour-sample set. This is useful for
        // crisp 2D masks but intentionally cannot represent different stencil values per sample.
        backend.ClearStencil(0);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        SetStencil(backend, /*Always*/ 0, /*Replace*/ 2, /*Keep*/ 0, 1);
        Draw(backend, *atlas, Rectangle(0, 0, 4, kHeight), redSource);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        SetStencil(backend, /*Equal*/ 4, /*Keep*/ 0, /*Keep*/ 0, 1);
        Draw(backend, *atlas, full, blueSource);
        ok &= ExpectPixel(ReadPixel(backend, 1, 5), blue,
                          "matching per-pixel stencil admits all four active colour samples");
        ok &= ExpectPixel(ReadPixel(backend, 6, 5), black,
                          "failing per-pixel stencil rejects all four active colour samples");

        DisableStencil(backend);
        SetOpaqueMask(backend, 0xFFFFFFFFu);
        ok &= Expect(backend.ApplyMultiSampleCount(0) == 0 &&
                         backend.GetMultiSampleCount() == 0,
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
        GraphicsBackendCreateArgs args;
        args.window = window;
        args.virtualWidth = kWidth;
        args.virtualHeight = kHeight;
        args.presentationMode = CnaPresentationMode::Stretch;
        std::unique_ptr<IGraphicsBackend> backend = CreateGraphicsBackend(args);

        if (CNA::getCurrentGraphicsBackendType() != CNA::GraphicsBackendType::Gdi)
        {
            std::fprintf(stderr, "GDI MSAA target selected a non-GDI backend.\n");
            result = 1;
        }
        else
        {
            result = ExerciseContract(*backend) ? 0 : 1;
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
