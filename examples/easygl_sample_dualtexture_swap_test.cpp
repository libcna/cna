// SPDX-License-Identifier: MS-PL
// Task 498 (sample 2 of 4 new samples; EasyGL backend): a "reactive material" demo -- a static
// full-screen DualTextureEffect quad whose Texture2 reference is swapped between two different
// Texture2D objects based on Update()-driven game state (an early-game vs. late-game "palette
// swap", a common real XNA gameplay pattern -- e.g. a damaged/healthy sprite variant). Proves the
// full Update()-state -> effect-property-change -> multi-texture-sample -> EasyGL draw pipeline
// reacts correctly to genuine per-frame game logic, not just a fixed/static material.
//
// FNA reference (Graphics/Effect/StockEffects/DualTextureEffect.cs + HLSL): the real DualTexture
// shader samples Texture and Texture2 at the SAME texture coordinate and multiplies the two
// samples together (used for XNA's classic lightmap technique). Texture is left as a constant
// white 1x1 for this sample, so the rendered color is exactly whichever color Texture2 currently
// holds (white * C = C) -- isolating the "did the swap actually reach the shader" question from
// the multiply-blend math itself (already covered by Task 191/etc.'s own dedicated tests).
//
// Update() logic: frame counter starts at 0; while frame_ < 2, Texture2 = a 1x1 Red texture;
// once frame_ >= 2, Texture2 = a 1x1 Green texture (4 Update() calls total, so the swap happens
// partway through the run, not on the very first or very last frame).
//
// Checks: (1) after Update() 1 (frame_==1, still red phase) the rendered center pixel is Red;
// (2) after Update() 4 (frame_==4, green phase) the rendered center pixel is Green; (3) these two
// captured colors are genuinely different, proving live Update()-driven state actually reached
// the render, not a value baked in once at Initialize().
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 32;
    constexpr int kFrameCount = 4;
    constexpr int kSwapAtFrame = 2;
}

class DualTextureSwapSample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<DualTextureEffect> effect_;
    std::unique_ptr<Texture2D> white_, red_, green_;

    int frame_ = 0;
    bool done_ = false;
    int pass_ = 0, fail_ = 0;
    Color capturedFrame1_{0, 0, 0, 0};
    bool capturedFrame1Set_ = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    static std::unique_ptr<Texture2D> makeSolid(GraphicsDevice& dev, Color c)
    {
        auto tex = std::make_unique<Texture2D>(dev, 1, 1);
        tex->SetData(&c, 1);
        return tex;
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle r(kSize / 2, kSize / 2, 1, 1);
        Color c(0, 0, 0, 0);
        dev.GetBackBufferData(&r, &c, 0, 1);
        return c;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        effect_ = std::make_unique<DualTextureEffect>(dev);
        white_ = makeSolid(dev, Color(255, 255, 255, 255));
        red_   = makeSolid(dev, Color(255, 0, 0, 255));
        green_ = makeSolid(dev, Color(0, 255, 0, 255));
        effect_->setTextureProperty(white_.get());
        effect_->setTexture2Property(red_.get());
    }

    void Update(GameTime&) override
    {
        if (done_) return;
        ++frame_;
        // Reactive game-state swap: switch the "material" once frame_ reaches kSwapAtFrame.
        effect_->setTexture2Property(frame_ < kSwapAtFrame ? red_.get() : green_.get());
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        effect_->Apply();

        const Vector3 tl(-1.0f, 1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br(1.0f, -1.0f, 0.0f), tr(1.0f, 1.0f, 0.0f);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
        const VertexPositionTexture q[6] = {
            { tl, uv0 }, { bl, uv1 }, { br, uv2 },
            { tl, uv0 }, { br, uv2 }, { tr, uv3 },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

        if (frame_ == 1 && !capturedFrame1Set_)
        {
            capturedFrame1_ = readCenter(dev);
            capturedFrame1Set_ = true;
            check(capturedFrame1_.getRProperty() >= 240 && capturedFrame1_.getGProperty() < 16,
                  "Frame 1 (pre-swap): DualTextureEffect samples Texture2=Red as expected");
        }

        if (frame_ < kFrameCount || done_) return;
        done_ = true;

        const Color finalPx = readCenter(dev);
        check(finalPx.getGProperty() >= 240 && finalPx.getRProperty() < 16,
              "Frame 4 (post-swap): DualTextureEffect samples Texture2=Green after Update()-driven swap");
        check(capturedFrame1Set_ &&
              !(finalPx.getRProperty() == capturedFrame1_.getRProperty() &&
                finalPx.getGProperty() == capturedFrame1_.getGProperty()),
              "Rendered color genuinely changed between frame 1 and frame 4 (live state, not baked-in)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    DualTextureSwapSample()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    DualTextureSwapSample game;
    game.Run();
    return game.getResult();
}
