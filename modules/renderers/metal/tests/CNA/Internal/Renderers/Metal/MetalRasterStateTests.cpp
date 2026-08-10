// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <climits>

#include "CNA/Internal/Renderers/Metal/MetalRasterState.hpp"

using namespace CNA::Internal::Renderers::Metal;

TEST(MetalRasterState, ClearBoundaryPreservesRequestedViewportAndScissor)
{
    MetalRasterState state;
    state.BeginEncoder(800, 600);
    state.SetViewport(11, 12, 320, 240, 0.25, 0.75);
    state.SetScissor(20, 30, 100, 110);
    state.SetScissorEnabled(true);
    const MetalViewportState viewport = state.EffectiveViewport();
    const MetalScissorState scissor = state.EffectiveScissor();

    state.BeginEncoder(800, 600);
    EXPECT_EQ(state.EffectiveViewport(), viewport);
    EXPECT_EQ(state.EffectiveScissor(), scissor);
}

TEST(MetalRasterState, DisabledScissorAlwaysUsesFullAttachment)
{
    MetalRasterState state;
    state.BeginEncoder(640, 480);
    state.SetScissor(10, 20, 30, 40);
    state.SetScissorEnabled(false);
    EXPECT_EQ(state.EffectiveScissor(), (MetalScissorState{0, 0, 640, 480}));
}

TEST(MetalRasterState, RasterizerToggleImmediatelyChangesEffectiveScissor)
{
    MetalRasterState state;
    state.BeginEncoder(640, 480);
    state.SetScissor(10, 20, 30, 40);
    state.SetScissorEnabled(true);
    EXPECT_EQ(state.EffectiveScissor(), (MetalScissorState{10, 20, 30, 40}));
    state.SetScissorEnabled(false);
    EXPECT_EQ(state.EffectiveScissor(), (MetalScissorState{0, 0, 640, 480}));
    state.SetScissorEnabled(true);
    EXPECT_EQ(state.EffectiveScissor(), (MetalScissorState{10, 20, 30, 40}));
}

TEST(MetalRasterState, TargetResizeClampsEnabledScissorWithoutLosingRequest)
{
    MetalRasterState state;
    state.BeginEncoder(300, 200);
    state.SetScissor(250, 150, 100, 100);
    state.SetScissorEnabled(true);
    EXPECT_EQ(state.EffectiveScissor(), (MetalScissorState{250, 150, 50, 50}));

    state.BeginEncoder(275, 175);
    EXPECT_EQ(state.EffectiveScissor(), (MetalScissorState{250, 150, 25, 25}));

    state.BeginEncoder(400, 300);
    EXPECT_EQ(state.EffectiveScissor(), (MetalScissorState{250, 150, 100, 100}));
}

TEST(MetalRasterState, NegativeAndOversizedScissorIntersectsAttachmentSafely)
{
    MetalRasterState state;
    state.BeginEncoder(100, 80);
    state.SetScissor(-10, -20, 50, 60);
    state.SetScissorEnabled(true);
    EXPECT_EQ(state.EffectiveScissor(), (MetalScissorState{0, 0, 40, 40}));
}

TEST(MetalRasterState, ExtremeSignedInputsClampWithoutOverflow)
{
    MetalRasterState state;
    state.BeginEncoder(100, 80);
    state.SetScissor(INT_MIN, INT_MIN, INT_MAX, INT_MAX);
    state.SetScissorEnabled(true);
    EXPECT_EQ(state.EffectiveScissor(), (MetalScissorState{0, 0, 0, 0}));

    state.SetScissor(INT_MAX, INT_MAX, INT_MAX, INT_MAX);
    EXPECT_EQ(state.EffectiveScissor(), (MetalScissorState{100, 80, 0, 0}));
}

TEST(MetalRasterState, DefaultViewportTracksAttachmentUntilExplicitlySet)
{
    MetalRasterState state;
    state.BeginEncoder(100, 80);
    EXPECT_EQ(state.EffectiveViewport(), (MetalViewportState{0, 0, 100, 80, 0, 1}));
    state.BeginEncoder(50, 40);
    EXPECT_EQ(state.EffectiveViewport(), (MetalViewportState{0, 0, 50, 40, 0, 1}));
    state.SetViewport(5, 6, 7, 8, 0.1, 0.9);
    state.BeginEncoder(500, 400);
    EXPECT_EQ(state.EffectiveViewport(), (MetalViewportState{5, 6, 7, 8, 0.1, 0.9}));
}

TEST(MetalRasterState, ZeroSizedAndFullyOutsideScissorsSkipDrawWithLegalNativePlaceholder)
{
    MetalRasterState state;
    state.BeginEncoder(100,80);
    state.SetScissor(10,20,0,40);
    state.SetScissorEnabled(true);
    EXPECT_TRUE(state.ShouldSkipDraw());
    EXPECT_EQ(state.EffectiveScissor(),(MetalScissorState{10,20,0,40}));
    EXPECT_EQ(state.NativeScissor(),(MetalScissorState{0,0,1,1}));

    state.SetScissor(200,200,10,10);
    EXPECT_TRUE(state.ShouldSkipDraw());
    EXPECT_EQ(state.EffectiveScissor(),(MetalScissorState{100,80,0,0}));
    EXPECT_EQ(state.NativeScissor(),(MetalScissorState{0,0,1,1}));
}

TEST(MetalRasterState, EmptyScissorSkipTracksToggleAndEncoderRecreation)
{
    MetalRasterState state;
    state.BeginEncoder(100,80);
    state.SetScissor(150,10,5,5);
    state.SetScissorEnabled(true);
    EXPECT_TRUE(state.ShouldSkipDraw());

    state.SetScissorEnabled(false);
    EXPECT_FALSE(state.ShouldSkipDraw());
    EXPECT_EQ(state.NativeScissor(),(MetalScissorState{0,0,100,80}));

    state.SetScissorEnabled(true);
    state.BeginEncoder(200,80);
    EXPECT_FALSE(state.ShouldSkipDraw());
    EXPECT_EQ(state.NativeScissor(),(MetalScissorState{150,10,5,5}));

    state.BeginEncoder(100,80);
    EXPECT_TRUE(state.ShouldSkipDraw());
    EXPECT_EQ(state.NativeScissor(),(MetalScissorState{0,0,1,1}));
}
