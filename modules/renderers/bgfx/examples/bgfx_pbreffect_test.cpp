// SPDX-License-Identifier: MS-PL
// plan_cnj.md CNB-58/60 (Phase 13A), Bgfx port: pixel test for PbrEffect's real glTF
// metallic-roughness BRDF shader (BgfxRenderer's vs_pbr3d.sc/fs_pbr3d.sc) -- proves the
// stride-48 VertexPositionNormalTangentTexture layout, the TBN construction, and the BRDF math
// itself all work end-to-end via a real GPU draw on the Bgfx renderer.
//
// Unlike examples/easygl_pbreffect_golden_test.cpp (a 4-quad multi-material scene whose own
// header comment explains why its exact expected values had to be captured-and-confirmed rather
// than purely hand-derived -- the shared eye/light rig there puts the sample point at a grazing
// view angle), this test uses a fully analytic setup: eye at (0,0,3) via CreateLookAt looking
// straight down -Z at a flat quad's z=0 plane, a single directional light also shining straight
// down -Z, a flat (unperturbed) surface normal (0,0,1) -- so N, V, L, H all coincide exactly at
// the sampled screen-center pixel (NdotL=NdotV=NdotH=VdotH=1). Every expected byte value below is
// independently reproduced by a small standalone Python re-implementation of this exact BRDF
// formula (GGX distribution + Smith-Schlick-GGX visibility + Schlick Fresnel, same as
// EnsurePbrProgram()'s PbrLight()) -- not captured from a single run and pasted.
//
// Per Task 364/884's finding: Bgfx's default RasterizerState cull state culls the standard NDC
// quad winding used throughout this pixel-test family unless RasterizerState::CullNone is set
// explicitly.
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
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
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

// Stride-48 GPU-compact PBR vertex: matches BgfxRenderer's MakeBgfxLayout stride==48 case
// (Position+Normal+Tangent+TextureCoordinate).
struct PbrGpuVertex
{
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz, tw;
    float u, v;
};
static_assert(sizeof(PbrGpuVertex) == 48, "PBR vertex must be 48 bytes");

class BgfxPbrEffectTest : public Game
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

    // Bgfx's readback only reliably reflects a single fresh GetBackBufferData() call per
    // rendered frame (Task 406 finding) -- each check gets its own full clear+draw+read pass.
    Color renderWith(GraphicsDevice& dev, VertexBuffer& vb, Texture2D* tex, Texture2D* normalMap,
                      float roughness, float metallic, const Vector3& ambient)
    {
        PbrEffect fx(dev);
        fx.setBaseColorTextureIsSrgbEXTProperty(false);
        fx.setEmissiveTextureIsSrgbEXTProperty(false);
        fx.setEncodeOutputToSrgbEXTProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
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
            // See Task 364/884 finding: Bgfx's default cull state culls this quad's winding.
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
        // Tangent-space normal tilted ~90 degrees toward +X: encoded (255,128,128) decodes to
        // ~(1,0,0) after the shader's rgb*2-1 (matches easygl_pbreffect_golden_test.cpp's own
        // encoding).
        const std::vector<uint8_t> tilted = { 255, 128, 128, 255 };
        tiltedNormalTex_ = Texture2D::CreateFromPixels(dev, 1, 1, tilted);
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        // Quad spans [-1,1]x[-1,1] at z=0, facing +Z, tangent=(1,0,0,+1) -- with World=Identity
        // and the CreateLookAt/CreatePerspectiveFieldOfView camera above, world (0,0,0) (the
        // look-at target) maps exactly to the screen-center pixel sampled below.
        const PbrGpuVertex quad[6] = {
            { -1,  1, 0,  0, 0, 1,  1, 0, 0, 1,  0, 0 },
            { -1, -1, 0,  0, 0, 1,  1, 0, 0, 1,  0, 1 },
            {  1, -1, 0,  0, 0, 1,  1, 0, 0, 1,  1, 1 },
            { -1,  1, 0,  0, 0, 1,  1, 0, 0, 1,  0, 0 },
            {  1, -1, 0,  0, 0, 1,  1, 0, 0, 1,  1, 1 },
            {  1,  1, 0,  0, 0, 1,  1, 0, 0, 1,  1, 0 },
        };
        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(quad, 6, static_cast<int>(sizeof(PbrGpuVertex)));

        // Check A: fully rough (RoughnessFactor=1.0), non-metallic, white albedo, ambient=0.
        // Hand-derived (glTF BRDF, NdotL=NdotV=NdotH=VdotH=1): Lo = kd*albedo/pi + specular =
        // 0.96/pi + 0.003183 = 0.308761 -> byte 79.
        const Color a = renderWith(dev, vb, &whiteTex_, nullptr, 1.0f, 0.0f, Vector3::Zero);
        check(matches(a, Color(79, 79, 79, 255)),
              "(A) rough=1.0 metallic=0.0 white, direct light only", a, "(79,79,79)");

        // Check B: RoughnessFactor=0.5 (still non-metallic) -- hand-derived value 0.356507 ->
        // byte 91, strictly brighter than (A), proving RoughnessFactor is actually read (not a
        // hardcoded constant).
        const Color b = renderWith(dev, vb, &whiteTex_, nullptr, 0.5f, 0.0f, Vector3::Zero);
        check(matches(b, Color(91, 91, 91, 255)),
              "(B) rough=0.5 metallic=0.0 white -- brighter than (A)", b, "(91,91,91)");
        check(b.getRProperty() > a.getRProperty(),
              "(B) differs from (A) -- RoughnessFactor changes the BRDF", b, "> (A)");

        // Check C: MetallicFactor=1.0, red albedo -- diffuseColor term vanishes
        // (albedo*(1-metallic)=0), only a red-tinted (F0=albedo=(1,0,0)) specular lobe remains.
        // Hand-derived: Lo.r = 0.079577 -> byte 20, Lo.g=Lo.b=0.
        const Color c = renderWith(dev, vb, &redTex_, nullptr, 1.0f, 1.0f, Vector3::Zero);
        check(matches(c, Color(20, 0, 0, 255)),
              "(C) metallic=1.0 red -- diffuse vanishes, thin red specular only", c, "(20,0,0)");

        // Check D: MetallicFactor=0.0, red albedo -- full Lambertian diffuse plus a thin
        // achromatic F0=0.04 specular. Hand-derived: Lo.r=0.308761 -> byte 79,
        // Lo.g=Lo.b=0.003183 -> byte 1.
        const Color d = renderWith(dev, vb, &redTex_, nullptr, 1.0f, 0.0f, Vector3::Zero);
        check(matches(d, Color(79, 1, 1, 255)),
              "(D) metallic=0.0 red -- full diffuse + thin specular", d, "(79,1,1)");
        check(!matches(d, c, 8),
              "(D) differs from (C) -- MetallicFactor changes the BRDF", d, "!= (C)");

        // Check E: tilted normal map. This quad's tangent=(1,0,0)/normal=(0,0,1) makes the TBN
        // basis the identity matrix, so the sampled tangent-space normal (1,0,0) becomes the
        // world-space finalNormal directly -- NdotL=NdotV~=0, zeroing the direct-light term
        // exactly (up to 8-bit texture quantization, hand-derived at 0.00123/channel, negligible
        // against the tolerance below). A nonzero AmbientLightColor still lights the surface via
        // the ambient term alone (ambientColor*albedo*occlusion, occlusion=1 with no bound
        // occlusion map): hand-derived byte (51,77,102). Proves the normal map is genuinely
        // sampled and perturbs N (not silently ignored), independent of the ambient term.
        const Vector3 ambientE(0.2f, 0.3f, 0.4f);
        const Color e = renderWith(dev, vb, &whiteTex_, &tiltedNormalTex_, 1.0f, 0.0f, ambientE);
        check(matches(e, Color(51, 77, 102, 255)),
              "(E) tilted normal map zeroes direct light -- ambient-only result", e, "(51,77,102)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxPbrEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxPbrEffectTest game;
    game.Run();
    return game.getResult();
}
