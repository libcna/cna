// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2090: indirect draws.
//
// The claim under test is not "a triangle appeared" -- an ordinary draw can do that. It is that the
// numbers deciding how much to draw were read out of GPU memory. So the same geometry, the same
// effect and the same target are drawn twice, and the ONLY difference between the two is what a
// buffer contains: one instance, then zero. A route that ignored the buffer and used a CPU count
// would produce the same picture both times, and that is what makes the pair of assertions mean
// something.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/GraphicsMemoryBarrier.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/IndirectDrawArguments.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using CNA::GraphicsCapability;
using CNA::GraphicsMemoryBarrier;
using CNA::IndirectDrawArguments;
using CNA::IndirectDrawIndexedArguments;
using CNA::Graphics::StorageBuffer;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

constexpr int kSize = 16;

/// A triangle that covers the whole target in clip space, with identity matrices. Wound clockwise
/// because XNA's default rasterizer culls counter-clockwise faces, and a culled triangle looks
/// exactly like a draw that never happened.
std::array<VertexPositionColor, 3> CoveringTriangle()
{
    return {VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), Color::White),
            VertexPositionColor(Vector3(-1.0f, 3.0f, 0.0f), Color::White),
            VertexPositionColor(Vector3(3.0f, -1.0f, 0.0f), Color::White)};
}

int CountLitPixels(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    int lit = 0;
    for (const Color& texel : pixels)
        if (texel.getRProperty() > 128) ++lit;
    return lit;
}

/// True when this renderer can run the whole route: the draw itself, and a buffer to put the
/// arguments in. On EasyGL the second is the narrower of the two -- a storage buffer is an SSBO,
/// which is GL ES 3.1 / desktop GL 4.3, while the indirect draw itself is GL 4.0.
bool CanRunIndirect(GraphicsDevice& device)
{
    return device.SupportsCapability(GraphicsCapability::IndirectDraw) &&
           device.SupportsCapability(GraphicsCapability::ComputeShaders);
}

TEST(IndirectDrawTest, TheArgumentLayoutIsTheOneEveryApiReads)
{
    // The GPU reads these structs verbatim, so their layout is the contract rather than an
    // implementation detail: a compute shader declaring the same words in the same order has to
    // land on the same bytes. Asserted field by field, because a compiler free to reorder or pad
    // would break the draw with nothing else changing.
    EXPECT_EQ(sizeof(IndirectDrawArguments), 16u);
    EXPECT_EQ(offsetof(IndirectDrawArguments, VertexCount), 0u);
    EXPECT_EQ(offsetof(IndirectDrawArguments, InstanceCount), 4u);
    EXPECT_EQ(offsetof(IndirectDrawArguments, FirstVertex), 8u);
    EXPECT_EQ(offsetof(IndirectDrawArguments, BaseInstance), 12u);

    EXPECT_EQ(sizeof(IndirectDrawIndexedArguments), 20u);
    EXPECT_EQ(offsetof(IndirectDrawIndexedArguments, IndexCount), 0u);
    EXPECT_EQ(offsetof(IndirectDrawIndexedArguments, InstanceCount), 4u);
    EXPECT_EQ(offsetof(IndirectDrawIndexedArguments, FirstIndex), 8u);
    EXPECT_EQ(offsetof(IndirectDrawIndexedArguments, BaseVertex), 12u);
    EXPECT_EQ(offsetof(IndirectDrawIndexedArguments, BaseInstance), 16u);
}

TEST(IndirectDrawTest, TheCommandBarrierIsItsOwnBitAndIsPartOfAll)
{
    // Writing a count through a storage binding and fetching it as a command are two accesses, and
    // ordering only the first is the bug this bit exists to make expressible.
    EXPECT_NE(static_cast<int>(GraphicsMemoryBarrier::IndirectCommand),
              static_cast<int>(GraphicsMemoryBarrier::ShaderStorage));
    EXPECT_TRUE(CNA::HasBarrier(GraphicsMemoryBarrier::All,
                                GraphicsMemoryBarrier::IndirectCommand));
}

TEST(IndirectDrawTest, ARendererWithoutTheCapabilityRefusesByName)
{
    GraphicsDevice device;
    if (device.SupportsCapability(GraphicsCapability::IndirectDraw))
        GTEST_SKIP() << "this renderer does support indirect drawing";
    if (!device.SupportsCapability(GraphicsCapability::ComputeShaders))
        GTEST_SKIP() << "no storage buffer can be allocated to hold the arguments";

    StorageBuffer arguments(device, sizeof(IndirectDrawArguments));
    EXPECT_THROW(device.DrawPrimitivesIndirectEXT(PrimitiveType::TriangleList,
                                                  *arguments.getRendererEXT(), 0),
                 System::NotSupportedException);
}

TEST(IndirectDrawTest, TheArgumentRangeIsCheckedEvenThoughTheCountsCannotBe)
{
    GraphicsDevice device;
    if (!CanRunIndirect(device)) GTEST_SKIP() << "this renderer has no indirect draw route";

    StorageBuffer arguments(device, sizeof(IndirectDrawArguments));
    const auto triangle = CoveringTriangle();
    VertexBuffer buffer(device, 3);
    buffer.SetData(triangle.data(), 3);
    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    device.SetVertexBuffer(&buffer);
    effect.Apply();

    EXPECT_THROW(device.DrawPrimitivesIndirectEXT(PrimitiveType::TriangleList,
                                                  *arguments.getRendererEXT(), -4),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(device.DrawPrimitivesIndirectEXT(PrimitiveType::TriangleList,
                                                  *arguments.getRendererEXT(), 2),
                 System::ArgumentOutOfRangeException)
        << "an unaligned argument address is invalid on every API that has this draw";
    EXPECT_THROW(device.DrawPrimitivesIndirectEXT(PrimitiveType::TriangleList,
                                                  *arguments.getRendererEXT(), 4),
                 System::ArgumentOutOfRangeException)
        << "the last word of the arguments would lie past the end of the buffer";
    device.SetVertexBuffer(nullptr);
}

TEST(IndirectDrawTest, ADrawWithNothingBoundStillRefusesBeforeTheGpuSeesIt)
{
    GraphicsDevice device;
    if (!CanRunIndirect(device)) GTEST_SKIP() << "this renderer has no indirect draw route";

    StorageBuffer arguments(device, sizeof(IndirectDrawArguments));
    device.SetVertexBuffer(nullptr);
    EXPECT_THROW(device.DrawPrimitivesIndirectEXT(PrimitiveType::TriangleList,
                                                  *arguments.getRendererEXT(), 0),
                 std::runtime_error);
}

TEST(IndirectDrawTest, TheCountsReallyComeFromTheBuffer)
{
    GraphicsDevice device;
    if (!CanRunIndirect(device)) GTEST_SKIP() << "this renderer has no indirect draw route";

    const auto triangle = CoveringTriangle();
    VertexBuffer buffer(device, 3);
    buffer.SetData(triangle.data(), 3);
    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    RenderTarget2D target(device, kSize, kSize);
    StorageBuffer arguments(device, sizeof(IndirectDrawArguments));

    const auto draw = [&](const IndirectDrawArguments& args) {
        arguments.setBytes(&args, sizeof(args));
        device.SetVertexBuffer(&buffer);
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        effect.Apply();
        device.DrawPrimitivesIndirectEXT(PrimitiveType::TriangleList,
                                         *arguments.getRendererEXT(), 0);
        device.SetRenderTarget(nullptr);
        device.SetVertexBuffer(nullptr);
        return CountLitPixels(target);
    };

    // The control: the same triangle through the ordinary route. If this does not cover the
    // target then nothing below says anything about indirect drawing.
    device.SetVertexBuffer(&buffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
    device.SetRenderTarget(nullptr);
    device.SetVertexBuffer(nullptr);
    ASSERT_EQ(CountLitPixels(target), kSize * kSize) << "the control draw covered nothing";

    IndirectDrawArguments drawOne;
    drawOne.VertexCount = 3;
    drawOne.InstanceCount = 1;
    EXPECT_EQ(draw(drawOne), kSize * kSize)
        << "the covering triangle did not reach the target at all";

    // Same geometry, same effect, same target, same call -- one word of GPU memory changed.
    IndirectDrawArguments drawNone = drawOne;
    drawNone.InstanceCount = 0;
    EXPECT_EQ(draw(drawNone), 0)
        << "zero instances still drew, so the counts are not being read from the buffer";

    // And the vertex count is read from it too, not only the instance count: one vertex cannot
    // make a triangle, so nothing may be rasterised.
    IndirectDrawArguments drawPartial = drawOne;
    drawPartial.VertexCount = 1;
    EXPECT_EQ(draw(drawPartial), 0);
}

TEST(IndirectDrawTest, TheArgumentsCanSitAtAnOffsetInsideTheBuffer)
{
    // What makes one buffer able to hold a whole frame's worth of draws, which is the shape
    // MOD-2091 needs: the offset selects which command this draw runs.
    GraphicsDevice device;
    if (!CanRunIndirect(device)) GTEST_SKIP() << "this renderer has no indirect draw route";

    const auto triangle = CoveringTriangle();
    VertexBuffer buffer(device, 3);
    buffer.SetData(triangle.data(), 3);
    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    RenderTarget2D target(device, kSize, kSize);

    std::array<IndirectDrawArguments, 2> commands{};
    commands[0].VertexCount = 3;
    commands[0].InstanceCount = 0;   // the one NOT selected draws nothing, so a wrong offset shows
    commands[1].VertexCount = 3;
    commands[1].InstanceCount = 1;

    StorageBuffer arguments(device, sizeof(commands));
    arguments.setBytes(commands.data(), sizeof(commands));

    device.SetVertexBuffer(&buffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawPrimitivesIndirectEXT(PrimitiveType::TriangleList, *arguments.getRendererEXT(),
                                     static_cast<int>(sizeof(IndirectDrawArguments)));
    device.SetRenderTarget(nullptr);
    device.SetVertexBuffer(nullptr);

    EXPECT_EQ(CountLitPixels(target), kSize * kSize)
        << "the second command was not the one that ran";
}

TEST(IndirectDrawTest, TheIndexedRouteReadsItsOwnFiveWordCommand)
{
    // The indexed command is one word longer and its counts mean index elements rather than
    // vertices, so it gets its own case rather than being assumed to follow from the other.
    GraphicsDevice device;
    if (!CanRunIndirect(device)) GTEST_SKIP() << "this renderer has no indirect draw route";

    const auto triangle = CoveringTriangle();
    VertexBuffer vertices(device, 3);
    vertices.SetData(triangle.data(), 3);
    const std::array<std::uint16_t, 3> order{0, 1, 2};
    IndexBuffer indices(device, 3);
    indices.SetData(order.data(), 3);

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    RenderTarget2D target(device, kSize, kSize);
    StorageBuffer arguments(device, sizeof(IndirectDrawIndexedArguments));

    const auto draw = [&](const IndirectDrawIndexedArguments& args) {
        arguments.setBytes(&args, sizeof(args));
        device.SetVertexBuffer(&vertices);
        device.SetIndexBuffer(&indices);
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        effect.Apply();
        device.DrawIndexedPrimitivesIndirectEXT(PrimitiveType::TriangleList,
                                                *arguments.getRendererEXT(), 0);
        device.SetRenderTarget(nullptr);
        device.SetIndexBuffer(nullptr);
        device.SetVertexBuffer(nullptr);
        return CountLitPixels(target);
    };

    IndirectDrawIndexedArguments drawOne;
    drawOne.IndexCount = 3;
    drawOne.InstanceCount = 1;
    EXPECT_EQ(draw(drawOne), kSize * kSize);

    IndirectDrawIndexedArguments drawNone = drawOne;
    drawNone.IndexCount = 0;
    EXPECT_EQ(draw(drawNone), 0)
        << "a zero index count still drew, so the command was not read";
}

TEST(IndirectDrawTest, TheIndexedRouteRefusesWithoutAnIndexBuffer)
{
    GraphicsDevice device;
    if (!CanRunIndirect(device)) GTEST_SKIP() << "this renderer has no indirect draw route";

    const auto triangle = CoveringTriangle();
    VertexBuffer vertices(device, 3);
    vertices.SetData(triangle.data(), 3);
    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    StorageBuffer arguments(device, sizeof(IndirectDrawIndexedArguments));

    device.SetVertexBuffer(&vertices);
    device.SetIndexBuffer(nullptr);
    effect.Apply();
    EXPECT_THROW(device.DrawIndexedPrimitivesIndirectEXT(PrimitiveType::TriangleList,
                                                         *arguments.getRendererEXT(), 0),
                 std::runtime_error);
    device.SetVertexBuffer(nullptr);
}

} // namespace

#endif // CNA_CNAEXT
