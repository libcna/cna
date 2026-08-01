// SPDX-License-Identifier: MS-PL
// GDI-051: deterministic damage tracking from the Software rasterizer's final SpriteBatch quad.

#include "CNA/Internal/Backends/Gdi/GdiGraphicsBackend.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <vector>

using namespace CNA::Internal::Backends;
using namespace CNA::Internal::Backends::Gdi;

namespace
{
    constexpr int kWidth = 64;
    constexpr int kHeight = 64;
    const Rectangle kSource(0, 0, 1, 1);

    bool Expect(bool condition, const char* message)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
        return condition;
    }

    bool SameRectangle(const Rectangle& actual, const Rectangle& expected)
    {
        return actual.X == expected.X && actual.Y == expected.Y &&
               actual.Width == expected.Width && actual.Height == expected.Height;
    }

    bool ExpectDamage(GdiGraphicsBackend& backend, const Rectangle& expected,
                      const char* message)
    {
        Rectangle actual;
        bool fullyDirty = false;
        const bool valid = backend.DebugGetBackbufferDamage(actual, fullyDirty);
        if (!valid || fullyDirty || !SameRectangle(actual, expected))
        {
            std::fprintf(stderr,
                         "%s: expected partial (%d,%d %dx%d), got valid=%d full=%d (%d,%d %dx%d).\n",
                         message, expected.X, expected.Y, expected.Width, expected.Height,
                         valid ? 1 : 0, fullyDirty ? 1 : 0,
                         actual.X, actual.Y, actual.Width, actual.Height);
            return Expect(false, message);
        }
        return Expect(true, message);
    }

    bool ExpectNoDamage(GdiGraphicsBackend& backend, const char* message)
    {
        Rectangle ignored;
        bool fullyDirty = false;
        return Expect(!backend.DebugGetBackbufferDamage(ignored, fullyDirty), message);
    }

    void ResetState(GdiGraphicsBackend& backend)
    {
        backend.SetViewport(0, 0, kWidth, kHeight, 0.0f, 1.0f);
        backend.SetScissorRect(0, 0, kWidth, kHeight);
        backend.ApplyRasterizerState(/*CullCounterClockwise*/ 2, /*Solid*/ 0,
                                     /*scissor disabled*/ false, 0.0f, 0.0f);
        backend.DebugResetBackbufferDamage();
    }

    void Draw(ISpriteBatchBackend& sprites, const ITextureBackend& texture,
              const Rectangle& destination,
              const Matrix& transform = Matrix::getIdentityProperty(),
              float rotation = 0.0f,
              const Vector2& origin = Vector2(0.0f, 0.0f))
    {
        sprites.SetTransformMatrix(transform);
        sprites.Begin();
        sprites.Draw(texture, destination, kSource, Color::White, rotation, origin,
                     SpriteEffects::None, 0.0f);
        sprites.End();
    }
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("CNA GDI dirty damage", kWidth, kHeight,
                                          SDL_WINDOW_HIDDEN);
    if (window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int result = 0;
    try
    {
        GdiGraphicsBackend backend(window, kWidth, kHeight, CnaPresentationMode::Stretch);
        const ImageData image{1, 1, std::vector<std::uint8_t>{255, 0, 0, 255}};
        std::unique_ptr<ITextureBackend> texture = backend.CreateTexture(image);
        std::unique_ptr<ISpriteBatchBackend> sprites = backend.CreateSpriteBatch();
        bool ok = true;

        ResetState(backend);
        Draw(*sprites, *texture, Rectangle(10, 12, 4, 3));
        ok &= ExpectDamage(backend, Rectangle(10, 12, 5, 4),
                           "axis-aligned damage comes from raster candidate bounds");

        ResetState(backend);
        Draw(*sprites, *texture, Rectangle(20, 20, 4, 4),
             Matrix::getIdentityProperty(), 0.0f, Vector2(1.0f, 1.0f));
        ok &= ExpectDamage(backend, Rectangle(16, 16, 5, 5),
                           "non-zero origin moves damage with the rasterized quad");

        ResetState(backend);
        backend.SetViewport(5, 7, 30, 30, 0.0f, 1.0f);
        Draw(*sprites, *texture, Rectangle(2, 3, 4, 4));
        ok &= ExpectDamage(backend, Rectangle(7, 10, 5, 5),
                           "viewport origin is included in damage coordinates");

        ResetState(backend);
        backend.SetScissorRect(12, 13, 2, 2);
        backend.ApplyRasterizerState(/*CullCounterClockwise*/ 2, /*Solid*/ 0,
                                     /*scissor enabled*/ true, 0.0f, 0.0f);
        Draw(*sprites, *texture, Rectangle(10, 10, 10, 10));
        ok &= ExpectDamage(backend, Rectangle(12, 13, 2, 2),
                           "damage is clipped by the active scissor rectangle");

        ResetState(backend);
        Draw(*sprites, *texture, Rectangle(-5, -4, 10, 8));
        ok &= ExpectDamage(backend, Rectangle(0, 0, 6, 5),
                           "negative partially-offscreen damage is safely clipped");

        ResetState(backend);
        Draw(*sprites, *texture, Rectangle(-100, -100, 10, 10));
        ok &= ExpectNoDamage(backend, "wholly offscreen sprite produces no damage");

        ResetState(backend);
        Draw(*sprites, *texture,
             Rectangle(0, 0, std::numeric_limits<int>::max(),
                       std::numeric_limits<int>::max()));
        ok &= ExpectDamage(backend, Rectangle(0, 0, kWidth, kHeight),
                           "very large intersecting rectangle saturates to the framebuffer");

        ResetState(backend);
        Draw(*sprites, *texture,
             Rectangle(std::numeric_limits<int>::max() - 4,
                       std::numeric_limits<int>::max() - 4, 4, 4));
        ok &= ExpectNoDamage(backend,
                             "very large offscreen coordinates do not overflow into damage");

        ResetState(backend);
        Draw(*sprites, *texture, Rectangle(1, 2, 2, 2));
        Draw(*sprites, *texture, Rectangle(10, 12, 2, 2));
        ok &= ExpectDamage(backend, Rectangle(1, 2, 12, 13),
                           "multiple sprite bounds are unioned without losing an edge");

        ResetState(backend);
        Draw(*sprites, *texture, Rectangle(3, 4, 2, 2),
             Matrix::CreateTranslation(7.0f, 9.0f, 0.0f));
        ok &= ExpectDamage(backend, Rectangle(10, 13, 3, 3),
                           "SpriteBatch transform is applied before damage reporting");

        ResetState(backend);
        Draw(*sprites, *texture, Rectangle(20, 20, 4, 2),
             Matrix::getIdentityProperty(), 1.57079632679f);
        Rectangle rotated;
        bool rotatedFull = false;
        const bool rotatedValid = backend.DebugGetBackbufferDamage(rotated, rotatedFull);
        ok &= Expect(rotatedValid && !rotatedFull && rotated.X <= 18 && rotated.Y <= 20 &&
                         rotated.getRightProperty() >= 20 && rotated.getBottomProperty() >= 24 &&
                         rotated.Width < kWidth && rotated.Height < kHeight,
                     "rotated quad reports a finite transformed partial bound");

        ResetState(backend);
        std::unique_ptr<IRenderTargetBackend> target =
            backend.CreateRenderTarget2D(8, 8, 0, true, false, 0);
        backend.SetRenderTarget2D(target.get());
        Draw(*sprites, *texture, Rectangle(0, 0, 4, 4));
        backend.SetRenderTarget2D(nullptr);
        ok &= ExpectNoDamage(backend, "off-screen target draws do not dirty the backbuffer");

        result = ok ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI dirty damage test failed: %s\n", error.what());
        result = 1;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
