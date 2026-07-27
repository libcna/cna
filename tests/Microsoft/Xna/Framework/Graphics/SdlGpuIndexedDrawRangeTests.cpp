// SPDX-License-Identifier: MS-PL
// REMED-GFX-117: SDL_GPU indexed draws must propagate the public startIndex/baseVertex arguments
// to the native SDL_DrawGPUIndexedPrimitives first_index/vertex_offset parameters.
//
// SDL_GPU implements no backbuffer readback at all, so RenderTarget2D::GetData is this backend's
// exact-pixel oracle (the same control REMED-GFX-111 established). Every test in this file renders
// into a render target, unbinds it, and reads the target's own pixels back.

#ifdef CNA_BACKEND_SDL_GPU

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include <gtest/gtest.h>

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

namespace
{
    constexpr int kTargetSize = 128;

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

    class SdlGpuIndexedDrawRangeTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        void RequireIndexedRendering()
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Backend explicitly does not support indexed rendering";
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

#endif  // CNA_BACKEND_SDL_GPU
