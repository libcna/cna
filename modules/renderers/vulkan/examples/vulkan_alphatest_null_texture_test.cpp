// SPDX-License-Identifier: MS-PL
// Task 379: AlphaTestEffect null/no-texture behavior (Vulkan renderer).
//
// See examples/easygl_alphatest_null_texture_test.cpp for the full derivation.
//
// plans/plan_vulkan_parity.md VKPAR-0004 corrected the expected value. This file used to assert a
// WHITE fallback and recorded "no bug found here"; that predates the measurement. XNA 4.0 reads an
// unbound AlphaTestEffect texture as opaque black — tools/xna-oracle/reference/null-texture/
// alphatest_null.png, centre (0,0,0,255) — so diffuse never reaches the target and the draw is
// black. The alpha channel still comes from the effect, which is what keeps the alpha test itself
// meaningful. GSC-0004 corrected DirectX11, DirectX12 and EasyGL the same way.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

static const Color kTexColor(200, 100, 50, 255);
// A colour no leg of this test can draw, so "blank frame" and "drew opaque black" differ.
static const Color kWitness(7, 199, 53, 255);
static const Vector3 kDiffuse(0.6f, 0.4f, 0.8f);

static const Color kExpectedWithTexture(120, 40, 40, 255);
// diffuse(0.6,0.4,0.8) * opaqueBlack(0,0,0,1) = (0,0,0); alpha stays the effect's own.
static const Color kExpectedNullTexture(0, 0, 0, 255);

class VulkanAlphaTestNullTextureTest : public Game
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

    Color renderWith(GraphicsDevice& dev, Texture2D* tex, const VertexPositionTexture (&quad)[6])
    {
        AlphaTestEffect fx(dev);
        fx.setTextureProperty(tex);
        fx.setDiffuseColorProperty(kDiffuse);
        // Task 896 finding (mirrors the Task 364/884 fix on Bgfx, retired 2026-09-17): the standard
        // NDC quad winding used throughout this pixel-test family is culled once the real default
        // RasterizerState reaches the GPU.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        fx.Apply();

        // VKPAR-0004: the loop used to clear to black and retry "until the pixel is not black",
        // which was a workable way to skip a blank first frame only while the expected answer could
        // never itself be black. It can be now -- that is the whole point of the corrected rule --
        // so the clear is a WITNESS colour the draw cannot produce, and the loop waits for the
        // quad to land rather than for it to be non-black. A black readback is then a real draw.
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(kWitness);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            got = readCenter(dev);
            if (!matches(got, kWitness))
                break; // the quad has landed; a blank frame still reads the clear colour
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kTexColor, 1);

        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
        const VertexPositionTexture quad[6] = {
            { tl, uv0 }, { bl, uv1 }, { br, uv2 },
            { tl, uv0 }, { br, uv2 }, { tr, uv3 },
        };

        const Color withTexGot = renderWith(dev, &tex, quad);
        check(matches(withTexGot, kExpectedWithTexture),
              "real texture bound: TextureColor*DiffuseColor", withTexGot, "(120,40,40)");

        const Color nullTexGot = renderWith(dev, nullptr, quad);
        check(matches(nullTexGot, kExpectedNullTexture),
              "Texture=null: samples XNA's opaque black (not the previous draw's stale texture)",
              nullTexGot, "(0,0,0)");
        check(!matches(nullTexGot, kExpectedWithTexture),
              "Texture=null: pixel != previous draw's texture (proves no stale-state leak)",
              nullTexGot, "not (120,40,40)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VulkanAlphaTestNullTextureTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // VKPAR-0004: GetBackBufferData is a HiDef operation. Without this these three tests
        // aborted before asserting anything -- "GetBackBufferData is not supported by the
        // Reach graphics profile" -- on the baseline as well as here, so their registrations
        // had been contributing nothing. The EasyGL siblings have always set it.
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanAlphaTestNullTextureTest game;
    game.Run();
    return game.getResult();
}
