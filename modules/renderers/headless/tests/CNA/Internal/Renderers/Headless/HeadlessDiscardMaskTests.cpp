// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#if defined(CNA_RENDERER_HEADLESS)
#include "CNA/Internal/Renderers/Headless/HeadlessRenderer.hpp"
using namespace CNA::Internal::Renderers::Headless;
TEST(HeadlessDiscardMaskTest, RecordsSelectedColorsWithoutWholeSetClear)
{
    HeadlessRenderer renderer(4,4);
    renderer.SharedState()->mode=HeadlessMode::Trace;
    // Internal protocol witness: mask 0b1010 means exactly slots 1 and 3, not "all".
    renderer.DiscardRenderTargetColorsEXT(10u,68.0f/255.0f,34.0f/255.0f,136.0f/255.0f,1.0f);
    ASSERT_EQ(renderer.TraceLog().size(),1u);
    EXPECT_EQ(renderer.TraceLog()[0].method,"DiscardRenderTargetColorsEXT");
    EXPECT_EQ(renderer.TraceLog()[0].argsSummary,"colorSlots=10");
    renderer.DiscardRenderTargetColorsEXT(0u,0,0,0,0);
    EXPECT_EQ(renderer.TraceLog().size(),1u);
}
#endif
