// SPDX-License-Identifier: MS-PL
// PBR + skinning combo pixel test for SkinnedPbrEffect's shader (BgfxRenderer's
// vs_pbr_skinned3d.sc, sharing fs_pbr3d.sc's fragment stage with the unskinned PbrEffect) --
// proves the stride-68 VertexPositionNormalTangentTextureSkinned layout, the bone-palette skin
// transform applied to Position/Normal/Tangent, and the PBR BRDF fragment stage all work
// end-to-end via a real GPU draw on the Bgfx renderer.
//
// Reuses examples/bgfx_pbreffect_test.cpp's exact scene (same camera, same single directional
// light, same per-check material setup) but through SkinnedPbrEffect with a single identity bone
// (weight 1.0 on bone index 0, SkinnedEffect's own "Task 406 identity-bone case" precedent). An
// identity bind pose is a mathematical no-op for the skin transform, so the rendered pixels must
// be identical to PbrEffect's own already-verified expected values -- this is the test's own
// oracle (not a freshly hand-derived number), and directly proves the skin matrix multiply in
// vs_pbr_skinned3d.sc is wired correctly (a bug there would shift/darken the result relative to
// the unskinned PbrEffect test).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

// Stride-68 GPU-compact skinned PBR vertex: matches BgfxRenderer's MakeBgfxLayout
// stride==68 case (Position+Normal+Tangent+TextureCoordinate+BlendWeight+BlendIndices).
struct SkinnedPbrGpuVertex
{
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz, tw;
    float u, v;
    float w0, w1, w2, w3;
    std::uint8_t i0, i1, i2, i3;
};
static_assert(sizeof(SkinnedPbrGpuVertex) == 68, "skinned PBR vertex must be 68 bytes");

class BgfxSkinnedPbrEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    Texture2D redTex_;
    Texture2D tiltedNormalTex_;
    int pass_ = 0, fail_ = 0;

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

    static bool matches(const Color& c, const Color& expected, int tol = 8)
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

    Color renderWith(GraphicsDevice& dev, VertexBuffer& vb, Texture2D* tex, Texture2D* normalMap,
                      float roughness, float metallic, const Vector3& ambient)
    {
        SkinnedPbrEffect fx(dev);
        fx.setBaseColorTextureIsSrgbEXTProperty(false);
        fx.setEmissiveTextureIsSrgbEXTProperty(false);
        fx.setEncodeOutputToSrgbEXTProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.setTextureProperty(tex);
        fx.setNormalMapProperty(normalMap);
        fx.setRoughnessFactorProperty(roughness);
        fx.setMetallicFactorProperty(metallic);
        fx.setAmbientLightColorProperty(ambient);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight0.setEnabledProperty(true);

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 255, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }
        return got;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1, white);
        const std::vector<uint8_t> red = { 255, 0, 0, 255 };
        redTex_ = Texture2D::CreateFromPixels(dev, 1, 1, red);
        const std::vector<uint8_t> tilted = { 255, 128, 128, 255 };
        tiltedNormalTex_ = Texture2D::CreateFromPixels(dev, 1, 1, tilted);
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        const SkinnedPbrGpuVertex quad[6] = {
            { -1,  1, 0,  0, 0, 1,  1, 0, 0, 1,  0, 0,  1, 0, 0, 0,  0, 0, 0, 0 },
            { -1, -1, 0,  0, 0, 1,  1, 0, 0, 1,  0, 1,  1, 0, 0, 0,  0, 0, 0, 0 },
            {  1, -1, 0,  0, 0, 1,  1, 0, 0, 1,  1, 1,  1, 0, 0, 0,  0, 0, 0, 0 },
            { -1,  1, 0,  0, 0, 1,  1, 0, 0, 1,  0, 0,  1, 0, 0, 0,  0, 0, 0, 0 },
            {  1, -1, 0,  0, 0, 1,  1, 0, 0, 1,  1, 1,  1, 0, 0, 0,  0, 0, 0, 0 },
            {  1,  1, 0,  0, 0, 1,  1, 0, 0, 1,  1, 0,  1, 0, 0, 0,  0, 0, 0, 0 },
        };
        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(quad, 6, static_cast<int>(sizeof(SkinnedPbrGpuVertex)));

        // Identity bind pose is a no-op skin transform, so these are exactly
        // bgfx_pbreffect_test.cpp's own already-verified expected values -- the shared oracle
        // this file's own header comment describes.
        const Color a = renderWith(dev, vb, &whiteTex_, nullptr, 1.0f, 0.0f, Vector3::Zero);
        check(matches(a, Color(79, 79, 79, 255)),
              "(A) rough=1.0 metallic=0.0 white, direct light only", a, "(79,79,79)");

        const Color b = renderWith(dev, vb, &whiteTex_, nullptr, 0.5f, 0.0f, Vector3::Zero);
        check(matches(b, Color(91, 91, 91, 255)),
              "(B) rough=0.5 metallic=0.0 white -- brighter than (A)", b, "(91,91,91)");
        check(b.getRProperty() > a.getRProperty(),
              "(B) differs from (A) -- RoughnessFactor changes the BRDF", b, "> (A)");

        const Color c = renderWith(dev, vb, &redTex_, nullptr, 1.0f, 1.0f, Vector3::Zero);
        check(matches(c, Color(20, 0, 0, 255)),
              "(C) metallic=1.0 red -- diffuse vanishes, thin red specular only", c, "(20,0,0)");

        const Color d = renderWith(dev, vb, &redTex_, nullptr, 1.0f, 0.0f, Vector3::Zero);
        check(matches(d, Color(79, 1, 1, 255)),
              "(D) metallic=0.0 red -- full diffuse + thin specular", d, "(79,1,1)");
        check(!matches(d, c, 8),
              "(D) differs from (C) -- MetallicFactor changes the BRDF", d, "!= (C)");

        const Vector3 ambientE(0.2f, 0.3f, 0.4f);
        const Color e = renderWith(dev, vb, &whiteTex_, &tiltedNormalTex_, 1.0f, 0.0f, ambientE);
        check(matches(e, Color(51, 77, 102, 255)),
              "(E) tilted normal map zeroes direct light -- ambient-only result", e, "(51,77,102)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxSkinnedPbrEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxSkinnedPbrEffectTest game;
    game.Run();
    return game.getResult();
}
