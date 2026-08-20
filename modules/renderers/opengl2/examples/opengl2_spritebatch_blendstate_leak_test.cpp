// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: adapts examples/easygl_spritebatch_blendstate_leak_test.cpp's own scene
// verbatim -- verifies SpriteBatch::Begin() does not clobber the real GL blend state with a
// hardcoded SrcAlpha/OneMinusSrcAlpha, and that a 3D draw issued after a SpriteBatch Begin()/End()
// pair -- without the game explicitly reassigning BlendState -- correctly inherits whatever
// GraphicsDevice.BlendState currently is (matching real FNA: SpriteBatch.Begin() genuinely
// changes GraphicsDevice.BlendState as a lasting side effect, it does not restore the prior state
// on End()).
//
// Sequence: Clear to mid-gray (100,100,100), SpriteBatch.Begin(Deferred, BlendState::Additive) +
// draw a small corner sprite + End() (sets GraphicsDevice.BlendState=Additive as a lasting side
// effect), then -- without touching BlendState again -- draw a full-viewport opaque red 3D quad.
// Expected at the readback point, with Additive genuinely still active: red(200,0,0) +
// gray(100,100,100) clamped = (255,100,100). G is the clean discriminator: a leftover hardcoded
// SrcAlpha/OneMinusSrcAlpha blend would drop the gray background entirely (G~0).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
}

class OpenGL2SpriteBatchBlendStateLeakTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> cornerTex_;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        const Color white(255, 255, 255, 255);
        cornerTex_ = std::make_unique<Texture2D>(dev, 1, 1);
        cornerTex_->SetData(&white, 1);
    }

    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        SpriteBatch sb(dev);

        dev.Clear(Color(100, 100, 100, 255));
        dev.SetDepthTestEnabled(false);

        sb.Begin(SpriteSortMode::Deferred, BlendState::Additive);
        sb.Draw(*cornerTex_, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
        sb.End();

        const Color kRed(200, 0, 0, 255);
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), kRed },
            { Vector3(-1.0f, -1.0f, 0.0f), kRed },
            { Vector3( 1.0f, -1.0f, 0.0f), kRed },
            { Vector3(-1.0f,  1.0f, 0.0f), kRed },
            { Vector3( 1.0f, -1.0f, 0.0f), kRed },
            { Vector3( 1.0f,  1.0f, 0.0f), kRed },
        };

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();

        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        const auto& vp = dev.getViewportProperty();
        ExpectPixel("blendstate-persists-from-spritebatch-to-3d-draw",
                    Rectangle(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1),
                    Color(255, 100, 100, 255), /*tolerance=*/20);
    }

public:
    OpenGL2SpriteBatchBlendStateLeakTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2SpriteBatchBlendStateLeakTest>();
}
