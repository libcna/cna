// SPDX-License-Identifier: MS-PL
// Deterministic pixel regression coverage for the Win32 GDI 2D contract.

#include "CNA/GraphicsBackendType.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace CNA::Internal::Backends;

namespace
{
    using Pixel = std::array<std::uint8_t, 4>;

    [[nodiscard]] ImageData MakeImage(int width, int height, std::vector<std::uint8_t> pixels)
    {
        return ImageData{ width, height, std::move(pixels) };
    }

    [[nodiscard]] Pixel ReadPixel(IGraphicsBackend& backend, int x, int y)
    {
        Pixel pixel{};
        backend.ReadBackbuffer(x, y, 1, 1, pixel.data());
        return pixel;
    }

    bool ExpectPixel(const char* label, const Pixel& actual, const Pixel& expected, int tolerance = 0)
    {
        for (std::size_t channel = 0; channel < actual.size(); ++channel)
        {
            const int difference = std::abs(static_cast<int>(actual[channel]) -
                                            static_cast<int>(expected[channel]));
            if (difference > tolerance)
            {
                std::fprintf(stderr,
                             "%s: expected RGBA(%u,%u,%u,%u), got RGBA(%u,%u,%u,%u).\n",
                             label,
                             expected[0], expected[1], expected[2], expected[3],
                             actual[0], actual[1], actual[2], actual[3]);
                return false;
            }
        }
        return true;
    }

    bool Expect(bool value, const char* label)
    {
        if (!value)
            std::fprintf(stderr, "%s\n", label);
        return value;
    }

    void Draw(IGraphicsBackend& backend, const ITextureBackend& texture,
              const Rectangle& destination, const Rectangle& source, const Color& color,
              float rotation = 0.0f, const Vector2& origin = Vector2(0.0f, 0.0f),
              SpriteEffects effects = SpriteEffects::None, int filter = 1,
              int addressU = 1, int addressV = 1)
    {
        std::unique_ptr<ISpriteBatchBackend> spriteBatch = backend.CreateSpriteBatch();
        spriteBatch->Begin();
        spriteBatch->SetSamplerFilter(filter); // Point for stable byte-exact sampling.
        spriteBatch->SetSamplerAddressMode(addressU, addressV);
        spriteBatch->Draw(texture, destination, source, color, rotation, origin, effects, 0.0f);
        spriteBatch->End();
    }

    void SetOpaque(IGraphicsBackend& backend)
    {
        backend.ApplyBlendState(/*One*/ 0, /*One*/ 0, /*Zero*/ 1, /*Zero*/ 1,
                                /*Add*/ 0, /*Add*/ 0, BlendWriteState{});
    }

    void SetAlphaBlend(IGraphicsBackend& backend)
    {
        backend.ApplyBlendState(/*One*/ 0, /*One*/ 0,
                                /*InverseSourceAlpha*/ 5, /*InverseSourceAlpha*/ 5,
                                /*Add*/ 0, /*Add*/ 0, BlendWriteState{});
    }

    bool RunRegression(IGraphicsBackend& backend, SDL_Window* window)
    {
        bool ok = true;
        const Pixel black{ 0, 0, 0, 255 };
        const Pixel red{ 255, 0, 0, 255 };
        const Pixel green{ 0, 255, 0, 255 };
        const Pixel blue{ 0, 0, 255, 255 };

        const ImageData atlasData = MakeImage(2, 2, {
            255, 0, 0, 255,    0, 255, 0, 255,
            0, 0, 255, 255,    255, 255, 0, 255,
        });
        std::unique_ptr<ITextureBackend> atlas = backend.CreateTexture(atlasData);

        // Upload + source rectangle: top-right texel is green.
        SetOpaque(backend);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *atlas, Rectangle(1, 1, 1, 1), Rectangle(1, 0, 1, 1), Color::White);
        ok &= ExpectPixel("source rectangle", ReadPixel(backend, 1, 1), green);
        ok &= ExpectPixel("source rectangle does not spill", ReadPixel(backend, 0, 0), black);

        // Tint + alpha blend: half-transparent red over opaque blue.
        SetAlphaBlend(backend);
        backend.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        Draw(backend, *atlas, Rectangle(2, 2, 1, 1), Rectangle(0, 0, 1, 1),
             Color(255, 255, 255, 128));
        ok &= ExpectPixel("tint alpha blend", ReadPixel(backend, 2, 2),
                          Pixel{ 128, 0, 127, 255 }, 1);
        SetOpaque(backend);

        // Horizontal flip maps the 2-pixel source row in reverse order.
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *atlas, Rectangle(0, 0, 2, 1), Rectangle(0, 0, 2, 1), Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::FlipHorizontally);
        ok &= ExpectPixel("horizontal flip left", ReadPixel(backend, 0, 0), green);
        ok &= ExpectPixel("horizontal flip right", ReadPixel(backend, 1, 0), red);

        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *atlas, Rectangle(0, 0, 1, 2), Rectangle(0, 0, 1, 2), Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::FlipVertically);
        ok &= ExpectPixel("vertical flip top", ReadPixel(backend, 0, 0), blue);
        ok &= ExpectPixel("vertical flip bottom", ReadPixel(backend, 0, 1), red);

        // A 90-degree rotation moves this 2x2 red source left of its unrotated position.
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *atlas, Rectangle(4, 4, 2, 2), Rectangle(0, 0, 1, 1), Color::White,
             1.57079632679f, Vector2(0.0f, 0.0f));
        ok &= ExpectPixel("rotation destination", ReadPixel(backend, 2, 4), red);
        ok &= ExpectPixel("rotation vacates unrotated area", ReadPixel(backend, 5, 4), black);

        // Point Clamp, Wrap and Mirror must sample their different address domains, not merely
        // store sampler state. Source X=-3,width=8 covers UV [-1.5, 2.5].
        const ImageData rowData = MakeImage(2, 1, {
            255, 0, 0, 255,    0, 255, 0, 255,
        });
        std::unique_ptr<ITextureBackend> row = backend.CreateTexture(rowData);
        const Rectangle extendedSource(-3, 0, 8, 1);
        const Rectangle rowDestination(0, 0, 8, 1);

        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *row, rowDestination, extendedSource, Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, /*Point*/ 1,
             /*Clamp*/ 1, /*Clamp*/ 1);
        ok &= ExpectPixel("point clamp left", ReadPixel(backend, 0, 0), red);
        ok &= ExpectPixel("point clamp right", ReadPixel(backend, 7, 0), green);

        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *row, rowDestination, extendedSource, Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, /*Point*/ 1,
             /*Wrap*/ 0, /*Clamp*/ 1);
        ok &= ExpectPixel("point wrap left", ReadPixel(backend, 0, 0), green);
        ok &= ExpectPixel("point wrap right", ReadPixel(backend, 7, 0), red);

        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        Draw(backend, *row, rowDestination, extendedSource, Color::White,
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, /*Point*/ 1,
             /*Mirror*/ 2, /*Clamp*/ 1);
        ok &= ExpectPixel("point mirror left", ReadPixel(backend, 0, 0), green);
        ok &= ExpectPixel("point mirror third", ReadPixel(backend, 2, 0), red);

        // Render-target writes are read back from their own CPU surface, then sampled into the
        // default backbuffer through the same SpriteBatch texture path as a regular texture.
        std::unique_ptr<IRenderTargetBackend> target =
            backend.CreateRenderTarget2D(2, 2, /*DepthFormat::None*/ 0);
        ok &= Expect(target != nullptr, "GDI must create a 2D render target.");
        if (target != nullptr)
        {
            backend.SetRenderTarget2D(target.get());
            backend.Clear(0.0f, 1.0f, 0.0f, 1.0f);
            std::array<std::uint8_t, 4> targetPixel{};
            ok &= Expect(target->GetData(0, 0, 0, 1, 1, targetPixel.data(),
                                         static_cast<int>(targetPixel.size())),
                         "render-target readback must report success.");
            ok &= ExpectPixel("render-target readback", targetPixel, green);

            backend.SetRenderTarget2D(nullptr);
            backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
            Draw(backend, *target, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 2, 2), Color::White);
            ok &= ExpectPixel("render-target sampling", ReadPixel(backend, 1, 1), green);
        }

        // Resizing changes the CPU backbuffer itself, not only the final window blit.
        backend.SetVirtualResolution(16, 8);
        int viewportWidth = 0;
        int viewportHeight = 0;
        backend.GetViewportSize(viewportWidth, viewportHeight);
        ok &= Expect(viewportWidth == 16 && viewportHeight == 8,
                     "SetVirtualResolution must resize the GDI CPU backbuffer.");
        backend.Clear(1.0f, 0.0f, 0.0f, 1.0f);
        ok &= ExpectPixel("resized backbuffer", ReadPixel(backend, 15, 7), red);

        // Letterbox transforms use the same presentation geometry as Present().
        ok &= Expect(SDL_SetWindowSize(window, 64, 64), "SDL_SetWindowSize failed.");
        ok &= Expect(SDL_SyncWindow(window), "SDL_SyncWindow failed.");
        backend.SetPresentationMode(static_cast<int>(CnaPresentationMode::Letterbox));
        float windowX = 0.0f;
        float windowY = 0.0f;
        float logicalX = 0.0f;
        float logicalY = 0.0f;
        ok &= Expect(backend.TransformLogicalToWindow(8.0f, 4.0f, windowX, windowY) &&
                         std::fabs(windowX - 32.0f) < 0.01f && std::fabs(windowY - 32.0f) < 0.01f,
                     "logical-to-window letterbox transform is wrong.");
        ok &= Expect(backend.TransformWindowToLogical(32.0f, 32.0f, logicalX, logicalY) &&
                         std::fabs(logicalX - 8.0f) < 0.01f && std::fabs(logicalY - 4.0f) < 0.01f,
                     "window-to-logical letterbox transform is wrong.");
        ok &= Expect(!backend.TransformWindowToLogical(1.0f, 1.0f, logicalX, logicalY),
                     "letterbox bar must not map to a logical coordinate.");

        backend.Present();
        return ok;
    }
} // namespace

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("CNA GDI 2D regression", 32, 32, SDL_WINDOW_HIDDEN);
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
        args.virtualWidth = 32;
        args.virtualHeight = 32;
        args.presentationMode = CnaPresentationMode::Stretch;
        std::unique_ptr<IGraphicsBackend> backend = CreateGraphicsBackend(args);

        if (CNA::getCurrentGraphicsBackendType() != CNA::GraphicsBackendType::Gdi)
        {
            std::fprintf(stderr, "GDI regression target selected a non-GDI backend.\n");
            result = 1;
        }
        else if (!RunRegression(*backend, window))
        {
            result = 1;
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI 2D regression failed: %s\n", error.what());
        result = 1;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
