// SPDX-License-Identifier: MS-PL
// Task 888: BasicEffect linear fog pixel integration test — Vulkan backend.
//
// Fog was a total GPU no-op on Vulkan for every 3D effect (opened by Task 378's investigation,
// confirmed by grepping every .glsl shader file for "fog" and finding zero matches). Fixed on
// the lit_textured3d pipeline (stride 32, VertexPositionNormalTexture) by packing fog data into
// its LitLightParams UBO's previously-unused trailing 32 bytes (Task 897/886/898's UBO had 32
// spare bytes left in its 256-byte stride) and computing the standard fog blend in-shader.
// colored3d/textured3d/colored_textured3d/dual_texture3d's fully-packed, zero-spare-byte shared
// 128-byte push constant needs new dedicated UBO infrastructure per pipeline before they can get
// fog too — deferred to Task 899, not fixed here.
//
// Formula (matches EasyGL's already-tested formula exactly, Task 195):
//   fogFactor = clamp((FogEnd - Z) / (FogEnd - FogStart), 0, 1)   (raw object-space Z)
//   finalRGB  = mix(FogColor, geomRGB, fogFactor)
//
// Uses a stride-32 VertexPositionNormalTexture quad (routes through lit_textured3d on Vulkan
// regardless of LightingEnabled) with LightingEnabled=false (isolates fog from lighting math)
// and a white 1x1 texture (identity factor, isolating fog from texture sampling).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

static const Color kWhite(255, 255, 255, 255);
static const Color kBlack(0, 0, 0, 255);
static const Color kBlue(0, 0, 255, 255);
static const Color kRed(255, 0, 0, 255);

class BasicEffectFogVulkanTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
            ++fail_;
        }
    }

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    static bool matches(const Color& c, const Color& expected, int tol = 30)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), tol)
            && closeTo(c.getGProperty(), expected.getGProperty(), tol)
            && closeTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    Color renderQuad(GraphicsDevice& dev, Texture2D& tex, float z, bool fogEnabled,
                      const Vector3& fogColor, float fogStart, float fogEnd)
    {
        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setLightingEnabledProperty(false);
        fx.setTextureProperty(&tex);
        fx.setTextureEnabledProperty(true);
        fx.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 1.0f)); // blue
        fx.setFogEnabledProperty(fogEnabled);
        fx.setFogColorProperty(fogColor);
        fx.setFogStartProperty(fogStart);
        fx.setFogEndProperty(fogEnd);
        fx.Apply();

        const Vector3 tl(-1.0f,  1.0f, z), bl(-1.0f, -1.0f, z);
        const Vector3 br( 1.0f, -1.0f, z), tr( 1.0f,  1.0f, z);
        const Vector3 n(0.0f, 0.0f, 1.0f);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
        const VertexPositionNormalTexture quad[6] = {
            { tl, n, uv0 }, { bl, n, uv1 }, { br, n, uv2 },
            { tl, n, uv0 }, { br, n, uv2 }, { tr, n, uv3 },
        };

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);

        // (a) Fog disabled: blue quad at Z=0 -> pure blue.
        const Color gotOff = renderQuad(dev, tex, 0.0f, false, Vector3(1, 0, 0), 0.0f, 1.0f);
        check(matches(gotOff, kBlue), "(a) fog OFF: blue quad -> pure blue", gotOff, "(0,0,255)");

        // (b) Fog 50%: Z=0.5, FogStart=0, FogEnd=1, FogColor=red -> mix(red,blue,0.5)=(128,0,128).
        const Color gotHalf = renderQuad(dev, tex, 0.5f, true, Vector3(1, 0, 0), 0.0f, 1.0f);
        check(matches(gotHalf, Color(128, 0, 128, 255)),
              "(b) fog 50%: Z=0.5 -> purple mix", gotHalf, "(128,0,128)");

        // (c) Full fog: FogEnd=0.5, Z=0.9 -> fogFactor=clamp(-0.8,0,1)=0 -> pure red.
        const Color gotFull = renderQuad(dev, tex, 0.9f, true, Vector3(1, 0, 0), 0.0f, 0.5f);
        check(matches(gotFull, kRed), "(c) full fog: FogEnd=0.5, Z=0.9 -> full red", gotFull, "(255,0,0)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BasicEffectFogVulkanTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectFogVulkanTest game;
    game.Run();
    return game.getResult();
}
