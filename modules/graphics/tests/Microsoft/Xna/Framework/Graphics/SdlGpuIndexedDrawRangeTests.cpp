// SPDX-License-Identifier: MS-PL
// REMED-GFX-117: SDL_GPU indexed draws must propagate the public startIndex/baseVertex arguments
// to the native SDL_DrawGPUIndexedPrimitives first_index/vertex_offset parameters.
//
// SDL_GPU implements no backbuffer readback at all, so RenderTarget2D::GetData is this renderer's
// exact-pixel oracle (the same control REMED-GFX-111 established). Every test in this file renders
// into a render target, unbinds it, and reads the target's own pixels back.

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include <limits>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Graphics::AlphaTestEffect;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::CompareFunction;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::CullMode;
using Microsoft::Xna::Framework::Graphics::DualTextureEffect;
using Microsoft::Xna::Framework::Graphics::DynamicIndexBuffer;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect;
using Microsoft::Xna::Framework::Graphics::PbrEffect;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::SkinnedEffect;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::TextureCube;
using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionTexture;

namespace
{
    constexpr int kTargetSize = 128;

    /// The decoy always sits in the left half of the target and the intended geometry in the
    /// right half, so an addressing failure is a half-of-the-frame difference, not a subtlety.
    constexpr float kDecoyX = -0.55f;
    constexpr float kIntendedX = 0.55f;
    const Vector3 kFacingNormal(0.0f, 0.0f, 1.0f);

    VertexDeclaration PositionColorDeclaration()
    {
        return VertexDeclaration(
            16,
            {
                VertexElement(
                    0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(
                    12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            });
    }

    /// A solid triangle centred on NDC x = @p centerX, wide enough that the pixel at (centerX, 0)
    /// lies well inside it. World/View/Projection stay identity, so the vertex position is the
    /// clip-space position.
    std::array<VertexPositionColor, 3> TriangleAt(float centerX, const Color& color)
    {
        return {
            VertexPositionColor(Vector3(centerX - 0.24f, -0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX + 0.24f, -0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX, 0.45f, 0.5f), color),
        };
    }

    template<std::size_t N>
    void Append(
        std::vector<VertexPositionColor>& destination,
        const std::array<VertexPositionColor, N>& source)
    {
        destination.insert(destination.end(), source.begin(), source.end());
    }

    /// The same triangle TriangleAt() builds, as bare positions, for the vertex formats that carry
    /// no colour of their own.
    std::array<Vector3, 3> TrianglePositions(float centerX)
    {
        return {
            Vector3(centerX - 0.24f, -0.45f, 0.5f),
            Vector3(centerX + 0.24f, -0.45f, 0.5f),
            Vector3(centerX, 0.45f, 0.5f),
        };
    }

    /// A four-vertex triangle strip (two triangles) covering the pixel at (centerX, 0).
    std::array<VertexPositionColor, 4> StripQuadAt(float centerX, const Color& color)
    {
        return {
            VertexPositionColor(Vector3(centerX - 0.2f, 0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX - 0.2f, -0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX + 0.2f, 0.45f, 0.5f), color),
            VertexPositionColor(Vector3(centerX + 0.2f, -0.45f, 0.5f), color),
        };
    }

    /// A two-vertex horizontal segment through the pixel row at NDC y = 0.
    std::array<VertexPositionColor, 2> HorizontalLineAt(float centerX, const Color& color)
    {
        return {
            VertexPositionColor(Vector3(centerX - 0.2f, 0.0f, 0.5f), color),
            VertexPositionColor(Vector3(centerX + 0.2f, 0.0f, 0.5f), color),
        };
    }

    /// Places a point on the centre of its target pixel, so no result depends on undefined
    /// point-edge coverage.
    VertexPositionColor PointAt(int pixelX, int pixelY, const Color& color)
    {
        const float x =
            (2.0f * (static_cast<float>(pixelX) + 0.5f) / static_cast<float>(kTargetSize)) - 1.0f;
        const float y =
            1.0f - (2.0f * (static_cast<float>(pixelY) + 0.5f) / static_cast<float>(kTargetSize));
        return VertexPositionColor(Vector3(x, y, 0.5f), color);
    }

    void FillWhite(Texture2D& texture)
    {
        const std::array<Color, 4> white{
            Color::White, Color::White, Color::White, Color::White};
        texture.SetData(white.data(), 4);
    }

    /// The exact GPU layout skinned3d declares: float3 position, float3 normal, float2 uv,
    /// float4 blend weights, ubyte4 blend indices.
    struct GpuSkinnedVertex
    {
        float x, y, z;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(GpuSkinnedVertex) == 52, "skinned3d requires an exact stride of 52");

    GpuSkinnedVertex MakeSkinnedVertex(const Vector3& position, float u, float v)
    {
        return GpuSkinnedVertex{
            position.X, position.Y, position.Z,
            0.0f, 0.0f, 1.0f,
            u, v,
            1.0f, 0.0f, 0.0f, 0.0f,
            0, 0, 0, 0};
    }

    /// The exact GPU layout pbr3d declares: float3 position, float3 normal, float4 tangent,
    /// float2 uv.
    struct GpuPbrVertex
    {
        float x, y, z;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(GpuPbrVertex) == 48, "pbr3d requires an exact stride of 48");

    GpuPbrVertex MakePbrVertex(const Vector3& position, float u, float v)
    {
        return GpuPbrVertex{
            position.X, position.Y, position.Z,
            0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.0f, 1.0f,
            u, v};
    }

    struct TargetSnapshot
    {
        int width = 0;
        int height = 0;
        std::vector<Color> pixels;

        [[nodiscard]] Color AtNdc(float x, float y = 0.0f) const
        {
            const int pixelX = std::clamp(
                static_cast<int>((x * 0.5f + 0.5f) * static_cast<float>(width - 1)),
                0, width - 1);
            const int pixelY = std::clamp(
                static_cast<int>(((-y) * 0.5f + 0.5f) * static_cast<float>(height - 1)),
                0, height - 1);
            return pixels[
                static_cast<std::size_t>(pixelY) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(pixelX)];
        }

        [[nodiscard]] int CountExact(const Color& color) const
        {
            int total = 0;
            for (const Color& pixel : pixels)
            {
                if (pixel == color)
                    ++total;
            }
            return total;
        }
    };

    TargetSnapshot CaptureTarget(RenderTarget2D& target)
    {
        TargetSnapshot snapshot;
        snapshot.width = kTargetSize;
        snapshot.height = kTargetSize;
        snapshot.pixels.assign(
            static_cast<std::size_t>(kTargetSize) * static_cast<std::size_t>(kTargetSize),
            Color::Transparent);
        const Rectangle region(0, 0, kTargetSize, kTargetSize);
        target.GetData(
            0, &region, snapshot.pixels.data(), 0,
            static_cast<int>(snapshot.pixels.size()));
        return snapshot;
    }

    void ExpectExactColor(const Color& actual, const Color& expected, const char* label)
    {
        EXPECT_EQ(expected.getRProperty(), actual.getRProperty()) << label;
        EXPECT_EQ(expected.getGProperty(), actual.getGProperty()) << label;
        EXPECT_EQ(expected.getBProperty(), actual.getBProperty()) << label;
        EXPECT_EQ(expected.getAProperty(), actual.getAProperty()) << label;
    }

    void ExpectColorAbsent(
        const TargetSnapshot& snapshot, const Color& color, const char* label)
    {
        EXPECT_EQ(0, snapshot.CountExact(color))
            << label << " (colour must not appear anywhere in the target)";
    }

    /// The family probe. Every pipeline family renders the same two-slot geometry, so a family
    /// that still submits vertex_offset = 0 lights the left half of the target and leaves the
    /// right half black. Shaded families produce family-specific colours, so the discriminator is
    /// which half is lit at all -- against an exactly black cleared background.
    int CountLitInHalf(const TargetSnapshot& snapshot, bool leftHalf)
    {
        int total = 0;
        for (int y = 0; y < snapshot.height; ++y)
        {
            for (int x = 0; x < snapshot.width; ++x)
            {
                const bool inHalf =
                    leftHalf ? (x < snapshot.width / 2) : (x >= snapshot.width / 2);
                if (!inHalf)
                    continue;
                const Color& pixel = snapshot.pixels[
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(snapshot.width) +
                    static_cast<std::size_t>(x)];
                if (pixel.getRProperty() != 0 || pixel.getGProperty() != 0 ||
                    pixel.getBProperty() != 0)
                {
                    ++total;
                }
            }
        }
        return total;
    }

    void ExpectOnlyIntendedHalfLit(const TargetSnapshot& snapshot, const char* label)
    {
        EXPECT_EQ(0, CountLitInHalf(snapshot, true))
            << label << ": the decoy half must stay exactly black";
        EXPECT_GT(CountLitInHalf(snapshot, false), 0)
            << label << ": the requested geometry must render";
    }

    class SdlGpuIndexedDrawRangeTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        // plan_runtimerenderer.md RTR-P9-5: this whole file used to sit behind
        // `#ifdef CNA_RENDERER_SDL_GPU`, so on every other renderer its 27 tests did not exist and
        // reported nothing at all. The gate belongs in SetUp() rather than in a helper: GTEST_SKIP()
        // returns from the function it is written in, and SetUp() is where GoogleTest itself checks
        // for a skip and suppresses the test body.
        void SetUp() override
        {
            CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::SdlGpu);
        }

        void RequireIndexedRendering()
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support indexed rendering";
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
        }

        void ApplyVertexColorEffect(BasicEffect& effect)
        {
            effect.VertexColorEnabled = true;
            effect.Apply();
        }
    };

    RenderTarget2D MakeTarget(GraphicsDevice& device)
    {
        return RenderTarget2D(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    }
}

// ---------------------------------------------------------------------------
// Isolation control. Exactly the geometry, topology, vertex declaration, culling, depth state and
// readback path the offset tests below use, but with the intended triangle already at index 0 /
// vertex 0 and both offsets zero. If this renders, nothing in the pipeline other than the offsets
// themselves can explain a failure in the tests that follow.
// ---------------------------------------------------------------------------
TEST_F(SdlGpuIndexedDrawRangeTest, ZeroOffsetIndexedDrawRendersTheIntendedTriangle)
{
    RequireIndexedRendering();

    const auto selected = TriangleAt(-0.65f, Color::Red);
    const std::array<VertexPositionColor, 3> vertices{
        selected[0], selected[1], selected[2],
    };
    const std::array<std::uint16_t, 3> indices{0, 1, 2};

    VertexBuffer vertexBuffer(device, PositionColorDeclaration(), 3, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 3);
    indexBuffer.SetData(indices.data(), 3);

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot pixels = CaptureTarget(target);
    ExpectExactColor(pixels.AtNdc(-0.65f), Color::Red, "zero-offset indexed control");
    ExpectExactColor(pixels.AtNdc(0.0f), Color::Black, "zero-offset control leaves centre clear");
    ExpectExactColor(pixels.AtNdc(0.65f), Color::Black, "zero-offset control leaves right clear");
}

// ---------------------------------------------------------------------------
// Proof 1: a nonzero startIndex alone must select its own index range.
// ---------------------------------------------------------------------------
TEST_F(SdlGpuIndexedDrawRangeTest, IndexedDrawHonorsNonzeroStartIndex)
{
    RequireIndexedRendering();

    const auto prefixDecoy = TriangleAt(0.0f, Color::Lime);
    const auto selected = TriangleAt(-0.65f, Color::Red);
    const auto suffixDecoy = TriangleAt(0.65f, Color::Blue);
    std::vector<VertexPositionColor> vertices;
    Append(vertices, prefixDecoy);
    Append(vertices, selected);
    Append(vertices, suffixDecoy);
    // Every index is valid and every triangle is visible; only the requested range may be drawn.
    const std::array<std::uint16_t, 9> indices{0, 1, 2, 3, 4, 5, 6, 7, 8};

    VertexBuffer vertexBuffer(device, PositionColorDeclaration(), 9, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 9, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 9);
    indexBuffer.SetData(indices.data(), 9);

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 3, 3, 3, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot pixels = CaptureTarget(target);
    ExpectExactColor(pixels.AtNdc(-0.65f), Color::Red, "startIndex selects its own range");
    ExpectColorAbsent(pixels, Color::Lime, "index prefix decoy before startIndex");
    ExpectColorAbsent(pixels, Color::Blue, "index suffix decoy after the consumed count");
}

// ---------------------------------------------------------------------------
// Proof 2: a nonzero baseVertex alone must be added to every decoded index exactly once.
// ---------------------------------------------------------------------------
TEST_F(SdlGpuIndexedDrawRangeTest, IndexedDrawHonorsPositiveBaseVertex)
{
    RequireIndexedRendering();

    const auto unbasedDecoy = TriangleAt(0.0f, Color::Lime);
    const auto based = TriangleAt(-0.65f, Color::Red);
    const auto doubleBasedDecoy = TriangleAt(0.65f, Color::Blue);
    std::vector<VertexPositionColor> vertices;
    Append(vertices, unbasedDecoy);       // vertices 0..2, reached only when baseVertex is dropped
    Append(vertices, based);              // vertices 3..5, the intended result
    Append(vertices, doubleBasedDecoy);   // vertices 6..8, reached only if baseVertex is added twice
    const std::array<std::uint16_t, 3> indices{0, 1, 2};

    VertexBuffer vertexBuffer(device, PositionColorDeclaration(), 9, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 9);
    indexBuffer.SetData(indices.data(), 3);

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot pixels = CaptureTarget(target);
    ExpectExactColor(pixels.AtNdc(-0.65f), Color::Red, "baseVertex rebases decoded indices");
    ExpectColorAbsent(pixels, Color::Lime, "unbased decoy reached only when baseVertex is dropped");
    ExpectColorAbsent(pixels, Color::Blue, "decoy reached only if baseVertex were added twice");
}

// ---------------------------------------------------------------------------
// Proof 3: startIndex and baseVertex must compose, and primitiveCount must still bound the
// consumed index count exactly.
// ---------------------------------------------------------------------------
TEST_F(SdlGpuIndexedDrawRangeTest, IndexedDrawCombinesStartIndexBaseVertexAndCount)
{
    RequireIndexedRendering();

    const auto unbasedDecoy = TriangleAt(-0.75f, Color::Lime);
    const auto basedPrefix = TriangleAt(-0.25f, Color::Yellow);
    const auto selected = TriangleAt(0.25f, Color::Red);
    const auto basedSuffix = TriangleAt(0.75f, Color::Blue);
    std::vector<VertexPositionColor> vertices;
    Append(vertices, unbasedDecoy);   // 0..2
    Append(vertices, basedPrefix);    // 3..5
    Append(vertices, selected);       // 6..8
    Append(vertices, basedSuffix);    // 9..11
    const std::array<std::uint16_t, 9> indices{0, 1, 2, 3, 4, 5, 6, 7, 8};

    VertexBuffer vertexBuffer(device, PositionColorDeclaration(), 12, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 9, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 12);
    indexBuffer.SetData(indices.data(), 9);

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);
    // startIndex 3 selects index elements {3,4,5} = {3,4,5}; baseVertex 3 rebases them to 6..8.
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 9, 3, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot pixels = CaptureTarget(target);
    ExpectExactColor(pixels.AtNdc(0.25f), Color::Red, "combined startIndex and baseVertex");
    ExpectColorAbsent(pixels, Color::Lime, "combined draw ignored the unbased decoy");
    ExpectColorAbsent(pixels, Color::Yellow, "combined draw ignored the based prefix");
    ExpectColorAbsent(pixels, Color::Blue, "primitiveCount still bounds the consumed range");
}

// ---------------------------------------------------------------------------
// Index width, buffer kind and topology.
// ---------------------------------------------------------------------------
TEST_F(SdlGpuIndexedDrawRangeTest, IndexedDrawHonorsOffsetsWithThirtyTwoBitIndices)
{
    RequireIndexedRendering();

    const auto unbasedDecoy = TriangleAt(-0.75f, Color::Lime);
    const auto basedPrefix = TriangleAt(-0.25f, Color::Yellow);
    const auto selected = TriangleAt(0.25f, Color::Red);
    const auto basedSuffix = TriangleAt(0.75f, Color::Blue);
    std::vector<VertexPositionColor> vertices;
    Append(vertices, unbasedDecoy);
    Append(vertices, basedPrefix);
    Append(vertices, selected);
    Append(vertices, basedSuffix);
    const std::array<std::uint32_t, 9> indices{0, 1, 2, 3, 4, 5, 6, 7, 8};

    VertexBuffer vertexBuffer(device, PositionColorDeclaration(), 12, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::ThirtyTwoBits, 9, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 12);
    indexBuffer.SetData(indices.data(), 9);

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 9, 3, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot pixels = CaptureTarget(target);
    ExpectExactColor(pixels.AtNdc(0.25f), Color::Red, "32-bit indices honour both offsets");
    ExpectColorAbsent(pixels, Color::Lime, "32-bit unbased decoy");
    ExpectColorAbsent(pixels, Color::Yellow, "32-bit based prefix");
    ExpectColorAbsent(pixels, Color::Blue, "32-bit based suffix");
}

TEST_F(SdlGpuIndexedDrawRangeTest, IndexedDrawHonorsOffsetsWithDynamicBuffers)
{
    RequireIndexedRendering();

    const auto unbasedDecoy = TriangleAt(-0.75f, Color::Lime);
    const auto basedPrefix = TriangleAt(-0.25f, Color::Yellow);
    const auto selected = TriangleAt(0.25f, Color::Red);
    const auto basedSuffix = TriangleAt(0.75f, Color::Blue);
    std::vector<VertexPositionColor> vertices;
    Append(vertices, unbasedDecoy);
    Append(vertices, basedPrefix);
    Append(vertices, selected);
    Append(vertices, basedSuffix);
    const std::array<std::uint16_t, 9> indices{0, 1, 2, 3, 4, 5, 6, 7, 8};

    DynamicVertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 12, BufferUsage::None);
    DynamicIndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 9, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 0, 12, SetDataOptions::Discard);
    indexBuffer.SetData(indices.data(), 0, 9, SetDataOptions::Discard);

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 9, 3, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot pixels = CaptureTarget(target);
    ExpectExactColor(pixels.AtNdc(0.25f), Color::Red, "dynamic buffers honour both offsets");
    ExpectColorAbsent(pixels, Color::Lime, "dynamic unbased decoy");
    ExpectColorAbsent(pixels, Color::Yellow, "dynamic based prefix");
    ExpectColorAbsent(pixels, Color::Blue, "dynamic based suffix");
}

// SDL_GPU supports all five CNA topologies natively (REMED-GFX-111 established the mapping), so
// every one of them owes the same addressing contract. Strips and lists differ in how
// primitiveCount becomes an index count, which is exactly what the resolver derives.
TEST_F(SdlGpuIndexedDrawRangeTest, IndexedTriangleStripHonorsOffsets)
{
    RequireIndexedRendering();

    const auto decoyStrip = StripQuadAt(-0.6f, Color::Lime);
    const auto selectedStrip = StripQuadAt(0.6f, Color::Red);
    std::vector<VertexPositionColor> vertices;
    Append(vertices, decoyStrip);      // vertices 0..3
    Append(vertices, selectedStrip);   // vertices 4..7
    // Index elements 0..3 are the decoy strip; 4..7 are the intended one.
    const std::array<std::uint16_t, 8> indices{0, 1, 2, 3, 0, 1, 2, 3};

    VertexBuffer vertexBuffer(device, PositionColorDeclaration(), 8, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 8, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 8);
    indexBuffer.SetData(indices.data(), 8);

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);
    // A 2-primitive strip consumes exactly 4 indices, starting at element 4, rebased by 4.
    device.DrawIndexedPrimitives(PrimitiveType::TriangleStrip, 4, 0, 4, 4, 2);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot pixels = CaptureTarget(target);
    ExpectExactColor(pixels.AtNdc(0.6f), Color::Red, "triangle strip honours both offsets");
    ExpectColorAbsent(pixels, Color::Lime, "triangle strip decoy");
}

TEST_F(SdlGpuIndexedDrawRangeTest, IndexedLineListHonorsOffsets)
{
    RequireIndexedRendering();

    std::vector<VertexPositionColor> vertices;
    Append(vertices, HorizontalLineAt(-0.5f, Color::Lime));   // vertices 0..1
    Append(vertices, HorizontalLineAt(0.5f, Color::Red));     // vertices 2..3
    const std::array<std::uint16_t, 4> indices{0, 1, 0, 1};

    VertexBuffer vertexBuffer(device, PositionColorDeclaration(), 4, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 4);
    indexBuffer.SetData(indices.data(), 4);

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::LineList, 2, 0, 2, 2, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot pixels = CaptureTarget(target);
    EXPECT_GT(pixels.CountExact(Color::Red), 0) << "line list honours both offsets";
    ExpectColorAbsent(pixels, Color::Lime, "line list decoy");
}

TEST_F(SdlGpuIndexedDrawRangeTest, IndexedLineStripHonorsOffsets)
{
    RequireIndexedRendering();

    std::vector<VertexPositionColor> vertices;
    Append(vertices, HorizontalLineAt(-0.5f, Color::Lime));
    Append(vertices, HorizontalLineAt(0.5f, Color::Red));
    const std::array<std::uint16_t, 4> indices{0, 1, 0, 1};

    VertexBuffer vertexBuffer(device, PositionColorDeclaration(), 4, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 4);
    indexBuffer.SetData(indices.data(), 4);

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);
    // A 1-primitive line strip consumes exactly 2 indices.
    device.DrawIndexedPrimitives(PrimitiveType::LineStrip, 2, 0, 2, 2, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot pixels = CaptureTarget(target);
    EXPECT_GT(pixels.CountExact(Color::Red), 0) << "line strip honours both offsets";
    ExpectColorAbsent(pixels, Color::Lime, "line strip decoy");
}

// REMED-GFX-111 gave SDL_GPU real point topology but deliberately left indexed addressing at
// offset zero because of this task. Points are the sharpest addressing probe available: one
// consumed index is exactly one lit pixel, so a dropped offset lights a different pixel entirely.
TEST_F(SdlGpuIndexedDrawRangeTest, IndexedPointListHonorsOffsets)
{
    RequireIndexedRendering();

    const std::array<VertexPositionColor, 6> vertices{
        PointAt(kTargetSize / 8, kTargetSize / 8, Color::Lime),
        PointAt(2 * kTargetSize / 8, kTargetSize / 8, Color::Lime),
        PointAt(3 * kTargetSize / 8, kTargetSize / 8, Color::Lime),
        PointAt(5 * kTargetSize / 8, 5 * kTargetSize / 8, Color::Red),
        PointAt(6 * kTargetSize / 8, 5 * kTargetSize / 8, Color::Red),
        PointAt(7 * kTargetSize / 8, 5 * kTargetSize / 8, Color::Red),
    };
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 1, 2};

    VertexBuffer vertexBuffer(device, PositionColorDeclaration(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 6);

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 3, 0, 3, 3, 3);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot pixels = CaptureTarget(target);
    EXPECT_EQ(3, pixels.CountExact(Color::Red)) << "point list honours both offsets";
    ExpectColorAbsent(pixels, Color::Lime, "point list decoys before the requested range");
}

// ---------------------------------------------------------------------------
// Deferred capture: this renderer queues draw commands and submits them at frame end, so each
// queued draw must carry its own offsets rather than reading a live "last set" value.
// ---------------------------------------------------------------------------
TEST_F(SdlGpuIndexedDrawRangeTest, DeferredIndexedDrawsKeepTheirOwnOffsets)
{
    RequireIndexedRendering();

    // Buffer set A: four slots, only two of which any draw may reach.
    std::vector<VertexPositionColor> verticesA;
    Append(verticesA, TriangleAt(-0.75f, Color::Red));      // 0..2   drawn by A
    Append(verticesA, TriangleAt(-0.25f, Color::Magenta));  // 3..5   decoy
    Append(verticesA, TriangleAt(0.75f, Color::Blue));      // 6..8   drawn by A'
    const std::array<std::uint16_t, 9> indicesA{0, 1, 2, 3, 4, 5, 0, 1, 2};

    // Buffer set B: a completely separate pair of buffers with its own offsets.
    std::vector<VertexPositionColor> verticesB;
    Append(verticesB, TriangleAt(-0.25f, Color::Yellow));   // 0..2   decoy
    Append(verticesB, TriangleAt(0.25f, Color::Lime));      // 3..5   drawn by B
    const std::array<std::uint16_t, 6> indicesB{0, 1, 2, 0, 1, 2};

    VertexBuffer vertexBufferA(device, PositionColorDeclaration(), 9, BufferUsage::None);
    IndexBuffer indexBufferA(device, IndexElementSize::SixteenBits, 9, BufferUsage::None);
    vertexBufferA.SetData(verticesA.data(), 9);
    indexBufferA.SetData(indicesA.data(), 9);

    VertexBuffer vertexBufferB(device, PositionColorDeclaration(), 6, BufferUsage::None);
    IndexBuffer indexBufferB(device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
    vertexBufferB.SetData(verticesB.data(), 6);
    indexBufferB.SetData(indicesB.data(), 6);

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);

    device.SetRenderTarget(&target);
    device.Clear(Color::Black);

    // A: startIndex 0, baseVertex 0.
    device.SetVertexBuffer(&vertexBufferA);
    device.SetIndexBuffer(&indexBufferA);
    ApplyVertexColorEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);

    // B: a different buffer pair, different startIndex and baseVertex.
    device.SetVertexBuffer(&vertexBufferB);
    device.SetIndexBuffer(&indexBufferB);
    ApplyVertexColorEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 3, 1);

    // A' back to the first pair, with offsets unlike either previous draw.
    device.SetVertexBuffer(&vertexBufferA);
    device.SetIndexBuffer(&indexBufferA);
    ApplyVertexColorEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 6, 0, 3, 6, 1);

    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot pixels = CaptureTarget(target);
    ExpectExactColor(pixels.AtNdc(-0.75f), Color::Red, "queued draw A kept its own offsets");
    ExpectExactColor(pixels.AtNdc(0.25f), Color::Lime, "queued draw B kept its own offsets");
    ExpectExactColor(pixels.AtNdc(0.75f), Color::Blue, "queued draw A' kept its own offsets");
    ExpectColorAbsent(pixels, Color::Magenta, "no queued draw reached buffer A's decoy slot");
    ExpectColorAbsent(pixels, Color::Yellow, "no queued draw reached buffer B's decoy slot");
}

TEST_F(SdlGpuIndexedDrawRangeTest, DeferredIndexedDrawsKeepTheirOwnOffsetsAcrossTargets)
{
    RequireIndexedRendering();

    std::vector<VertexPositionColor> vertices;
    Append(vertices, TriangleAt(-0.6f, Color::Red));      // 0..2
    Append(vertices, TriangleAt(0.0f, Color::Lime));      // 3..5
    Append(vertices, TriangleAt(0.6f, Color::Blue));      // 6..8
    const std::array<std::uint16_t, 9> indices{0, 1, 2, 0, 1, 2, 0, 1, 2};

    VertexBuffer vertexBuffer(device, PositionColorDeclaration(), 9, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 9, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 9);
    indexBuffer.SetData(indices.data(), 9);

    RenderTarget2D first = MakeTarget(device);
    RenderTarget2D second = MakeTarget(device);
    BasicEffect effect(device);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);

    device.SetRenderTarget(&first);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);

    device.SetRenderTarget(&second);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 3, 1);

    device.SetRenderTarget(&first);
    ApplyVertexColorEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 6, 0, 3, 6, 1);

    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot firstPixels = CaptureTarget(first);
    ExpectExactColor(firstPixels.AtNdc(-0.6f), Color::Red, "target A first draw");
    ExpectExactColor(firstPixels.AtNdc(0.6f), Color::Blue, "target A second draw");
    ExpectColorAbsent(firstPixels, Color::Lime, "target A never received target B's draw");

    const TargetSnapshot secondPixels = CaptureTarget(second);
    ExpectExactColor(secondPixels.AtNdc(0.0f), Color::Lime, "target B draw kept its own offsets");
    ExpectColorAbsent(secondPixels, Color::Red, "target B never received target A's draws");
    ExpectColorAbsent(secondPixels, Color::Blue, "target B never received target A's draws");
}

TEST_F(SdlGpuIndexedDrawRangeTest, MutatingSourceBuffersAfterQueuingDoesNotChangeQueuedDraws)
{
    RequireIndexedRendering();

    std::vector<VertexPositionColor> vertices;
    Append(vertices, TriangleAt(-0.6f, Color::Magenta));  // 0..2   decoy
    Append(vertices, TriangleAt(0.6f, Color::Red));       // 3..5   intended
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 1, 2};

    VertexBuffer vertexBuffer(device, PositionColorDeclaration(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 6);

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 3, 1);

    // Rewrite both sources after the draw was queued but before the frame is rendered.
    std::vector<VertexPositionColor> replacement;
    Append(replacement, TriangleAt(-0.6f, Color::Yellow));
    Append(replacement, TriangleAt(0.6f, Color::Yellow));
    const std::array<std::uint16_t, 6> replacementIndices{3, 4, 5, 3, 4, 5};
    vertexBuffer.SetData(replacement.data(), 6);
    indexBuffer.SetData(replacementIndices.data(), 6);

    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot pixels = CaptureTarget(target);
    ExpectExactColor(
        pixels.AtNdc(0.6f), Color::Red, "the queued draw kept its own captured source data");
    ExpectColorAbsent(pixels, Color::Yellow, "post-queue mutation never reached the queued draw");
    ExpectColorAbsent(pixels, Color::Magenta, "the queued draw kept its own offsets");
}

TEST_F(SdlGpuIndexedDrawRangeTest, DisposingSourceBuffersAfterQueuingRemainsSafe)
{
    RequireIndexedRendering();

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);

    {
        std::vector<VertexPositionColor> vertices;
        Append(vertices, TriangleAt(-0.6f, Color::Magenta));
        Append(vertices, TriangleAt(0.6f, Color::Red));
        const std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 1, 2};

        VertexBuffer vertexBuffer(device, PositionColorDeclaration(), 6, BufferUsage::None);
        IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
        vertexBuffer.SetData(vertices.data(), 6);
        indexBuffer.SetData(indices.data(), 6);

        device.SetVertexBuffer(&vertexBuffer);
        device.SetIndexBuffer(&indexBuffer);
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyVertexColorEffect(effect);
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 3, 1);
        device.SetVertexBuffer(nullptr);
        device.SetIndexBuffer(nullptr);
    }

    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot pixels = CaptureTarget(target);
    ExpectExactColor(
        pixels.AtNdc(0.6f), Color::Red, "a queued draw survives its source buffers");
    ExpectColorAbsent(pixels, Color::Magenta, "the surviving draw kept its own offsets");
}

// ---------------------------------------------------------------------------
// DrawUser* keeps native offsets at zero on purpose: GraphicsDevice copies and rebases the
// caller's vertex and index data into temporary buffers first, so passing the public offsets
// through a second time would apply them twice.
// ---------------------------------------------------------------------------
TEST_F(SdlGpuIndexedDrawRangeTest, DrawUserIndexedPrimitivesRebasesBeforeTheRendererSeesIt)
{
    RequireIndexedRendering();

    // Index offset: the intended triangle is at elements 0..2 of the *vertex* array, and the
    // requested index range starts at element 3 of the index array.
    const std::array<VertexPositionColor, 6> byIndexOffset{
        TriangleAt(0.6f, Color::Red)[0],
        TriangleAt(0.6f, Color::Red)[1],
        TriangleAt(0.6f, Color::Red)[2],
        TriangleAt(-0.6f, Color::Magenta)[0],
        TriangleAt(-0.6f, Color::Magenta)[1],
        TriangleAt(-0.6f, Color::Magenta)[2],
    };
    const std::array<std::uint16_t, 6> indexOffsetIndices{3, 4, 5, 0, 1, 2};

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);

    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, byIndexOffset.data(), 0, 6,
        indexOffsetIndices.data(), 3, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot indexOffsetPixels = CaptureTarget(target);
    ExpectExactColor(
        indexOffsetPixels.AtNdc(0.6f), Color::Red, "DrawUser honoured its index offset");
    ExpectColorAbsent(
        indexOffsetPixels, Color::Magenta, "DrawUser index offset was not applied twice");

    // Vertex offset: the intended triangle is at vertices 3..5, selected by vertexOffset alone.
    const std::array<VertexPositionColor, 6> byVertexOffset{
        TriangleAt(-0.6f, Color::Magenta)[0],
        TriangleAt(-0.6f, Color::Magenta)[1],
        TriangleAt(-0.6f, Color::Magenta)[2],
        TriangleAt(0.6f, Color::Blue)[0],
        TriangleAt(0.6f, Color::Blue)[1],
        TriangleAt(0.6f, Color::Blue)[2],
    };
    const std::array<std::uint16_t, 3> vertexOffsetIndices{0, 1, 2};

    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, byVertexOffset.data(), 3, 3,
        vertexOffsetIndices.data(), 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot vertexOffsetPixels = CaptureTarget(target);
    ExpectExactColor(
        vertexOffsetPixels.AtNdc(0.6f), Color::Blue, "DrawUser honoured its vertex offset");
    ExpectColorAbsent(
        vertexOffsetPixels, Color::Magenta, "DrawUser vertex offset was not applied twice");
}

// ---------------------------------------------------------------------------
// Invalid ranges must be rejected before anything reaches SDL, never clamped.
// ---------------------------------------------------------------------------
TEST_F(SdlGpuIndexedDrawRangeTest, RejectsIndexedRangesOutsideTheBoundBuffers)
{
    RequireIndexedRendering();

    std::vector<VertexPositionColor> vertices;
    Append(vertices, TriangleAt(0.0f, Color::Red));
    Append(vertices, TriangleAt(0.5f, Color::Blue));
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 3, 4, 5};

    VertexBuffer vertexBuffer(device, PositionColorDeclaration(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 6);

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);

    using System::ArgumentOutOfRangeException;
    // A valid draw with the same buffers, to prove the rejections below are about the arguments.
    EXPECT_NO_THROW(
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 0, 1));
    // Negative startIndex.
    EXPECT_THROW(
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, -1, 1),
        ArgumentOutOfRangeException);
    // Negative baseVertex.
    EXPECT_THROW(
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, -1, 0, 3, 0, 1),
        ArgumentOutOfRangeException);
    // Negative minVertexIndex.
    EXPECT_THROW(
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, -1, 3, 0, 1),
        ArgumentOutOfRangeException);
    // Non-positive numVertices.
    EXPECT_THROW(
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 0, 0, 1),
        ArgumentOutOfRangeException);
    // Non-positive primitiveCount.
    EXPECT_THROW(
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 0),
        ArgumentOutOfRangeException);
    // startIndex past the end of the index buffer.
    EXPECT_THROW(
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 7, 1),
        ArgumentOutOfRangeException);
    // startIndex plus the consumed count past the end of the index buffer.
    EXPECT_THROW(
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 4, 1),
        ArgumentOutOfRangeException);
    // baseVertex plus the declared decoded range past the end of the vertex buffer.
    EXPECT_THROW(
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 4, 0, 3, 0, 1),
        ArgumentOutOfRangeException);
    // An index count that would overflow when derived from primitiveCount.
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 0, 0, 3, 0,
            std::numeric_limits<int>::max()),
        ArgumentOutOfRangeException);

    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    // Only the one valid draw rendered.
    const TargetSnapshot pixels = CaptureTarget(target);
    ExpectExactColor(pixels.AtNdc(0.5f), Color::Blue, "the single valid draw still rendered");
    ExpectColorAbsent(pixels, Color::Red, "no rejected draw reached the target");
}

// ---------------------------------------------------------------------------
// Every one of the eight indexed pipeline families. Each renders the same two-slot geometry: a
// decoy triangle in the left half of the target and the intended one in the right half, selected
// only by baseVertex. A family that still submits vertex_offset = 0 lights the left half.
// ---------------------------------------------------------------------------
TEST_F(SdlGpuIndexedDrawRangeTest, ColoredFamilyHonorsIndexedOffsets)
{
    RequireIndexedRendering();

    std::vector<VertexPositionColor> vertices;
    Append(vertices, TriangleAt(kDecoyX, Color::White));
    Append(vertices, TriangleAt(kIntendedX, Color::White));
    const std::array<std::uint16_t, 3> indices{0, 1, 2};

    VertexBuffer vertexBuffer(device, PositionColorDeclaration(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 3);

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyIntendedHalfLit(CaptureTarget(target), "colored3d family");
}

TEST_F(SdlGpuIndexedDrawRangeTest, TexturedFamilyHonorsIndexedOffsets)
{
    RequireIndexedRendering();

    Texture2D texture(device, 2, 2);
    FillWhite(texture);

    const auto decoy = TrianglePositions(kDecoyX);
    const auto intended = TrianglePositions(kIntendedX);
    const std::array<VertexPositionTexture, 6> vertices{
        VertexPositionTexture(decoy[0], Vector2(0.0f, 0.0f)),
        VertexPositionTexture(decoy[1], Vector2(1.0f, 0.0f)),
        VertexPositionTexture(decoy[2], Vector2(0.5f, 1.0f)),
        VertexPositionTexture(intended[0], Vector2(0.0f, 0.0f)),
        VertexPositionTexture(intended[1], Vector2(1.0f, 0.0f)),
        VertexPositionTexture(intended[2], Vector2(0.5f, 1.0f)),
    };
    const std::array<std::uint16_t, 3> indices{0, 1, 2};

    VertexBuffer vertexBuffer(
        device, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 3);

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);
    effect.setTextureProperty(&texture);
    effect.setTextureEnabledProperty(true);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyIntendedHalfLit(CaptureTarget(target), "textured3d family (stride 20)");
}

TEST_F(SdlGpuIndexedDrawRangeTest, ColoredTexturedFamilyHonorsIndexedOffsets)
{
    RequireIndexedRendering();

    Texture2D texture(device, 2, 2);
    FillWhite(texture);

    const auto decoy = TrianglePositions(kDecoyX);
    const auto intended = TrianglePositions(kIntendedX);
    const std::array<VertexPositionColorTexture, 6> vertices{
        VertexPositionColorTexture(decoy[0], Color::White, Vector2(0.0f, 0.0f)),
        VertexPositionColorTexture(decoy[1], Color::White, Vector2(1.0f, 0.0f)),
        VertexPositionColorTexture(decoy[2], Color::White, Vector2(0.5f, 1.0f)),
        VertexPositionColorTexture(intended[0], Color::White, Vector2(0.0f, 0.0f)),
        VertexPositionColorTexture(intended[1], Color::White, Vector2(1.0f, 0.0f)),
        VertexPositionColorTexture(intended[2], Color::White, Vector2(0.5f, 1.0f)),
    };
    const std::array<std::uint16_t, 3> indices{0, 1, 2};

    VertexBuffer vertexBuffer(
        device, VertexPositionColorTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 3);

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);
    effect.setTextureProperty(&texture);
    effect.setTextureEnabledProperty(true);
    effect.VertexColorEnabled = true;

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyIntendedHalfLit(CaptureTarget(target), "colored_textured3d family (stride 24)");
}

TEST_F(SdlGpuIndexedDrawRangeTest, LitTexturedFamilyHonorsIndexedOffsets)
{
    RequireIndexedRendering();

    Texture2D texture(device, 2, 2);
    FillWhite(texture);

    const auto decoy = TrianglePositions(kDecoyX);
    const auto intended = TrianglePositions(kIntendedX);
    const std::array<VertexPositionNormalTexture, 6> vertices{
        VertexPositionNormalTexture(decoy[0], kFacingNormal, Vector2(0.0f, 0.0f)),
        VertexPositionNormalTexture(decoy[1], kFacingNormal, Vector2(1.0f, 0.0f)),
        VertexPositionNormalTexture(decoy[2], kFacingNormal, Vector2(0.5f, 1.0f)),
        VertexPositionNormalTexture(intended[0], kFacingNormal, Vector2(0.0f, 0.0f)),
        VertexPositionNormalTexture(intended[1], kFacingNormal, Vector2(1.0f, 0.0f)),
        VertexPositionNormalTexture(intended[2], kFacingNormal, Vector2(0.5f, 1.0f)),
    };
    const std::array<std::uint16_t, 3> indices{0, 1, 2};

    VertexBuffer vertexBuffer(
        device, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 3);

    RenderTarget2D target = MakeTarget(device);
    BasicEffect effect(device);
    effect.setTextureProperty(&texture);
    effect.setTextureEnabledProperty(true);
    effect.EnableDefaultLighting();

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyIntendedHalfLit(CaptureTarget(target), "lit_textured3d family (stride 32)");
}

TEST_F(SdlGpuIndexedDrawRangeTest, AlphaTestFamilyHonorsIndexedOffsets)
{
    RequireIndexedRendering();

    Texture2D texture(device, 2, 2);
    FillWhite(texture);

    const auto decoy = TrianglePositions(kDecoyX);
    const auto intended = TrianglePositions(kIntendedX);
    const std::array<VertexPositionTexture, 6> vertices{
        VertexPositionTexture(decoy[0], Vector2(0.0f, 0.0f)),
        VertexPositionTexture(decoy[1], Vector2(1.0f, 0.0f)),
        VertexPositionTexture(decoy[2], Vector2(0.5f, 1.0f)),
        VertexPositionTexture(intended[0], Vector2(0.0f, 0.0f)),
        VertexPositionTexture(intended[1], Vector2(1.0f, 0.0f)),
        VertexPositionTexture(intended[2], Vector2(0.5f, 1.0f)),
    };
    const std::array<std::uint16_t, 3> indices{0, 1, 2};

    VertexBuffer vertexBuffer(
        device, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 3);

    RenderTarget2D target = MakeTarget(device);
    AlphaTestEffect effect(device);
    effect.setTextureProperty(&texture);
    effect.setAlphaFunctionProperty(CompareFunction::Greater);
    effect.setReferenceAlphaProperty(0);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyIntendedHalfLit(CaptureTarget(target), "alpha_test3d family");
}

TEST_F(SdlGpuIndexedDrawRangeTest, DualTextureFamilyHonorsIndexedOffsets)
{
    RequireIndexedRendering();

    Texture2D first(device, 2, 2);
    Texture2D second(device, 2, 2);
    FillWhite(first);
    FillWhite(second);

    const auto decoy = TrianglePositions(kDecoyX);
    const auto intended = TrianglePositions(kIntendedX);
    const std::array<VertexPositionTexture, 6> vertices{
        VertexPositionTexture(decoy[0], Vector2(0.0f, 0.0f)),
        VertexPositionTexture(decoy[1], Vector2(1.0f, 0.0f)),
        VertexPositionTexture(decoy[2], Vector2(0.5f, 1.0f)),
        VertexPositionTexture(intended[0], Vector2(0.0f, 0.0f)),
        VertexPositionTexture(intended[1], Vector2(1.0f, 0.0f)),
        VertexPositionTexture(intended[2], Vector2(0.5f, 1.0f)),
    };
    const std::array<std::uint16_t, 3> indices{0, 1, 2};

    VertexBuffer vertexBuffer(
        device, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 3);

    RenderTarget2D target = MakeTarget(device);
    DualTextureEffect effect(device);
    effect.setTextureProperty(&first);
    effect.setTexture2Property(&second);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyIntendedHalfLit(CaptureTarget(target), "dual_texture3d family");
}

TEST_F(SdlGpuIndexedDrawRangeTest, EnvMapFamilyHonorsIndexedOffsets)
{
    RequireIndexedRendering();

    Texture2D texture(device, 2, 2);
    FillWhite(texture);
    TextureCube cube(device, 1, false, SurfaceFormat::Color);
    const Color face = Color::White;
    for (int i = 0; i < 6; ++i)
        cube.SetData(static_cast<CubeMapFace>(i), &face, 1);

    const auto decoy = TrianglePositions(kDecoyX);
    const auto intended = TrianglePositions(kIntendedX);
    const std::array<VertexPositionNormalTexture, 6> vertices{
        VertexPositionNormalTexture(decoy[0], kFacingNormal, Vector2(0.0f, 0.0f)),
        VertexPositionNormalTexture(decoy[1], kFacingNormal, Vector2(1.0f, 0.0f)),
        VertexPositionNormalTexture(decoy[2], kFacingNormal, Vector2(0.5f, 1.0f)),
        VertexPositionNormalTexture(intended[0], kFacingNormal, Vector2(0.0f, 0.0f)),
        VertexPositionNormalTexture(intended[1], kFacingNormal, Vector2(1.0f, 0.0f)),
        VertexPositionNormalTexture(intended[2], kFacingNormal, Vector2(0.5f, 1.0f)),
    };
    const std::array<std::uint16_t, 3> indices{0, 1, 2};

    VertexBuffer vertexBuffer(
        device, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 3);

    RenderTarget2D target = MakeTarget(device);
    EnvironmentMapEffect effect(device);
    effect.setTextureProperty(&texture);
    effect.setEnvironmentMapProperty(&cube);
    effect.setEnvironmentMapAmountProperty(1.0f);
    effect.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyIntendedHalfLit(CaptureTarget(target), "env_map3d family");
}

TEST_F(SdlGpuIndexedDrawRangeTest, SkinnedFamilyHonorsIndexedOffsets)
{
    RequireIndexedRendering();

    Texture2D texture(device, 2, 2);
    FillWhite(texture);

    const auto decoy = TrianglePositions(kDecoyX);
    const auto intended = TrianglePositions(kIntendedX);
    const std::array<GpuSkinnedVertex, 6> vertices{
        MakeSkinnedVertex(decoy[0], 0.0f, 0.0f),
        MakeSkinnedVertex(decoy[1], 1.0f, 0.0f),
        MakeSkinnedVertex(decoy[2], 0.5f, 1.0f),
        MakeSkinnedVertex(intended[0], 0.0f, 0.0f),
        MakeSkinnedVertex(intended[1], 1.0f, 0.0f),
        MakeSkinnedVertex(intended[2], 0.5f, 1.0f),
    };
    const std::array<std::uint16_t, 3> indices{0, 1, 2};

    VertexBuffer vertexBuffer(device, 6);
    vertexBuffer.SetDataRaw(vertices.data(), 6, static_cast<int>(sizeof(GpuSkinnedVertex)));
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    indexBuffer.SetData(indices.data(), 3);

    RenderTarget2D target = MakeTarget(device);
    SkinnedEffect effect(device);
    effect.setTextureProperty(&texture);
    effect.SetBoneTransforms(std::vector<Matrix>(1, Matrix::getIdentityProperty()));
    effect.setWeightsPerVertexProperty(1);
    effect.EnableDefaultLighting();

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyIntendedHalfLit(CaptureTarget(target), "skinned3d family (stride 52)");
}

TEST_F(SdlGpuIndexedDrawRangeTest, PbrFamilyHonorsIndexedOffsets)
{
    RequireIndexedRendering();

    Texture2D texture(device, 2, 2);
    FillWhite(texture);

    const auto decoy = TrianglePositions(kDecoyX);
    const auto intended = TrianglePositions(kIntendedX);
    const std::array<GpuPbrVertex, 6> vertices{
        MakePbrVertex(decoy[0], 0.0f, 0.0f),
        MakePbrVertex(decoy[1], 1.0f, 0.0f),
        MakePbrVertex(decoy[2], 0.5f, 1.0f),
        MakePbrVertex(intended[0], 0.0f, 0.0f),
        MakePbrVertex(intended[1], 1.0f, 0.0f),
        MakePbrVertex(intended[2], 0.5f, 1.0f),
    };
    const std::array<std::uint16_t, 3> indices{0, 1, 2};

    VertexBuffer vertexBuffer(device, 6);
    vertexBuffer.SetDataRaw(vertices.data(), 6, static_cast<int>(sizeof(GpuPbrVertex)));
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    indexBuffer.SetData(indices.data(), 3);

    RenderTarget2D target = MakeTarget(device);
    PbrEffect effect(device);
    effect.setTextureProperty(&texture);
    effect.setMetallicFactorProperty(0.0f);
    effect.setRoughnessFactorProperty(1.0f);
    effect.EnableDefaultLighting();

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyIntendedHalfLit(CaptureTarget(target), "pbr3d family (stride 48)");
}

// ---------------------------------------------------------------------------
// Target and state coverage: none of the per-draw dynamic state this renderer captures may disturb
// indexed addressing, and the offsets must work against a depth-backed target too.
// ---------------------------------------------------------------------------
TEST_F(SdlGpuIndexedDrawRangeTest, IndexedOffsetsHoldOnADepthBackedTarget)
{
    RequireIndexedRendering();

    std::vector<VertexPositionColor> vertices;
    Append(vertices, TriangleAt(kDecoyX, Color::Magenta));
    Append(vertices, TriangleAt(kIntendedX, Color::Red));
    const std::array<std::uint16_t, 3> indices{0, 1, 2};

    VertexBuffer vertexBuffer(device, PositionColorDeclaration(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 3);

    RenderTarget2D target(
        device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
        DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::PreserveContents);
    BasicEffect effect(device);

    device.setDepthStencilStateProperty(DepthStencilState::Default);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyVertexColorEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot pixels = CaptureTarget(target);
    ExpectExactColor(pixels.AtNdc(kIntendedX), Color::Red, "depth-backed target honours baseVertex");
    ExpectColorAbsent(pixels, Color::Magenta, "depth-backed target decoy");
}

TEST_F(SdlGpuIndexedDrawRangeTest, IndexedOffsetsAreIndependentOfViewportScissorAndRenderStates)
{
    RequireIndexedRendering();

    std::vector<VertexPositionColor> vertices;
    Append(vertices, TriangleAt(kDecoyX, Color::Magenta));
    Append(vertices, TriangleAt(kIntendedX, Color::Red));
    const std::array<std::uint16_t, 3> indices{0, 1, 2};

    VertexBuffer vertexBuffer(device, PositionColorDeclaration(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 3);

    RenderTarget2D target(
        device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
        DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::PreserveContents);
    BasicEffect effect(device);

    // Explicit, non-default state on every axis the renderer captures per draw. The scissor keeps
    // both candidate triangles inside it, so it cannot itself decide which one is visible.
    RasterizerState rasterizer;
    rasterizer.setCullModeProperty(CullMode::None);
    rasterizer.setScissorTestEnableProperty(true);
    rasterizer.setDepthBiasProperty(0.0001f);            // REMED-GFX-051
    rasterizer.setSlopeScaleDepthBiasProperty(0.5f);
    device.setRasterizerStateProperty(rasterizer);
    device.setBlendStateProperty(BlendState::Opaque);
    device.setDepthStencilStateProperty(DepthStencilState::Default);

    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    device.setViewportProperty(
        Microsoft::Xna::Framework::Graphics::Viewport(0, 0, kTargetSize, kTargetSize));
    device.setScissorRectangleProperty(
        Rectangle(4, 4, kTargetSize - 8, kTargetSize - 8));
    ApplyVertexColorEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const TargetSnapshot pixels = CaptureTarget(target);
    ExpectExactColor(
        pixels.AtNdc(kIntendedX), Color::Red, "explicit render state does not disturb addressing");
    ExpectColorAbsent(pixels, Color::Magenta, "explicit render state decoy");
}

