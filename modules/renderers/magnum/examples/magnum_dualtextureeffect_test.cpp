// SPDX-License-Identifier: MS-PL
// plans/plan_magnum.md MAGNUM-50: DualTextureEffect pixel test for the MAGNUM renderer.
//
// Checks the two things a two-layer shader can get wrong without any other test noticing: FNA's
// `color.rgb *= 2` overbright factor on the base layer, and whether the second layer participates
// at all. Both are invisible to a scene built from pure 0/1-saturated texels -- a missing doubling
// clamps straight back to the same result there -- so the base layer here is a mid-tone.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
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
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    bool ChannelsClose(Color got, Color want, int tolerance = 20)
    {
        const auto close = [tolerance](int a, int b) { return std::abs(a - b) <= tolerance; };
        return close(got.getRProperty(), want.getRProperty())
            && close(got.getGProperty(), want.getGProperty())
            && close(got.getBProperty(), want.getBProperty());
    }
}

class MagnumDualTextureEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> deviceManager_;
    bool done_ = false;
    int result_ = 0;

    void Check(const char* label, Color got, Color want)
    {
        if (ChannelsClose(got, want))
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
            return;
        }
        std::printf("[FAIL] %s: got=(%d,%d,%d), expected~(%d,%d,%d)\n", label,
                    got.getRProperty(), got.getGProperty(), got.getBProperty(),
                    want.getRProperty(), want.getGProperty(), want.getBProperty());
        result_ = 1;
    }

    Color DrawAndRead(GraphicsDevice& device, Texture2D& layer0, Texture2D& layer1,
                      const Vector3& diffuse, const VertexPositionTexture (&quad)[6])
    {
        DualTextureEffect effect(device);
        effect.setTextureProperty(&layer0);
        effect.setTexture2Property(&layer1);
        effect.setDiffuseColorProperty(diffuse);
        effect.Apply();

        device.Clear(Color(0, 0, 0, 255));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&centre, &pixel, 0, 1);
        return pixel;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();

        const Color white(255, 255, 255, 255);
        const Color midTone(100, 100, 100, 255);
        Texture2D whiteTexture(device, 1, 1);
        whiteTexture.SetData(&white, 1);
        Texture2D midToneTexture(device, 1, 1);
        midToneTexture.SetData(&midTone, 1);

        const VertexPositionTexture quad[6] = {
            {Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f)},
            {Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f)},
            {Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f)},
            {Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f)},
            {Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f)},
            {Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f)},
        };

        // The doubling isolated: a 100 base against a white overlay and a white diffuse must
        // arrive at 200, not 100.
        Check("base(100) x2 * white overlay -> 200",
              DrawAndRead(device, midToneTexture, whiteTexture, Vector3(1.0f, 1.0f, 1.0f), quad),
              Color(200, 200, 200, 255));

        // The overlay genuinely participating: a 100 overlay halves a doubled white base back to
        // ~200, which a shader ignoring the second layer would render as a saturated 255.
        Check("white base x2 * overlay(100) -> 200",
              DrawAndRead(device, whiteTexture, midToneTexture, Vector3(1.0f, 1.0f, 1.0f), quad),
              Color(200, 200, 200, 255));

        // DiffuseColor still multiplies the two-layer result.
        Check("two white layers * diffuse red -> red",
              DrawAndRead(device, whiteTexture, whiteTexture, Vector3(1.0f, 0.0f, 0.0f), quad),
              Color(255, 0, 0, 255));

        Exit();
    }

public:
    MagnumDualTextureEffectTest()
    {
        deviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        deviceManager_->setPreferredBackBufferWidthProperty(kSize);
        deviceManager_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    MagnumDualTextureEffectTest game;
    game.Run();
    return game.GetResult();
}
