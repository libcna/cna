// SPDX-License-Identifier: MS-PL
// Task 367: BasicEffect pixel test — TextureEnabled=true AND VertexColorEnabled=true, the
// stride-24 VertexPositionColorTexture path (Vulkan backend).
//
// See examples/easygl_basiceffect_texture_vertexcolor_enabled_test.cpp for the full FNA-derived
// expected-output derivation. Summary: with LightingEnabled=false, VertexColorEnabled=true and
// TextureEnabled=true (both explicitly set here), BasicEffect's shader must output
// TextureColor*VertexColor*DiffuseColor*Alpha, component-wise (shaderIndex 7 ->
// VSBasicTxVcNoFog/PSBasicTxNoFog). Unlike EasyGL and Bgfx, Vulkan's stride-24
// `colored_textured3d.vert.glsl` already computed `fragTint = (vertexColorEnabled>0.5) ?
// inColor*diffuseColor : diffuseColor` correctly before this task — confirmed here by pixel
// readback, not fixed.
//
// Uses the same 3 distinctly-valued colors as the EasyGL test: texture color (200,100,50), vertex
// color (150,200,100), DiffuseColor (0.8,0.4,0.6). Correct output: (94,31,12).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

static const Color kTexColor(200, 100, 50, 255);
static const Color kVertexColor(150, 200, 100, 255);
static const Vector3 kDiffuse(0.8f, 0.4f, 0.6f);

static const Color kExpected(94, 31, 12, 255);
static const Color kTextureDiffuseOnly(160, 40, 30, 255);
static const Color kVertexDiffuseOnly(120, 80, 60, 255);
static const Color kTextureVertexOnly(118, 78, 20, 255);
static const Color kTextureOnly(200, 100, 50, 255);
static const Color kVertexOnly(150, 200, 100, 255);
static const Color kDiffuseOnly(204, 102, 153, 255);

class VulkanBasicEffectTextureVertexColorEnabledTest : public Game
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

    static bool matches(const Color& c, const Color& expected)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), 8)
            && closeTo(c.getGProperty(), expected.getGProperty(), 8)
            && closeTo(c.getBProperty(), expected.getBProperty(), 8);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kTexColor, 1);

        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.VertexColorEnabled = true;
        fx.setDiffuseColorProperty(kDiffuse);

        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
        const VertexPositionColorTexture q[6] = {
            { tl, kVertexColor, uv0 }, { bl, kVertexColor, uv1 }, { br, kVertexColor, uv2 },
            { tl, kVertexColor, uv0 }, { br, kVertexColor, uv2 }, { tr, kVertexColor, uv3 },
        };

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): the standard NDC
            // quad winding used throughout this pixel-test family is culled once the real
            // default RasterizerState reaches the GPU.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }

        check(matches(got, kExpected),
              "Texture+VertexColor: pixel == TextureColor*VertexColor*DiffuseColor",
              got, "(94,31,12)");
        check(!matches(got, kTextureDiffuseOnly),
              "Texture+VertexColor: pixel != texture*diffuse alone (vertex color not ignored)",
              got, "not (160,40,30)");
        check(!matches(got, kVertexDiffuseOnly),
              "Texture+VertexColor: pixel != vertex*diffuse alone (texture not ignored)",
              got, "not (120,80,60)");
        check(!matches(got, kTextureVertexOnly),
              "Texture+VertexColor: pixel != texture*vertex alone (diffuse not ignored)",
              got, "not (118,78,20)");
        check(!matches(got, kTextureOnly), "Texture+VertexColor: pixel != texture color alone",
              got, "not (200,100,50)");
        check(!matches(got, kVertexOnly), "Texture+VertexColor: pixel != vertex color alone",
              got, "not (150,200,100)");
        check(!matches(got, kDiffuseOnly), "Texture+VertexColor: pixel != DiffuseColor alone",
              got, "not (204,102,153)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VulkanBasicEffectTextureVertexColorEnabledTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanBasicEffectTextureVertexColorEnabledTest game;
    game.Run();
    return game.getResult();
}
