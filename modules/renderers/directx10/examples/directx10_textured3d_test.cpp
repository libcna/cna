// SPDX-License-Identifier: MS-PL
// plans/plan_d3d10.md D3D10-85: permanent pixel oracle for the unlit textured BasicEffect route.

#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/Internal/Renderers/DirectX10/DirectX10Renderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers;
using CNA::Internal::Renderers::DirectX10::DirectX10Renderer;

namespace
{
    struct RawVertexPositionTexture
    {
        float x, y, z, u, v;
    };

    constexpr RawVertexPositionTexture kTriangle[] = {
        {-1.0f, -1.0f, 0.5f, 0.0f, 1.0f},
        { 3.0f, -1.0f, 0.5f, 2.0f, 1.0f},
        {-1.0f,  3.0f, 0.5f, 0.0f, -1.0f},
    };
    constexpr std::uint16_t kIndices[] = {0, 1, 2};
}

class D3D10Textured3DTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    int result_ = 1;
    int frame_ = 0;

    static Color ReadCenter(GraphicsDevice& device)
    {
        const Rectangle region(31, 31, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ == 0) return;
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<DirectX10Renderer&>(device.GetRenderer());
        renderer.ApplyRasterizerState(0, 0, false, 0.0f, 0.0f);
        renderer.ApplyDepthStencilState(false, false, 0, false, 0, 0, 0, 0,
                                        0, 0, 0, false, 0, 0, 0, 0);
        renderer.ApplySamplerState(0, 1, 1, 1, 1);

        ImageData image;
        image.width = 1;
        image.height = 1;
        image.pixels = {200, 120, 40, 255};
        auto texture = renderer.CreateTexture(image);
        auto vertices = renderer.CreateVertexBuffer(3);
        vertices->SetData(kTriangle, 3, sizeof(RawVertexPositionTexture));
        auto indices = renderer.CreateIndexBuffer16(3);
        indices->SetData16(kIndices, 3);

        GpuDrawParams params;
        params.textureEnabled = true;
        params.vertexColorEnabled = false;
        params.texture0 = texture.get();
        params.diffuseColor[0] = 0.5f;
        params.diffuseColor[1] = 1.0f;
        params.diffuseColor[2] = 0.5f;
        params.diffuseColor[3] = 1.0f;

        const Matrix identity = Matrix::getIdentityProperty();
        device.Clear(Color(0, 0, 255, 255));
        renderer.DrawPrimitivesEx(*vertices, identity, identity, identity,
                                  PrimitiveType::TriangleList, 1, params);
        const Color nonIndexedPixel = ReadCenter(device);

        device.Clear(Color(0, 0, 255, 255));
        renderer.DrawIndexedPrimitivesEx(*vertices, *indices, identity, identity, identity,
                                         PrimitiveType::TriangleList, 1, params);
        const Color pixel = ReadCenter(device);
        const bool nonIndexedPassed = nonIndexedPixel.getRProperty() == 100 &&
                                      nonIndexedPixel.getGProperty() == 120 &&
                                      nonIndexedPixel.getBProperty() == 20 &&
                                      nonIndexedPixel.getAProperty() == 255;
        const bool indexedPassed = pixel.getRProperty() == 100 && pixel.getGProperty() == 120 &&
                            pixel.getBProperty() == 20 && pixel.getAProperty() == 255;
        const bool passed = nonIndexedPassed && indexedPassed;
        std::printf("[%s] DirectX10 non-indexed VertexPositionTexture BasicEffect "
                    "texture*diffuse readback = (%u,%u,%u,%u)\n",
                    nonIndexedPassed ? "PASS" : "FAIL", nonIndexedPixel.getRProperty(),
                    nonIndexedPixel.getGProperty(), nonIndexedPixel.getBProperty(),
                    nonIndexedPixel.getAProperty());
        std::printf("[%s] DirectX10 indexed VertexPositionTexture BasicEffect texture*diffuse "
                    "readback = (%u,%u,%u,%u)\n", indexedPassed ? "PASS" : "FAIL",
                    pixel.getRProperty(), pixel.getGProperty(), pixel.getBProperty(),
                    pixel.getAProperty());
        result_ = passed ? 0 : 1;
        Exit();
    }

public:
    D3D10Textured3DTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(64);
        graphics_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    D3D10Textured3DTest game;
    game.Run();
    return game.Result();
}
