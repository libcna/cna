// SPDX-License-Identifier: MS-PL
// plans/plan_dx2.md Phase O4 (DX2-34, DX2-38): real Direct3D v2 texture sampling in a 3D draw --
// D3DRENDERSTATE_TEXTUREHANDLE bound via IDirect3DTexture2::GetHandle on a DirectX6TextureRenderer
// surface (mirrors DX2-0's own dx2_spike7_full.cpp test B, now through the full XNA public API).
//
// Check A -- a full-screen quad sampling a 2x2 checker texture (nearest-neighbor at the corners)
//   reads back the correct texel color near each of two opposite corners.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <vector>

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

class DirectX6Texture3DTest : public Game
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
        // Explicit PointClamp: this test verifies "does texture sampling read the right texel",
        // not wrap/bilinear edge behavior -- XNA's real default sampler state is LinearWrap, and
        // with real ApplySamplerState wired (Phase O6), a corner sample at the exact UV edge (0,0)
        // would otherwise genuinely blend with the wrapped-around opposite-edge texel.
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;

        dev.Clear(Color::Black, 1.0f);
        const std::vector<uint8_t> checkerPixels = {
            255, 0, 0, 255,    0, 255, 0, 255,     // row 0: Red, Green
            0, 0, 255, 255,    255, 255, 255, 255, // row 1: Blue, White
        };
        Texture2D checker = Texture2D::CreateFromPixels(dev, 2, 2, checkerPixels);

        const VertexPositionTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.5f), Vector2(0.0f, 0.0f) }, // TL
            { Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.0f, 1.0f) }, // BL
            { Vector3( 1.0f,  1.0f, 0.5f), Vector2(1.0f, 0.0f) }, // TR
            { Vector3( 1.0f,  1.0f, 0.5f), Vector2(1.0f, 0.0f) }, // TR
            { Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.0f, 1.0f) }, // BL
            { Vector3( 1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) }, // BR
        };
        VertexBuffer vb(dev, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(verts, 6);
        BasicEffect fx(dev);
        fx.setTextureProperty(&checker);
        fx.setTextureEnabledProperty(true);
        fx.Apply();
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        const Color nearTopLeft = ReadPixel(dev, 2, 2);
        const Color nearBottomRight = ReadPixel(dev, 61, 61);
        Check(Close(nearTopLeft.getRProperty(), 255, 10) && Close(nearTopLeft.getGProperty(), 0, 10) &&
              Close(nearTopLeft.getBProperty(), 0, 10) &&
              Close(nearBottomRight.getRProperty(), 255, 10) && Close(nearBottomRight.getGProperty(), 255, 10) &&
              Close(nearBottomRight.getBProperty(), 255, 10),
              "textured quad samples the correct texel near opposite corners (top-left=Red, bottom-right=White) "
              "via a real IDirect3DTexture2 bound to D3DRENDERSTATE_TEXTUREHANDLE");

        std::printf("=== %d/%d PASS ===\n", g_passCount, 1);
        result_ = (g_passCount == 1) ? 0 : 1;
        Exit();
    }

public:
    DirectX6Texture3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    DirectX6Texture3DTest game;
    game.Run();
    return game.getResult();
}
