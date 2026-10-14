// SPDX-License-Identifier: MS-PL
//
// BasicEffect.PunctualLightEXT lights a surface (point, then spot), and a disabled light lights nothing.
//
// A white wall faces the camera and nothing else lights it, so what the centre pixel reads is the punctual light:
//
//   A  no punctual light            -> dark (the control: the faint directional light that keeps the lit path
//                                      selected is not enough to read as lit)
//   B  a point light in front       -> bright
//   C  the same light, out of range -> dark again (the range window reaches zero)
//   D  a spot light aimed at the wall -> bright; the same spot aimed away -> dark
//
// Written after the museum-simulator found the SOFTWARE renderer ignoring the light altogether: the property was
// accepted and the surface stayed black, with no capability flag to ask first. Renderer-agnostic by design.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/PunctualLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {
constexpr int kSize = 64;
}

class BasicEffectPunctualLightTest final : public Game
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

    /// Red channel of the centre pixel after drawing the wall with `light`.
    int Shade(GraphicsDevice& dev, const PunctualLightEXT& light)
    {
        dev.Clear(Color(0, 0, 0, 255));
        BasicEffect fx(dev);
        fx.setLightingEnabledProperty(true);
        fx.setTextureEnabledProperty(false);
        fx.setVertexColorEnabledProperty(false);
        fx.setPreferPerPixelLightingProperty(true);
        fx.setAmbientLightColorProperty(Vector3(0, 0, 0));
        fx.setDiffuseColorProperty(Vector3(1, 1, 1));
        fx.setEmissiveColorProperty(Vector3(0, 0, 0));
        fx.setSpecularColorProperty(Vector3(0, 0, 0));
        fx.getDirectionalLight0Property().setEnabledProperty(true);
        fx.getDirectionalLight0Property().setDirectionProperty(Vector3(0, -1, 0));
        fx.getDirectionalLight0Property().setDiffuseColorProperty(Vector3(0.02f, 0.02f, 0.02f));
        fx.getDirectionalLight1Property().setEnabledProperty(false);
        fx.getDirectionalLight2Property().setEnabledProperty(false);
        fx.setPunctualLightEXT(light);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0, 0, 3), Vector3::Zero, Vector3::Up));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::ToRadians(60.0f), 1.0f, 0.1f, 20.0f));
        const Vector3 n(0, 0, 1);
        const VertexPositionNormalTexture tl(Vector3(-1.5f, 1.5f, 0), n, Vector2(0, 0)), tr(Vector3(1.5f, 1.5f, 0), n, Vector2(1, 0)),
            bl(Vector3(-1.5f, -1.5f, 0), n, Vector2(0, 1)), br(Vector3(1.5f, -1.5f, 0), n, Vector2(1, 1));
        const VertexPositionNormalTexture quad[6] = {tl, bl, br, tl, br, tr};
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&centre, &px, 0, 1);
        return px.getRProperty();
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetDepthTestEnabled(false);

        PunctualLightEXT none;
        PunctualLightEXT point;
        point.Kind = PunctualLightKindEXT::Point;
        point.Position = Vector3(0, 0, 1);
        point.Range = 10.0f;
        point.DiffuseColor = Vector3(1, 1, 1) * 6.0f;
        PunctualLightEXT far = point;
        far.Range = 0.5f;   // the wall is a metre away: outside the range
        PunctualLightEXT spotAt = point;
        spotAt.Kind = PunctualLightKindEXT::Spot;
        spotAt.Direction = Vector3(0, 0, -1);
        spotAt.OuterAngle = MathHelper::ToRadians(40.0f);
        spotAt.InnerAngle = MathHelper::ToRadians(20.0f);
        PunctualLightEXT spotAway = spotAt;
        spotAway.Direction = Vector3(0, 0, 1);

        const int a = Shade(dev, none), b = Shade(dev, point), c = Shade(dev, far), d = Shade(dev, spotAt), e = Shade(dev, spotAway);
        check(a < 40, "A no punctual light: the wall stays dark (" + std::to_string(a) + ")");
        check(b > 120, "B a point light a metre in front lights it (" + std::to_string(b) + ")");
        check(c < 40, "C the same light with a range of half a metre lights nothing (" + std::to_string(c) + ")");
        check(d > 120, "D a spot light aimed at the wall lights it (" + std::to_string(d) + ")");
        check(e < 40, "E the same spot aimed away lights nothing (" + std::to_string(e) + ")");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    BasicEffectPunctualLightTest()
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
    BasicEffectPunctualLightTest game;
    game.Run();
    return game.getResult();
}
