// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"



TEST(GraphicsRendererCompileDefinitionsTest, ExactlyOneGraphicsRendererIsSelected)
{
    int enabled = 0;

#ifdef CNA_RENDERER_EASYGL
    ++enabled;
#endif
#ifdef CNA_RENDERER_WEBGPU
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
    // A genuine, previously-uncaught gap in the D3D9 branch (feature/dx9): no commit in this
    // file's own history ever added a D3D9 entry here (found 2026-07-16 while merging
    // feature/sdlgpu). The full unfiltered CnaTests suite was never run under
    // CNA_GRAPHICS_RENDERER=D3D9 (the DX9 plan's D9-123 note says as much), so this would have
    // silently failed EXPECT_EQ(enabled, 1) the first time anyone actually did.
#ifdef CNA_RENDERER_DIRECTX9
    ++enabled;
#endif
    // DX2-84: the same class of gap the D3D9 comment above documents -- no commit in
    // this file's history ever added a DIRECTX2 entry either, so the full CnaTests suite's first-ever
    // run under CNA_GRAPHICS_RENDERER=DIRECTX2 (this regression pass) would have silently failed
    // EXPECT_EQ(enabled, 1) the same way D3D9's did.
    // DX30-*: same class of gap DX2-84's own comment above documents -- DIRECTX3 needs its own
    // entry here too, added proactively this time rather than discovered by a from-scratch regression.
#ifdef CNA_RENDERER_SDLGPU
    ++enabled;
#endif
    // OPENGLES1-* (retired 2026-09-17): same class of gap DX2-84's comment above
    // documents -- a new renderer that never gets an entry here makes this test report 0 enabled
    // renderers rather than 1.
    // The OPENGL1 plan's phase 12 finding: same gap class as the D3D9 comment above -- no commit in
    // this file's own history ever added an OPENGL1 entry either, and the full unfiltered
    // CnaTests suite had never actually been run under CNA_GRAPHICS_RENDERER=OPENGL1 until this
    // audit did so.
    // WICKED-*: same gap class the D3D9 comment above documents -- the registration union
    // that added the WICKED identity (retired 2026-09-17) everywhere else never conflicted on this
    // file, so its silent omission surfaced only when the full CnaTests suite first ran under
    // CNA_GRAPHICS_RENDERER=WICKED and this test reported 0 enabled renderers. The trap is not
    // specific to that identity: this counter must gain an arm for every renderer that is added.

#ifdef CNA_RENDERER_FNA3D
    ++enabled;
#endif

    // The IGL identity (IGL-*), registered here at the same time it was registered
    // everywhere else.

    // PIXIJS-*: same registration discipline as every renderer above -- add the PIXIJS
    // entry here in the same task that adds the identity everywhere else, not after the fact.

    // NVG-*: same registration discipline -- add the NANOVG entry here in the same task
    // that adds the identity everywhere else, not after the fact.

    // RTR-P7-8: exactly one, in BOTH modes.
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







