// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"

#if defined(CNA_RENDERER_BGFX) && __has_include(<bgfx/bgfx.h>)
#define CNA_TEST_BGFX_AVAILABLE 1
#include <bgfx/bgfx.h>
#include "CNA/Internal/Renderers/Bgfx/BgfxRenderer.hpp"
#endif


TEST(GraphicsRendererCompileDefinitionsTest, ExactlyOneGraphicsRendererIsSelected)
{
    int enabled = 0;

#ifdef CNA_RENDERER_SDL_RENDERER
    ++enabled;
#endif
#ifdef CNA_RENDERER_EASYGL
    ++enabled;
#endif
#ifdef CNA_RENDERER_BGFX
    ++enabled;
#endif
#ifdef CNA_RENDERER_VULKAN
    ++enabled;
#endif
#ifdef CNA_RENDERER_WEBGPU
    ++enabled;
#endif
#ifdef CNA_RENDERER_MAGNUM
    ++enabled;
#endif
#ifdef CNA_RENDERER_HEADLESS
    ++enabled;
#endif
#ifdef CNA_RENDERER_SOFTWARE
    ++enabled;
#endif
#ifdef CNA_RENDERER_STUB
    ++enabled;
#endif
#ifdef CNA_RENDERER_CANVAS
    ++enabled;
#endif
#ifdef CNA_RENDERER_HTML_DOM
    ++enabled;
#endif
#ifdef CNA_RENDERER_SKIA
    ++enabled;
#endif
#ifdef CNA_RENDERER_DIRECTX11
    ++enabled;
#endif
#ifdef CNA_RENDERER_DIRECTX12
    ++enabled;
#endif
#ifdef CNA_RENDERER_DIRECT2D
    ++enabled;
#endif
#ifdef CNA_RENDERER_FREEDIRECT
    ++enabled;
#endif
    // A genuine, previously-uncaught gap in the D3D9 branch (feature/dx9): no commit in this
    // file's own history ever added a D3D9 entry here (found 2026-07-16 while merging
    // feature/sdlgpu). The full unfiltered CnaTests suite was never run under
    // CNA_GRAPHICS_RENDERER=D3D9 (NEXT.md's own D9-123 note says as much), so this would have
    // silently failed EXPECT_EQ(enabled, 1) the first time anyone actually did.
#ifdef CNA_RENDERER_DIRECTX9
    ++enabled;
#endif
#ifdef CNA_RENDERER_DIRECTX1
    ++enabled;
#endif
    // plans/plan_dx2.md DX2-84: the same class of gap the D3D9 comment above documents -- no commit in
    // this file's history ever added a DIRECTX2 entry either, so the full CnaTests suite's first-ever
    // run under CNA_GRAPHICS_RENDERER=DIRECTX2 (this regression pass) would have silently failed
    // EXPECT_EQ(enabled, 1) the same way D3D9's did.
#ifdef CNA_RENDERER_DIRECTX2
    ++enabled;
#endif
    // plans/plan_dx3.md: same class of gap DX2-84's own comment above documents -- DIRECTX3 needs its own
    // entry here too, added proactively this time rather than discovered by a from-scratch regression.
#ifdef CNA_RENDERER_DIRECTX3
    ++enabled;
#endif
#ifdef CNA_RENDERER_DIRECTX5
    ++enabled;
#endif
#ifdef CNA_RENDERER_DIRECTX6
    ++enabled;
#endif
#ifdef CNA_RENDERER_DIRECTX7
    ++enabled;
#endif
#ifdef CNA_RENDERER_DIRECTX8
    ++enabled;
#endif
#ifdef CNA_RENDERER_DIRECTX10
    ++enabled;
#endif
#ifdef CNA_RENDERER_SDL_GPU
    ++enabled;
#endif
    // plans/plan_opengles1.md: same class of gap DX2-84's comment above documents -- a new renderer that
    // never gets an entry here makes this test report 0 enabled renderers rather than 1.
#ifdef CNA_RENDERER_OPENGLES1
    ++enabled;
#endif
#ifdef CNA_RENDERER_OPENGL4
    ++enabled;
#endif
    // plans/plan_opengl1.md phase 12 finding: same gap class as the D3D9 comment above -- no commit in
    // this file's own history ever added an OPENGL1 entry either, and the full unfiltered
    // CnaTests suite had never actually been run under CNA_GRAPHICS_RENDERER=OPENGL1 until this
    // audit did so.
#ifdef CNA_RENDERER_OPENGL1
    ++enabled;
#endif
#ifdef CNA_RENDERER_OPENGL2
    ++enabled;
#endif
    // plans/plan_wicked.md: same gap class the D3D9 comment above documents -- the registration union
    // that added the WICKED identity everywhere else never conflicted on this file, so its silent
    // omission surfaced only when the full CnaTests suite first ran under
    // CNA_GRAPHICS_RENDERER=WICKED and this test reported 0 enabled renderers.
#ifdef CNA_RENDERER_WICKED
    ++enabled;
#endif
#ifdef CNA_RENDERER_SOKOL
    ++enabled;
#endif
#ifdef CNA_RENDERER_DILIGENT
    ++enabled;
#endif
#ifdef CNA_RENDERER_GLIDE
    ++enabled;
#endif
#ifdef CNA_RENDERER_GDI
    ++enabled;
#endif

    // plans/plan_metal.md METAL-232: the identical class of gap the D3D9 comment above documents --
    // no commit in this file's own history ever added a Metal entry here, and this file has never
    // actually been built/run under CNA_GRAPHICS_RENDERER=METAL (no Apple toolchain in any session
    // to date), so this would have silently failed EXPECT_EQ(enabled, 1) the first time
    // metal-macos-ci.yml's build actually ran the full CnaTests suite.
#ifdef CNA_RENDERER_METAL
    ++enabled;
#endif
#ifdef CNA_RENDERER_FNA3D
    ++enabled;
#endif
#ifdef CNA_RENDERER_OPENVG
    ++enabled;
#endif

    // The PORTABLEGL identity, registered here at the same time it was registered everywhere
    // else -- the omission this whole comment block records is exactly what a new renderer keeps
    // reproducing when only the non-test registries are updated.
#ifdef CNA_RENDERER_PORTABLEGL
    ++enabled;
#endif
#ifdef CNA_RENDERER_TINYGL
    ++enabled;
#endif

    // The IGL identity (plans/plan_igl.md), registered here at the same time it was registered
    // everywhere else.
#ifdef CNA_RENDERER_IGL
    ++enabled;
#endif

    // plans/plan_pixijs.md: same registration discipline as every renderer above -- add the PIXIJS
    // entry here in the same task that adds the identity everywhere else, not after the fact.
#ifdef CNA_RENDERER_PIXIJS
    ++enabled;
#endif

    // plans/plan_nanovg.md: same registration discipline -- add the NANOVG entry here in the same task
    // that adds the identity everywhere else, not after the fact.
#ifdef CNA_RENDERER_NANOVG
    ++enabled;
#endif

    // plans/plan_runtimerenderer.md RTR-P7-8: exactly one, in BOTH modes.
    //
    // In a multi-renderer build this is not an accident -- it is the property that keeps this whole
    // file, and the ~892 other renderer-gated sites in the test and example corpus, meaningful.
    // Only the DEFAULT renderer's macro is defined project-wide; each family's own macro is private
    // to that family's target. So a renderer-gated test still describes exactly one renderer: the
    // default. (Making the corpus itself renderer-agnostic is phase P9.)
    //
    // If this ever reads more than 1, a renderer's macro has escaped its target and every gated
    // test in the corpus has silently become ambiguous.
    EXPECT_EQ(enabled, 1);
}

#ifdef CNA_RENDERER_IGL
TEST(GraphicsRendererCompileDefinitionsTest, IglRendererIsReportedByName)
{
    // The compile-time renderer identity has to agree with the CNA_RENDERER_IGL define
    // cmake/RendererSelection.cmake set; a new renderer that forgets its GraphicsRendererType.hpp
    // entry would otherwise still link and silently report another renderer's name.
    EXPECT_EQ(CNA::getCurrentGraphicsRendererType(), CNA::GraphicsRendererType::Igl);
    EXPECT_EQ(CNA::getCurrentGraphicsRendererName(), "IGL");
}
#endif

TEST(GraphicsRendererCompileDefinitionsTest, CompileTimeIdentityIsTheBuildDefault)
{
    // getCurrentGraphicsRendererType() answers "what did this build select by default", and stays a
    // constant expression in both modes. GraphicsDevice::GetGraphicsRendererType() is the one that
    // answers "what is this device actually using", and is deliberately NOT constexpr.
    static_assert(CNA::getCurrentGraphicsRendererName()
                  == CNA::getGraphicsRendererName(CNA::getCurrentGraphicsRendererType()));

#ifdef CNA_MULTI_RENDERER
    // A multi-renderer build has more than one renderer available, but still exactly one default.
    EXPECT_GT(CNA::GraphicsRendererSelection::GetAvailable().size(), 1u);
#endif
    EXPECT_TRUE(CNA::GraphicsRendererSelection::IsAvailable(
        CNA::getCurrentGraphicsRendererType()));
}

#ifdef CNA_RENDERER_SOKOL
TEST(GraphicsRendererCompileDefinitionsTest, SokolRendererIsReportedByName)
{
    // The compile-time renderer identity has to agree with the CNA_RENDERER_SOKOL define
    // cmake/RendererSelection.cmake set; a new renderer that forgets its GraphicsRendererType.hpp
    // entry would otherwise still link and silently report another renderer's name.
    EXPECT_EQ(CNA::getCurrentGraphicsRendererType(), CNA::GraphicsRendererType::Sokol);
    EXPECT_EQ(CNA::getCurrentGraphicsRendererName(), "SOKOL");
}
#endif

#ifdef CNA_RENDERER_SKIA
TEST(GraphicsRendererCompileDefinitionsTest, SkiaMacroMatchesPublicRendererIdentity)
{
    EXPECT_EQ(CNA::getCurrentGraphicsRendererType(), CNA::GraphicsRendererType::Skia);
    EXPECT_EQ(CNA::getCurrentGraphicsRendererName(), "SKIA");
}
#endif

#ifdef CNA_RENDERER_OPENVG
TEST(GraphicsRendererCompileDefinitionsTest, OpenVgMacroMatchesPublicRendererIdentity)
{
    EXPECT_EQ(CNA::getCurrentGraphicsRendererType(), CNA::GraphicsRendererType::OpenVg);
    EXPECT_EQ(CNA::getCurrentGraphicsRendererName(), "OPENVG");
}
#endif

#ifdef CNA_RENDERER_NANOVG
TEST(GraphicsRendererCompileDefinitionsTest, NanoVgMacroMatchesPublicRendererIdentity)
{
    EXPECT_EQ(CNA::getCurrentGraphicsRendererType(), CNA::GraphicsRendererType::NanoVg);
    EXPECT_EQ(CNA::getCurrentGraphicsRendererName(), "NANOVG");
}
#endif


#ifdef CNA_TEST_BGFX_AVAILABLE
TEST(GraphicsRendererCompileDefinitionsTest, BgfxApiIsLinkedForBgfxRenderer)
{
    const bgfx::TextureHandle invalidTexture = BGFX_INVALID_HANDLE;
    EXPECT_FALSE(bgfx::isValid(invalidTexture));
}

TEST(GraphicsRendererCompileDefinitionsTest, BgfxRendererTypeDefaultIsSafeForPlatform)
{
    const auto renderer = CNA::Internal::Renderers::Bgfx::Detail::GetDefaultRendererType();
#if defined(__linux__)
EXPECT_EQ(renderer, bgfx::RendererType::OpenGL);
#else
EXPECT_EQ(renderer, bgfx::RendererType::Count);
#endif
}

TEST(GraphicsRendererCompileDefinitionsTest, BgfxRendererTypeOverrideParsingWorks)
{
    EXPECT_EQ(
        CNA::Internal::Renderers::Bgfx::Detail::ParseRendererTypeOverride("vulkan"),
        bgfx::RendererType::Vulkan
    );
    EXPECT_EQ(
        CNA::Internal::Renderers::Bgfx::Detail::ParseRendererTypeOverride("OpenGL"),
        bgfx::RendererType::OpenGL
    );
    EXPECT_EQ(
        CNA::Internal::Renderers::Bgfx::Detail::ParseRendererTypeOverride("auto"),
        bgfx::RendererType::Count
    );
}

TEST(GraphicsRendererCompileDefinitionsTest, BgfxRendererTypeOverrideRejectsInvalidValue)
{
    EXPECT_THROW(
        CNA::Internal::Renderers::Bgfx::Detail::ParseRendererTypeOverride("invalid-renderer"),
        std::runtime_error
    );
}
#endif
