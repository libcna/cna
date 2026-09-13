// SPDX-License-Identifier: MS-PL
// Task 379 / SOFTWARE-303: AlphaTestEffect null/no-texture behavior (EasyGL renderer).
//
// FNA reference: `AlphaTestEffect` has no `TextureEnabled` flag at all (unlike `BasicEffect`) —
// every one of its 4 shader variants unconditionally does `SAMPLE_TEXTURE(Texture,pin.TexCoord) *
// pin.Diffuse`. Microsoft XNA 4.0 was measured directly: an unbound sampler contributes opaque
// black. SOFTWARE-303 supersedes CNA's former invented opaque-white convention.
//
// Task 379 originally used this test to eliminate stale bindings, but chose opaque white without
// measuring Microsoft XNA. The stale-binding structure remains useful; SOFTWARE-303 corrects its
// authority and expected value.
//
// Uses a distinctive DiffuseColor=(0.6,0.4,0.8) (not white) so the stale previous texture and
// obsolete white fallback are both distinguishable from the measured opaque-black result.
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
static const Vector3 kDiffuse(0.6f, 0.4f, 0.8f);

// Expected with a real texture bound: TextureColor * DiffuseColor.
static const Color kExpectedWithTexture(120, 40, 40, 255);
static const Color kExpectedNullTexture(0, 0, 0, 255);
static const Color kBackground(7, 199, 53, 255);

class AlphaTestNullTextureTest : public Game
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
            && closeTo(c.getBProperty(), expected.getBProperty(), 8)
            && closeTo(c.getAProperty(), expected.getAProperty(), 8);
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
        fx.Apply();

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(kBackground);
            dev.setBlendStateProperty(BlendState::Opaque);
            // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): this quad's
            // winding is culled by the real default RasterizerState once EasyGL pushes it at
            // construction.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            got = readCenter(dev);
            if (!matches(got, kBackground))
                break;
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

        // Sub-test 1: real texture bound, establishes the "previous draw" state.
        const Color withTexGot = renderWith(dev, &tex, quad);
        check(matches(withTexGot, kExpectedWithTexture),
              "real texture bound: TextureColor*DiffuseColor", withTexGot, "(120,40,40)");

        // Sub-test 2: Texture=null. Must sample opaque black, not retain sub-test 1's texture.
        const Color nullTexGot = renderWith(dev, nullptr, quad);
        check(matches(nullTexGot, kExpectedNullTexture),
              "Texture=null: samples opaque black like Microsoft XNA",
              nullTexGot, "(0,0,0,255)");
        check(!matches(nullTexGot, kExpectedWithTexture),
              "Texture=null: pixel != previous draw's texture (proves no stale-state leak)",
              nullTexGot, "not (120,40,40)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    AlphaTestNullTextureTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    AlphaTestNullTextureTest game;
    game.Run();
    return game.getResult();
}
