// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-196 (finding F-36) -- Direct3D 9 saturates a vertex shader's colour output
// registers, and `EnvironmentMapEffect` carries its blend factor in one of them.
//
// `FX-123` established the semantic against real XNA frames: D3D9 clamps `oD0` **and `oD1`** to
// [0,1] BEFORE interpolation. FNA's `EnvironmentMapEffect.fx` writes the Fresnel/amount factor to
// `vout.Specular.rgb` (`ComputeEnvMapVSOutput`), and `Structures.fxh:156` declares
// `Specular : COLOR1` -- so XNA clamps it. `EnvironmentMapAmount` itself is NOT clamped by the
// property setter (`EnvironmentMapEffect.cs:283`), so a game may hand the shader a value above 1
// and XNA will saturate it; a renderer that interpolates the raw value extrapolates the blend
// instead of replacing with the environment map.
//
// Renderer-agnostic on purpose. EasyGL already clamps (`EasyGLRenderer.cpp:8423`) and Vulkan did
// not, so this is a parity gap as well as an XNA divergence, and the same source registered on both
// is what makes that statement checkable rather than asserted.
//
// The legs are RELATIONAL, never absolute, because the base colour a lit `EnvironmentMapEffect`
// produces depends on the lighting model and is not this test's subject:
//
//   A  amount 0.5 and amount 1.0 give DIFFERENT pixels -- the positive control. Without it,
//      leg B could pass on a renderer that ignores `EnvironmentMapAmount` entirely.
//   B  amount 1.0 and amount 2.0 give the SAME pixel -- the saturate.
//   C  amount 1.0 and amount 3.0 give the same pixel too, so B is not one value's coincidence.
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
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {
constexpr int kSize = 64;
/// Deliberately far apart, and neither near a channel end: the difference between "replace with the
/// environment map" and "extrapolate past it" has to be visible in eight bits.
const Color kTex(60, 60, 60, 255);
const Color kCube(200, 200, 200, 255);
} // namespace

class EnvironmentMapAmountClampTest final : public Game
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
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    /// Two colours are "the same pixel" within the tolerance every other env-map test here uses.
    static bool Same(const Color& a, const Color& b, int tol = 4)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tol;
    }

    Color ReadCentre(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    std::unique_ptr<TextureCube> MakeSolidCube(GraphicsDevice& dev, Color col)
    {
        auto cube = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (CubeMapFace face : faces) cube->SetData(face, &col, 1);
        return cube;
    }

    Color RenderAtAmount(GraphicsDevice& dev, Texture2D& tex, TextureCube& cube,
                         const VertexPositionNormalTexture (&quad)[6], float amount)
    {
        dev.Clear(Color(0, 0, 0, 255));
        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setEnvironmentMapProperty(&cube);
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        // Fresnel OFF, so `Specular.rgb` carries `EnvironmentMapAmount` unmodified and this test
        // measures the register clamp rather than the Fresnel curve.
        fx.setFresnelFactorProperty(0.0f);
        fx.setEnvironmentMapAmountProperty(amount);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        return ReadCentre(dev);
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        Texture2D tex(dev, 1, 1, false, SurfaceFormat::Color);
        tex.SetData(&kTex, 1);
        auto cube = MakeSolidCube(dev, kCube);

        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(1.0f, 1.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(1.0f, 1.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(1.0f, 0.0f) },
        };

        const Color half  = RenderAtAmount(dev, tex, *cube, quad, 0.5f);
        const Color one   = RenderAtAmount(dev, tex, *cube, quad, 1.0f);
        const Color two   = RenderAtAmount(dev, tex, *cube, quad, 2.0f);
        const Color three = RenderAtAmount(dev, tex, *cube, quad, 3.0f);

        check(!Same(half, one),
              "A the amount knob is live: 0.5 gives " + Text(half) + " and 1.0 gives " + Text(one) +
                  " (without this, leg B could pass on a renderer that ignores the amount)");
        check(Same(one, two),
              "B EnvironmentMapAmount 2.0 is saturated to 1.0, as D3D9 saturates oD1: 1.0 gives " +
                  Text(one) + " and 2.0 gives " + Text(two));
        check(Same(one, three),
              "C and so is 3.0, so B is not one value's coincidence: 3.0 gives " + Text(three) +
                  " (want " + Text(one) + ")");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    EnvironmentMapAmountClampTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    EnvironmentMapAmountClampTest game;
    game.Run();
    return game.getResult();
}
