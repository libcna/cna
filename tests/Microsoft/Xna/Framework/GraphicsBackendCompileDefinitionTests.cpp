// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#if defined(CNA_BACKEND_BGFX) && __has_include(<bgfx/bgfx.h>)
#define CNA_TEST_BGFX_AVAILABLE 1
#include <bgfx/bgfx.h>
#include "CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp"
#endif

TEST(GraphicsBackendCompileDefinitionsTest, ExactlyOneGraphicsBackendIsSelected)
{
    int enabled = 0;

#ifdef CNA_BACKEND_SDL_RENDERER
    ++enabled;
#endif
#ifdef CNA_BACKEND_EASYGL
    ++enabled;
#endif
#ifdef CNA_BACKEND_BGFX
    ++enabled;
#endif
#ifdef CNA_BACKEND_VULKAN
    ++enabled;
#endif
#ifdef CNA_BACKEND_WEBGPU
    ++enabled;
#endif
#ifdef CNA_BACKEND_MAGNUM
    ++enabled;
#endif
#ifdef CNA_BACKEND_HEADLESS
    ++enabled;
#endif
#ifdef CNA_BACKEND_SOFTWARE
    ++enabled;
#endif
#ifdef CNA_BACKEND_STUB
    ++enabled;
#endif
#ifdef CNA_BACKEND_CANVAS
    ++enabled;
#endif
#ifdef CNA_BACKEND_ASCII
    ++enabled;
#endif
#ifdef CNA_BACKEND_D3D11
    ++enabled;
#endif
#ifdef CNA_BACKEND_D3D12
    ++enabled;
#endif
#ifdef CNA_BACKEND_FREEDIRECT
    ++enabled;
#endif
    // A genuine, previously-uncaught gap in the D3D9 branch (feature/dx9): no commit in this
    // file's own history ever added a D3D9 entry here (found 2026-07-16 while merging
    // feature/sdlgpu). The full unfiltered CnaTests suite was never run under
    // CNA_GRAPHICS_BACKEND=D3D9 (NEXT.md's own D9-123 note says as much), so this would have
    // silently failed EXPECT_EQ(enabled, 1) the first time anyone actually did.
#ifdef CNA_BACKEND_D3D9
    ++enabled;
#endif
#ifdef CNA_BACKEND_DX1
    ++enabled;
#endif
    // plan_dx2.md DX2-84: the same class of gap the D3D9 comment above documents -- no commit in
    // this file's history ever added a DX2 entry either, so the full CnaTests suite's first-ever
    // run under CNA_GRAPHICS_BACKEND=DX2 (this regression pass) would have silently failed
    // EXPECT_EQ(enabled, 1) the same way D3D9's did.
#ifdef CNA_BACKEND_DX2
    ++enabled;
#endif
    // plan_dx3.md: same class of gap DX2-84's own comment above documents -- DX3 needs its own
    // entry here too, added proactively this time rather than discovered by a from-scratch regression.
#ifdef CNA_BACKEND_DX3
    ++enabled;
#endif
#ifdef CNA_BACKEND_DX5
    ++enabled;
#endif
#ifdef CNA_BACKEND_DX6
    ++enabled;
#endif
#ifdef CNA_BACKEND_DX7
    ++enabled;
#endif
#ifdef CNA_BACKEND_DX8
    ++enabled;
#endif
#ifdef CNA_BACKEND_D3D10
    ++enabled;
#endif
#ifdef CNA_BACKEND_SDL_GPU
    ++enabled;
#endif
    // plan_opengles1.md: same class of gap DX2-84's comment above documents -- a new backend that
    // never gets an entry here makes this test report 0 enabled backends rather than 1.
#ifdef CNA_BACKEND_OPENGLES1
    ++enabled;
#endif
#ifdef CNA_BACKEND_OPENGL4
    ++enabled;
#endif
    // plan_opengl1.md phase 12 finding: same gap class as the D3D9 comment above -- no commit in
    // this file's own history ever added an OPENGL1 entry either, and the full unfiltered
    // CnaTests suite had never actually been run under CNA_GRAPHICS_BACKEND=OPENGL1 until this
    // audit did so.
#ifdef CNA_BACKEND_OPENGL1
    ++enabled;
#endif
#ifdef CNA_BACKEND_OPENGL2
    ++enabled;
#endif
    // plan_wicked.md: same gap class the D3D9 comment above documents -- the registration union
    // that added the WICKED identity everywhere else never conflicted on this file, so its silent
    // omission surfaced only when the full CnaTests suite first ran under
    // CNA_GRAPHICS_BACKEND=WICKED and this test reported 0 enabled backends.
#ifdef CNA_BACKEND_WICKED
    ++enabled;
#endif

    EXPECT_EQ(enabled, 1);
}

#ifdef CNA_TEST_BGFX_AVAILABLE
TEST(GraphicsBackendCompileDefinitionsTest, BgfxApiIsLinkedForBgfxBackend)
{
    const bgfx::TextureHandle invalidTexture = BGFX_INVALID_HANDLE;
    EXPECT_FALSE(bgfx::isValid(invalidTexture));
}

TEST(GraphicsBackendCompileDefinitionsTest, BgfxRendererTypeDefaultIsSafeForPlatform)
{
    const auto renderer = CNA::Internal::Backends::Bgfx::Detail::GetDefaultRendererType();
#if defined(__linux__)
EXPECT_EQ(renderer, bgfx::RendererType::OpenGL);
#else
EXPECT_EQ(renderer, bgfx::RendererType::Count);
#endif
}

TEST(GraphicsBackendCompileDefinitionsTest, BgfxRendererTypeOverrideParsingWorks)
{
    EXPECT_EQ(
        CNA::Internal::Backends::Bgfx::Detail::ParseRendererTypeOverride("vulkan"),
        bgfx::RendererType::Vulkan
    );
    EXPECT_EQ(
        CNA::Internal::Backends::Bgfx::Detail::ParseRendererTypeOverride("OpenGL"),
        bgfx::RendererType::OpenGL
    );
    EXPECT_EQ(
        CNA::Internal::Backends::Bgfx::Detail::ParseRendererTypeOverride("auto"),
        bgfx::RendererType::Count
    );
}

TEST(GraphicsBackendCompileDefinitionsTest, BgfxRendererTypeOverrideRejectsInvalidValue)
{
    EXPECT_THROW(
        CNA::Internal::Backends::Bgfx::Detail::ParseRendererTypeOverride("invalid-renderer"),
        std::runtime_error
    );
}
#endif
