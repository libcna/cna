// SPDX-License-Identifier: MS-PL
// plans/plan_magnum.md MAGNUM-51: EnvironmentMapEffect pixel test for the MAGNUM renderer.
//
// The reflection blend is a lerp, not an addition, so the two hypotheses only separate when the
// cube map is a mid-tone and the lit/textured base is genuinely non-zero: a saturated cube map
// clamps both to the same result. The three cases below are therefore Amount=0 (cube ignored),
// Amount=1 (cube fully replaces the base), and EnvironmentMapSpecular adding on top of it.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

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

class MagnumEnvironmentMapEffectTest : public Game
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

    /// Every face carries the same colour, so the result does not depend on which one the
    /// reflection vector happens to land on -- that is not what these cases are measuring.
    std::unique_ptr<TextureCube> MakeSolidCube(GraphicsDevice& device, Color colour)
    {
        auto cube = std::make_unique<TextureCube>(device, 1, false, SurfaceFormat::Color);
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (const CubeMapFace face : faces)
            cube->SetData(face, &colour, 1);
        return cube;
    }

    Color DrawAndRead(GraphicsDevice& device, Texture2D& texture, TextureCube& cube,
                      float amount, const Vector3& specular,
                      const VertexPositionNormalTexture (&quad)[6])
    {
        device.Clear(Color(0, 0, 0, 255));

        EnvironmentMapEffect effect(device);
        effect.setTextureProperty(&texture);
        effect.setEnvironmentMapProperty(&cube);
        effect.setEmissiveColorProperty(Vector3(0.5f, 0.5f, 0.5f));
        effect.setEnvironmentMapAmountProperty(amount);
        effect.setEnvironmentMapSpecularProperty(specular);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();
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
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        // GraphicsDevice's real default RasterizerState culls this quad's winding.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        // Emissive 0.5 against a (200,100,50) texture gives a lit base of (100,50,25) -- non-zero
        // in every channel and nowhere near saturation, so a wrong blend cannot hide in a clamp.
        const Color textureColour(200, 100, 50, 255);
        Texture2D texture(device, 1, 1);
        texture.SetData(&textureColour, 1);

        auto midToneCube = MakeSolidCube(device, Color(128, 128, 128, 255));
        auto greenCube = MakeSolidCube(device, Color(0, 255, 0, 255));

        const Vector3 normal(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            {Vector3(-1.0f,  1.0f, 0.0f), normal, Vector2(0.0f, 1.0f)},
            {Vector3(-1.0f, -1.0f, 0.0f), normal, Vector2(0.0f, 0.0f)},
            {Vector3( 1.0f, -1.0f, 0.0f), normal, Vector2(1.0f, 0.0f)},
            {Vector3(-1.0f,  1.0f, 0.0f), normal, Vector2(0.0f, 1.0f)},
            {Vector3( 1.0f, -1.0f, 0.0f), normal, Vector2(1.0f, 0.0f)},
            {Vector3( 1.0f,  1.0f, 0.0f), normal, Vector2(1.0f, 1.0f)},
        };

        // Amount 0: the cube contributes nothing at all. A deliberately loud green makes any leak
        // unmistakable rather than a few units of drift.
        Check("amount 0 with a green cube -> texture-only (100,50,25)",
              DrawAndRead(device, texture, *greenCube, 0.0f, Vector3(0.0f, 0.0f, 0.0f), quad),
              Color(100, 50, 25, 255));

        // Amount 1: the lerp replaces the base entirely. An additive blend would answer
        // (100,50,25)+(128,128,128) = (228,178,153) here, which is what makes this the case that
        // actually discriminates between the two formulas.
        Check("amount 1 with a mid-tone cube -> cube replaces the base (128,128,128)",
              DrawAndRead(device, texture, *midToneCube, 1.0f, Vector3(0.0f, 0.0f, 0.0f), quad),
              Color(128, 128, 128, 255));

        // EnvironmentMapSpecular is added on top of the blended result, scaled by the cube's alpha
        // (1 here): 0.25 of full white over the amount-0 base.
        Check("amount 0 plus specular 0.25 -> base plus 64",
              DrawAndRead(device, texture, *greenCube, 0.0f, Vector3(0.25f, 0.25f, 0.25f), quad),
              Color(164, 114, 89, 255));

        Exit();
    }

public:
    MagnumEnvironmentMapEffectTest()
    {
        deviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        deviceManager_->setPreferredBackBufferWidthProperty(kSize);
        deviceManager_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    MagnumEnvironmentMapEffectTest game;
    game.Run();
    return game.GetResult();
}
