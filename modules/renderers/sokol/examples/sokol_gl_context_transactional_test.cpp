// SPDX-License-Identifier: MS-PL
// plan_sokol.md SOKOL-45: SokolRenderer construction must be fully transactional, including
// the GL-context-creation step itself.
//
// Before this fix, context creation ran before the constructor's cleanup boundary. A successful
// platform CreateContext() followed by MakeCurrent() failing therefore leaked the new context.
//
// This test injects that failure in a test-only implementation of IPlatformGlContext and counts
// every DestroyContext() call. The production renderer sees only the platform interface.
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
#include "common/SdlTestGraphicsServices.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <stdexcept>
#include <string>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Sokol;
using CNA::Examples::SdlTestGlContext;
using CNA::Examples::SdlTestRendererArgs;

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

    SdlTestGlContext failingGlContext(window, /*forceMakeCurrentFailure=*/true);
    GraphicsRendererCreateArgs args = SdlTestRendererArgs(
        window, &failingGlContext, nullptr, 64, 64,
        CnaPresentationMode::NativeBackBuffer, 0);
    args.multiSampleCount = 1;

    bool threw = false;
    std::string diagnostic;
    try
    {
        SokolRenderer renderer(args);
    }
    catch (const std::exception& exception)
    {
        threw = true;
        diagnostic = exception.what();
    }

    Check(threw, "construction throws when platform MakeCurrent fails after context creation");
    if (!threw)
        std::printf("       (no exception thrown at all)\n");
    else
        std::printf("       diagnostic: %s\n", diagnostic.c_str());
    Check(failingGlContext.DestroyCount() == 1,
          "exactly one platform DestroyContext() call -- the leaked-context bug reports zero");
    Check(IGraphicsRenderer::GetForWindow(SDL_GetWindowID(window)) == nullptr,
          "no renderer is left registered for the window after the failed construction");

    SdlTestGlContext succeedingGlContext(window);
    args.glContext = &succeedingGlContext;
    bool usable = false;
    try
    {
        {
            SokolRenderer renderer(args);
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
    Check(succeedingGlContext.DestroyCount() == 1,
          "the succeeding renderer destroys its own context exactly once on teardown");

    SDL_DestroyWindow(window);
    SDL_Quit();

    std::printf("=== %d/%d PASS ===\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
