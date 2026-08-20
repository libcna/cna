// SPDX-License-Identifier: MS-PL
// plans/plan_opengles1.md OPENGLES1-1..OPENGLES1-20-ish baseline smoke test: Clear()/Present() and
// SpriteBatch through the real OpenGL ES 1.1 fixed-function renderer.
//
// Check A -- Clear(Red) is visible via GetBackBufferData().
// Check B -- a second Clear(Blue) right after replaces it.
// Check C -- a SpriteBatch-drawn solid green quad is visible inside its destination rectangle...
// Check D -- ...and the still-black background outside that rectangle is unaffected.
// Check E -- a colored (non-textured) triangle drawn via DrawColoredPrimitives (VertexPositionColor,
//   GraphicsDevice::DrawUserPrimitives) is visible at its center.
//
// Exit code 0 = all checks PASS, 1 = any FAILs. Requires a genuine OpenGL ES 1.1 driver -- see
// docs/opengles1-renderer.md for this project's own finding that not every EGL/Mesa host actually
// implements ES1 context creation.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    bool colorNear(Color a, Color b, int tol = 8)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tol;
    }

    Color readPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }
}

class OpenGLES1ClearReadbackTest : public Game
{
    static constexpr int kChecks = 5;

    std::unique_ptr<GraphicsDeviceManager> gdm_;
    SpriteBatch* spriteBatch_ = nullptr;
    Texture2D greenTex_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void LoadContent() override
    {
        spriteBatch_ = new SpriteBatch(getGraphicsDeviceProperty());
        // Color::Lime, not Color::Green (XNA's Color::Green is the darker (0,128,0) "web green").
        greenTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                 std::vector<std::uint8_t>{0, 255, 0, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // Check A: Clear(Red).
        dev.Clear(Color::Red);
        check(colorNear(readPixel(dev, kSize / 2, kSize / 2), Color::Red),
              "Clear(Red) visible via GetBackBufferData()");

        // Check B: a second Clear() right after replaces it.
        dev.Clear(Color::Blue);
        check(colorNear(readPixel(dev, kSize / 2, kSize / 2), Color::Blue),
              "Clear(Blue) right after Clear(Red) is observed, not the stale Red frame");

        // Check C/D: Clear(Black), draw a green quad over the left half only.
        dev.Clear(Color::Black);
        const Rectangle destination(0, 0, kSize / 2, kSize);
        spriteBatch_->Begin();
        spriteBatch_->Draw(greenTex_, destination, Color::White);
        spriteBatch_->End();

        check(colorNear(readPixel(dev, kSize / 4, kSize / 2), Color::Lime),
              "SpriteBatch-drawn green (Lime) quad is visible inside its destination rectangle");
        check(colorNear(readPixel(dev, kSize - kSize / 4, kSize / 2), Color::Black),
              "background outside the quad's destination rectangle stays black");

        // Check E: a colored (fixed-function, no texture) triangle covering the full viewport.
        dev.Clear(Color::Black);
        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();
        std::vector<VertexPositionColor> verts = {
            VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), Color::Yellow),
            VertexPositionColor(Vector3(0.0f, 1.0f, 0.0f), Color::Yellow),
            VertexPositionColor(Vector3(1.0f, -1.0f, 0.0f), Color::Yellow),
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts.data(), 0, 1);
        check(colorNear(readPixel(dev, kSize / 2, kSize / 2), Color::Yellow),
              "DrawUserPrimitives(VertexPositionColor) triangle visible at viewport center");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = (passCount_ == kChecks) ? 0 : 1;
        Exit();
    }

public:
    OpenGLES1ClearReadbackTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    OpenGLES1ClearReadbackTest game;
    game.Run();
    return game.getResult();
}
