// SPDX-License-Identifier: MS-PL
// REMED-GFX-008: analytic SkinnedEffect ambient/emissive lighting conformance harness.
//
// Backend-agnostic (registered per backend by the CMake test files). Isolates each individual
// lighting term of FNA's SkinnedEffect lighting model so an incorrect ambient/emissive
// consumption is numerically distinguishable from the correct one.
//
// The FNA-correct final lit RGB (lighting enabled), per channel, is:
//
//     litRGB = (Σ_i lightᵢDiffuse · max(0, N·(−Lᵢ))) · DiffuseColor  +  EmissiveColor
//
// where DiffuseColor.rgb = diffuse·alpha and EmissiveColor = (emissive + ambient·diffuse)·alpha are
// the CPU pre-folded uniforms (EffectHelpers.SetMaterialColor). Equivalently:
//
//     litRGB = ((ambient + Σ lightᵢ·max(0,N·L)) · diffuse + emissive) · alpha
//
// The historical backend bug reads an always-zero `ambientColor` for the ambient term AND never adds
// the pre-folded emissive uniform, so it silently drops BOTH ambient and emissive. Every case below
// is chosen so that bug family renders a numerically different (usually much darker or black) pixel.
//
// Geometry: one identity-bone quad whose normal faces +Z; DirectionalLight0 points down −Z, so
// N·(−L0) = 1 exactly. Lights 1/2 are disabled (they contribute nothing) unless a case needs them.
// Identity World/View/Proj, white 1×1 texture, opaque blend, CullNone. Backbuffer readback is linear
// on every backend this test is registered for (matches the established Vulkan/EasyGL/SdlGpu/Bgfx/
// D3D11 skinned tests, which assert linear 0.3→77 etc.).
//
// Exit code 0 = PASS (all cases), 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

// GPU-compact skinned vertex: matches the EasyGL stride-52 layout used by the other skinned tests.
struct SkinnedGpuVertex
{
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float w0, w1, w2, w3;
    uint8_t i0, i1, i2, i3;
};
static_assert(sizeof(SkinnedGpuVertex) == 52, "skinned vertex must be 52 bytes");

static const Color kWhite(255, 255, 255, 255);
// Normal faces +Z (toward the camera); Light0 shines down −Z so N·(−L0) = 1.
static const float kNx = 0.0f, kNy = 0.0f, kNz = 1.0f;
static const Vector3 kLight0Dir(0.0f, 0.0f, -1.0f);

struct Case
{
    const char* name;
    Vector3 diffuse;
    float   alpha;
    Vector3 ambient;
    Vector3 emissive;
    bool    light0On;
    Vector3 light0Diffuse;
    Color   expectedCorrect; // FNA-correct pixel (what this harness asserts)
    Color   expectedOldBug;  // what the pre-fix ambient/emissive-dropping bug produced (documentation)
};

// Each case: correct = ((ambient + light0·1)·diffuse + emissive)·alpha ; oldBug drops ambient+emissive.
static const Case kCases[] = {
    // A. diffuse + directional only (control; both formulas agree — proves the rig lights at all).
    {"A diffuse+light",        Vector3(0.5f,0.4f,0.3f), 1.0f, Vector3(0,0,0),        Vector3(0,0,0),
        true,  Vector3(1,1,1),  Color(128,102, 76,255), Color(128,102, 76,255)},
    // B. ambient only (light off). Correct = ambient·diffuse; old bug = black.
    {"B ambient only",         Vector3(0.5f,0.5f,0.5f), 1.0f, Vector3(0.6f,0.6f,0.6f), Vector3(0,0,0),
        true,  Vector3(0,0,0),  Color( 77, 77, 77,255), Color(  0,  0,  0,255)},
    // C. emissive only (light off), diffuse≠1 to prove emissive is added RAW (102) not ·diffuse (51).
    {"C emissive only",        Vector3(0.5f,0.5f,0.5f), 1.0f, Vector3(0,0,0),        Vector3(0.4f,0.3f,0.2f),
        true,  Vector3(0,0,0),  Color(102, 77, 51,255), Color(  0,  0,  0,255)},
    // D. ambient + directional. Correct = (0.3+0.5)·0.8 = 0.64; old bug = 0.5·0.8 = 0.4.
    {"D ambient+directional",  Vector3(0.8f,0.8f,0.8f), 1.0f, Vector3(0.3f,0.3f,0.3f), Vector3(0,0,0),
        true,  Vector3(0.5f,0.5f,0.5f), Color(163,163,163,255), Color(102,102,102,255)},
    // E. emissive + directional. Correct = 0.5·0.6 + 0.2 = 0.5; old bug = 0.3.
    {"E emissive+directional", Vector3(0.6f,0.6f,0.6f), 1.0f, Vector3(0,0,0),        Vector3(0.2f,0.2f,0.2f),
        true,  Vector3(0.5f,0.5f,0.5f), Color(128,128,128,255), Color( 77, 77, 77,255)},
    // F. ambient + emissive (light off). Correct = 0.4·0.5 + 0.15 = 0.35; old bug = black.
    {"F ambient+emissive",     Vector3(0.5f,0.5f,0.5f), 1.0f, Vector3(0.4f,0.4f,0.4f), Vector3(0.15f,0.15f,0.15f),
        true,  Vector3(0,0,0),  Color( 89, 89, 89,255), Color(  0,  0,  0,255)},
    // G. ambient + emissive + directional. Correct = (0.2+0.5)·0.6 + 0.1 = 0.52; old bug = 0.3.
    {"G ambient+emissive+dir",  Vector3(0.6f,0.6f,0.6f), 1.0f, Vector3(0.2f,0.2f,0.2f), Vector3(0.1f,0.1f,0.1f),
        true,  Vector3(0.5f,0.5f,0.5f), Color(133,133,133,255), Color( 77, 77, 77,255)},
    // H. per-channel independence. R=ambient only, G=emissive only, B=directional only.
    //    Correct: R=(0.5)·0.6=0.3→77 ; G=0.3(emissive)→77 ; B=(0.8)·0.6=0.48→122.
    //    Old bug: R=0 ; G=0 ; B=0.48→122 (blue survives because it is pure directional).
    {"H per-channel",          Vector3(0.6f,0.6f,0.6f), 1.0f, Vector3(0.5f,0.0f,0.0f), Vector3(0.0f,0.3f,0.0f),
        true,  Vector3(0.0f,0.0f,0.8f), Color( 77, 77,122,255), Color(  0,  0,122,255)},
    // I. alpha premultiply on the emissive path. α=0.5, emissive 0.8 → (0.8)·0.5 = 0.4 → 102.
    {"I alpha·emissive",       Vector3(1,1,1),          0.5f, Vector3(0,0,0),        Vector3(0.8f,0.8f,0.8f),
        true,  Vector3(0,0,0),  Color(102,102,102,255), Color(  0,  0,  0,255)},
};

class SkinnedEffectLightingConformanceTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0, fail_ = 0;

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    static bool matches(const Color& c, const Color& e, int tol)
    {
        return closeTo(c.getRProperty(), e.getRProperty(), tol)
            && closeTo(c.getGProperty(), e.getGProperty(), tol)
            && closeTo(c.getBProperty(), e.getBProperty(), tol);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    Color renderCase(GraphicsDevice& dev, Texture2D& tex, const Case& c)
    {
        SkinnedEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(&tex);
        fx.setDiffuseColorProperty(c.diffuse);
        fx.setAlphaProperty(c.alpha);
        fx.setAmbientLightColorProperty(c.ambient);
        fx.setEmissiveColorProperty(c.emissive);
        fx.setSpecularColorProperty(Vector3(0, 0, 0)); // no specular confound

        fx.DirectionalLight0.setEnabledProperty(c.light0On);
        fx.DirectionalLight0.setDirectionProperty(kLight0Dir);
        fx.DirectionalLight0.setDiffuseColorProperty(c.light0Diffuse);
        fx.DirectionalLight0.setSpecularColorProperty(Vector3(0, 0, 0));
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.Apply();

        const SkinnedGpuVertex verts[6] = {
            { -1,  1, 0,  kNx,kNy,kNz,  0,0,  1,0,0,0,  0,0,0,0 },
            { -1, -1, 0,  kNx,kNy,kNz,  0,1,  1,0,0,0,  0,0,0,0 },
            {  1, -1, 0,  kNx,kNy,kNz,  1,1,  1,0,0,0,  0,0,0,0 },
            { -1,  1, 0,  kNx,kNy,kNz,  0,0,  1,0,0,0,  0,0,0,0 },
            {  1, -1, 0,  kNx,kNy,kNz,  1,1,  1,0,0,0,  0,0,0,0 },
            {  1,  1, 0,  kNx,kNy,kNz,  1,0,  1,0,0,0,  0,0,0,0 },
        };
        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(verts, 6, static_cast<int>(sizeof(SkinnedGpuVertex)));

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            got = readCenter(dev);
            // Cases B/C/F/I legitimately render black on a buggy backend; accept the frame once the
            // clear/draw cycle has settled (2 warm-up frames), do not spin waiting for non-black.
            if (i >= 2) break;
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);

        for (const auto& c : kCases)
        {
            const Color got = renderCase(dev, tex, c);
            const bool ok = matches(got, c.expectedCorrect, 8);
            std::printf("[%s] %-24s got=(%3d,%3d,%3d) expectedCorrect=(%3d,%3d,%3d) oldBug=(%3d,%3d,%3d)\n",
                        ok ? "PASS" : "FAIL", c.name,
                        got.getRProperty(), got.getGProperty(), got.getBProperty(),
                        c.expectedCorrect.getRProperty(), c.expectedCorrect.getGProperty(), c.expectedCorrect.getBProperty(),
                        c.expectedOldBug.getRProperty(), c.expectedOldBug.getGProperty(), c.expectedOldBug.getBProperty());
            if (ok) ++pass_; else ++fail_;
        }

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    SkinnedEffectLightingConformanceTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    SkinnedEffectLightingConformanceTest game;
    game.Run();
    return game.getResult();
}
