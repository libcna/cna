// SPDX-License-Identifier: MS-PL
// REMED-GFX-111: the public PointListEXT topology contract.
//
// XNA/FNA define PointListEXT as "treats each vertex as a single point; vertex n defines point n;
// N points are drawn". The reconciled CNA contract that this file locks down is therefore:
//
//   non-indexed consumed vertices = primitiveCount, starting at vertexStart
//   indexed consumed index elements = primitiveCount, starting at startIndex (an ELEMENT offset)
//   fetched vertex = decoded index + baseVertex (added exactly once)
//   minVertexIndex/numVertices are validation hints, never addressing
//
// Point primitives carry no winding, so they are never front/back-facing and can never be removed
// by CullMode. Point size is not a public CNA/XNA API: each point covers at least the pixel that
// contains its window position, and total coverage stays within a small bounded budget -- which is
// what separates real point topology from a triangle-list approximation covering thousands of
// pixels.
//
// Renderer scope. Bgfx, EasyGL, Vulkan and WebGPU render points and support backbuffer readback, so
// they carry the permanent pixel coverage here. SDL_GPU maps PointListEXT natively but implements no
// backbuffer readback, so it is covered by a separate practical control. Software explicitly
// rejects every non-TriangleList topology (its documented v1 boundary) and is asserted as a
// rejection, never as approximate geometry. D3D9, D3D11 and D3D12 still route PointListEXT through
// their triangle-list default -- an independent defect recorded as REMED-GFX-114 -- so they are
// deliberately not yet in the pixel guard below. Vulkan joined this matrix with plan_gltf.md
// GLTF-393.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#ifdef CNA_RENDERER_BGFX
#include "CNA/Internal/Renderers/Bgfx/BgfxRenderer.hpp"
#endif
#ifdef CNA_RENDERER_VULKAN
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#endif

using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::CullMode;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::DynamicIndexBuffer;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::FillMode;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::PbrEffect;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SamplerState;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTangentTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionTexture;
using Microsoft::Xna::Framework::Graphics::Viewport;

namespace
{
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

    /// Exactly-packed GPU-layout vertex for the explicit-VertexDeclaration DrawUser overloads,
    /// which read the caller array at the declaration's own stride.
    struct CompactPositionColor
    {
        float x;
        float y;
        float z;
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
        std::uint8_t a;
    };
    static_assert(sizeof(CompactPositionColor) == 16);

    CompactPositionColor Compact(const VertexPositionColor& vertex)
    {
        return CompactPositionColor{
            vertex.Position.X,
            vertex.Position.Y,
            vertex.Position.Z,
            static_cast<std::uint8_t>(vertex.Color.getRProperty()),
            static_cast<std::uint8_t>(vertex.Color.getGProperty()),
            static_cast<std::uint8_t>(vertex.Color.getBProperty()),
            static_cast<std::uint8_t>(vertex.Color.getAProperty()),
        };
    }

    /// One intended point: the exact target pixel, its distinctive colour and its clip-space depth.
    struct PointSpec
    {
        int pixelX = 0;
        int pixelY = 0;
        Color color = Color::White;
        float depth = 0.5f;
    };

    /// Places a point on the centre of its target pixel. World/View/Projection stay identity in
    /// this file, so the vertex position *is* the clip-space position and the viewport transform
    /// maps it to window coordinate (pixelX + 0.5, pixelY + 0.5) -- a stable pixel centre, never a
    /// pixel edge, so no result depends on undefined point-edge coverage.
    VertexPositionColor MakePoint(int width, int height, const PointSpec& point)
    {
        const float x =
            (2.0f * (static_cast<float>(point.pixelX) + 0.5f) /
             static_cast<float>(width)) - 1.0f;
        const float y =
            1.0f - (2.0f * (static_cast<float>(point.pixelY) + 0.5f) /
                    static_cast<float>(height));
        return VertexPositionColor(Vector3(x, y, point.depth), point.color);
    }

    struct FrameSnapshot
    {
        int width = 0;
        int height = 0;
        std::vector<Color> pixels;

        [[nodiscard]] Color At(int x, int y) const
        {
            const int clampedX = std::clamp(x, 0, width - 1);
            const int clampedY = std::clamp(y, 0, height - 1);
            return pixels[
                static_cast<std::size_t>(clampedY) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(clampedX)];
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

        [[nodiscard]] int CountDifferentFrom(const Color& background) const
        {
            int total = 0;
            for (const Color& pixel : pixels)
            {
                if (!(pixel == background))
                    ++total;
            }
            return total;
        }

        [[nodiscard]] bool HasExactWithin(
            int centerX, int centerY, int radius, const Color& color) const
        {
            for (int offsetY = -radius; offsetY <= radius; ++offsetY)
            {
                for (int offsetX = -radius; offsetX <= radius; ++offsetX)
                {
                    if (At(centerX + offsetX, centerY + offsetY) == color)
                        return true;
                }
            }
            return false;
        }
    };

    /// Reads the whole backbuffer. The explicit-size overload exists so a frame drawn under a
    /// custom Viewport can be read back before the Viewport is restored -- restoring it first
    /// would let a renderer that resolves viewport state at replay time rather than per draw
    /// silently pass.
    FrameSnapshot CaptureBackbuffer(GraphicsDevice& device, int width, int height)
    {
        FrameSnapshot snapshot;
        snapshot.width = width;
        snapshot.height = height;
        snapshot.pixels.assign(
            static_cast<std::size_t>(snapshot.width) *
                static_cast<std::size_t>(snapshot.height),
            Color::Transparent);
        const Rectangle region(0, 0, snapshot.width, snapshot.height);
        device.GetBackBufferData(
            &region,
            snapshot.pixels.data(),
            0,
            static_cast<int>(snapshot.pixels.size()));
        return snapshot;
    }

    FrameSnapshot CaptureBackbuffer(GraphicsDevice& device)
    {
        const auto& viewport = device.getViewportProperty();
        return CaptureBackbuffer(
            device, viewport.getWidthProperty(), viewport.getHeightProperty());
    }

    /// Reads a render target's own rendered pixels directly. Used where a renderer implements
    /// render-target readback but no backbuffer readback.
    FrameSnapshot CaptureRenderTarget(RenderTarget2D& target, int width, int height)
    {
        FrameSnapshot snapshot;
        snapshot.width = width;
        snapshot.height = height;
        snapshot.pixels.assign(
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
            Color::Transparent);
        const Rectangle region(0, 0, width, height);
        target.GetData(
            0, &region, snapshot.pixels.data(), 0,
            static_cast<int>(snapshot.pixels.size()));
        return snapshot;
    }

    /// A point is "rendered" when its exact RGBA appears within a small raster probe around its
    /// target pixel. The probe absorbs the one-pixel differences legitimately allowed by each
    /// renderer's own viewport/pixel-centre convention without accepting a different pixel.
    void ExpectPointRendered(
        const FrameSnapshot& snapshot, const PointSpec& point, const char* label)
    {
        EXPECT_TRUE(
            snapshot.HasExactWithin(point.pixelX, point.pixelY, 2, point.color))
            << label << " (no exact RGBA pixel in the 5x5 probe at "
            << point.pixelX << ',' << point.pixelY << ')';
    }

    void ExpectColorAbsent(
        const FrameSnapshot& snapshot, const Color& color, const char* label)
    {
        EXPECT_EQ(0, snapshot.CountExact(color))
            << label << " (colour must not appear anywhere in the frame)";
    }

    /// The decisive point-versus-triangle discriminator. Real point topology lights a handful of
    /// pixels per point; the triangle-list approximation this task removes filled thousands.
    void ExpectPointCoverageBudget(
        const FrameSnapshot& snapshot,
        const Color& background,
        int pointCount,
        const char* label)
    {
        const int budget = 25 * pointCount + 8;
        const int lit = snapshot.CountDifferentFrom(background);
        EXPECT_LE(lit, budget)
            << label << " (" << lit << " lit pixels for " << pointCount
            << " point(s) is area coverage, not point coverage)";
    }

    /// Draws @p target over the whole backbuffer through the ordinary BasicEffect texture path
    /// with point sampling, so a render target of exactly the backbuffer size reproduces its
    /// rendered pixels 1:1. Rendered target pixels exist only on the GPU, so this is the portable
    /// way to observe them.
    void SampleTargetToBackbuffer(GraphicsDevice& device, RenderTarget2D& target)
    {
        const std::array<VertexPositionTexture, 6> quad{
            VertexPositionTexture(Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)),
            VertexPositionTexture(Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)),
            VertexPositionTexture(Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
            VertexPositionTexture(Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)),
            VertexPositionTexture(Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
            VertexPositionTexture(Vector3(1.0f, 1.0f, 0.0f), Vector2(1.0f, 0.0f)),
        };
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        BasicEffect sampleEffect(device);
        sampleEffect.setTextureEnabledProperty(true);
        sampleEffect.setTextureProperty(&target);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(Color::Black);
        sampleEffect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
    }

    class PointListPrimitiveTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        // GTEST_SKIP() only unwinds the function it is called from; called from an ordinary
        // member function like RequirePointRendering() below it cannot skip the test body that
        // invokes it. SetUp() is where GoogleTest itself checks for a skip, so the capability gate
        // has to run here too -- RequirePointRendering() keeps its own copy for the state-setup
        // calls that follow it, which only run once SetUp() has already let the test proceed.
        void SetUp() override
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support 3D rendering";
        }

        void TearDown() override
        {
#ifdef CNA_RENDERER_VULKAN
            auto* renderer =
                dynamic_cast<CNA::Internal::Renderers::Vulkan::VulkanRenderer*>(
                    &device.GetRenderer());
            ASSERT_NE(nullptr, renderer);
            const auto& messages = renderer->GetValidationMessagesEXT();
            EXPECT_TRUE(messages.empty())
                << "Vulkan point topology emitted a validation warning/error: "
                << (messages.empty() ? std::string{} : messages.front());
#endif
        }

        void RequirePointRendering()
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support 3D rendering";
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            device.setScissorRectangleProperty(
                Rectangle(0, 0,
                          device.getViewportProperty().getWidthProperty(),
                          device.getViewportProperty().getHeightProperty()));
        }

        [[nodiscard]] int BackbufferWidth() const
        {
            return device.getViewportProperty().getWidthProperty();
        }

        [[nodiscard]] int BackbufferHeight() const
        {
            return device.getViewportProperty().getHeightProperty();
        }

        void ApplyVertexColorEffect(BasicEffect& effect)
        {
            effect.VertexColorEnabled = true;
            effect.Apply();
        }
    };
}

#if defined(CNA_RENDERER_BGFX) || defined(CNA_RENDERER_EASYGL) || \
    defined(CNA_RENDERER_VULKAN) || defined(CNA_RENDERER_WEBGPU)

// The canonical depthless non-indexed case: four points at distinct pixel centres, four distinct
// colours, one framebuffer snapshot. A triangle-list approximation of this draw fills the whole
// area spanned by the first three vertices and drops the fourth entirely.
TEST_F(PointListPrimitiveTest, NonIndexedPointListRendersEveryRequestedPoint)
{
    RequirePointRendering();

    const int width = BackbufferWidth();
    const int height = BackbufferHeight();
    const std::array<PointSpec, 4> plan{
        PointSpec{width / 8, height / 4, Color::Red, 0.5f},
        PointSpec{3 * width / 8, 3 * height / 4, Color::Lime, 0.5f},
        PointSpec{5 * width / 8, height / 4, Color::Blue, 0.5f},
        PointSpec{7 * width / 8, 3 * height / 4, Color::Yellow, 0.5f},
    };

    std::array<VertexPositionColor, 4> vertices{};
    for (std::size_t i = 0; i < plan.size(); ++i)
        vertices[i] = MakePoint(width, height, plan[i]);

    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 4, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 4);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.DrawPrimitives(PrimitiveType::PointListEXT, 0, 4);

    const FrameSnapshot pixels = CaptureBackbuffer(device);
    ExpectPointRendered(pixels, plan[0], "non-indexed point 0");
    ExpectPointRendered(pixels, plan[1], "non-indexed point 1");
    ExpectPointRendered(pixels, plan[2], "non-indexed point 2");
    ExpectPointRendered(pixels, plan[3], "non-indexed point 3");
    ExpectPointCoverageBudget(pixels, Color::Black, 4, "non-indexed point list");
}

#ifdef CNA_RENDERER_VULKAN
// glTF's generated POINTS witness uses the stride-48 PBR layout. The generic point tests above
// exercise BasicEffect; this companion makes Vulkan create the actual PBR point pipeline too, so
// the validation-message assertion in TearDown guards pbr3d.vert.glsl's required PointSize write.
TEST_F(PointListPrimitiveTest, VulkanPbrPointUsesTheNativePointPipeline)
{
    RequirePointRendering();

    const int width = BackbufferWidth();
    const int height = BackbufferHeight();
    const PointSpec point{width / 2, height / 2, Color::Red, 0.5f};
    const VertexPositionColor positioned = MakePoint(width, height, point);
    const VertexPositionNormalTangentTexture vertex(
        positioned.Position,
        Vector3(0.0f, 0.0f, 1.0f),
        Vector4(1.0f, 0.0f, 0.0f, 1.0f),
        Vector2::Zero);

    VertexBuffer vertexBuffer(
        device,
        VertexPositionNormalTangentTexture::getVertexDeclarationStatic(),
        1,
        BufferUsage::None);
    vertexBuffer.SetData(&vertex, 1);

    PbrEffect effect(device);
    effect.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
    effect.setAmbientLightColorProperty(Vector3::One);
    effect.setEncodeOutputToSrgbEXTProperty(false);
    effect.getDirectionalLight0Property().setEnabledProperty(false);
    effect.getDirectionalLight1Property().setEnabledProperty(false);
    effect.getDirectionalLight2Property().setEnabledProperty(false);

    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::PointListEXT, 0, 1);

    const FrameSnapshot pixels = CaptureBackbuffer(device);
    ExpectPointRendered(pixels, point, "Vulkan PBR point");
    ExpectPointCoverageBudget(pixels, Color::Black, 1, "Vulkan PBR point");
}
#endif

// The exact case REMED-GFX-106 discovered: one indexed point must produce one point.
TEST_F(PointListPrimitiveTest, IndexedPointListRendersEveryRequestedPoint)
{
    RequirePointRendering();

    const int width = BackbufferWidth();
    const int height = BackbufferHeight();
    const std::array<PointSpec, 4> plan{
        PointSpec{width / 8, height / 4, Color::Red, 0.5f},
        PointSpec{3 * width / 8, 3 * height / 4, Color::Lime, 0.5f},
        PointSpec{5 * width / 8, height / 4, Color::Blue, 0.5f},
        PointSpec{7 * width / 8, 3 * height / 4, Color::Yellow, 0.5f},
    };

    std::array<VertexPositionColor, 4> vertices{};
    for (std::size_t i = 0; i < plan.size(); ++i)
        vertices[i] = MakePoint(width, height, plan[i]);
    const std::array<std::uint16_t, 4> indices{0, 1, 2, 3};

    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 4, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 4);
    indexBuffer.SetData(indices.data(), 4);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 4, 0, 4);

    const FrameSnapshot manyPoints = CaptureBackbuffer(device);
    ExpectPointRendered(manyPoints, plan[0], "indexed point 0");
    ExpectPointRendered(manyPoints, plan[1], "indexed point 1");
    ExpectPointRendered(manyPoints, plan[2], "indexed point 2");
    ExpectPointRendered(manyPoints, plan[3], "indexed point 3");
    ExpectPointCoverageBudget(manyPoints, Color::Black, 4, "indexed point list");

    // A single indexed point is the minimal reproduction from the GFX-106 topology matrix.
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 4, 2, 1);

    const FrameSnapshot onePoint = CaptureBackbuffer(device);
    ExpectPointRendered(onePoint, plan[2], "single indexed point");
    ExpectColorAbsent(onePoint, Color::Red, "point outside the one-point range");
    ExpectColorAbsent(onePoint, Color::Lime, "point outside the one-point range");
    ExpectColorAbsent(onePoint, Color::Yellow, "point outside the one-point range");
    ExpectPointCoverageBudget(onePoint, Color::Black, 1, "single indexed point");
}

// startIndex is an index-ELEMENT offset, baseVertex is added to each decoded index exactly once,
// and primitiveCount limits the consumed range to exactly that many points. Decoy points sit both
// before and after the intended range and share one distinctive colour, so any addressing or count
// error makes that colour appear.
TEST_F(PointListPrimitiveTest, IndexedPointListHonorsStartIndexBaseVertexAndExactCount)
{
    RequirePointRendering();

    const int width = BackbufferWidth();
    const int height = BackbufferHeight();
    const std::array<PointSpec, 3> wanted{
        PointSpec{width / 4, height / 4, Color::Red, 0.5f},
        PointSpec{width / 2, height / 2, Color::Lime, 0.5f},
        PointSpec{3 * width / 4, 3 * height / 4, Color::Blue, 0.5f},
    };
    const std::array<PointSpec, 5> decoys{
        PointSpec{width / 16, height / 16, Color::Magenta, 0.5f},
        PointSpec{15 * width / 16, height / 16, Color::Magenta, 0.5f},
        PointSpec{width / 16, 15 * height / 16, Color::Magenta, 0.5f},
        PointSpec{15 * width / 16, 15 * height / 16, Color::Magenta, 0.5f},
        PointSpec{width / 2, 15 * height / 16, Color::Magenta, 0.5f},
    };

    // Vertices 0-1 are decoys before the intended range, 2-4 are the intended points, 5-7 are
    // decoys after it.
    std::array<VertexPositionColor, 8> vertices{
        MakePoint(width, height, decoys[0]),
        MakePoint(width, height, decoys[1]),
        MakePoint(width, height, wanted[0]),
        MakePoint(width, height, wanted[1]),
        MakePoint(width, height, wanted[2]),
        MakePoint(width, height, decoys[2]),
        MakePoint(width, height, decoys[3]),
        MakePoint(width, height, decoys[4]),
    };
    // With baseVertex = 2 and startIndex = 2 the consumed elements are {0, 1, 2}, which address
    // vertices 2, 3, 4. Every unconsumed element deliberately addresses a decoy.
    const std::array<std::uint16_t, 8> indices{3, 4, 0, 1, 2, 3, 4, 5};

    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 8, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 8, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 8);
    indexBuffer.SetData(indices.data(), 8);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 2, 0, 6, 2, 3);

    const FrameSnapshot pixels = CaptureBackbuffer(device);
    ExpectPointRendered(pixels, wanted[0], "based/started indexed point 0");
    ExpectPointRendered(pixels, wanted[1], "based/started indexed point 1");
    ExpectPointRendered(pixels, wanted[2], "based/started indexed point 2");
    ExpectColorAbsent(
        pixels, Color::Magenta, "decoy points before and after the indexed range");
    ExpectPointCoverageBudget(
        pixels, Color::Black, 3, "indexed point range with base and start");
}

// The same addressing contract through a public 32-bit index buffer. On Bgfx this also proves that
// REMED-GFX-108's native BGFX_BUFFER_INDEX32 creation still applies to point topology.
TEST_F(PointListPrimitiveTest, IndexedPointListHonorsThirtyTwoBitIndexElements)
{
    RequirePointRendering();

    const int width = BackbufferWidth();
    const int height = BackbufferHeight();
    const std::array<PointSpec, 3> wanted{
        PointSpec{width / 4, 3 * height / 4, Color::Red, 0.5f},
        PointSpec{width / 2, height / 4, Color::Lime, 0.5f},
        PointSpec{3 * width / 4, height / 2, Color::Blue, 0.5f},
    };
    const PointSpec decoy{width / 16, height / 2, Color::Magenta, 0.5f};

    std::array<VertexPositionColor, 8> vertices{
        MakePoint(width, height, PointSpec{width / 16, height / 16, Color::Magenta, 0.5f}),
        MakePoint(width, height, PointSpec{15 * width / 16, height / 16, Color::Magenta, 0.5f}),
        MakePoint(width, height, wanted[0]),
        MakePoint(width, height, wanted[1]),
        MakePoint(width, height, wanted[2]),
        MakePoint(width, height, decoy),
        MakePoint(width, height, PointSpec{15 * width / 16, height / 2, Color::Magenta, 0.5f}),
        MakePoint(width, height, PointSpec{width / 2, 15 * height / 16, Color::Magenta, 0.5f}),
    };
    const std::array<std::uint32_t, 8> indices{3, 4, 0, 1, 2, 3, 4, 5};

    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 8, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::ThirtyTwoBits, 8, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 8);
    indexBuffer.SetData(indices.data(), 8);

#ifdef CNA_RENDERER_BGFX
    auto* nativeIndex =
        dynamic_cast<CNA::Internal::Renderers::Bgfx::BgfxIndexBufferRenderer*>(
            &indexBuffer.GetRenderer());
    ASSERT_NE(nullptr, nativeIndex);
    EXPECT_TRUE(nativeIndex->IsThirtyTwoBit());
    EXPECT_EQ(
        static_cast<std::uint16_t>(BGFX_BUFFER_ALLOW_RESIZE | BGFX_BUFFER_INDEX32),
        nativeIndex->GetNativeCreationFlagsEXT());
#endif

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 2, 0, 6, 2, 3);

    const FrameSnapshot pixels = CaptureBackbuffer(device);
    ExpectPointRendered(pixels, wanted[0], "32-bit indexed point 0");
    ExpectPointRendered(pixels, wanted[1], "32-bit indexed point 1");
    ExpectPointRendered(pixels, wanted[2], "32-bit indexed point 2");
    ExpectColorAbsent(pixels, Color::Magenta, "32-bit decoy points");
    ExpectPointCoverageBudget(pixels, Color::Black, 3, "32-bit indexed point range");
}

// Dynamic buffers reach exactly the same addressing and count contract.
TEST_F(PointListPrimitiveTest, DynamicPointBuffersHonorRangeAndCount)
{
    RequirePointRendering();

    const int width = BackbufferWidth();
    const int height = BackbufferHeight();
    const std::array<PointSpec, 3> wanted{
        PointSpec{width / 4, height / 2, Color::Red, 0.5f},
        PointSpec{width / 2, 3 * height / 4, Color::Lime, 0.5f},
        PointSpec{3 * width / 4, height / 4, Color::Blue, 0.5f},
    };

    std::array<VertexPositionColor, 8> vertices{
        MakePoint(width, height, PointSpec{width / 16, height / 16, Color::Magenta, 0.5f}),
        MakePoint(width, height, PointSpec{15 * width / 16, height / 16, Color::Magenta, 0.5f}),
        MakePoint(width, height, wanted[0]),
        MakePoint(width, height, wanted[1]),
        MakePoint(width, height, wanted[2]),
        MakePoint(width, height, PointSpec{width / 16, 15 * height / 16, Color::Magenta, 0.5f}),
        MakePoint(width, height, PointSpec{15 * width / 16, 15 * height / 16, Color::Magenta, 0.5f}),
        MakePoint(width, height, PointSpec{width / 2, 15 * height / 16, Color::Magenta, 0.5f}),
    };
    const std::array<std::uint16_t, 8> indices{3, 4, 0, 1, 2, 3, 4, 5};

    DynamicVertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 8, BufferUsage::None);
    DynamicIndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 8, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 0, 8, SetDataOptions::None);
    indexBuffer.SetData(indices.data(), 0, 8, SetDataOptions::None);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 2, 0, 6, 2, 3);

    const FrameSnapshot pixels = CaptureBackbuffer(device);
    ExpectPointRendered(pixels, wanted[0], "dynamic indexed point 0");
    ExpectPointRendered(pixels, wanted[1], "dynamic indexed point 1");
    ExpectPointRendered(pixels, wanted[2], "dynamic indexed point 2");
    ExpectColorAbsent(pixels, Color::Magenta, "dynamic decoy points");
    ExpectPointCoverageBudget(pixels, Color::Black, 3, "dynamic indexed point range");
}

// DrawUserPrimitives owns its own exactly-sized temporary buffer, so vertexOffset and
// primitiveCount together select exactly the intended points out of a larger caller array.
TEST_F(PointListPrimitiveTest, DrawUserPrimitivesPointListHonorsVertexOffsetAndCount)
{
    RequirePointRendering();

    const int width = BackbufferWidth();
    const int height = BackbufferHeight();
    const std::array<PointSpec, 3> wanted{
        PointSpec{width / 4, height / 4, Color::Red, 0.5f},
        PointSpec{width / 2, height / 2, Color::Lime, 0.5f},
        PointSpec{3 * width / 4, 3 * height / 4, Color::Blue, 0.5f},
    };

    const std::array<VertexPositionColor, 7> vertices{
        MakePoint(width, height, PointSpec{width / 16, height / 16, Color::Magenta, 0.5f}),
        MakePoint(width, height, PointSpec{15 * width / 16, height / 16, Color::Magenta, 0.5f}),
        MakePoint(width, height, wanted[0]),
        MakePoint(width, height, wanted[1]),
        MakePoint(width, height, wanted[2]),
        MakePoint(width, height, PointSpec{width / 16, 15 * height / 16, Color::Magenta, 0.5f}),
        MakePoint(width, height, PointSpec{15 * width / 16, 15 * height / 16, Color::Magenta, 0.5f}),
    };

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.DrawUserPrimitives(PrimitiveType::PointListEXT, vertices.data(), 2, 3);

    const FrameSnapshot pixels = CaptureBackbuffer(device);
    ExpectPointRendered(pixels, wanted[0], "DrawUser point 0");
    ExpectPointRendered(pixels, wanted[1], "DrawUser point 1");
    ExpectPointRendered(pixels, wanted[2], "DrawUser point 2");
    ExpectColorAbsent(pixels, Color::Magenta, "DrawUser decoy points");
    ExpectPointCoverageBudget(pixels, Color::Black, 3, "DrawUser point list");

    // The explicit-VertexDeclaration overload (REMED-GFX-043) reaches the same contract. It reads
    // the caller array at the declaration's own stride, so the source must be exactly packed.
    std::array<CompactPositionColor, 7> compact{};
    for (std::size_t i = 0; i < vertices.size(); ++i)
        compact[i] = Compact(vertices[i]);

    device.Clear(Color::Black);
    effect.Apply();
    device.DrawUserPrimitives(
        PrimitiveType::PointListEXT, compact.data(), 2, 3, PositionColorDeclaration());

    const FrameSnapshot declared = CaptureBackbuffer(device);
    ExpectPointRendered(declared, wanted[0], "declared DrawUser point 0");
    ExpectPointRendered(declared, wanted[1], "declared DrawUser point 1");
    ExpectPointRendered(declared, wanted[2], "declared DrawUser point 2");
    ExpectColorAbsent(declared, Color::Magenta, "declared DrawUser decoy points");
    ExpectPointCoverageBudget(
        declared, Color::Black, 3, "declared DrawUser point list");
}

// DrawUserIndexedPrimitives applies vertexOffset to the caller vertex array and indexOffset to the
// caller index array; the decoded indices then address the packed range, never the caller array.
TEST_F(PointListPrimitiveTest, DrawUserIndexedPrimitivesPointListHonorsOffsetsAndCount)
{
    RequirePointRendering();

    const int width = BackbufferWidth();
    const int height = BackbufferHeight();
    const std::array<PointSpec, 3> wanted{
        PointSpec{width / 4, 3 * height / 4, Color::Red, 0.5f},
        PointSpec{width / 2, height / 4, Color::Lime, 0.5f},
        PointSpec{3 * width / 4, height / 2, Color::Blue, 0.5f},
    };

    const std::array<VertexPositionColor, 7> vertices{
        MakePoint(width, height, PointSpec{width / 16, height / 16, Color::Magenta, 0.5f}),
        MakePoint(width, height, PointSpec{15 * width / 16, height / 16, Color::Magenta, 0.5f}),
        MakePoint(width, height, wanted[0]),
        MakePoint(width, height, wanted[1]),
        MakePoint(width, height, wanted[2]),
        MakePoint(width, height, PointSpec{width / 16, 15 * height / 16, Color::Magenta, 0.5f}),
        MakePoint(width, height, PointSpec{15 * width / 16, 15 * height / 16, Color::Magenta, 0.5f}),
    };
    // vertexOffset = 2 packs {Red, Lime, Blue, Magenta, Magenta}; indexOffset = 2 consumes
    // {0, 1, 2}. Both unconsumed index pairs address the packed decoys.
    const std::array<std::uint16_t, 7> indices16{3, 4, 0, 1, 2, 3, 4};
    const std::array<std::uint32_t, 7> indices32{3, 4, 0, 1, 2, 3, 4};

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::PointListEXT, vertices.data(), 2, 5, indices16.data(), 2, 3);

    const FrameSnapshot narrow = CaptureBackbuffer(device);
    ExpectPointRendered(narrow, wanted[0], "DrawUserIndexed 16-bit point 0");
    ExpectPointRendered(narrow, wanted[1], "DrawUserIndexed 16-bit point 1");
    ExpectPointRendered(narrow, wanted[2], "DrawUserIndexed 16-bit point 2");
    ExpectColorAbsent(narrow, Color::Magenta, "DrawUserIndexed 16-bit decoys");
    ExpectPointCoverageBudget(
        narrow, Color::Black, 3, "DrawUserIndexed 16-bit point list");

    device.Clear(Color::Black);
    effect.Apply();
    device.DrawUserIndexedPrimitives(
        PrimitiveType::PointListEXT, vertices.data(), 2, 5, indices32.data(), 2, 3);

    const FrameSnapshot wide = CaptureBackbuffer(device);
    ExpectPointRendered(wide, wanted[0], "DrawUserIndexed 32-bit point 0");
    ExpectPointRendered(wide, wanted[1], "DrawUserIndexed 32-bit point 1");
    ExpectPointRendered(wide, wanted[2], "DrawUserIndexed 32-bit point 2");
    ExpectColorAbsent(wide, Color::Magenta, "DrawUserIndexed 32-bit decoys");
    ExpectPointCoverageBudget(
        wide, Color::Black, 3, "DrawUserIndexed 32-bit point list");
}

// Topology must be captured for the intended draw, never left over from the last live draw.
TEST_F(PointListPrimitiveTest, PointTopologyIsCapturedPerDrawAcrossTopologySwitches)
{
    RequirePointRendering();

    const int width = BackbufferWidth();
    const int height = BackbufferHeight();
    const PointSpec first{width / 8, height / 8, Color::Red, 0.5f};
    const PointSpec second{7 * width / 8, height / 8, Color::Red, 0.5f};
    const PointSpec third{width / 8, 7 * height / 8, Color::Blue, 0.5f};
    const PointSpec fourth{7 * width / 8, 7 * height / 8, Color::Blue, 0.5f};

    // A lime triangle around the frame centre plus a yellow line across the middle.
    const auto triangleVertex = [&](float x, float y) {
        return VertexPositionColor(Vector3(x, y, 0.5f), Color::Lime);
    };
    const auto lineVertex = [&](float x, float y) {
        return VertexPositionColor(Vector3(x, y, 0.5f), Color::Yellow);
    };

    const std::array<VertexPositionColor, 9> vertices{
        MakePoint(width, height, first),
        MakePoint(width, height, second),
        triangleVertex(-0.25f, -0.25f),
        triangleVertex(0.25f, -0.25f),
        triangleVertex(0.0f, 0.25f),
        MakePoint(width, height, third),
        MakePoint(width, height, fourth),
        lineVertex(-0.6f, -0.6f),
        lineVertex(0.6f, -0.6f),
    };

    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 9, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 9);

    const std::array<std::uint16_t, 9> indices{0, 1, 2, 3, 4, 5, 6, 7, 8};
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 9, BufferUsage::None);
    indexBuffer.SetData(indices.data(), 9);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);

    // PointListEXT -> TriangleList -> PointListEXT -> LineList -> PointListEXT.
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 9, 0, 2);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 9, 2, 1);
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 9, 5, 2);
    device.DrawIndexedPrimitives(PrimitiveType::LineList, 0, 0, 9, 7, 1);
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 9, 0, 1);

    const FrameSnapshot pixels = CaptureBackbuffer(device);
    ExpectPointRendered(pixels, first, "point before the triangle");
    ExpectPointRendered(pixels, second, "point before the triangle");
    ExpectPointRendered(pixels, third, "point between the triangle and the line");
    ExpectPointRendered(pixels, fourth, "point between the triangle and the line");
    EXPECT_EQ(Color::Lime, pixels.At(width / 2, height / 2))
        << "the interleaved triangle still fills its area";
    EXPECT_TRUE(pixels.HasExactWithin(width / 2, 4 * height / 5, 3, Color::Yellow))
        << "the interleaved line still draws its segment";

    // A point draw must never spread into the area between its own points: the midpoint of the two
    // red points and the midpoint of the two blue points both stay background.
    EXPECT_EQ(Color::Black, pixels.At(width / 2, height / 8))
        << "no geometry between the two red points";
    EXPECT_EQ(Color::Black, pixels.At(width / 2, 7 * height / 8))
        << "no geometry between the two blue points";
}

// One point -> several points -> one point, each range read once.
TEST_F(PointListPrimitiveTest, PointRangeTransitionsBetweenOneAndManyPoints)
{
    RequirePointRendering();

    const int width = BackbufferWidth();
    const int height = BackbufferHeight();
    const std::array<PointSpec, 4> plan{
        PointSpec{width / 8, height / 4, Color::Red, 0.5f},
        PointSpec{3 * width / 8, 3 * height / 4, Color::Lime, 0.5f},
        PointSpec{5 * width / 8, height / 4, Color::Blue, 0.5f},
        PointSpec{7 * width / 8, 3 * height / 4, Color::Yellow, 0.5f},
    };

    std::array<VertexPositionColor, 4> vertices{};
    for (std::size_t i = 0; i < plan.size(); ++i)
        vertices[i] = MakePoint(width, height, plan[i]);
    const std::array<std::uint16_t, 4> indices{0, 1, 2, 3};

    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 4, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 4);
    indexBuffer.SetData(indices.data(), 4);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);

    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 4, 0, 1);
    const FrameSnapshot firstOne = CaptureBackbuffer(device);
    ExpectPointRendered(firstOne, plan[0], "first one-point range");
    ExpectColorAbsent(firstOne, Color::Lime, "first one-point range excludes point 1");
    ExpectColorAbsent(firstOne, Color::Blue, "first one-point range excludes point 2");
    ExpectColorAbsent(firstOne, Color::Yellow, "first one-point range excludes point 3");
    ExpectPointCoverageBudget(firstOne, Color::Black, 1, "first one-point range");

    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 4, 0, 4);
    const FrameSnapshot many = CaptureBackbuffer(device);
    ExpectPointRendered(many, plan[0], "four-point range point 0");
    ExpectPointRendered(many, plan[1], "four-point range point 1");
    ExpectPointRendered(many, plan[2], "four-point range point 2");
    ExpectPointRendered(many, plan[3], "four-point range point 3");
    ExpectPointCoverageBudget(many, Color::Black, 4, "four-point range");

    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 4, 3, 1);
    const FrameSnapshot lastOne = CaptureBackbuffer(device);
    ExpectPointRendered(lastOne, plan[3], "restored one-point range");
    ExpectColorAbsent(lastOne, Color::Red, "restored one-point range excludes point 0");
    ExpectColorAbsent(lastOne, Color::Lime, "restored one-point range excludes point 1");
    ExpectColorAbsent(lastOne, Color::Blue, "restored one-point range excludes point 2");
    ExpectPointCoverageBudget(lastOne, Color::Black, 1, "restored one-point range");
}

// Backbuffer -> RenderTarget2D -> backbuffer, plus producer/unbind/sample of the target itself.
TEST_F(PointListPrimitiveTest, PointListRendersToRenderTargetAndBackToBackbuffer)
{
    RequirePointRendering();

    const int width = BackbufferWidth();
    const int height = BackbufferHeight();
    const PointSpec backbufferPoint{width / 4, height / 4, Color::Red, 0.5f};
    const PointSpec targetPoint{width / 2, height / 2, Color::Lime, 0.5f};
    const PointSpec restoredPoint{3 * width / 4, 3 * height / 4, Color::Blue, 0.5f};

    const std::array<VertexPositionColor, 3> vertices{
        MakePoint(width, height, backbufferPoint),
        MakePoint(width, height, targetPoint),
        MakePoint(width, height, restoredPoint),
    };
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 3);

    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    indexBuffer.SetData(indices.data(), 3);

    RenderTarget2D target(
        device, width, height, false, SurfaceFormat::Color, DepthFormat::None, 0,
        RenderTargetUsage::PreserveContents);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);

    // backbuffer -> target -> backbuffer within one frame.
    device.Clear(Color::Black);
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 3, 0, 1);

    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 3, 1, 1);

    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 3, 2, 1);

    const FrameSnapshot backbuffer = CaptureBackbuffer(device);
    ExpectPointRendered(backbuffer, backbufferPoint, "backbuffer point before the target");
    ExpectPointRendered(backbuffer, restoredPoint, "backbuffer point after the target");
    ExpectColorAbsent(
        backbuffer, Color::Lime, "the target-only point never reaches the backbuffer");
    ExpectPointCoverageBudget(
        backbuffer, Color::Black, 2, "backbuffer segments around a render target");

    // Producer -> unbind -> sample: the target's own pixels observed through the texture path.
    device.SetVertexBuffer(nullptr);
    device.SetIndexBuffer(nullptr);
    SampleTargetToBackbuffer(device, target);

    const FrameSnapshot sampled = CaptureBackbuffer(device);
    ExpectPointRendered(sampled, targetPoint, "render-target point sampled back");
    ExpectColorAbsent(sampled, Color::Red, "backbuffer-only point is not in the target");
    ExpectColorAbsent(sampled, Color::Blue, "backbuffer-only point is not in the target");
    ExpectPointCoverageBudget(
        sampled, Color::Black, 1, "render-target point coverage after sampling");
}

// Points participate in depth testing like any other primitive, and DepthRead must not write.
TEST_F(PointListPrimitiveTest, PointListRespectsDepthTestingBetweenPoints)
{
    RequirePointRendering();

    const int width = BackbufferWidth();
    const int height = BackbufferHeight();
    const int nearFarX = width / 3;
    const int readOnlyX = 2 * width / 3;
    const int row = height / 2;

    const std::array<VertexPositionColor, 7> vertices{
        MakePoint(width, height, PointSpec{nearFarX, row, Color::Blue, 0.8f}),
        MakePoint(width, height, PointSpec{nearFarX, row, Color::Red, 0.3f}),
        MakePoint(width, height, PointSpec{nearFarX, row, Color::Lime, 0.9f}),
        MakePoint(width, height, PointSpec{readOnlyX, row, Color::Blue, 0.5f}),
        MakePoint(width, height, PointSpec{readOnlyX, row, Color::Yellow, 0.4f}),
        MakePoint(width, height, PointSpec{readOnlyX, row, Color::Magenta, 0.45f}),
        MakePoint(width, height, PointSpec{readOnlyX, row, Color::Cyan, 0.6f}),
    };
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 7, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 7);

    const std::array<std::uint16_t, 7> indices{0, 1, 2, 3, 4, 5, 6};
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 7, BufferUsage::None);
    indexBuffer.SetData(indices.data(), 7);

    RenderTarget2D target(
        device, width, height, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
        RenderTargetUsage::PreserveContents);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);

    device.setDepthStencilStateProperty(DepthStencilState::Default);
    effect.Apply();
    // Far blue writes depth 0.8; near red passes and writes 0.3; farther lime is rejected. The
    // read-only column's own reference depth of 0.5 is established here, while writes are on.
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 7, 0, 1);
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 7, 1, 1);
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 7, 2, 1);
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 7, 3, 1);

    device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
    effect.Apply();
    // Yellow at 0.4 passes the stored 0.5 but must not write it, so magenta at 0.45 passes too --
    // it would have been rejected against a written 0.4. Cyan at 0.6 proves the test still runs.
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 7, 4, 1);
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 7, 5, 1);
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 7, 6, 1);

    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    device.SetVertexBuffer(nullptr);
    device.SetIndexBuffer(nullptr);
    // Close the producing frame before the target is read as a texture: a render target sampled in
    // the same frame that wrote it is not observable on every renderer.
    device.Present();
    SampleTargetToBackbuffer(device, target);

    const FrameSnapshot pixels = CaptureBackbuffer(device);
    EXPECT_TRUE(pixels.HasExactWithin(nearFarX, row, 2, Color::Red))
        << "the nearest depth-tested point wins";
    ExpectColorAbsent(pixels, Color::Lime, "a farther point is depth-rejected");
    EXPECT_TRUE(pixels.HasExactWithin(readOnlyX, row, 2, Color::Magenta))
        << "DepthRead passed the depth test without writing the previous point's depth";
    ExpectColorAbsent(
        pixels, Color::Cyan, "DepthRead still rejects a point behind the stored depth");
}

// Points carry no winding, so no CullMode may remove them -- including the device default.
TEST_F(PointListPrimitiveTest, PointListIsNotAffectedByTriangleCulling)
{
    RequirePointRendering();

    const int width = BackbufferWidth();
    const int height = BackbufferHeight();
    const std::array<PointSpec, 3> plan{
        PointSpec{width / 4, height / 2, Color::Red, 0.5f},
        PointSpec{width / 2, height / 2, Color::Lime, 0.5f},
        PointSpec{3 * width / 4, height / 2, Color::Blue, 0.5f},
    };

    std::array<VertexPositionColor, 3> vertices{};
    for (std::size_t i = 0; i < plan.size(); ++i)
        vertices[i] = MakePoint(width, height, plan[i]);
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 3);

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    device.SetVertexBuffer(&vertexBuffer);

    const std::array<const RasterizerState*, 3> states{
        &RasterizerState::CullNone,
        &RasterizerState::CullClockwise,
        &RasterizerState::CullCounterClockwise,
    };
    const std::array<const char*, 3> labels{
        "CullNone", "CullClockwiseFace", "CullCounterClockwiseFace",
    };

    for (std::size_t i = 0; i < states.size(); ++i)
    {
        device.setRasterizerStateProperty(*states[i]);
        device.Clear(Color::Black);
        effect.Apply();
        device.DrawPrimitives(PrimitiveType::PointListEXT, 0, 3);

        const FrameSnapshot pixels = CaptureBackbuffer(device);
        ExpectPointRendered(pixels, plan[0], labels[i]);
        ExpectPointRendered(pixels, plan[1], labels[i]);
        ExpectPointRendered(pixels, plan[2], labels[i]);
        ExpectPointCoverageBudget(pixels, Color::Black, 3, labels[i]);
    }

    // FillMode::WireFrame has no meaning for points; they must stay points, not become lines.
    RasterizerState wireframe;
    wireframe.setCullModeProperty(CullMode::None);
    wireframe.setFillModeProperty(FillMode::WireFrame);
    device.setRasterizerStateProperty(wireframe);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::PointListEXT, 0, 3);

    const FrameSnapshot wire = CaptureBackbuffer(device);
    ExpectPointRendered(wire, plan[0], "WireFrame point 0");
    ExpectPointRendered(wire, plan[1], "WireFrame point 1");
    ExpectPointRendered(wire, plan[2], "WireFrame point 2");
    ExpectPointCoverageBudget(wire, Color::Black, 3, "WireFrame point list");
}

// Viewport, scissor and blending apply to points exactly as they do to any other primitive.
TEST_F(PointListPrimitiveTest, PointListRespectsViewportScissorAndBlendState)
{
    RequirePointRendering();

    const int width = BackbufferWidth();
    const int height = BackbufferHeight();

    // A single point at NDC (-0.5, 0.5) lands at a different absolute pixel under a sub-viewport
    // than it does under the full viewport.
    const std::array<VertexPositionColor, 4> vertices{
        VertexPositionColor(Vector3(-0.5f, 0.5f, 0.5f), Color::Red),
        VertexPositionColor(Vector3(0.0f, 0.0f, 0.5f), Color::Lime),
        VertexPositionColor(Vector3(-0.9f, 0.9f, 0.5f), Color::Yellow),
        VertexPositionColor(Vector3(0.0f, 0.0f, 0.5f), Color::Blue),
    };
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 4, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 4);

    const std::array<std::uint16_t, 4> indices{0, 1, 2, 3};
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    indexBuffer.SetData(indices.data(), 4);

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);

    const Viewport subViewport(width / 4, height / 4, width / 2, height / 2);
    device.setViewportProperty(subViewport);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 4, 0, 1);

    const FrameSnapshot viewportPixels = CaptureBackbuffer(device, width, height);
    device.setViewportProperty(Viewport(0, 0, width, height));
    const int expectedX = width / 4 + (width / 2) / 4;
    const int expectedY = height / 4 + (height / 2) / 4;
    EXPECT_TRUE(viewportPixels.HasExactWithin(expectedX, expectedY, 2, Color::Red))
        << "the point lands inside the sub-viewport";
    EXPECT_FALSE(viewportPixels.HasExactWithin(width / 4, height / 4, 2, Color::Red))
        << "the point does not land at its full-viewport position";
    ExpectPointCoverageBudget(
        viewportPixels, Color::Black, 1, "sub-viewport point coverage");

    // Scissor: one point inside the rect, one outside it.
    RasterizerState scissored;
    scissored.setCullModeProperty(CullMode::None);
    scissored.setScissorTestEnableProperty(true);
    device.setRasterizerStateProperty(scissored);
    device.setScissorRectangleProperty(
        Rectangle(width / 4, height / 4, width / 2, height / 2));
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 4, 1, 2);

    const FrameSnapshot scissorPixels = CaptureBackbuffer(device);
    EXPECT_TRUE(scissorPixels.HasExactWithin(width / 2, height / 2, 2, Color::Lime))
        << "the point inside the scissor rect survives";
    ExpectColorAbsent(
        scissorPixels, Color::Yellow, "the point outside the scissor rect is clipped");
    ExpectPointCoverageBudget(
        scissorPixels, Color::Black, 1, "scissored point coverage");

    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setScissorRectangleProperty(Rectangle(0, 0, width, height));

    // Additive blending of two points at the same pixel is an exact deterministic sum.
    device.setBlendStateProperty(BlendState::Additive);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 4, 3, 1);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 4, 1, 1);

    const FrameSnapshot blendPixels = CaptureBackbuffer(device);
    EXPECT_TRUE(blendPixels.HasExactWithin(
        width / 2, height / 2, 2, Color(0, 255, 255, 255)))
        << "blue + lime additive point is exactly (0,0,255)+(0,255,0) per channel";
    device.setBlendStateProperty(BlendState::Opaque);
}
#endif

#if defined(CNA_RENDERER_BGFX) || defined(CNA_RENDERER_EASYGL) || \
    defined(CNA_RENDERER_VULKAN) || defined(CNA_RENDERER_WEBGPU)
// Non-indexed point addressing: vertexStart selects the first consumed vertex and primitiveCount
// limits the consumed range to exactly that many points.
//
// Bgfx joined this guard with REMED-GFX-113, which replaced its whole-buffer
// bgfx::setVertexBuffer(0, handle) binding with the exact [vertexStart, vertexStart + consumed)
// range. The general non-indexed range contract for every topology lives in
// NonIndexedDrawRangeTests.cpp; this case keeps the point-specific coverage alongside the rest of
// REMED-GFX-111's topology contract.
TEST_F(PointListPrimitiveTest, NonIndexedPointListHonorsVertexStartAndExactCount)
{
    RequirePointRendering();

    const int width = BackbufferWidth();
    const int height = BackbufferHeight();
    const std::array<PointSpec, 3> wanted{
        PointSpec{width / 4, height / 4, Color::Red, 0.5f},
        PointSpec{width / 2, height / 2, Color::Lime, 0.5f},
        PointSpec{3 * width / 4, 3 * height / 4, Color::Blue, 0.5f},
    };

    const std::array<VertexPositionColor, 7> vertices{
        MakePoint(width, height, PointSpec{width / 16, height / 16, Color::Magenta, 0.5f}),
        MakePoint(width, height, PointSpec{15 * width / 16, height / 16, Color::Magenta, 0.5f}),
        MakePoint(width, height, wanted[0]),
        MakePoint(width, height, wanted[1]),
        MakePoint(width, height, wanted[2]),
        MakePoint(width, height, PointSpec{width / 16, 15 * height / 16, Color::Magenta, 0.5f}),
        MakePoint(width, height, PointSpec{15 * width / 16, 15 * height / 16, Color::Magenta, 0.5f}),
    };
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 7, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 7);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.DrawPrimitives(PrimitiveType::PointListEXT, 2, 3);

    const FrameSnapshot pixels = CaptureBackbuffer(device);
    ExpectPointRendered(pixels, wanted[0], "non-indexed offset point 0");
    ExpectPointRendered(pixels, wanted[1], "non-indexed offset point 1");
    ExpectPointRendered(pixels, wanted[2], "non-indexed offset point 2");
    ExpectColorAbsent(
        pixels, Color::Magenta, "non-indexed decoy points before and after the range");
    ExpectPointCoverageBudget(
        pixels, Color::Black, 3, "non-indexed point range with vertexStart");
}
#endif

#ifdef CNA_RENDERER_BGFX
// Bgfx expresses topology as per-submission state (BGFX_STATE_PT_*), not as a cached graphics
// pipeline object, so switching to and from point topology must not allocate any native resource.
TEST_F(PointListPrimitiveTest, BgfxPointDrawsAllocateNoPerDrawNativeResources)
{
    RequirePointRendering();

    device.Present();
    device.Present();
    const bgfx::Stats* stats = bgfx::getStats();
    ASSERT_NE(nullptr, stats);
    const std::uint16_t processVertexBaseline = stats->numDynamicVertexBuffers;
    const std::uint16_t processIndexBaseline = stats->numDynamicIndexBuffers;

    const int width = BackbufferWidth();
    const int height = BackbufferHeight();
    const std::array<VertexPositionColor, 3> vertices{
        MakePoint(width, height, PointSpec{width / 4, height / 2, Color::Red, 0.5f}),
        MakePoint(width, height, PointSpec{width / 2, height / 2, Color::Lime, 0.5f}),
        MakePoint(width, height, PointSpec{3 * width / 4, height / 2, Color::Blue, 0.5f}),
    };
    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 3, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 3);
    indexBuffer.SetData(indices.data(), 3);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);

    device.Present();
    device.Present();
    stats = bgfx::getStats();
    ASSERT_NE(nullptr, stats);
    const std::uint16_t liveVertexBaseline = stats->numDynamicVertexBuffers;
    const std::uint16_t liveIndexBaseline = stats->numDynamicIndexBuffers;
    EXPECT_EQ(processVertexBaseline + 1u, liveVertexBaseline);
    EXPECT_EQ(processIndexBaseline + 1u, liveIndexBaseline);

    for (int frame = 0; frame < 24; ++frame)
    {
        device.Clear(Color::Black);
        device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 3, 0, 1);
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
        device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 3, 0, 3);
        device.DrawIndexedPrimitives(PrimitiveType::LineList, 0, 0, 3, 0, 1);
        device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 3, 2, 1);
        device.Present();

        stats = bgfx::getStats();
        ASSERT_NE(nullptr, stats);
        EXPECT_LE(stats->numDynamicVertexBuffers, liveVertexBaseline);
        EXPECT_LE(stats->numDynamicIndexBuffers, liveIndexBaseline);
    }

    device.SetVertexBuffer(nullptr);
    device.SetIndexBuffer(nullptr);
    vertexBuffer.Dispose();
    indexBuffer.Dispose();
    device.Present();
    device.Present();

    stats = bgfx::getStats();
    ASSERT_NE(nullptr, stats);
    EXPECT_EQ(processVertexBaseline, stats->numDynamicVertexBuffers);
    EXPECT_EQ(processIndexBaseline, stats->numDynamicIndexBuffers);
}

// REMED-GFX-113 result on the exact buffer REMED-GFX-111 used to pin the defect. Bgfx used to bind
// the whole bound vertex buffer for every non-indexed draw, so all seven vertices became points;
// the binding is now the exact [vertexStart, vertexStart + primitiveCount) element range and only
// the three requested points render. The topology itself is REMED-GFX-111's own result and is still
// asserted here: three point-sized marks, never area geometry.
TEST_F(PointListPrimitiveTest, BgfxNonIndexedPointRangeCoversExactlyTheRequestedVertices)
{
    RequirePointRendering();

    const int width = BackbufferWidth();
    const int height = BackbufferHeight();
    const std::array<PointSpec, 3> wanted{
        PointSpec{width / 4, height / 4, Color::Red, 0.5f},
        PointSpec{width / 2, height / 2, Color::Lime, 0.5f},
        PointSpec{3 * width / 4, 3 * height / 4, Color::Blue, 0.5f},
    };
    const std::array<PointSpec, 4> outside{
        PointSpec{width / 16, height / 16, Color::Magenta, 0.5f},
        PointSpec{15 * width / 16, height / 16, Color::Magenta, 0.5f},
        PointSpec{width / 16, 15 * height / 16, Color::Magenta, 0.5f},
        PointSpec{15 * width / 16, 15 * height / 16, Color::Magenta, 0.5f},
    };

    const std::array<VertexPositionColor, 7> vertices{
        MakePoint(width, height, outside[0]),
        MakePoint(width, height, outside[1]),
        MakePoint(width, height, wanted[0]),
        MakePoint(width, height, wanted[1]),
        MakePoint(width, height, wanted[2]),
        MakePoint(width, height, outside[2]),
        MakePoint(width, height, outside[3]),
    };
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 7, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 7);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.DrawPrimitives(PrimitiveType::PointListEXT, 2, 3);

    const FrameSnapshot pixels = CaptureBackbuffer(device);
    ExpectPointRendered(pixels, wanted[0], "requested point 0 renders");
    ExpectPointRendered(pixels, wanted[1], "requested point 1 renders");
    ExpectPointRendered(pixels, wanted[2], "requested point 2 renders");
    // REMED-GFX-111 result: every consumed vertex becomes a point, never area geometry.
    ExpectPointCoverageBudget(
        pixels, Color::Black, 3, "exact-range non-indexed point coverage");
    // REMED-GFX-113 result: the four decoy vertices outside [2, 5) are never consumed.
    ExpectColorAbsent(
        pixels, Color::Magenta,
        "decoy points before and after the requested non-indexed range");
}
#endif

#ifdef CNA_RENDERER_SDL_GPU
// SDL_GPU maps PointListEXT to SDL_GPU_PRIMITIVETYPE_POINTLIST and consumes exactly
// primitiveCount vertices/indices, but implements no backbuffer readback. Its practical exact-pixel
// control therefore runs through RenderTarget2D::GetData, which this renderer does support.
//
// Indexed addressing is deliberately exercised only at offset zero here: SDL_GPU passes literal
// 0/0 for first_index and vertex_offset at every SDL_DrawGPUIndexedPrimitives site, so public
// startIndex/baseVertex do not reach the native draw (the SDL_GPU counterpart of
// REMED-GFX-020/060/107, recorded separately as REMED-GFX-117). That is an addressing defect, not a
// topology defect, and is not corrected here. The non-indexed case below does carry a nonzero
// vertexStart, which this renderer honours.
TEST_F(PointListPrimitiveTest, SdlGpuPointListRendersExactRenderTargetPixels)
{
    RequirePointRendering();

    constexpr int kSize = 128;
    const std::array<PointSpec, 3> wanted{
        PointSpec{kSize / 4, kSize / 4, Color::Red, 0.5f},
        PointSpec{kSize / 2, kSize / 2, Color::Lime, 0.5f},
        PointSpec{3 * kSize / 4, 3 * kSize / 4, Color::Blue, 0.5f},
    };

    const std::array<VertexPositionColor, 8> vertices{
        MakePoint(kSize, kSize, PointSpec{kSize / 8, kSize / 8, Color::Magenta, 0.5f}),
        MakePoint(kSize, kSize, PointSpec{7 * kSize / 8, kSize / 8, Color::Magenta, 0.5f}),
        MakePoint(kSize, kSize, wanted[0]),
        MakePoint(kSize, kSize, wanted[1]),
        MakePoint(kSize, kSize, wanted[2]),
        MakePoint(kSize, kSize, PointSpec{kSize / 8, 7 * kSize / 8, Color::Magenta, 0.5f}),
        MakePoint(kSize, kSize, PointSpec{7 * kSize / 8, 7 * kSize / 8, Color::Magenta, 0.5f}),
        MakePoint(kSize, kSize, PointSpec{kSize / 2, 7 * kSize / 8, Color::Magenta, 0.5f}),
    };
    // Exactly the three intended points; every other vertex in the buffer is a decoy that must
    // stay unlit.
    const std::array<std::uint16_t, 3> indices{2, 3, 4};

    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 8, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 8);
    indexBuffer.SetData(indices.data(), 3);

    RenderTarget2D target(
        device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
        RenderTargetUsage::PreserveContents);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 8, 0, 3);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const FrameSnapshot indexed = CaptureRenderTarget(target, kSize, kSize);
    ExpectPointRendered(indexed, wanted[0], "SDL_GPU indexed render-target point 0");
    ExpectPointRendered(indexed, wanted[1], "SDL_GPU indexed render-target point 1");
    ExpectPointRendered(indexed, wanted[2], "SDL_GPU indexed render-target point 2");
    ExpectColorAbsent(indexed, Color::Magenta, "SDL_GPU indexed decoy points");
    ExpectPointCoverageBudget(
        indexed, Color::Black, 3, "SDL_GPU indexed render-target point coverage");

    // Non-indexed: vertexStart selects the first consumed vertex and primitiveCount limits the
    // range to exactly three points, with decoys on both sides.
    device.SetIndexBuffer(nullptr);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::PointListEXT, 2, 3);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const FrameSnapshot direct = CaptureRenderTarget(target, kSize, kSize);
    ExpectPointRendered(direct, wanted[0], "SDL_GPU non-indexed render-target point 0");
    ExpectPointRendered(direct, wanted[1], "SDL_GPU non-indexed render-target point 1");
    ExpectPointRendered(direct, wanted[2], "SDL_GPU non-indexed render-target point 2");
    ExpectColorAbsent(direct, Color::Magenta, "SDL_GPU non-indexed decoy points");
    ExpectPointCoverageBudget(
        direct, Color::Black, 3, "SDL_GPU non-indexed render-target point coverage");
}
#endif

#ifdef CNA_RENDERER_SOFTWARE
// Software's documented v1 raster boundary is TriangleList only. It must keep rejecting
// PointListEXT explicitly on every public entry point rather than approximating it.
TEST_F(PointListPrimitiveTest, SoftwareExplicitlyRejectsPointListTopology)
{
    RequirePointRendering();

    const std::array<VertexPositionColor, 3> vertices{
        VertexPositionColor(Vector3(-0.5f, -0.5f, 0.5f), Color::Red),
        VertexPositionColor(Vector3(0.0f, 0.5f, 0.5f), Color::Lime),
        VertexPositionColor(Vector3(0.5f, -0.5f, 0.5f), Color::Blue),
    };
    const std::array<std::uint16_t, 3> indices{0, 1, 2};

    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 3, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 3);
    indexBuffer.SetData(indices.data(), 3);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);

    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::PointListEXT, 0, 3), std::runtime_error);
    EXPECT_THROW(
        device.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 3, 0, 3),
        std::runtime_error);
    EXPECT_THROW(
        device.DrawUserPrimitives(PrimitiveType::PointListEXT, vertices.data(), 0, 3),
        std::runtime_error);
    EXPECT_THROW(
        device.DrawUserIndexedPrimitives(
            PrimitiveType::PointListEXT, vertices.data(), 0, 3, indices.data(), 0, 3),
        std::runtime_error);
}
#endif
