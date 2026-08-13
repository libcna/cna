// SPDX-License-Identifier: MS-PL
// plan_sokol.md SOKOL-45: SokolRenderer construction must be fully transactional, including
// the GL-context-creation step itself.
//
// Before this fix, CreateGpuContext() ran BEFORE the constructor's cleanup try/catch block. A real
// SDL_GL_CreateContext() succeeding followed by SDL_GL_MakeCurrent() failing therefore threw out of
// CreateGpuContext() with no catch handler around it yet, leaking the just-created GL context: the
// only cleanup path (the try/catch around SetupSokol()/CreateSpriteResources()) was never entered.
//
// This test uses the CNAEXT test-only constructor overload to force exactly that: a real
// SDL_GL_CreateContext() success immediately followed by a forced SDL_GL_MakeCurrent() failure, and
// counts every SDL_GL_DestroyContext() call the instance makes via contextDestroyCountEXT.
//
// Check A -- construction throws.
// Check B -- exactly one context was destroyed (the leaked-context bug would report zero).
// Check C -- no renderer is left registered for the window.
// Check D -- the same window can construct a real, working renderer immediately afterward (proves
//   the window itself, and SDL's own GL state, were left usable -- not just that this instance's
//   own destructor ran).
// Check E -- that immediately-succeeding renderer also destroys its own context exactly once when
//   it is torn down normally, confirming the shared cleanup path double-counts nothing.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = skipped (no GPU/display).

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Sokol/SokolRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <stdexcept>
#include <string>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Sokol;

namespace
{
    int checks = 0;
    int failures = 0;

    void Check(bool ok, const char* label)
    {
        ++checks;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) ++failures;
    }
}

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("[FAIL] SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    // SOKOL-4: SDL refuses SDL_GL_CreateContext on a window not created with SDL_WINDOW_OPENGL.
    SDL_Window* window = SDL_CreateWindow(
        "CNA SOKOL-45", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (window == nullptr)
    {
        std::printf("[FAIL] SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    GraphicsRendererCreateArgs args;
    args.surface.windowId = SDL_GetWindowID(window);
    args.virtualWidth = 64;
    args.virtualHeight = 64;
    args.multiSampleCount = 1;
    args.swapInterval = 0;

    int failedDestroyCount = 0;
    bool threw = false;
    std::string diagnostic;
    try
    {
        SokolRenderer renderer(args, /*forceMakeCurrentFailureEXT=*/true, &failedDestroyCount);
    }
    catch (const std::exception& exception)
    {
        threw = true;
        diagnostic = exception.what();
    }

    Check(threw, "construction throws when SDL_GL_MakeCurrent fails after context creation");
    if (!threw)
        std::printf("       (no exception thrown at all)\n");
    else
        std::printf("       diagnostic: %s\n", diagnostic.c_str());
    Check(failedDestroyCount == 1,
          "exactly one SDL_GL_DestroyContext() call -- the leaked-context bug reports zero");
    Check(IGraphicsRenderer::GetForWindow(SDL_GetWindowID(window)) == nullptr,
          "no renderer is left registered for the window after the failed construction");

    int successDestroyCount = 0;
    bool usable = false;
    try
    {
        {
            SokolRenderer renderer(args, /*forceMakeCurrentFailureEXT=*/false,
                                          &successDestroyCount);
            usable = IGraphicsRenderer::GetForWindow(SDL_GetWindowID(window)) == &renderer;
            renderer.Clear(0.1f, 0.2f, 0.3f, 1.0f);
            renderer.Present();
        }
        usable = usable && IGraphicsRenderer::GetForWindow(SDL_GetWindowID(window)) == nullptr;
    }
    catch (const std::exception& exception)
    {
        usable = false;
        std::printf("       unexpected recovery exception: %s\n", exception.what());
    }
    Check(usable, "the same window constructs a real, working renderer immediately afterward");
    Check(successDestroyCount == 1,
          "the succeeding renderer destroys its own context exactly once on teardown");

    SDL_DestroyWindow(window);
    SDL_Quit();

    std::printf("=== %d/%d PASS ===\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
