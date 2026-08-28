// SPDX-License-Identifier: MS-PL
// plans/plan_fx.md FX-125: BasicEffect.VertexColorEnabled has a "Vc" variant of every lit family in
// XNA -- VSBasicVertexLightingVc and friends -- which multiplies the lit diffuse by the per-vertex
// colour. EasyGL's lit programs had no colour input at all, and SelectStockProgramShape dispatched
// on stride alone with cases for 20, 24 and 32 only. A Position+Normal+Color+TextureCoordinate
// vertex is 36 bytes -- exactly what the stock ModelProcessor emits for a mesh carrying a colour
// channel, alongside setting VertexColorEnabled -- so it matched no case, fell through to the
// UNLIT prog_colored_, and the model rendered flat: no shading, no specular, no light at all.
//
// Found by cna-samples SAMPLE-047 (PickingSample), whose Sphere01 mesh is exactly that: stride 36,
// VertexColorEnabled true. Against the real XNA executable the sphere agreed on 46.94 % of pixels
// within 8 levels and was up to 84 levels too dark; after this fix, 99.76 %.
//
// The scene is built so the two answers are far apart and computable by hand:
//
//   AmbientLightColor = (0.5, 0.5, 0.5), DiffuseColor = (1,1,1), no directional light, specular
//   black, a white texture, and a vertex colour of (0.4, 0.8, 0.2).
//
//   lit and vertex-coloured (XNA): 0.5 * (0.4, 0.8, 0.2) = (0.2, 0.4, 0.1) -> (51, 102, 26)
//   unlit vertex colour (the bug): (0.4, 0.8, 0.2)       -> (102, 204, 51)
//   lit but colour ignored:        0.5                   -> (128, 128, 128)
//
// Three checks, because "not the bug" is not the same as "right":
//   (a) the centre reads the lit AND vertex-coloured value;
//   (b) it is not the unlit vertex colour, which is what the missing case produced;
//   (c) it is not the lit-but-colourless value, which is what a program that ignores the colour
//       attribute would produce -- the failure mode the attribute's LOCATION caused. The location
//       is the element's index in the program's own input table, not a number a shader may choose:
//       declaring it as 5 while the table bound it at 3 left aColor at its generic default and the
//       model went black under lighting.
//
// Exit code 0 = PASS, 1 = FAIL.

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

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

static const Color kWhite(255, 255, 255, 255);
static const Vector3 kAmbient(0.5f, 0.5f, 0.5f);
static const Vector3 kOne(1.0f, 1.0f, 1.0f);
static const Vector3 kNormal(0.0f, 0.0f, 1.0f);
static const Vector3 kEye(0.0f, 0.0f, 3.0f);

// (0.4, 0.8, 0.2) in 8-bit, which is what the vertex carries.
static const Color kVertexColor(102, 204, 51, 255);

static const Color kExpectedLitAndColoured(51, 102, 26, 255);
static const Color kUnlitVertexColour(102, 204, 51, 255);
static const Color kLitButColourless(128, 128, 128, 255);

/// Position + Normal + Color + TextureCoordinate, the 36-byte layout the ModelProcessor emits for
/// a mesh with a colour channel. CNA has no built-in vertex type for it, exactly as XNA has none.
struct LitColourVertex
{
    Vector3 position;
    Vector3 normal;
    unsigned int color;   ///< packed BGRA, as VertexElementFormat::Color is
    Vector2 uv;
};
static_assert(sizeof(LitColourVertex) == 36, "the layout under test is the 36-byte one");

class BasicEffectLitVertexColorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        std::printf("[%s] %s: got=(%d,%d,%d) expected %s\n", ok ? "PASS" : "FAIL", label,
                    got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
        ok ? ++pass_ : ++fail_;
    }

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    static bool matches(const Color& c, const Color& expected)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), 6)
            && closeTo(c.getGProperty(), expected.getGProperty(), 6)
            && closeTo(c.getBProperty(), expected.getBProperty(), 6);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &px, 0, 1);
        return px;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);

        const VertexDeclaration declaration{
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Color, VertexElementUsage::Color, 0),
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
        // XNA's own default, and the family this defect lived in.
        fx.setPreferPerPixelLightingProperty(false);
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
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }

        check(matches(got, kExpectedLitAndColoured),
              "(a) a lit, vertex-coloured, 36-byte vertex is both lit AND coloured",
              got, "(51,102,26)");
        check(!matches(got, kUnlitVertexColour),
              "(b) it is NOT the unlit vertex colour the missing stride case produced",
              got, "not (102,204,51)");
        check(!matches(got, kLitButColourless),
              "(c) it is NOT lit-but-colourless, which an unbound colour attribute gives",
              got, "not (128,128,128)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BasicEffectLitVertexColorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectLitVertexColorTest game;
    game.Run();
    return game.getResult();
}
