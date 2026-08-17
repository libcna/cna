// SPDX-License-Identifier: MS-PL
// Coverage for every primitive/index route advertised by the TinyGL fixed-function renderer.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kChecks = 6;

    VertexDeclaration PosColorDecl()
    {
        return VertexDeclaration(16, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
    }

    VertexDeclaration PosColorTexDecl()
    {
        return VertexDeclaration(24, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            VertexElement(16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
    }

    Color ReadPixel(GraphicsDevice& device, int x, int y)
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    int CountDominantRed(GraphicsDevice& device)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kSize * kSize), Color::Black);
        device.GetBackBufferData(nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
        int count = 0;
        for (const Color& pixel : pixels)
        {
            if (pixel.getRProperty() > 160 && pixel.getGProperty() < 80 &&
                pixel.getBProperty() < 80)
                ++count;
        }
        return count;
    }
}

class TinyGLDrawRoutesTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    void DrawColored(GraphicsDevice& device, PrimitiveType primitive, int primitiveCount,
                     const VertexPositionColor* vertices, int vertexCount)
    {
        VertexBuffer buffer(device, PosColorDecl(), vertexCount, BufferUsage::None);
        buffer.SetData(vertices, 0, vertexCount);
        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.Apply();
        device.SetVertexBuffer(&buffer);
        device.DrawPrimitives(primitive, 0, primitiveCount);
        device.SetVertexBuffer(nullptr);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        const Color red(255, 0, 0, 255);

        device.Clear(Color::Black);
        const VertexPositionColor point[] = {{Vector3(0, 0, 0), red}};
        DrawColored(device, PrimitiveType::PointListEXT, 1, point, 1);
        const int pointPixels = CountDominantRed(device);
        Check(pointPixels >= 1 && pointPixels <= 4,
              "PointListEXT submits exactly one small rasterized point");

        device.Clear(Color::Black);
        const VertexPositionColor line[] = {
            {Vector3(-0.75f, 0, 0), red}, {Vector3(0.75f, 0, 0), red},
        };
        DrawColored(device, PrimitiveType::LineList, 1, line, 2);
        const int linePixels = CountDominantRed(device);
        Check(linePixels >= 40 && linePixels <= 60,
              "LineList rasterizes the requested independent segment");

        device.Clear(Color::Black);
        const VertexPositionColor stripLine[] = {
            {Vector3(-0.75f, 0.5f, 0), red},
            {Vector3(0, -0.5f, 0), red},
            {Vector3(0.75f, 0.5f, 0), red},
        };
        DrawColored(device, PrimitiveType::LineStrip, 2, stripLine, 3);
        Check(CountDominantRed(device) >= 60,
              "LineStrip consumes primitiveCount+1 vertices and joins both segments");

        device.Clear(Color::Black);
        const VertexPositionColor triangleStrip[] = {
            {Vector3(-0.75f, 0.75f, 0), red},
            {Vector3(-0.75f, -0.75f, 0), red},
            {Vector3(0.75f, 0.75f, 0), red},
            {Vector3(0.75f, -0.75f, 0), red},
        };
        DrawColored(device, PrimitiveType::TriangleStrip, 2, triangleStrip, 4);
        Check(CountDominantRed(device) >= 1800,
              "TriangleStrip consumes primitiveCount+2 vertices and fills both triangles");

        device.Clear(Color::Black);
        VertexBuffer indexedBuffer(device, PosColorDecl(), 4, BufferUsage::None);
        indexedBuffer.SetData(triangleStrip, 0, 4);
        IndexBuffer indices(device, IndexElementSize::ThirtyTwoBits, 6, BufferUsage::None);
        const std::uint32_t indexData[6] = {0, 1, 2, 2, 1, 3};
        indices.SetData(indexData, 0, 6);
        BasicEffect indexedEffect(device);
        indexedEffect.VertexColorEnabled = true;
        indexedEffect.Apply();
        device.SetVertexBuffer(&indexedBuffer);
        device.setIndicesProperty(&indices);
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        Check(CountDominantRed(device) >= 1800,
              "the 32-bit IndexBuffer route decodes and replays every index");
        device.setIndicesProperty(nullptr);
        device.SetVertexBuffer(nullptr);

        Texture2D perspectiveTexture(device, 4, 1, false, SurfaceFormat::Color);
        const Color texels[4] = {red, Color::Green, Color::Green, Color::Green};
        perspectiveTexture.SetData(texels, 4);
        const Color white = Color::White;
        const VertexPositionColorTexture perspectiveTriangle[3] = {
            {Vector3(-1, -1, -2), white, Vector2(0, 0)},
            {Vector3(1, -1, -8), white, Vector2(1, 0)},
            {Vector3(-1, 1, -2), white, Vector2(0, 1)},
        };
        VertexBuffer perspectiveBuffer(device, PosColorTexDecl(), 3, BufferUsage::None);
        perspectiveBuffer.SetData(perspectiveTriangle, 0, 3);
        BasicEffect perspectiveEffect(device);
        perspectiveEffect.VertexColorEnabled = true;
        perspectiveEffect.setTextureEnabledProperty(true);
        perspectiveEffect.setTextureProperty(&perspectiveTexture);
        perspectiveEffect.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            3.14159265f / 2.0f, 1.0f, 0.1f, 100.0f));
        perspectiveEffect.Apply();
        device.Clear(Color::Black);
        device.SetVertexBuffer(&perspectiveBuffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        device.SetVertexBuffer(nullptr);

        // At pixel (25,32), screen-space affine interpolation gives u≈0.45 (green), while
        // perspective correction by reciprocal clip W gives u≈0.17 (the red first quarter).
        const Color perspectiveSample = ReadPixel(device, 25, 32);
        Check(perspectiveSample.getRProperty() > 160 && perspectiveSample.getGProperty() < 80,
              "texture coordinates are perspective-correct rather than affine");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    TinyGLDrawRoutesTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(kSize);
        manager_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    TinyGLDrawRoutesTest test;
    test.Run();
    return test.Result();
}
