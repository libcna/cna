// SPDX-License-Identifier: MS-PL
// Short, repeatable GDI baseline benchmark. It is intentionally a manual target rather than CTest:
// the timings depend on the native Windows/Wine compositor and the host CPU.

#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace CNA::Internal::Renderers;

namespace
{
    using Clock = std::chrono::steady_clock;

    struct BenchmarkCase
    {
        const char* name;
        int width;
        int height;
        int spriteCount;
    };

    struct Measurements
    {
        double rasterMilliseconds = 0.0;
        double presentMilliseconds = 0.0;
    };

    [[nodiscard]] double Milliseconds(Clock::time_point start, Clock::time_point end)
    {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    [[nodiscard]] ImageData MakeBenchmarkTexture()
    {
        constexpr int size = 64;
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4u);
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                const std::size_t index = (static_cast<std::size_t>(y) * size + x) * 4u;
                const bool checker = ((x / 8) + (y / 8)) % 2 == 0;
                pixels[index + 0] = checker ? 255 : 80;
                pixels[index + 1] = checker ? 180 : 255;
                pixels[index + 2] = 120;
                pixels[index + 3] = 192;
            }
        }
        return ImageData{ size, size, std::move(pixels) };
    }

    void SetAlphaBlend(IGraphicsRenderer& renderer)
    {
        renderer.ApplyBlendState(/*One*/ 0, /*One*/ 0,
                                /*InverseSourceAlpha*/ 5, /*InverseSourceAlpha*/ 5,
                                /*Add*/ 0, /*Add*/ 0, BlendWriteState{});
    }

    Measurements RunCase(const BenchmarkCase& benchmarkCase, int frames, int requestedMultiSampleCount,
                         int textureFilter)
    {
        SDL_Window* window = SDL_CreateWindow(benchmarkCase.name, benchmarkCase.width,
                                               benchmarkCase.height, SDL_WINDOW_HIDDEN);
        if (window == nullptr)
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

        try
        {
            Measurements measurements;
            {
                GraphicsRendererCreateArgs args;
                args.surface.windowId = SDL_GetWindowID(window);
                args.virtualWidth = benchmarkCase.width;
                args.virtualHeight = benchmarkCase.height;
                args.presentationMode = CnaPresentationMode::Stretch;
                std::unique_ptr<IGraphicsRenderer> renderer = CreateGraphicsRenderer(args);
                if (renderer->ApplyMultiSampleCount(requestedMultiSampleCount) !=
                    requestedMultiSampleCount)
                    throw std::runtime_error("GDI benchmark could not apply its requested MSAA count.");
                std::unique_ptr<ITextureRenderer> texture = renderer->CreateTexture(MakeBenchmarkTexture());
                std::unique_ptr<ISpriteBatchRenderer> spriteBatch = renderer->CreateSpriteBatch();
                SetAlphaBlend(*renderer);

                constexpr int warmupFrames = 1;
                for (int frame = -warmupFrames; frame < frames; ++frame)
                {
                    const Clock::time_point rasterStart = Clock::now();
                    renderer->Clear(0.05f, 0.08f, 0.12f, 1.0f);
                    spriteBatch->Begin();
                    spriteBatch->SetSamplerFilter(textureFilter);
                    spriteBatch->SetSamplerAddressMode(/*Clamp*/ 1, /*Clamp*/ 1);

                    for (int sprite = 0; sprite < benchmarkCase.spriteCount; ++sprite)
                    {
                        const int maxX = std::max(1, benchmarkCase.width - 96);
                        const int maxY = std::max(1, benchmarkCase.height - 96);
                        const int x = (sprite * 73 + std::max(frame, 0) * 5) % maxX;
                        const int y = (sprite * 47 + std::max(frame, 0) * 3) % maxY;
                        spriteBatch->Draw(*texture, Rectangle(x, y, 96, 96), Rectangle(0, 0, 64, 64),
                                          Color(255, 255, 255, 192),
                                          0.08f * static_cast<float>(sprite + std::max(frame, 0)),
                                          Vector2(32.0f, 32.0f), SpriteEffects::None, 0.0f);
                    }
                    spriteBatch->End();
                    const Clock::time_point rasterEnd = Clock::now();
                    renderer->Present();
                    const Clock::time_point presentEnd = Clock::now();

                    if (frame >= 0)
                    {
                        measurements.rasterMilliseconds += Milliseconds(rasterStart, rasterEnd);
                        measurements.presentMilliseconds += Milliseconds(rasterEnd, presentEnd);
                    }
                }
            }

            SDL_DestroyWindow(window);
            return measurements;
        }
        catch (...)
        {
            SDL_DestroyWindow(window);
            throw;
        }
    }
} // namespace

int main(int argc, char* argv[])
{
    int frames = 4;
    int textureFilter = /*Point*/ 1;
    const char* filterName = "Point";
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--frames" && i + 1 < argc)
            frames = std::clamp(std::stoi(argv[++i]), 1, 30);
        else if (std::string(argv[i]) == "--linear")
        {
            textureFilter = /*Linear*/ 0;
            filterName = "Linear";
        }
        else if (std::string(argv[i]) == "--anisotropic")
        {
            textureFilter = /*Anisotropic*/ 2;
            filterName = "Anisotropic->Linear";
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int result = 0;
    try
    {
        if (CNA::getCurrentGraphicsRendererType() != CNA::GraphicsRendererType::Gdi)
            throw std::runtime_error("GDI benchmark was built with a non-GDI renderer.");

        constexpr BenchmarkCase cases[] = {
            { "GDI benchmark 800x600 modest", 800, 600, 12 },
            { "GDI benchmark 800x600 stress", 800, 600, 50 },
            { "GDI benchmark 1280x720 modest", 1280, 720, 20 },
        };
        std::printf("GDI 2D benchmark: %d measured frames (+ 1 warm-up), %sClamp, 96px rotating alpha sprites\n",
                    frames, filterName);
        for (const BenchmarkCase& benchmarkCase : cases)
        {
            for (const int multiSampleCount : { 0, 4 })
            {
                const Measurements measurements =
                    RunCase(benchmarkCase, frames, multiSampleCount, textureFilter);
                const double raster = measurements.rasterMilliseconds / frames;
                const double present = measurements.presentMilliseconds / frames;
                std::printf("%-30s msaa=%d sprites=%-3d raster=%7.3f ms  present=%7.3f ms  frame=%7.3f ms\n",
                            benchmarkCase.name, multiSampleCount, benchmarkCase.spriteCount,
                            raster, present, raster + present);
            }
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI benchmark failed: %s\n", error.what());
        result = 1;
    }

    SDL_Quit();
    return result;
}
