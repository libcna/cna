// SPDX-License-Identifier: MS-PL
// Task 134: EasyGL integration test — EnvironmentMapEffect cube-map reflection.
//
// Renders a full-screen quad using EnvironmentMapEffect.  The expected pixel
// colour is computed analytically so the test does not depend on a complex
// cube-map image.
//
// Shader formula (env+mapped GLSL):
//   litRGB   = (uEmissiveColor + uLight0Diffuse * NdotL) * uDiffuseColor.rgb
//   rgb      = litRGB * texColor.rgb + envColor * uEnvMapAmount + uEnvMapSpecular
//   FragColor = vec4(rgb, uDiffuseColor.a * texColor.a)
//
// Parameter choices that give a predictable red output:
//   emissiveColor    = (1, 0, 0)   — combined with ambient=0 gives uEmissiveColor=(1,0,0)
//   diffuseColor     = (1, 1, 1)   — uDiffuseColor.rgb=(1,1,1)
//   DirectionalLight0.DiffuseColor = (0, 0, 0) default — no light contribution
//   texture0         = solid white  — texColor.rgb=(1,1,1)
//   envMapAmount     = 0.0          — no cube-map contribution
//   envMapSpecular   = (0, 0, 0)
//   => rgb = (1,0,0)*(1,1,1) + 0 + 0 = (1,0,0) = red
//
// The TextureCube still needs to be a valid GPU object (avoiding sampler type
// mismatch on the samplerCube uniform), so a 1×1 all-white cube is created.
//
// Uses VertexPositionNormalTexture (stride=32): vec3 pos, vec3 normal, vec2 uv.
// Normal = (0,0,1) (facing viewer); identity world/view/projection.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class EnvMapTest : public Game
{
    Texture2D                    diffuseTex_;  // 1×1 white
    std::unique_ptr<TextureCube> envCube_;     // 1×1 all-white cube (amount=0, colour irrelevant)
    bool                         done_   = false;
    int                          result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        // Diffuse texture: solid white.
        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        diffuseTex_ = Texture2D::CreateFromPixels(device, 1, 1, white);

        // Cube map: 1×1 per face, all white.  Amount is 0 so the colour is
        // irrelevant, but a real GL cube-map object avoids samplerCube type
        // mismatch on the uniform.
        envCube_ = std::make_unique<TextureCube>(device, 1, false, SurfaceFormat::Color);
        const Color faceColor(255, 255, 255, 255);
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (CubeMapFace face : faces)
            envCube_->SetData(face, &faceColor, 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 255, 0, 255)); // green background
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        EnvironmentMapEffect fx(device);

        // emissiveColor = red; diffuseColor = white; light0 diffuse = black (default).
        // Shader: litRGB = ((1,0,0) + (0,0,0)*NdotL) * (1,1,1) = (1,0,0).
        fx.setEmissiveColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setEnvironmentMapAmountProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setTextureProperty(&diffuseTex_);
        fx.setEnvironmentMapProperty(envCube_.get());
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        // Full-screen quad, normal = (0,0,1) facing viewer.
        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        // Centre pixel: emissive=(1,0,0), light=(0,0,0), envAmount=0 → red.
        const Rectangle centReg(W / 2, H / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        device.GetBackBufferData(&centReg, &centPx, 0, 1);

        const bool pass = (centPx.getRProperty() >= 200 &&
                           centPx.getGProperty() <= 50  &&
                           centPx.getBProperty() <= 50);

        if (pass)
        {
            std::printf("[PASS] EnvironmentMapEffect: centre=(%d,%d,%d)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] EnvironmentMapEffect: centre=(%d,%d,%d), "
                        "expected red (R>=200, G<=50, B<=50)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    EnvMapTest game;
    game.Run();
    return game.getResult();
}
