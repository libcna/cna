// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-200 (finding F-37) -- the per-pixel half of the lit, vertex-coloured,
// 36-byte layout.
//
// `easygl_basiceffect_lit_vertex_color_test.cpp` (plans/plan_fx.md FX-125) covers this layout on
// the family XNA selects by default, and it says so in its own source: it sets
// `PreferPerPixelLighting(false)` explicitly. So it exercises `VSBasicVertexLightingTxVc` and says
// nothing at all about `VSBasicPixelLightingTxVc`, whose vertex-colour handling is a different
// shape rather than the same one moved:
//
//   per-vertex : `vout.Diffuse *= vin.Color` in the VERTEX stage, ahead of oD0, so the colour is
//                inside Direct3D 9's saturate (FX-123/FX-125).
//   per-pixel  : `vout.Diffuse.rgb = vin.Color.rgb`, and PSBasicPixelLightingTx then computes
//                `color = tex * pin.Diffuse` followed by `color.rgb *= lightResult.Diffuse` --
//                so the colour multiplies the WHOLE lit bracket, emissive included, and is
//                applied in the FRAGMENT stage.
//
// A renderer can therefore have one right and the other wrong, and a suite that registers only the
// first would ship the second untested. This is the same scene with the flag the other way up.
//
// The scene is ambient-only on purpose (no directional light, all three disabled), so per-vertex
// and per-pixel lighting are arithmetically identical here and the SAME expected pixel applies to
// both -- which is what makes it a test of the colour path rather than of the lighting model:
//
//   ambient 0.5 * DiffuseColor 1.0 = 0.5, times the vertex colour (0.4, 0.8, 0.2) = (51,102,26).
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
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {
constexpr int kSize = 64;

const Color   kWhite(255, 255, 255, 255);
const Vector3 kAmbient(0.5f, 0.5f, 0.5f);
const Vector3 kOne(1.0f, 1.0f, 1.0f);
const Vector3 kNormal(0.0f, 0.0f, 1.0f);
const Vector3 kEye(0.0f, 0.0f, 3.0f);

/// (0.4, 0.8, 0.2) in eight bits, which is what the vertex carries.
const Color kVertexColor(102, 204, 51, 255);

const Color kExpectedLitAndColoured(51, 102, 26, 255);
const Color kUnlitVertexColour(102, 204, 51, 255);
const Color kLitButColourless(128, 128, 128, 255);

/// Position + Normal + Colour + TextureCoordinate -- the 36-byte layout the stock ModelProcessor
/// emits for a mesh with a colour channel. CNA has no built-in vertex type for it, exactly as XNA
/// has none.
struct LitColourVertex
{
    Vector3      position;
    Vector3      normal;
    unsigned int color;   ///< packed BGRA, as VertexElementFormat::Color is
    Vector2      uv;
};
static_assert(sizeof(LitColourVertex) == 36, "the layout under test is the 36-byte one");
} // namespace

class BasicEffectLitVertexColorPerPixelTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label, const Color& got, const std::string& expected)
    {
        std::printf("[%s] %s: got=(%d,%d,%d) expected %s\n", ok ? "PASS" : "FAIL", label.c_str(),
                    got.getRProperty(), got.getGProperty(), got.getBProperty(), expected.c_str());
        std::fflush(stdout);
        ok ? ++pass_ : ++fail_;
    }

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    static bool matches(const Color& c, const Color& want)
    {
        return closeTo(c.getRProperty(), want.getRProperty(), 6)
            && closeTo(c.getGProperty(), want.getGProperty(), 6)
            && closeTo(c.getBProperty(), want.getBProperty(), 6);
    }

    Color ReadCentre(GraphicsDevice& dev)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &px, 0, 1);
        return px;
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);

        const VertexDeclaration declaration{
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Color,   VertexElementUsage::Color, 0),
            VertexElement(28, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        };

        const unsigned int packed = kVertexColor.getPackedValueProperty();
        const LitColourVertex quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), kNormal, packed, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), kNormal, packed, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), kNormal, packed, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), kNormal, packed, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), kNormal, packed, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), kNormal, packed, Vector2(1.0f, 1.0f) },
        };

        VertexBuffer buffer(dev, declaration, 6, BufferUsage::WriteOnly);
        buffer.SetDataRaw(quad, 6, static_cast<int>(sizeof(LitColourVertex)));

        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setLightingEnabledProperty(true);
        // The one thing this test changes from its per-vertex twin, and the whole reason it exists.
        fx.setPreferPerPixelLightingProperty(true);
        fx.setVertexColorEnabledProperty(true);
        fx.setAmbientLightColorProperty(kAmbient);
        fx.setDiffuseColorProperty(kOne);
        fx.setEmissiveColorProperty(Vector3::Zero);
        fx.setSpecularColorProperty(Vector3::Zero);
        fx.setSpecularPowerProperty(1.0f);
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);

        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(kEye, Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(
            Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            fx.Apply();
            // The real default RasterizerState culls this quad's winding, same finding as the
            // other BasicEffect tests next to this one.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.SetVertexBuffer(&buffer);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            got = ReadCentre(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }

        check(matches(got, kExpectedLitAndColoured),
              "(a) PreferPerPixelLighting=true: a lit, vertex-coloured 36-byte vertex is both lit "
              "AND coloured", got, "(51,102,26)");
        check(!matches(got, kUnlitVertexColour),
              "(b) it is NOT the unlit vertex colour a missing lit-colour program produces",
              got, "not (102,204,51)");
        check(!matches(got, kLitButColourless),
              "(c) it is NOT lit-but-colourless, which an unbound colour attribute gives",
              got, "not (128,128,128)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    BasicEffectLitVertexColorPerPixelTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectLitVertexColorPerPixelTest game;
    game.Run();
    return game.getResult();
}
