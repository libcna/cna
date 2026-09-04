// SPDX-License-Identifier: MS-PL
//
// plans/plan_wicked.md WICKED-77: instanced draws must honour the geometry stream's VertexOffset.
//
// The instanced route does not fold VertexOffset into baseVertex the way the ordinary routes do:
// GraphicsDevice::DrawInstancedPrimitives fills the stream table with foldedOffset = 0, so every
// stream carries its whole public offset and the renderer owes it at bind time. This renderer's
// single-geometry-stream binding dropped it, so a draw whose offset selected a later record read
// the wrong bytes -- the selected quad never appeared while the ordinary indexed route rendered
// the identical buffer, declaration and offset correctly.
//
// The scene makes each failure mode loud and distinct. The buffer holds a DECOY quad on the left
// half of clip space followed by a TARGET quad on the right half, in different colours, and the
// draw binds the buffer with a VertexOffset that selects the target records:
//   * offset honoured  -> right half lit in the target colour, left half empty;
//   * offset ignored   -> left half lit in the decoy colour, right half empty;
//   * offset in bytes  -> a misaligned position stream, neither half lit as declared.

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::GraphicsCapability;

namespace
{
    constexpr int kTargetSize = 64;

    // 0xCD filler: bytes no declared record claims must read loudly if a wrong offset picks them
    // up as a position or a colour.
    constexpr std::uint8_t kFiller = 0xCD;

    struct Rgba
    {
        int r, g, b, a;
    };
    constexpr Rgba kDecoyColor{255, 0, 0, 255};
    constexpr Rgba kTargetColor{0, 255, 0, 255};
    constexpr Rgba kClearColor{16, 24, 32, 255};

    bool Near(int lhs, int rhs) { return (lhs > rhs ? lhs - rhs : rhs - lhs) <= 3; }
    bool Matches(const Color& pixel, const Rgba& expected)
    {
        return Near(pixel.getRProperty(), expected.r) && Near(pixel.getGProperty(), expected.g) &&
               Near(pixel.getBProperty(), expected.b) && Near(pixel.getAProperty(), expected.a);
    }
}

class WickedGeometryVertexOffsetTest : public ::testing::Test
{
protected:
    GraphicsDevice device;

    void SetUp() override
    {
        ASSERT_TRUE(device.SupportsCapability(GraphicsCapability::ThreeD));
        // Wicked's Vulkan device binds a negative-height viewport, which reverses the effective
        // winding -- a default-culled quad reads exactly like "the renderer drew nothing", so the
        // cull state is explicit here.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
    }

    /// One quad = 4 records. The decoy fills the LEFT half of clip space, the target the RIGHT.
    static void WriteQuad(std::uint8_t* base, int stride, int positionOffset, int colorOffset,
                          float x0, float x1, const Rgba& color)
    {
        const float y0 = -0.6f;
        const float y1 = 0.6f;
        const float corners[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
        for (int v = 0; v < 4; ++v)
        {
            std::uint8_t* record = base + static_cast<std::size_t>(v) * stride;
            const float pos[3] = {corners[v][0], corners[v][1], 0.5f};
            std::memcpy(record + positionOffset, pos, sizeof(pos));
            const std::uint8_t rgba[4] = {
                static_cast<std::uint8_t>(color.r), static_cast<std::uint8_t>(color.g),
                static_cast<std::uint8_t>(color.b), static_cast<std::uint8_t>(color.a)};
            std::memcpy(record + colorOffset, rgba, sizeof(rgba));
        }
    }

    /// @p decoyRecords records of decoy quad data (padded with filler when it is not a multiple
    /// of 4) followed by the 4 target records. VertexOffset = @p decoyRecords selects the target.
    static std::vector<std::uint8_t> BuildStream(int stride, int positionOffset, int colorOffset,
                                                 int decoyRecords)
    {
        std::vector<std::uint8_t> bytes(
            static_cast<std::size_t>(decoyRecords + 4) * stride, kFiller);
        for (int first = 0; first + 4 <= decoyRecords; first += 4)
        {
            WriteQuad(bytes.data() + static_cast<std::size_t>(first) * stride, stride,
                      positionOffset, colorOffset, -0.9f, -0.1f, kDecoyColor);
        }
        WriteQuad(bytes.data() + static_cast<std::size_t>(decoyRecords) * stride, stride,
                  positionOffset, colorOffset, 0.1f, 0.9f, kTargetColor);
        return bytes;
    }

    struct HalfCounts
    {
        int leftDecoy = 0;
        int leftAny = 0;
        int rightTarget = 0;
        int rightAny = 0;
    };

    HalfCounts Render(int stride, int positionOffset, int colorOffset, int decoyRecords,
                      bool instanced, int instanceCount)
    {
        const std::vector<std::uint8_t> bytes =
            BuildStream(stride, positionOffset, colorOffset, decoyRecords);
        const int totalRecords = decoyRecords + 4;

        std::vector<VertexElement> elements;
        elements.emplace_back(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0);
        elements.emplace_back(colorOffset, VertexElementFormat::Color,
                              VertexElementUsage::Color, 0);
        if (stride == 24)
        {
            elements.emplace_back(16, VertexElementFormat::Vector2,
                                  VertexElementUsage::TextureCoordinate, 0);
        }
        VertexDeclaration declaration(stride, elements);
        VertexBuffer meshBuffer(device, declaration, totalRecords, BufferUsage::None);
        meshBuffer.SetDataRaw(bytes.data(), totalRecords, stride);

        // The per-instance stream: identity world matrices, one 64-byte record per instance.
        VertexDeclaration instanceDecl(
            64, {VertexElement(0, VertexElementFormat::Vector4,
                               VertexElementUsage::TextureCoordinate, 1),
                 VertexElement(16, VertexElementFormat::Vector4,
                               VertexElementUsage::TextureCoordinate, 2),
                 VertexElement(32, VertexElementFormat::Vector4,
                               VertexElementUsage::TextureCoordinate, 3),
                 VertexElement(48, VertexElementFormat::Vector4,
                               VertexElementUsage::TextureCoordinate, 4)});
        std::vector<float> matrices(static_cast<std::size_t>(instanceCount) * 16, 0.0f);
        for (int i = 0; i < instanceCount; ++i)
        {
            float* m = matrices.data() + static_cast<std::size_t>(i) * 16;
            m[0] = m[5] = m[10] = m[15] = 1.0f;
        }
        VertexBuffer instanceBuffer(device, instanceDecl, instanceCount, BufferUsage::None);
        instanceBuffer.SetDataRaw(matrices.data(), instanceCount, 64);

        const std::array<std::uint16_t, 6> indices = {0, 1, 2, 0, 2, 3};
        IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits,
                                static_cast<int>(indices.size()), BufferUsage::None);
        indexBuffer.SetData(indices.data(), static_cast<int>(indices.size()));

        RenderTarget2D target(device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);

        if (instanced)
        {
            device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, decoyRecords, 0),
                                     VertexBufferBinding(&instanceBuffer, 0, 1)});
        }
        else
        {
            device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, decoyRecords, 0)});
        }
        device.SetIndexBuffer(&indexBuffer);

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setFogEnabledProperty(false);
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.setAlphaProperty(1.0f);

        device.SetRenderTarget(&target);
        device.Clear(Color(kClearColor.r, kClearColor.g, kClearColor.b, kClearColor.a));
        effect.Apply();
        if (instanced)
            device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2,
                                           instanceCount);
        else
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        device.SetRenderTarget(nullptr);
        device.SetIndexBuffer(nullptr);
        device.SetVertexBuffer(nullptr);

        std::vector<Color> pixels(static_cast<std::size_t>(kTargetSize) * kTargetSize,
                                  Color::Transparent);
        const Rectangle region(0, 0, kTargetSize, kTargetSize);
        target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));

        HalfCounts counts;
        for (int y = 0; y < kTargetSize; ++y)
        {
            for (int x = 0; x < kTargetSize; ++x)
            {
                const Color& p = pixels[static_cast<std::size_t>(y) * kTargetSize + x];
                if (Matches(p, kClearColor))
                    continue;
                if (x < kTargetSize / 2)
                {
                    ++counts.leftAny;
                    if (Matches(p, kDecoyColor))
                        ++counts.leftDecoy;
                }
                else
                {
                    ++counts.rightAny;
                    if (Matches(p, kTargetColor))
                        ++counts.rightTarget;
                }
            }
        }
        return counts;
    }

    void ExpectTargetQuadOnly(const HalfCounts& counts, const char* route)
    {
        // The target quad spans x in [0.1, 0.9] of clip space: about 40% of the 64-pixel width,
        // all of it in the right half. Require a comfortable floor rather than an exact count so
        // edge rasterization differences cannot flake the test.
        EXPECT_GT(counts.rightTarget, 400)
            << route << ": the offset-selected target quad did not render where declared.";
        EXPECT_EQ(counts.leftAny, 0)
            << route << ": pixels lit in the decoy region -- the VertexOffset was ignored and "
                        "the draw read the records before the offset.";
        EXPECT_EQ(counts.rightAny, counts.rightTarget)
            << route << ": the right half carries non-target colours -- the records were read "
                        "from the wrong bytes.";
    }
};

/// The control: the ordinary indexed route has always honoured the offset.
TEST_F(WickedGeometryVertexOffsetTest, OrdinaryIndexedHonorsVertexOffset)
{
    ExpectTargetQuadOnly(Render(16, 0, 12, 4, /*instanced=*/false, 1), "ordinary-indexed");
}

/// A zero offset through the instanced route -- the case that always worked -- must keep working.
TEST_F(WickedGeometryVertexOffsetTest, InstancedZeroVertexOffsetRenders)
{
    ExpectTargetQuadOnly(Render(16, 0, 12, 0, /*instanced=*/true, 2), "instanced-offset0");
}

/// The WICKED-77 defect: a nonzero VertexOffset selecting the records after one whole decoy quad
/// (the fourth-and-later declared records) rendered nothing through the instanced route.
TEST_F(WickedGeometryVertexOffsetTest, InstancedVertexOffsetSelectsLaterRecords)
{
    ExpectTargetQuadOnly(Render(16, 0, 12, 4, /*instanced=*/true, 2), "instanced-offset4");
}

/// The same shape through the instanceCount == 1 degenerate of the instanced route, which is the
/// exact call the shared VertexDeclarationLayoutTest oracle makes.
TEST_F(WickedGeometryVertexOffsetTest, InstancedSingleInstanceHonorsVertexOffset)
{
    ExpectTargetQuadOnly(Render(16, 0, 12, 4, /*instanced=*/true, 1), "instanced-count1");
}

/// A 24-byte stride with a 2-record offset: the byte offset (48) differs loudly from the record
/// count (2), so an implementation that applied the offset in bytes instead of vertex elements
/// would bind a misaligned stream and fail every expectation here.
TEST_F(WickedGeometryVertexOffsetTest, InstancedVertexOffsetIsVertexElementsNotBytes)
{
    ExpectTargetQuadOnly(Render(24, 0, 12, 2, /*instanced=*/true, 2), "instanced-stride24");
}
