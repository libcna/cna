// SPDX-License-Identifier: MS-PL
// Task 899: BasicEffect linear fog pixel integration test — Vulkan colored3d pipeline
// (stride 16, VertexPositionColor: TextureEnabled=false, VertexColorEnabled=false).
//
// colored3d was one of 5 Vulkan pipelines sharing FillExtPushConst()'s fully-packed 128-byte
// push constant (zero spare bytes for fog) -- Task 888 fixed lit_textured3d and part of
// alpha_test3d/alpha_test_colored3d via spare bytes in their own dedicated blocks, but colored3d/
// textured3d/colored_textured3d/dual_texture3d/skinned3d were deferred to this task. Fixed here
// by giving colored3d/textured3d/colored_textured3d a new shared dynamic UBO (set=0, binding=1)
// carrying fogColor/fogEnabled/fogStart/fogEnd, bound alongside a (for colored3d, unused)
// fallback white texture at binding=0 so all three pipelines can share one bundle.
//
// Formula (matches EasyGL's already-tested formula exactly, Task 195):
//   fogFactor = clamp((FogEnd - Z) / (FogEnd - FogStart), 0, 1)   (raw object-space Z)
//   finalRGB  = mix(FogColor, geomRGB, fogFactor)
//
// Uses a stride-16 VertexPositionColor quad (routes through colored3d on Vulkan) with
// TextureEnabled=false, VertexColorEnabled=false so the pre-fog color reduces to exactly
// DiffuseColor, isolating fog cleanly.
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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

static const Color kBlack(0, 0, 0, 255);
static const Color kBlue(0, 0, 255, 255);
static const Color kRed(255, 0, 0, 255);

class BasicEffectColored3DFogVulkanTest : public Game
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

    Color renderQuad(GraphicsDevice& dev, float z, bool fogEnabled,
                      const Vector3& fogColor, float fogStart, float fogEnd)
    {
        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.VertexColorEnabled = false;
        fx.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 1.0f)); // blue
        fx.setFogEnabledProperty(fogEnabled);
        fx.setFogColorProperty(fogColor);
        fx.setFogStartProperty(fogStart);
        fx.setFogEndProperty(fogEnd);
        fx.Apply();

        const Vector3 tl(-1.0f,  1.0f, z), bl(-1.0f, -1.0f, z);
        const Vector3 br( 1.0f, -1.0f, z), tr( 1.0f,  1.0f, z);
        const Color vc(0, 255, 0, 255); // green vertex color, must be ignored (VertexColorEnabled=false)
        const VertexPositionColor quad[6] = {
            { tl, vc }, { bl, vc }, { br, vc },
            { tl, vc }, { br, vc }, { tr, vc },
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

        // (a) Fog disabled: blue quad at Z=0 -> pure blue.
        const Color gotOff = renderQuad(dev, 0.0f, false, Vector3(1, 0, 0), 0.0f, 1.0f);
        check(matches(gotOff, kBlue), "(a) fog OFF: blue quad -> pure blue", gotOff, "(0,0,255)");

        // (b) Fog 50%: Z=0.5, FogStart=0, FogEnd=1, FogColor=red -> mix(red,blue,0.5)=(128,0,128).
        const Color gotHalf = renderQuad(dev, 0.5f, true, Vector3(1, 0, 0), 0.0f, 1.0f);
        check(matches(gotHalf, Color(128, 0, 128, 255)),
              "(b) fog 50%: Z=0.5 -> purple mix", gotHalf, "(128,0,128)");

        // (c) Full fog: FogEnd=0.5, Z=0.9 -> fogFactor=clamp(-0.8,0,1)=0 -> pure red.
        const Color gotFull = renderQuad(dev, 0.9f, true, Vector3(1, 0, 0), 0.0f, 0.5f);
        check(matches(gotFull, kRed), "(c) full fog: FogEnd=0.5, Z=0.9 -> full red", gotFull, "(255,0,0)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BasicEffectColored3DFogVulkanTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectColored3DFogVulkanTest game;
    game.Run();
    return game.getResult();
}
