// SPDX-License-Identifier: MS-PL
//
// SKIA-159: a small standalone (non-GTest) executable proving the separately pinned Ganesh/OpenGL
// Skia artifact (docs/skia-ganesh-artifact.md) is not just link-complete but genuinely functional --
// it creates a real SDL OpenGL context, makes it current, and asks Skia's own
// GrDirectContexts::MakeGL() to construct a real Ganesh GrDirectContext over it (internally via
// GrGLMakeNativeInterface(), which on this pinned revision's Linux/X11 GN configuration resolves to
// the GLX native interface -- see docs/skia-ganesh-artifact.md for the GN args that select that
// path). This is strictly a below-the-API artifact-correctness probe, matching this backend's
// established "prove it below the API first" sequencing (SKIA-93/145/147/153-156); it is not part of
// SkiaGraphicsBackend and is not wired into the raster CTest suite -- SKIA-160/161 own the real
// construction-time mode selection and backend integration.
//
// Only built when -DCNA_SKIA_GANESH_BUILD_DIR is explicitly set (cmake/Harnesses.cmake); the
// ordinary CNA_GRAPHICS_BACKEND=SKIA (raster) build never sets it, so this target does not exist
// there and the validated raster artifact/build graph is unaffected.
//
// Needs a real GLX-capable display (e.g. DISPLAY=:0), not Xvfb -- the same requirement already
// established for the EasyGL golden build; Xvfb does not provide real hardware GLX.
//
// Exit codes: 0 = a real GrDirectContext was constructed and reports a positive max texture size,
// 1 = SDL/GL context setup failed, 2 = GrDirectContexts::MakeGL() returned null.
#include <SDL3/SDL.h>

#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"

#include <cstdio>

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);

    SDL_Window* window = SDL_CreateWindow(
        "cna_skia_ganesh_artifact_probe", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext)
    {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!SDL_GL_MakeCurrent(window, glContext))
    {
        std::fprintf(stderr, "SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    int exitCode = 0;
    {
        sk_sp<GrDirectContext> grContext = GrDirectContexts::MakeGL();
        if (!grContext)
        {
            std::fprintf(stderr,
                "GrDirectContexts::MakeGL() returned null -- the Ganesh artifact linked, but "
                "Skia's GLX native interface could not be assembled over this GL context.\n");
            exitCode = 2;
        }
        else
        {
            const int maxTextureSize = grContext->maxTextureSize();
            std::printf(
                "GrDirectContexts::MakeGL() succeeded; maxTextureSize=%d, abandoned=%d\n",
                maxTextureSize, grContext->abandoned() ? 1 : 0);
            if (maxTextureSize <= 0)
            {
                std::fprintf(stderr, "maxTextureSize was not positive; treating as a failure.\n");
                exitCode = 2;
            }
            grContext->abandonContext();
        }
    }

    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}
