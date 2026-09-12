// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-031: declaration-driven primitive topology, offset, transform, and
// public user/static-buffer evidence. Exit 77 means no usable GL context.

#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"
#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/NotSupportedException.hpp"

#include "RlglBridge.hpp"
#include "RlglResources.hpp"

#include "common/PixelTestGame.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    namespace Rlgl = CNA::Internal::Renderers::Rlgl;
    namespace Bridge = CNA::Internal::Renderers::Rlgl::Bridge;
    using CNA::Internal::Graphics::PositionColorStream;

    constexpr int kWidth = 64;
    constexpr int kHeight = 48;

    [[nodiscard]] float PixelCenterX(const int x)
    {
        return 2.0f * (static_cast<float>(x) + 0.5f) / kWidth - 1.0f;
    }

    [[nodiscard]] float PixelCenterY(const int topY)
    {
        const int bottomY = kHeight - topY - 1;
        return 2.0f * (static_cast<float>(bottomY) + 0.5f) / kHeight - 1.0f;
    }

    [[nodiscard]] bool IsColor(const Color& actual, const Color& expected)
    {
        constexpr int tolerance = 3;
        return std::abs(static_cast<int>(actual.getRProperty()) -
                        static_cast<int>(expected.getRProperty())) <= tolerance &&
            std::abs(static_cast<int>(actual.getGProperty()) -
                     static_cast<int>(expected.getGProperty())) <= tolerance &&
            std::abs(static_cast<int>(actual.getBProperty()) -
                     static_cast<int>(expected.getBProperty())) <= tolerance;
    }

    [[nodiscard]] std::array<PositionColorStream, 3> Triangle(
        const std::uint8_t r, const std::uint8_t g, const std::uint8_t b)
    {
        return {{{-0.75f, -0.75f, 0.0f, r, g, b, 255},
                 {0.75f, -0.75f, 0.0f, r, g, b, 255},
                 {0.0f, 0.75f, 0.0f, r, g, b, 255}}};
    }
}

class RlglPrimitiveTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglPrimitiveTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphics_->setPreferredBackBufferWidthProperty(kWidth);
        graphics_->setPreferredBackBufferHeightProperty(kHeight);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<Rlgl::RlglRenderer&>(device.GetRenderer());
        renderer.SetBlendEnabled(false);
        renderer.SetDepthTestEnabled(false);
        renderer.ApplyRasterizerState(0, 0, false);

        const auto read = [&](const int x, const int y) {
            Color result;
            const Rectangle pixel(x, y, 1, 1);
            device.GetBackBufferData(&pixel, &result, 0, 1);
            return result;
        };
        const auto clear = [&] { device.Clear(Color(0, 0, 0, 255)); };
        const Matrix identity = Matrix::getIdentityProperty();

        const auto drawNonIndexed = [&](
            const PrimitiveType type,
            const PositionColorStream* const vertices,
            const int vertexCount, const int primitiveCount)
        {
            auto buffer = renderer.CreateVertexBuffer(vertexCount);
            buffer->SetVertexDeclaration(VertexPositionColor::getVertexDeclarationStatic());
            buffer->SetData(vertices, vertexCount, sizeof(PositionColorStream));
            clear();
            renderer.DrawColoredPrimitives(
                *buffer, identity, identity, identity, type, primitiveCount);
            return Bridge::GetPrimitiveDrawSnapshotForTesting();
        };

        const auto redTriangle = Triangle(255, 0, 0);
        const auto triangleCall = drawNonIndexed(
            PrimitiveType::TriangleList, redTriangle.data(), 3, 1);
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(255, 0, 0, 255)) &&
                  triangleCall.primitiveMode == 0x0004 && triangleCall.elementCount == 3 &&
                  !triangleCall.indexed && triangleCall.usedRlglDrawWrapper,
              "triangle-list submission uses rlgl's low-level draw wrapper and shades pixels");

        const std::array<PositionColorStream, 4> strip{{
            {-0.75f, -0.75f, 0.0f, 0, 255, 0, 255},
            {0.75f, -0.75f, 0.0f, 0, 255, 0, 255},
            {-0.75f, 0.75f, 0.0f, 0, 255, 0, 255},
            {0.75f, 0.75f, 0.0f, 0, 255, 0, 255}}};
        const auto stripCall = drawNonIndexed(
            PrimitiveType::TriangleStrip, strip.data(), 4, 2);
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(0, 255, 0, 255)) &&
                  stripCall.primitiveMode == 0x0005 && stripCall.elementCount == 4 &&
                  !stripCall.usedRlglDrawWrapper,
              "triangle-strip uses the measured arbitrary-topology bridge and shades pixels");

        const float pointY = PixelCenterY(kHeight / 2);
        const std::array<PositionColorStream, 2> line{{
            {-0.8f, pointY, 0.0f, 0, 0, 255, 255},
            {0.8f, pointY, 0.0f, 0, 0, 255, 255}}};
        const auto lineListCall = drawNonIndexed(
            PrimitiveType::LineList, line.data(), 2, 1);
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(0, 0, 255, 255)) &&
                  lineListCall.primitiveMode == 0x0001 && lineListCall.elementCount == 2,
              "line-list topology reaches exact GL lines and a rasterized center pixel");

        const std::array<PositionColorStream, 3> lineStrip{{
            {-0.8f, pointY, 0.0f, 255, 255, 0, 255},
            {PixelCenterX(kWidth / 2), pointY, 0.0f, 255, 255, 0, 255},
            {0.8f, pointY, 0.0f, 255, 255, 0, 255}}};
        const auto lineStripCall = drawNonIndexed(
            PrimitiveType::LineStrip, lineStrip.data(), 3, 2);
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(255, 255, 0, 255)) &&
                  lineStripCall.primitiveMode == 0x0003 && lineStripCall.elementCount == 3,
              "line-strip topology consumes primitiveCount plus one vertices");

        const PositionColorStream point{
            PixelCenterX(kWidth / 2), pointY, 0.0f, 255, 0, 255, 255};
        const auto pointCall = drawNonIndexed(
            PrimitiveType::PointListEXT, &point, 1, 1);
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(255, 0, 255, 255)) &&
                  pointCall.primitiveMode == 0x0000 && pointCall.elementCount == 1,
              "point-list topology programs point size and shades the selected pixel");

        auto indexedVertex = renderer.CreateVertexBuffer(3);
        indexedVertex->SetVertexDeclaration(VertexPositionColor::getVertexDeclarationStatic());
        indexedVertex->SetData(redTriangle.data(), 3, sizeof(PositionColorStream));
        auto index16 = renderer.CreateIndexBuffer16(3);
        const std::array<std::uint16_t, 3> indices16{0, 1, 2};
        index16->SetData16(indices16.data(), 3);
        clear();
        renderer.DrawIndexedColoredPrimitives(
            *indexedVertex, *index16, identity, identity, identity,
            PrimitiveType::TriangleList, 1);
        const auto indexed16Call = Bridge::GetPrimitiveDrawSnapshotForTesting();
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(255, 0, 0, 255)) &&
                  indexed16Call.indexed && indexed16Call.indexType == 0x1403 &&
                  indexed16Call.usedRlglDrawWrapper,
              "16-bit indexed triangle lists use rlgl's public indexed wrapper");

        const auto drawIndexed16 = [&](
            const PrimitiveType type,
            const PositionColorStream* const vertices, const int vertexCount,
            const std::uint16_t* const indices, const int indexCount,
            const int primitiveCount)
        {
            auto vertexBuffer = renderer.CreateVertexBuffer(vertexCount);
            vertexBuffer->SetVertexDeclaration(
                VertexPositionColor::getVertexDeclarationStatic());
            vertexBuffer->SetData(vertices, vertexCount, sizeof(PositionColorStream));
            auto indexBuffer = renderer.CreateIndexBuffer16(indexCount);
            indexBuffer->SetData16(indices, indexCount);
            clear();
            renderer.DrawIndexedColoredPrimitives(
                *vertexBuffer, *indexBuffer, identity, identity, identity,
                type, primitiveCount);
            return Bridge::GetPrimitiveDrawSnapshotForTesting();
        };
        const std::array<std::uint16_t, 4> stripIndices{0, 1, 2, 3};
        const auto indexedStripCall = drawIndexed16(
            PrimitiveType::TriangleStrip, strip.data(), 4,
            stripIndices.data(), 4, 2);
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(0, 255, 0, 255)) &&
                  indexedStripCall.indexed && indexedStripCall.primitiveMode == 0x0005 &&
                  indexedStripCall.elementCount == 4,
              "indexed triangle-strip submission preserves its topology and element formula");

        const std::array<std::uint16_t, 2> lineIndices{0, 1};
        const auto indexedLineCall = drawIndexed16(
            PrimitiveType::LineList, line.data(), 2,
            lineIndices.data(), 2, 1);
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(0, 0, 255, 255)) &&
                  indexedLineCall.indexed && indexedLineCall.primitiveMode == 0x0001 &&
                  indexedLineCall.elementCount == 2,
              "indexed line-list submission shades the same pixels as its array counterpart");

        const std::array<std::uint16_t, 3> lineStripIndices{0, 1, 2};
        const auto indexedLineStripCall = drawIndexed16(
            PrimitiveType::LineStrip, lineStrip.data(), 3,
            lineStripIndices.data(), 3, 2);
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(255, 255, 0, 255)) &&
                  indexedLineStripCall.indexed &&
                  indexedLineStripCall.primitiveMode == 0x0003 &&
                  indexedLineStripCall.elementCount == 3,
              "indexed line-strip submission consumes primitiveCount plus one indices");

        const std::uint16_t pointIndex = 0;
        const auto indexedPointCall = drawIndexed16(
            PrimitiveType::PointListEXT, &point, 1, &pointIndex, 1, 1);
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(255, 0, 255, 255)) &&
                  indexedPointCall.indexed && indexedPointCall.primitiveMode == 0x0000 &&
                  indexedPointCall.elementCount == 1,
              "indexed point-list submission programs and shades one exact point");

        std::array<PositionColorStream, 4> offsetVertices{};
        offsetVertices[1] = Triangle(0, 255, 255)[0];
        offsetVertices[2] = Triangle(0, 255, 255)[1];
        offsetVertices[3] = Triangle(0, 255, 255)[2];
        auto offsetVertex = renderer.CreateVertexBuffer(4);
        offsetVertex->SetVertexDeclaration(VertexPositionColor::getVertexDeclarationStatic());
        offsetVertex->SetData(offsetVertices.data(), 4, sizeof(PositionColorStream));
        auto index32 = renderer.CreateIndexBuffer32(6);
        const std::array<std::uint32_t, 6> indices32{3, 3, 3, 0, 1, 2};
        index32->SetData32(indices32.data(), 6);
        GpuDrawParams indexedParams;
        indexedParams.vertexColorEnabled = true;
        indexedParams.startIndex = 3;
        indexedParams.baseVertex = 1;
        clear();
        renderer.DrawIndexedPrimitivesEx(
            *offsetVertex, *index32, identity, identity, identity,
            PrimitiveType::TriangleList, 1, indexedParams);
        const auto indexed32Call = Bridge::GetPrimitiveDrawSnapshotForTesting();
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(0, 255, 255, 255)) &&
                  indexed32Call.indexed && indexed32Call.indexType == 0x1405 &&
                  indexed32Call.startIndex == 3 && indexed32Call.baseVertex == 1 &&
                  !indexed32Call.usedRlglDrawWrapper,
              "32-bit indexed draws honor startIndex and baseVertex through the GL 3.3 bridge");

        GpuDrawParams vertexStartParams;
        vertexStartParams.vertexColorEnabled = true;
        vertexStartParams.vertexStart = 1;
        clear();
        renderer.DrawPrimitivesEx(
            *offsetVertex, identity, identity, identity,
            PrimitiveType::TriangleList, 1, vertexStartParams);
        const auto vertexStartCall = Bridge::GetPrimitiveDrawSnapshotForTesting();
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(0, 255, 255, 255)) &&
                  vertexStartCall.firstVertex == 1,
              "non-indexed DrawPrimitivesEx applies vertexStart exactly once");

        struct ColorPosition
        {
            std::uint8_t r, g, b, a;
            float x, y, z;
        };
        static_assert(sizeof(ColorPosition) == 16);
        const VertexDeclaration reorderedDeclaration(
            16,
            {{0, VertexElementFormat::Color, VertexElementUsage::Color, 0},
             {4, VertexElementFormat::Vector3, VertexElementUsage::Position, 0}});
        const auto greenTriangle = Triangle(0, 255, 0);
        std::array<ColorPosition, 3> reordered{};
        for (std::size_t i = 0; i < reordered.size(); ++i)
        {
            reordered[i] = {
                greenTriangle[i].r, greenTriangle[i].g,
                greenTriangle[i].b, greenTriangle[i].a,
                greenTriangle[i].x, greenTriangle[i].y, greenTriangle[i].z};
        }
        auto reorderedVertex = renderer.CreateVertexBuffer(3);
        reorderedVertex->SetVertexDeclaration(reorderedDeclaration);
        reorderedVertex->SetData(reordered.data(), 3, sizeof(ColorPosition));
        clear();
        renderer.DrawColoredPrimitives(
            *reorderedVertex, identity, identity, identity,
            PrimitiveType::TriangleList, 1);
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(0, 255, 0, 255)),
              "stock attributes bind by Position0/Color0 semantic rather than declaration order");

        struct PositionOnly
        {
            float x, y, z;
        };
        const VertexDeclaration positionDeclaration(
            12, {{0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0}});
        const std::array<PositionOnly, 3> positionTriangle{{
            {-0.75f, -0.75f, 0.0f}, {0.75f, -0.75f, 0.0f}, {0.0f, 0.75f, 0.0f}}};
        auto positionVertex = renderer.CreateVertexBuffer(3);
        positionVertex->SetVertexDeclaration(positionDeclaration);
        positionVertex->SetData(positionTriangle.data(), 3, sizeof(PositionOnly));
        GpuDrawParams diffuseParams;
        diffuseParams.vertexColorEnabled = false;
        diffuseParams.diffuseColor[0] = 0.25f;
        diffuseParams.diffuseColor[1] = 0.5f;
        diffuseParams.diffuseColor[2] = 1.0f;
        diffuseParams.diffuseColor[3] = 1.0f;
        clear();
        renderer.DrawPrimitivesEx(
            *positionVertex, identity, identity, identity,
            PrimitiveType::TriangleList, 1, diffuseParams);
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(64, 128, 255, 255)),
              "uncolored stock draws use BasicEffect diffuse color without a fake color stream");

        const Matrix shifted = Matrix::CreateTranslation(0.5f, 0.0f, 0.0f);
        clear();
        renderer.DrawPrimitivesEx(
            *positionVertex, shifted, identity, identity,
            PrimitiveType::TriangleList, 1, diffuseParams);
        Check(IsColor(read(44, kHeight / 2), Color(64, 128, 255, 255)) &&
                  IsColor(read(12, kHeight / 2), Color(0, 0, 0, 255)),
              "world/view/projection multiplication moves primitive pixels exactly once");

        const std::array<VertexElementFormat, 12> formats{
            VertexElementFormat::Single, VertexElementFormat::Vector2,
            VertexElementFormat::Vector3, VertexElementFormat::Vector4,
            VertexElementFormat::Color, VertexElementFormat::Byte4,
            VertexElementFormat::Short2, VertexElementFormat::Short4,
            VertexElementFormat::NormalizedShort2,
            VertexElementFormat::NormalizedShort4,
            VertexElementFormat::HalfVector2, VertexElementFormat::HalfVector4};
        const std::array<int, 12> components{1, 2, 3, 4, 4, 4, 2, 4, 2, 4, 2, 4};
        const std::array<int, 12> scalarTypes{
            0x1406, 0x1406, 0x1406, 0x1406, 0x1401, 0x1401,
            0x1402, 0x1402, 0x1402, 0x1402, 0x140B, 0x140B};
        bool formatMappingExact = true;
        for (std::size_t i = 0; i < formats.size(); ++i)
        {
            const VertexElement element(
                4, formats[i], VertexElementUsage::TextureCoordinate, 0);
            const Rlgl::VertexAttributeBinding binding =
                Rlgl::DescribeVertexAttribute(element, 7, 32, 8);
            const Bridge::VertexAttributeSnapshot native =
                Bridge::GetVertexAttributeSnapshotForTesting(
                    Rlgl::GetNativeBufferId(*positionVertex), binding);
            formatMappingExact = formatMappingExact && binding.location == 7 &&
                binding.componentCount == components[i] &&
                binding.scalarType == scalarTypes[i] && binding.stride == 32 &&
                binding.offset == 12 &&
                binding.normalized == (formats[i] == VertexElementFormat::Color ||
                    formats[i] == VertexElementFormat::NormalizedShort2 ||
                    formats[i] == VertexElementFormat::NormalizedShort4) &&
                native.enabled && native.componentCount == binding.componentCount &&
                native.scalarType == binding.scalarType &&
                native.normalized == binding.normalized && native.stride == binding.stride &&
                native.offset == binding.offset &&
                native.buffer == Rlgl::GetNativeBufferId(*positionVertex);
        }
        Check(formatMappingExact,
              "all twelve classic VertexElementFormat values reach exact native VAO shapes");

        bool malformedAttributeRejected = false;
        try
        {
            const VertexElement outsideStride(
                28, VertexElementFormat::Vector2,
                VertexElementUsage::TextureCoordinate, 0);
            (void)Rlgl::DescribeVertexAttribute(outsideStride, 2, 32);
        }
        catch (const std::invalid_argument&)
        {
            malformedAttributeRejected = true;
        }
        Check(malformedAttributeRejected,
              "vertex attribute format extents cannot escape their declared stride");

        BasicEffect effect(device);
        effect.setVertexColorEnabledProperty(true);
        effect.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();
        const std::array<VertexPositionColor, 3> publicTriangle{
            VertexPositionColor(Vector3(-0.75f, -0.75f, 0.0f), Color(255, 128, 0, 255)),
            VertexPositionColor(Vector3(0.75f, -0.75f, 0.0f), Color(255, 128, 0, 255)),
            VertexPositionColor(Vector3(0.0f, 0.75f, 0.0f), Color(255, 128, 0, 255))};
        clear();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, publicTriangle.data(), 0, 1);
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(255, 128, 0, 255)),
              "public DrawUserPrimitives packs and submits VertexPositionColor through RLGL");

        const std::array<std::uint16_t, 3> public16{0, 1, 2};
        clear();
        device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleList, publicTriangle.data(), 0, 3,
            public16.data(), 0, 1);
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(255, 128, 0, 255)) &&
                  Bridge::GetPrimitiveDrawSnapshotForTesting().indexType == 0x1403,
              "public 16-bit DrawUserIndexedPrimitives reaches indexed RLGL submission");

        const std::array<std::uint32_t, 3> public32{0, 1, 2};
        clear();
        device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleList, publicTriangle.data(), 0, 3,
            public32.data(), 0, 1);
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(255, 128, 0, 255)) &&
                  Bridge::GetPrimitiveDrawSnapshotForTesting().indexType == 0x1405,
              "public 32-bit DrawUserIndexedPrimitives reaches exact index-width submission");

        VertexBuffer boundVertex(
            device, VertexPositionColor::getVertexDeclarationStatic(), 4, BufferUsage::None);
        std::array<VertexPositionColor, 4> boundVertices{};
        boundVertices[1] = publicTriangle[0];
        boundVertices[2] = publicTriangle[1];
        boundVertices[3] = publicTriangle[2];
        boundVertex.SetData(boundVertices.data(), 4);
        device.SetVertexBuffer(&boundVertex);
        effect.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();
        clear();
        device.DrawPrimitives(PrimitiveType::TriangleList, 1, 1);
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(255, 128, 0, 255)) &&
                  Bridge::GetPrimitiveDrawSnapshotForTesting().firstVertex == 1,
              "public bound-buffer DrawPrimitives forwards its vertexStart");

        IndexBuffer boundIndex(
            device, IndexElementSize::ThirtyTwoBits, 6, BufferUsage::None);
        const std::array<std::uint32_t, 6> boundIndices{3, 3, 3, 0, 1, 2};
        boundIndex.SetData(boundIndices.data(), 6);
        device.SetIndexBuffer(&boundIndex);
        clear();
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 1, 0, 3, 3, 1);
        const auto publicIndexedCall = Bridge::GetPrimitiveDrawSnapshotForTesting();
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(255, 128, 0, 255)) &&
                  publicIndexedCall.startIndex == 3 && publicIndexedCall.baseVertex == 1 &&
                  publicIndexedCall.indexType == 0x1405,
              "public bound indexed drawing forwards 32-bit start/base offsets");

        Texture2D whiteTexture(device, 1, 1);
        const Color whitePixel(255, 255, 255, 255);
        whiteTexture.SetData(&whitePixel, 1);
        SpriteBatch spriteBatch(device);
        clear();
        spriteBatch.Begin();
        spriteBatch.Draw(
            whiteTexture, Rectangle(0, 0, 8, 8), Color(255, 255, 255, 255));
        spriteBatch.End();
        renderer.SetBlendEnabled(false);
        renderer.ApplyRasterizerState(0, 0, false);
        renderer.DrawColoredPrimitives(
            *indexedVertex, identity, identity, identity,
            PrimitiveType::TriangleList, 1);
        Check(IsColor(read(kWidth / 2, kHeight / 2), Color(255, 0, 0, 255)) &&
                  IsColor(read(2, 2), Color(255, 255, 255, 255)),
              "SpriteBatch-to-primitive transition rebinds shader, VAO, buffers, and attributes");

        effect.setTextureEnabledProperty(true);
        effect.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();
        bool rejectedUnsupportedEffect = false;
        try
        {
            device.DrawUserPrimitives(
                PrimitiveType::TriangleList, publicTriangle.data(), 0, 1);
        }
        catch (const System::NotSupportedException&)
        {
            rejectedUnsupportedEffect = true;
        }
        Check(rejectedUnsupportedEffect,
              "textured stock effects are rejected until RLGL-012 instead of drawn incorrectly");
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    RlglPrimitiveTest game;
    game.Run();
    return game.getResultProperty();
}
