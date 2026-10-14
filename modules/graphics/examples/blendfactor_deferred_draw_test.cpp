// SPDX-License-Identifier: MS-PL
//
// A 3D draw keeps the BlendState.BlendFactor it was made with, even when the state is changed again before the frame is flushed.
//
// A renderer that records draws and replays them later (WebGPU does) has to carry the blend factor with each draw: replayed with
// whatever factor is current at the flush, a draw made with Blend.BlendFactor and followed by `BlendState.Opaque` (which resets the
// factor to white) came out as if the factor were 1. Found by the museum-simulator's "BlendFactor" render-state room, which looked
// right on every renderer except WebGPU.
//
//   A  source (200,100,50) * factor (128,64,255)/255 over black, drawn with DrawUserPrimitives, then BlendState.Opaque is set before
//      anything is read back                                      -> (100,25,50)
//   B  two draws, two factors, one frame: the second factor must not overwrite the first
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {
constexpr int kSize = 64;
}

class BlendFactorDeferredDrawTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ok ? ++pass_ : ++fail_;
    }

    static std::string Text(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) + "," + std::to_string(c.getBProperty()) + ")";
    }

    static bool Near(const Color& c, int r, int g, int b, int tol = 3)
    {
        return std::abs(c.getRProperty() - r) <= tol && std::abs(c.getGProperty() - g) <= tol && std::abs(c.getBProperty() - b) <= tol;
    }

    Color ReadPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle reg(x, y, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    static BlendState FactorState(Color factor)
    {
        BlendState bs;
        bs.setColorSourceBlendProperty(Blend::BlendFactor);
        bs.setColorDestinationBlendProperty(Blend::InverseBlendFactor);
        bs.setAlphaSourceBlendProperty(Blend::One);
        bs.setAlphaDestinationBlendProperty(Blend::Zero);
        bs.setBlendFactorProperty(factor);
        return bs;
    }

    /// A quad covering [x0, x1] x [y0, y1] in clip space.
    static void Quad(VertexPositionColor (&v)[6], float x0, float y0, float x1, float y1, Color c)
    {
        const VertexPositionColor a(Vector3(x0, y1, 0.5f), c), b(Vector3(x1, y1, 0.5f), c), d(Vector3(x1, y0, 0.5f), c), e(Vector3(x0, y0, 0.5f), c);
        const VertexPositionColor q[6] = {a, b, d, a, d, e};
        for (int i = 0; i < 6; ++i) v[i] = q[i];
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        BasicEffect fx(dev);
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.setVertexColorEnabledProperty(true);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        const auto draw = [&](const VertexPositionColor (&v)[6]) {
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, v, 0, 2);
        };
        const Color src(200, 100, 50, 255);

        // A: one factor draw, then back to Opaque before the read-back.
        dev.Clear(Color(0, 0, 0, 255));
        VertexPositionColor q[6];
        dev.setBlendStateProperty(FactorState(Color(128, 64, 255, 255)));
        Quad(q, -0.5f, -0.5f, 0.5f, 0.5f, src);
        draw(q);
        dev.setBlendStateProperty(BlendState::Opaque);
        const Color a = ReadPixel(dev, kSize / 2, kSize / 2);
        check(Near(a, 100, 25, 50), "A a draw made with BlendFactor (128,64,255) keeps it after BlendState.Opaque: got " + Text(a) + ", want (100,25,50)");

        // B: two draws with two factors in one frame.
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(FactorState(Color(255, 255, 255, 255)));
        Quad(q, -0.9f, -0.4f, -0.1f, 0.4f, src);
        draw(q);
        dev.setBlendStateProperty(FactorState(Color(64, 64, 64, 255)));
        Quad(q, 0.1f, -0.4f, 0.9f, 0.4f, src);
        draw(q);
        dev.setBlendStateProperty(BlendState::Opaque);
        const Color left = ReadPixel(dev, kSize / 4, kSize / 2), right = ReadPixel(dev, kSize * 3 / 4, kSize / 2);
        check(Near(left, 200, 100, 50), "B1 the first of two draws keeps factor 1.0: got " + Text(left) + ", want (200,100,50)");
        check(Near(right, 50, 25, 12), "B2 the second keeps factor 0.25: got " + Text(right) + ", want (50,25,12)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    BlendFactorDeferredDrawTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BlendFactorDeferredDrawTest game;
    game.Run();
    return game.getResult();
}
