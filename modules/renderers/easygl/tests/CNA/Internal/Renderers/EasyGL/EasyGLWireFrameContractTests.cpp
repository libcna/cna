// SPDX-License-Identifier: MS-PL

// SOFTWARE-178: GLES/WebGL wireframe without native polygon mode has a bounded stock route.
// A stock single-stream PositionColor draw with no rasterizer side effects can clip a triangle
// and draw its resulting polygon as a line loop. Other unsupported cases still refuse.

#include <array>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/NotSupportedException.hpp"

namespace
{
    using namespace CNA::Testing::Renderers;  // NOLINT(google-build-using-namespace)
    using namespace Microsoft::Xna::Framework;  // NOLINT(google-build-using-namespace)
    using namespace Microsoft::Xna::Framework::Graphics;  // NOLINT(google-build-using-namespace)

    VertexDeclaration PositionColorDeclaration()
    {
        return VertexDeclaration(
            16,
            {VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
             VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0)});
    }

    std::array<VertexPositionColor, 3> Triangle()
    {
        return {{
            {Vector3(-0.75f, -0.75f, 0.0f), Color::White},
            {Vector3( 0.75f, -0.50f, 0.0f), Color::White},
            {Vector3(-0.25f,  0.75f, 0.0f), Color::White},
        }};
    }

    RasterizerState WireState()
    {
        RasterizerState state;
        state.setCullModeProperty(CullMode::None);
        state.setFillModeProperty(FillMode::WireFrame);
        return state;
    }

    class EasyGLUnsupportedWireFrameTest : public ::testing::Test
    {
    protected:
        std::unique_ptr<GraphicsDevice> device;

        void SetUp() override
        {
            if (!CNA_RENDERER_IS(OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2))
                GTEST_SKIP() << "the selected renderer is not in the EasyGL family";

            device = std::make_unique<GraphicsDevice>(
                GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                PresentationParameters());
            if (device->SupportsCapability(CNA::GraphicsCapability::WireFrame))
            {
                GTEST_SKIP() << "this context exposes native polygon-mode wireframe; the shared "
                                "positive pixel and depth-bias oracles cover it";
            }
        }

        void ApplyBasicEffect(BasicEffect& effect)
        {
            effect.VertexColorEnabled = true;
            effect.World = Matrix::getIdentityProperty();
            effect.View = Matrix::getIdentityProperty();
            effect.Projection = Matrix::getIdentityProperty();
            effect.Apply();
        }
    };
}

TEST_F(EasyGLUnsupportedWireFrameTest,
       OrdinaryIndexedAndUserTriangleRoutesRefuseThenRecoverToSolid)
{
    const auto vertices = Triangle();
    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    VertexBuffer vertexBuffer(*device, PositionColorDeclaration(), 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 3);
    IndexBuffer indexBuffer(*device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    indexBuffer.SetData(indices.data(), 3);
    BasicEffect effect(*device);

    const auto expectRefusedThenSolid = [&](const auto& draw) {
        RasterizerState wire = WireState();
        device->setRasterizerStateProperty(wire);
        ApplyBasicEffect(effect);
        EXPECT_THROW(draw(), System::NotSupportedException);

        device->setRasterizerStateProperty(RasterizerState::CullNone);
        ApplyBasicEffect(effect);
        EXPECT_NO_THROW(draw());
    };

    device->SetVertexBuffer(&vertexBuffer);
    expectRefusedThenSolid([&] {
        device->DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
    });

    device->SetIndexBuffer(&indexBuffer);
    expectRefusedThenSolid([&] {
        device->DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
    });

    device->SetVertexBuffer(nullptr);
    device->SetIndexBuffer(nullptr);
    expectRefusedThenSolid([&] {
        device->DrawUserPrimitives(
            PrimitiveType::TriangleList, vertices.data(), 0, 1,
            PositionColorDeclaration());
    });
    expectRefusedThenSolid([&] {
        device->DrawUserIndexedPrimitives(
            PrimitiveType::TriangleList, vertices.data(), 0, 3,
            indices.data(), 0, 1);
    });
    expectRefusedThenSolid([&] {
        device->DrawUserPrimitives(
            PrimitiveType::TriangleStrip, vertices.data(), 0, 1,
            PositionColorDeclaration());
    });

    RasterizerState wire = WireState();
    device->setRasterizerStateProperty(wire);
    ApplyBasicEffect(effect);
    EXPECT_NO_THROW(device->DrawUserPrimitives(
        PrimitiveType::LineList, vertices.data(), 0, 1,
        PositionColorDeclaration()));
}

TEST_F(EasyGLUnsupportedWireFrameTest,
       UnclippedAndClippedPositionColorTrianglesDrawWithoutRasterizerSideEffects)
{
    auto vertices = Triangle();
    for (auto& vertex : vertices)
        vertex.Position.Z = 0.5f;

    device->setDepthStencilStateProperty(DepthStencilState::None);
    device->setRasterizerStateProperty(WireState());
    BasicEffect effect(*device);
    ApplyBasicEffect(effect);
    EXPECT_NO_THROW(device->DrawUserPrimitives(
        PrimitiveType::TriangleList, vertices.data(), 0, 1,
        PositionColorDeclaration()));

    vertices[0].Position.X = -2.0f;
    device->Clear(Color::Black);
    ApplyBasicEffect(effect);
    EXPECT_NO_THROW(device->DrawUserPrimitives(
        PrimitiveType::TriangleList, vertices.data(), 0, 1,
        PositionColorDeclaration()));
    const auto viewport = device->getViewportProperty();
    const std::size_t pixelCount = static_cast<std::size_t>(viewport.getWidthProperty()) *
                                   static_cast<std::size_t>(viewport.getHeightProperty());
    std::vector<Color> pixels(pixelCount);
    device->GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
    const auto whitePixels = std::count_if(pixels.begin(), pixels.end(), [](const Color& pixel) {
        return pixel.getRProperty() > 200 && pixel.getGProperty() > 200 &&
               pixel.getBProperty() > 200;
    });
    EXPECT_GT(whitePixels, 10);
    EXPECT_LT(static_cast<std::size_t>(whitePixels), pixelCount / 20);

    for (auto& vertex : vertices)
        vertex.Position.X -= 3.0f;
    ApplyBasicEffect(effect);
    EXPECT_NO_THROW(device->DrawUserPrimitives(
        PrimitiveType::TriangleList, vertices.data(), 0, 1,
        PositionColorDeclaration()));
}

TEST_F(EasyGLUnsupportedWireFrameTest, MultiStreamTriangleRouteRefuses)
{
    struct PositionVertex { float x, y, z; };
    struct ColorVertex { std::uint32_t color; };
    const std::array<PositionVertex, 3> positions{{
        {-0.75f, -0.75f, 0.0f}, {0.75f, -0.50f, 0.0f}, {-0.25f, 0.75f, 0.0f},
    }};
    const std::array<ColorVertex, 3> colors{{
        {0xFFFFFFFFu}, {0xFFFFFFFFu}, {0xFFFFFFFFu},
    }};
    const VertexDeclaration positionDeclaration(
        12, {VertexElement(0, VertexElementFormat::Vector3,
                           VertexElementUsage::Position, 0)});
    const VertexDeclaration colorDeclaration(
        4, {VertexElement(0, VertexElementFormat::Color,
                          VertexElementUsage::Color, 0)});
    VertexBuffer positionBuffer(*device, positionDeclaration, 3, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), 3, sizeof(PositionVertex));
    VertexBuffer colorBuffer(*device, colorDeclaration, 3, BufferUsage::None);
    colorBuffer.SetDataRaw(colors.data(), 3, sizeof(ColorVertex));
    device->SetVertexBuffers({VertexBufferBinding(&positionBuffer),
                              VertexBufferBinding(&colorBuffer)});

    BasicEffect effect(*device);
    RasterizerState wire = WireState();
    device->setRasterizerStateProperty(wire);
    ApplyBasicEffect(effect);
    EXPECT_THROW(device->DrawPrimitives(PrimitiveType::TriangleList, 0, 1),
                 System::NotSupportedException);
}

TEST_F(EasyGLUnsupportedWireFrameTest, InstancedTriangleRouteRefuses)
{
    if (!device->SupportsCapability(CNA::GraphicsCapability::Instancing))
        GTEST_SKIP() << "this EasyGL profile has no instanced draw route";

    const auto vertices = Triangle();
    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    VertexBuffer vertexBuffer(*device, PositionColorDeclaration(), 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 3);
    IndexBuffer indexBuffer(*device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    indexBuffer.SetData(indices.data(), 3);

    const VertexDeclaration instanceDeclaration(
        64,
        {VertexElement(0, VertexElementFormat::Vector4,
                       VertexElementUsage::TextureCoordinate, 1),
         VertexElement(16, VertexElementFormat::Vector4,
                       VertexElementUsage::TextureCoordinate, 2),
         VertexElement(32, VertexElementFormat::Vector4,
                       VertexElementUsage::TextureCoordinate, 3),
         VertexElement(48, VertexElementFormat::Vector4,
                       VertexElementUsage::TextureCoordinate, 4)});
    VertexBuffer instanceBuffer(*device, instanceDeclaration, 1, BufferUsage::None);
    const Matrix identity = Matrix::getIdentityProperty();
    instanceBuffer.SetDataRaw(&identity, 1, sizeof(Matrix));
    device->SetVertexBuffers({VertexBufferBinding(&vertexBuffer, 0, 0),
                              VertexBufferBinding(&instanceBuffer, 0, 1)});
    device->SetIndexBuffer(&indexBuffer);

    BasicEffect effect(*device);
    RasterizerState wire = WireState();
    device->setRasterizerStateProperty(wire);
    ApplyBasicEffect(effect);
    EXPECT_THROW(
        device->DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, 3, 0, 1, 1),
        System::NotSupportedException);
}

TEST_F(EasyGLUnsupportedWireFrameTest, StockSpriteBatchTriangleRouteRefuses)
{
    Texture2D texture(*device, 1, 1);
    const Color white = Color::White;
    texture.SetData(&white, 1);

    RasterizerState wire = WireState();
    SpriteBatch batch(*device);
    batch.Begin(SpriteSortMode::Immediate, BlendState::Opaque,
                nullptr, nullptr, &wire);
    EXPECT_THROW(batch.Draw(texture, Rectangle(0, 0, 8, 8), Color::White),
                 System::NotSupportedException);
}

TEST_F(EasyGLUnsupportedWireFrameTest,
       RefusalSurvivesCullBiasClippingMsaaAndTwoSidedStencilState)
{
    // One vertex crosses the left clip plane. This combines clipping with rasterizer state that a
    // line-list substitution cannot faithfully inherit; the route must refuse without polygon mode.
    const std::array<VertexPositionColor, 3> clipped{{
        {Vector3(-2.0f, 0.0f, 0.5f), Color::White},
        {Vector3( 0.5f, -0.5f, 0.5f), Color::White},
        {Vector3( 0.5f,  0.5f, 0.5f), Color::White},
    }};

    RasterizerState wire;
    wire.setCullModeProperty(CullMode::CullClockwiseFace);
    wire.setFillModeProperty(FillMode::WireFrame);
    wire.setDepthBiasProperty(0.02f);
    wire.setSlopeScaleDepthBiasProperty(1.0f);
    wire.setMultiSampleAntiAliasProperty(true);
    device->setRasterizerStateProperty(wire);

    DepthStencilState stencil;
    stencil.setDepthBufferEnableProperty(true);
    stencil.setStencilEnableProperty(true);
    stencil.setTwoSidedStencilModeProperty(true);
    stencil.setStencilFunctionProperty(CompareFunction::Always);
    stencil.setStencilPassProperty(StencilOperation::Replace);
    stencil.setCounterClockwiseStencilFunctionProperty(CompareFunction::Always);
    stencil.setCounterClockwiseStencilPassProperty(StencilOperation::Replace);
    device->setDepthStencilStateProperty(stencil);

    RenderTarget2D target(
        *device, 32, 32, false, SurfaceFormat::Color,
        DepthFormat::Depth24Stencil8, 4, RenderTargetUsage::PreserveContents);
    device->SetRenderTarget(&target);
    BasicEffect effect(*device);
    ApplyBasicEffect(effect);
    EXPECT_THROW(
        device->DrawUserPrimitives(
            PrimitiveType::TriangleList, clipped.data(), 0, 1,
            PositionColorDeclaration()),
        System::NotSupportedException);
    device->SetRenderTarget(nullptr);
}
