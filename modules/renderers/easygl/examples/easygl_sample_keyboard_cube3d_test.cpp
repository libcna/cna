// SPDX-License-Identifier: MS-PL
// Task 498 (sample 3 of 4 new samples; EasyGL renderer): a "keyboard-driven 3D object" demo --
// the 3D counterpart to Task 730's SDL_Renderer keyboard-sprite sample. A small unlit,
// vertex-colored BasicEffect quad moves along the World-space X axis only while
// Keyboard::GetState().IsKeyDown(Keys::Right) is true, proving the full
// input-state -> Update() -> World matrix -> BasicEffect -> EasyGL draw pipeline works end to
// end for genuine 3D rendering driven by real (here, simulated) input, not just a scripted
// animation.
//
// Since this runs headlessly under Xvfb via ctest (no real keyboard), the real platform is wrapped
// by the shared canned-keyboard test decorator. The platform remains real for windowing, timing,
// and graphics while its once-per-frame keyboard snapshot holds the Right arrow down.
//
// View/Projection stay at BasicEffect's own real default (Identity), so World-space X coincides
// with NDC X and the screen-space mapping is the same simple (x+1)/2*width formula used by
// this task's own sample 1 (the plain moving-quad sample).
//
// Motion: starts at World-space X=-0.6, +0.3/Update() while Right is held, 4 Update() calls ->
// final X=0.6. Expected screen X: start ~13, end ~51 (64x64 backbuffer).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "CNA/Platform/CannedKeyboard.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Input;

namespace
{
    constexpr int kSize = 64;
    constexpr int kFrameCount = 4;
    constexpr float kStartX = -0.6f;
    constexpr float kStepX = 0.3f;
    constexpr float kHalf = 0.15f;

    int ndcToScreenX(float ndcX) { return static_cast<int>((ndcX + 1.0f) * 0.5f * kSize); }
}

class KeyboardCube3DSample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<BasicEffect> effect_;

    float x_ = kStartX;
    int frame_ = 0;
    bool done_ = false;
    int pass_ = 0, fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    Color readAt(GraphicsDevice& dev, int px, int py)
    {
        Rectangle r(px, py, 1, 1);
        Color c(0, 0, 0, 0);
        dev.GetBackBufferData(&r, &c, 0, 1);
        return c;
    }

    void drawQuad(GraphicsDevice& dev)
    {
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        effect_->setWorldProperty(Matrix::CreateTranslation(x_, 0.0f, 0.0f));
        effect_->Apply();

        const Color blue(0, 128, 255, 255);
        const VertexPositionColor q[6] = {
            { Vector3(-kHalf,  kHalf, 0.0f), blue }, { Vector3(-kHalf, -kHalf, 0.0f), blue },
            { Vector3( kHalf, -kHalf, 0.0f), blue },
            { Vector3(-kHalf,  kHalf, 0.0f), blue }, { Vector3( kHalf, -kHalf, 0.0f), blue },
            { Vector3( kHalf,  kHalf, 0.0f), blue },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        effect_ = std::make_unique<BasicEffect>(getGraphicsDeviceProperty());
        effect_->VertexColorEnabled = true;

    }

    void Update(GameTime&) override
    {
        if (done_ || frame_ >= kFrameCount) return;
        if (Keyboard::GetState().IsKeyDown(Keys::Right))
            x_ += kStepX;
        ++frame_;
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        drawQuad(dev);

        if (frame_ < kFrameCount || done_) return;
        done_ = true;

        const int screenY = kSize / 2;
        const int endScreenX = ndcToScreenX(x_);
        const float expectedX = kStartX + kStepX * kFrameCount;
        std::printf("[INFO] keyboard-driven position: actual %.9f, expected %.9f after %d updates\n",
                    static_cast<double>(x_), static_cast<double>(expectedX), frame_);
        check(std::abs(x_ - expectedX) <= 1.0e-6f,
              "Object moved the exact distance driven by simulated Keyboard::GetState() over 4 Update() cycles");

        Color endPx(0, 0, 0, 0);
        for (int i = 0; i < 10; ++i)
        {
            endPx = readAt(dev, endScreenX, screenY);
            if (endPx.getBProperty() > 0) break;
            drawQuad(dev);
        }
        check(endPx.getRProperty() < 16 && endPx.getGProperty() >= 100 && endPx.getBProperty() >= 240,
              "3D object is genuinely rendered at the keyboard-driven World-space position, not just tracked internally");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    explicit KeyboardCube3DSample(std::unique_ptr<CNA::Platform::IPlatform> platform)
        : Game(std::move(platform))
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    auto platform = std::make_unique<CNA::Platform::Testing::CannedKeyboardPlatform>();
    platform->Canned().SetPending({CNA::Platform::KeyCode::Right});
    KeyboardCube3DSample game(std::move(platform));
    game.Run();
    return game.getResult();
}
