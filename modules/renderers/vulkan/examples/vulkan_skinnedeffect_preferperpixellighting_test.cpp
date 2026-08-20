// SPDX-License-Identifier: MS-PL
// Task 1103 (plans/plan_graphics.md Phase 80 / plans/plan_dx9.md Divergence 1): SkinnedEffect pixel test --
// PreferPerPixelLighting genuinely selects between two different lighting evaluations (Vulkan
// renderer), mirroring the BasicEffect test exactly.
//
// Real XNA 4.0 default: PreferPerPixelLighting=false -> lighting is computed ONCE per vertex
// (VSSkinnedVertexLighting*) and Gouraud-interpolated across the triangle. true -> lighting is
// re-evaluated per fragment. Before this task, this renderer's SkinnedEffect path always
// evaluated per pixel regardless of this flag's value (the same gap this task already fixed for
// BasicEffect).
//
// Reuses the EXACT scene from vulkan_skinnedeffect_specular_test.cpp's own "(a) eye straight on"
// case: same 2-triangle quad, same shared vertex normal, same light/material setup, same centre-
// pixel sample point (sitting exactly on the diagonal seam between the two triangles). A single
// Identity bone at 100% weight keeps skinning a mathematical no-op, isolating the lighting-mode
// difference under test (same convention this project's own D9-A5 oracle corpus and
// vulkan_skinnedeffect_specular_test.cpp already establish).
//
// vulkan_skinnedeffect_specular_test.cpp's own "(a) eye straight on" case measured 125 (vertex-
// lit, this renderer's real render) vs 155 (pixel-lit) for this identical scene -- confirmed
// against the actual render, not assumed by analogy to BasicEffect (which measured 127 for the
// same math on this renderer, a small, expected floating-point/interpolation difference between
// the two vertex formats' own shader variants).
//
// 3 checks:
//   (a) Default (PreferPerPixelLighting left at its real XNA default, false): expect the
//       vertex-lit/Gouraud value (~125).
//   (b) PreferPerPixelLighting=true: expect the pixel-lit value (~155) -- the OLD, pre-Task-1103
//       value this renderer always produced regardless of the flag.
//   (c) (a) != (b): proves the flag is a genuine, live dispatch selector, not a decorative no-op.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
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
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

// GPU-compact skinned vertex: matches this renderer's stride-52 layout (same convention
// vulkan_skinnedeffect_specular_test.cpp already established).
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
static const Vector3 kAmbient(0.02f, 0.02f, 0.02f);
static const Vector3 kMaterialDiffuse(0.4f, 0.4f, 0.4f);
static const Vector3 kLightDiffuse(0.5f, 0.5f, 0.5f);
static const Vector3 kLightSpecular(1.0f, 1.0f, 1.0f);
static const Vector3 kSpecularColor(1.0f, 1.0f, 1.0f);
static const Vector3 kZero(0.0f, 0.0f, 0.0f);
static constexpr float kSpecularPower = 32.0f;
static const Vector3 kLightDirRaw(0.5f, 0.0f, -1.0f);
static const float kNx = 0.0f, kNy = 0.0f, kNz = 1.0f;
static const Vector3 kEyeStraightOn(0.0f, 0.0f, 3.0f);

// Same values as vulkan_skinnedeffect_specular_test.cpp's own case (a), confirmed against this
// renderer's real render (Task 1103).
static const Color kExpectedVertexLit(125, 125, 125, 255);
static const Color kExpectedPixelLit(155, 155, 155, 255);

class VulkanSkinnedEffectPreferPerPixelLightingTest : public Game
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
        return closeTo(c.getRProperty(), expected.getRProperty(), 10)
            && closeTo(c.getGProperty(), expected.getGProperty(), 10)
            && closeTo(c.getBProperty(), expected.getBProperty(), 10);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    Color renderWith(GraphicsDevice& dev, Texture2D& tex, bool preferPerPixelLighting)
    {
        Vector3 lightDir = kLightDirRaw;
        lightDir.Normalize();

        SkinnedEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setPreferPerPixelLightingProperty(preferPerPixelLighting);
        fx.setAmbientLightColorProperty(kAmbient);
        fx.setDiffuseColorProperty(kMaterialDiffuse);
        fx.setEmissiveColorProperty(kZero);
        fx.setSpecularColorProperty(kSpecularColor);
        fx.setSpecularPowerProperty(kSpecularPower);

        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(lightDir);
        fx.DirectionalLight0.setDiffuseColorProperty(kLightDiffuse);
        fx.DirectionalLight0.setSpecularColorProperty(kLightSpecular);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);

        // Single Identity bone at 100% weight -- skinning is a mathematical no-op, isolating the
        // lighting-mode difference under test.
        fx.SetBoneTransforms(std::vector<Matrix>{ Matrix::getIdentityProperty() });

        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(kEyeStraightOn, Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
        fx.Apply();

        const SkinnedGpuVertex verts[6] = {
            { -1,  1, 0,  kNx,kNy,kNz,  0,1,  1,0,0,0,  0,0,0,0 },
            { -1, -1, 0,  kNx,kNy,kNz,  0,0,  1,0,0,0,  0,0,0,0 },
            {  1, -1, 0,  kNx,kNy,kNz,  1,0,  1,0,0,0,  0,0,0,0 },
            { -1,  1, 0,  kNx,kNy,kNz,  0,1,  1,0,0,0,  0,0,0,0 },
            {  1, -1, 0,  kNx,kNy,kNz,  1,0,  1,0,0,0,  0,0,0,0 },
            {  1,  1, 0,  kNx,kNy,kNz,  1,1,  1,0,0,0,  0,0,0,0 },
        };
        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(verts, 6, static_cast<int>(sizeof(SkinnedGpuVertex)));

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
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

        const Color vertexLit = renderWith(dev, tex, false);
        check(matches(vertexLit, kExpectedVertexLit),
              "(a) PreferPerPixelLighting=false (XNA's real default): Gouraud-interpolated specular",
              vertexLit, "(125,125,125)");

        const Color pixelLit = renderWith(dev, tex, true);
        check(matches(pixelLit, kExpectedPixelLit),
              "(b) PreferPerPixelLighting=true: fresh per-fragment specular",
              pixelLit, "(155,155,155)");

        check(!matches(vertexLit, pixelLit),
              "(c) (a) differs from (b) -- PreferPerPixelLighting is a real dispatch selector",
              vertexLit, "!= (155,155,155)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VulkanSkinnedEffectPreferPerPixelLightingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanSkinnedEffectPreferPerPixelLightingTest game;
    game.Run();
    return game.getResult();
}
