// SPDX-License-Identifier: MS-PL
//
// Task REMED-GFX-001: regression coverage for the EasyGLRenderer constructor's
// RegisterForWindow ordering fix. Before the fix, RegisterForWindow(window, this) ran before the
// first fallible step (SDL_GL_CreateContext); a subsequent constructor failure left a dangling
// entry in IGraphicsRenderer's static window registry, because the destructor of a
// never-fully-constructed object is never invoked. SDL_GL_CreateContext deterministically fails
// on a window created without SDL_WINDOW_OPENGL (SDL_video.c's own NOT_AN_OPENGL_WINDOW check),
// so no extra test-only seam is needed to reproduce the exact throwing path.
#include <gtest/gtest.h>

// plan_runtimerenderer.md RTR-P9-9: PRESENT_, not the identity macro. This suite is
// device-free policy coverage for its own renderer, so it is worth compiling and running
// whenever that renderer is COMPILED IN -- in a multi-renderer build it need not be the
// selected one. Only the default renderer's CNA_RENDERER_EASYGL is defined project-wide.
// EasyGL is a FAMILY, not a public identity, so cmake never generates
// CNA_RENDERER_PRESENT_EASYGL -- UnitTests.cmake emits one PRESENT_ macro per PUBLIC IDENTITY
// (CNA_RENDERER_IDENTITIES). Naming the family here made the guard dead, so this suite still
// compiled only for the default renderer: exactly the gap RTR-P9-9 existed to close, reintroduced
// by the fix for it. Verified against a real multi build, whose build.ninja defines PRESENT_ only
// for the identities in the set. The five public identities this family serves are named instead.
#if defined(CNA_RENDERER_EASYGL) \
    || defined(CNA_RENDERER_PRESENT_OPENGLES2) || defined(CNA_RENDERER_PRESENT_OPENGLES3) \
    || defined(CNA_RENDERER_PRESENT_OPENGL33) \
    || defined(CNA_RENDERER_PRESENT_WEBGL1) || defined(CNA_RENDERER_PRESENT_WEBGL2)
#include <SDL3/SDL.h>
#include <stdexcept>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/EasyGL/EasyGLRenderer.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"

using CNA::Internal::Renderers::CnaPresentationMode;
using CNA::Internal::Renderers::IGraphicsRenderer;
using CNA::Internal::Renderers::EasyGL::EasyGLRenderer;
using Microsoft::Xna::Framework::Input::Mouse;

TEST(EasyGLRendererConstructor, FailedContextCreationLeavesNoDanglingRegistryEntry)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    // Deliberately created WITHOUT SDL_WINDOW_OPENGL: SDL_GL_CreateContext() fails
    // deterministically, reproducing the exact throwing path (EasyGLRenderer.cpp's
    // "SDL_GL_CreateContext failed" branch) the fix must survive without leaking a registry entry.
    SDL_Window* window = SDL_CreateWindow("EasyGLRendererTests", 64, 64, SDL_WINDOW_HIDDEN);
    if (!window)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateWindow failed: " << SDL_GetError();
    }

    EXPECT_THROW(
        (EasyGLRenderer(window, 64, 64, CnaPresentationMode::FixedHeightDynamicWidth, false, 1, 1)),
        std::runtime_error);

    // Core regression check: a constructor that throws must never have registered (the fix moves
    // RegisterForWindow to the constructor's last statement, after every fallible step), so the
    // registry must have no entry for this window at all -- not a dangling one.
    EXPECT_EQ(IGraphicsRenderer::GetForWindow(window), nullptr);

    // Dispatch a real public-API mouse event against the same window. Mouse::SetPosition()
    // resolves the window's registered renderer via IGraphicsRenderer::GetForWindow() -- pre-fix,
    // this is exactly the call that would have dereferenced the dangling pointer left behind by
    // the failed construction above. Must not crash (verified under ASan/UBSan builds).
    Mouse::setWindowHandleProperty(reinterpret_cast<std::uintptr_t>(window));
    EXPECT_NO_THROW(Mouse::SetPosition(10, 10));
    Mouse::setWindowHandleProperty(0);

    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
#endif
