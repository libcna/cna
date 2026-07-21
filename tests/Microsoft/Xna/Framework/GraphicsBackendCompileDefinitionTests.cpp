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
#ifdef CNA_BACKEND_HEADLESS
    ++enabled;
#endif
#ifdef CNA_BACKEND_SOFTWARE
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
#ifdef CNA_BACKEND_DX3
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
    // plan_dx30.md: same class of gap DX2-84's own comment above documents -- DX30 needs its own
    // entry here too, added proactively this time rather than discovered by a from-scratch regression.
#ifdef CNA_BACKEND_DX30
    ++enabled;
#endif
#ifdef CNA_BACKEND_SDL_GPU
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
