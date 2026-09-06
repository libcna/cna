// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-189: render-target semantics -- `RenderTargetUsage`, a mid-run MSAA
// change, and the public properties a target reports about itself.
//
// Renderer-neutral, mirroring EasyGL's `render_target_usage`, `msaa_change`,
// `rendertarget2d_properties`, `rendertargetcube_properties`, `rendertargetcube_depthformat` and
// `rt_roundtrip`.
//
// `RenderTargetUsage` IS THE ONE WITH PIXELS BEHIND IT, and the pair is what makes it a test rather
// than a spelling check: `PreserveContents` must still hold what was drawn when the target is bound
// a second time, and `DiscardContents` must not. Asserting only the first would pass on a renderer
// that never discards anything -- which is to say on a renderer that ignores the flag entirely --
// so the two targets are rendered, unbound, and re-bound identically and differ only in the usage
// they were created with.
//
// THE MSAA CHANGE is not about the pixels but about what changing it DOES: `ApplyMultiSampleCount()`
// clears every pipeline cache, so a draw after the change is the first user of a freshly empty
// cache. A renderer that keyed a cached pipeline on the old sample count either fails validation or
// renders into the wrong attachment shape. Both targets are therefore drawn into and read back, in
// one run, in that order.
//
// The `applied <= requested` direction is the shared contract everywhere here: a renderer may
// honour four samples, two, or none, but never more than it was asked for.

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetCube;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

namespace
{
    constexpr int kSize = 16;
    const Color kPainted(0x21, 0x43, 0x65, 0xFF);

    /// Binds @p target, clears it to @p colour, unbinds. The content afterwards is whatever the
    /// target's usage says it should be.
    void PaintTarget(GraphicsDevice& device, RenderTarget2D& target, const Color& colour)
    {
        device.SetRenderTarget(&target);
        device.Clear(colour);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    /// Binds and unbinds without drawing -- which is the whole question for RenderTargetUsage.
    void RebindWithoutDrawing(GraphicsDevice& device, RenderTarget2D& target)
    {
        device.SetRenderTarget(&target);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    /// XNA spells "not multisampled" as 0 and a GPU spells it as 1, and a target created with 0
    /// may report either -- measured: WEBGPU reports 0 where a 4-sample request reports 4. Both are
    /// the same thing, so every claim here is made about the EFFECTIVE count.
    [[nodiscard]] int EffectiveSamples(int reported) { return reported < 1 ? 1 : reported; }

    [[nodiscard]] Color FirstTexel(RenderTarget2D& target)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels[0];
    }
}

TEST(RenderTargetSemantics, PreserveContentsKeepsWhatWasDrawnAcrossARebind)
{
    GraphicsDevice device;
    RenderTarget2D preserve(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
                            RenderTargetUsage::PreserveContents);
    PaintTarget(device, preserve, kPainted);
    RebindWithoutDrawing(device, preserve);

    const Color after = FirstTexel(preserve);
    EXPECT_EQ(after.getPackedValueProperty(), kPainted.getPackedValueProperty())
        << "PreserveContents means exactly this: binding the target again does not throw its "
           "content away";
}

TEST(RenderTargetSemantics, DiscardContentsDoesNotKeepIt)
{
    GraphicsDevice device;
    RenderTarget2D discard(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
    PaintTarget(device, discard, kPainted);
    RebindWithoutDrawing(device, discard);

    const Color after = FirstTexel(discard);
    EXPECT_NE(after.getPackedValueProperty(), kPainted.getPackedValueProperty())
        << "DiscardContents must not preserve -- and this is the half that catches a renderer "
           "ignoring the flag, because such a renderer passes the PreserveContents test by never "
           "discarding anything";
}

// The properties a target reports about itself. Not a spelling check: each one is a value the
// caller PASSED, and a target that forgot it, or substituted its own, is a target whose behaviour
// cannot be predicted from its API.
TEST(RenderTargetSemantics, ARenderTarget2DReportsWhatItWasAskedFor)
{
    GraphicsDevice device;
    RenderTarget2D target(device, kSize, kSize, true, SurfaceFormat::Color, DepthFormat::Depth24,
                          4, RenderTargetUsage::PreserveContents);

    EXPECT_EQ(target.getWidthProperty(), kSize);
    EXPECT_EQ(target.getHeightProperty(), kSize);
    EXPECT_EQ(static_cast<int>(target.getFormatProperty()),
              static_cast<int>(SurfaceFormat::Color));
    EXPECT_EQ(static_cast<int>(target.getRenderTargetUsageProperty()),
              static_cast<int>(RenderTargetUsage::PreserveContents));
    EXPECT_GT(target.getLevelCountProperty(), 1)
        << "mipMap = true was requested, so the chain must be there";
    // The two the renderer is allowed to reduce, in the one direction it is allowed to reduce them.
    EXPECT_LE(EffectiveSamples(target.getMultiSampleCountProperty()), 4)
        << "the applied sample count may never exceed the requested one";
    EXPECT_GE(EffectiveSamples(target.getMultiSampleCountProperty()), 1);
    const int depth = static_cast<int>(target.getDepthStencilFormatProperty());
    EXPECT_TRUE(depth == static_cast<int>(DepthFormat::Depth24) ||
                depth == static_cast<int>(DepthFormat::Depth24Stencil8) ||
                depth == static_cast<int>(DepthFormat::Depth16))
        << "a renderer may widen a depth request to a format it has, but it may not report None "
           "for a target created with a depth buffer";
}

TEST(RenderTargetSemantics, ARenderTargetCubeReportsWhatItWasAskedFor)
{
    GraphicsDevice device;
    std::unique_ptr<RenderTargetCube> cube;
    try
    {
        cube = std::make_unique<RenderTargetCube>(device, kSize, false, SurfaceFormat::Color,
                                                  DepthFormat::Depth24, 0,
                                                  RenderTargetUsage::DiscardContents);
    }
    catch (const System::NotSupportedException&)
    {
        GTEST_SKIP() << "this renderer has no RenderTargetCube";
    }
    catch (const std::runtime_error&)
    {
        GTEST_SKIP() << "this renderer has no RenderTargetCube";
    }

    EXPECT_EQ(static_cast<int>(cube->getFormatProperty()), static_cast<int>(SurfaceFormat::Color));
    EXPECT_EQ(static_cast<int>(cube->getRenderTargetUsageProperty()),
              static_cast<int>(RenderTargetUsage::DiscardContents));
    EXPECT_EQ(cube->getLevelCountProperty(), 1) << "mipMap = false was requested";
    EXPECT_EQ(EffectiveSamples(cube->getMultiSampleCountProperty()), 1);
    EXPECT_NE(static_cast<int>(cube->getDepthStencilFormatProperty()),
              static_cast<int>(DepthFormat::None))
        << "a cube created with Depth24 must not report None";
}

// A mid-run MSAA change. See the header: the point is not the pixels but that a draw AFTER the
// change works, because ApplyMultiSampleCount clears every pipeline cache and that draw is the
// first user of an empty one.
TEST(RenderTargetSemantics, TargetsWithDifferentSampleCountsBothRenderInOneRun)
{
    GraphicsDevice device;
    const Color first(0x11, 0x99, 0x22, 0xFF);
    const Color second(0x99, 0x22, 0x44, 0xFF);

    RenderTarget2D multisampled(device, kSize, kSize, false, SurfaceFormat::Color,
                                DepthFormat::None, 4, RenderTargetUsage::PreserveContents);
    PaintTarget(device, multisampled, first);
    const Color fromMultisampled = FirstTexel(multisampled);

    RenderTarget2D single(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::PreserveContents);
    PaintTarget(device, single, second);
    const Color fromSingle = FirstTexel(single);

    EXPECT_EQ(fromMultisampled.getPackedValueProperty(), first.getPackedValueProperty())
        << "the multisampled target resolved to the colour it was cleared to";
    EXPECT_EQ(fromSingle.getPackedValueProperty(), second.getPackedValueProperty())
        << "and the single-sampled target drawn AFTER it did too -- a pipeline cached against the "
           "previous sample count would fail validation or land in the wrong attachment shape";
    EXPECT_LE(EffectiveSamples(multisampled.getMultiSampleCountProperty()), 4);
    EXPECT_EQ(EffectiveSamples(single.getMultiSampleCountProperty()), 1)
        << "a target created with 0 is not multisampled, however the renderer spells it";

    // And back again, so the transition is exercised in both directions rather than once.
    const Color third(0x44, 0x22, 0x99, 0xFF);
    PaintTarget(device, multisampled, third);
    EXPECT_EQ(FirstTexel(multisampled).getPackedValueProperty(), third.getPackedValueProperty())
        << "returning to the multisampled target after a single-sampled one works too";
}

// A round trip through every cube face: each face is its own attachment, and a renderer that shared
// one face view writes all six with the last colour.
TEST(RenderTargetSemantics, EachRenderTargetCubeFaceKeepsItsOwnContent)
{
    GraphicsDevice device;
    std::unique_ptr<RenderTargetCube> cube;
    try
    {
        cube = std::make_unique<RenderTargetCube>(device, kSize, false, SurfaceFormat::Color,
                                                  DepthFormat::None, 0,
                                                  RenderTargetUsage::PreserveContents);
    }
    catch (const System::NotSupportedException&)
    {
        GTEST_SKIP() << "this renderer has no RenderTargetCube";
    }
    catch (const std::runtime_error&)
    {
        GTEST_SKIP() << "this renderer has no RenderTargetCube";
    }

    constexpr std::array<CubeMapFace, 6> kFaces{
        CubeMapFace::PositiveX, CubeMapFace::NegativeX, CubeMapFace::PositiveY,
        CubeMapFace::NegativeY, CubeMapFace::PositiveZ, CubeMapFace::NegativeZ};
    const auto faceColour = [](int face) {
        return Color(static_cast<SharpRuntime::bytecs>(30 + face * 35),
                     static_cast<SharpRuntime::bytecs>(200 - face * 25),
                     static_cast<SharpRuntime::bytecs>(60 + face * 15), 255);
    };

    for (int face = 0; face < 6; ++face)
    {
        device.SetRenderTarget(cube.get(), kFaces[static_cast<std::size_t>(face)]);
        device.Clear(faceColour(face));
    }
    device.SetRenderTarget(static_cast<RenderTargetCube*>(nullptr), CubeMapFace::PositiveX);

    for (int face = 0; face < 6; ++face)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        try
        {
            cube->GetData(kFaces[static_cast<std::size_t>(face)], pixels.data(),
                          static_cast<int>(pixels.size()));
        }
        catch (const System::NotSupportedException&)
        {
            GTEST_SKIP() << "this renderer cannot read a RenderTargetCube face back";
        }
        EXPECT_EQ(pixels[0].getPackedValueProperty(), faceColour(face).getPackedValueProperty())
            << "face " << face << " -- a renderer sharing one face view writes every face with the "
               "last colour, and all six would read back as face 5";
    }
}
