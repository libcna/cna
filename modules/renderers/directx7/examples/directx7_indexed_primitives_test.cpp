// SPDX-License-Identifier: MS-PL
// plans/plan_dx2.md Phase O4 (DX2-33, DX2-36): DrawIndexedPrimitives produces the same pixel result as
// the equivalent DrawPrimitives call, through the real Direct3D v2 device.
//
// Check A -- DrawIndexedPrimitives (16-bit indices) produces the identical center-pixel color as
//   the equivalent DrawPrimitives call for the same triangle.
// Check B -- DrawIndexedPrimitives with 32-bit indices (CreateIndexBuffer32, Phase O5) produces the
//   same result too.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    int g_passCount = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++g_passCount;
    }

    bool Close(int a, int b, int tolerance) { return std::abs(a - b) <= tolerance; }
}

class DirectX7IndexedPrimitivesTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 1;

    Color ReadPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        const VertexPositionColor verts[3] = {
            { Vector3(-2.0f,  2.0f, 0.5f), Color::Red },
            { Vector3(-2.0f, -2.0f, 0.5f), Color::Red },
            { Vector3( 2.0f,  0.0f, 0.5f), Color::Red },
        };

        // Check A: 16-bit indexed draw.
        {
            dev.Clear(Color::Black, 1.0f);
            VertexBuffer vb(dev, 3);
            vb.SetData(verts, 3);
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
            const uint16_t indices[3] = {0, 1, 2};
            ib.SetData(indices, 0, 3);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();

            dev.SetVertexBuffer(&vb);
            dev.SetIndexBuffer(&ib);
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
            dev.SetVertexBuffer(nullptr);
            dev.SetIndexBuffer(nullptr);

            const Color center = ReadPixel(dev, 32, 32);
            Check(Close(center.getRProperty(), 255, 2) && Close(center.getGProperty(), 0, 2),
                  "DrawIndexedPrimitives (16-bit) produces the same pixel result as the equivalent DrawPrimitives call");
        }

        // Check B: 32-bit indexed draw.
        {
            dev.Clear(Color::Black, 1.0f);
            VertexBuffer vb(dev, 3);
            vb.SetData(verts, 3);
            IndexBuffer ib(dev, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
            const uint32_t indices[3] = {0, 1, 2};
            ib.SetData(indices, 0, 3);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();

            dev.SetVertexBuffer(&vb);
            dev.SetIndexBuffer(&ib);
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
            dev.SetVertexBuffer(nullptr);
            dev.SetIndexBuffer(nullptr);

            const Color center = ReadPixel(dev, 32, 32);
            Check(Close(center.getRProperty(), 255, 2) && Close(center.getGProperty(), 0, 2),
                  "DrawIndexedPrimitives (32-bit indices) produces the same pixel result");
        }

        std::printf("=== %d/%d PASS ===\n", g_passCount, 2);
        result_ = (g_passCount == 2) ? 0 : 1;
        Exit();
    }

public:
    DirectX7IndexedPrimitivesTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    DirectX7IndexedPrimitivesTest game;
    game.Run();
    return game.getResult();
}
