// SPDX-License-Identifier: MS-PL
// Task 1105 (plans/plan_graphics.md Phase 80): WebGPURenderer real per-vertex-lit shader
// (GetOrCreatePipelineLitTextured3DVertexLit) + BasicEffect.PreferPerPixelLighting dispatch.
//
// Same discriminating scene as EasyGL's/Vulkan's/Bgfx's own PreferPerPixelLighting tests
// (examples/easygl_basiceffect_specular_test.cpp "(a) Eye straight on" case): a quad at the
// origin with ONE shared normal (0,0,1) -- this makes diffuse spatially constant across the quad
// (N.L is the same everywhere for a flat surface + one directional light), but leaves specular
// genuinely position-dependent (it depends on the view vector, which varies across the quad even
// with a single shared normal). The centre pixel sits exactly on this quad's diagonal seam
// between its two triangles, so:
//   - PreferPerPixelLighting=false (XNA's real default, vertex-lit/Gouraud): the centre reads the
//     INTERPOLATED AVERAGE of the two seam vertices' own independently-computed specular terms.
//   - PreferPerPixelLighting=true (pixel-lit): the centre reads a FRESH per-fragment evaluation
//     at that exact point instead.
// These are provably different values, not the same math evaluated twice. Real XNA/FNA has no
// PreferPerPixelLighting support on this experimental renderer to compare pixel-for-pixel here
// (this renderer has no oracle harness) -- the check is that dispatch genuinely selects a
// different, real shader per the flag, matching the same discriminating-scene technique already
// proven on 3 other renderers, not that the numeric result matches another renderer's raw value:
// REMED-GFX-131 made this renderer's readback directly comparable to the other three: its render
// targets and backbuffer now use a non-sRGB format, matching SurfaceFormat::Color's plain UNORM
// byte semantics, so the values asserted below are the SAME linear numbers EasyGL/Vulkan/Bgfx read
// for this identical scene rather than their gamma-encoded counterparts.
//
// 3 checks:
//   (a) PreferPerPixelLighting=false: vertex-lit Gouraud-averaged result.
//   (b) PreferPerPixelLighting=true: pixel-lit fresh-per-fragment result.
//   (c) (a) and (b) differ -- proves the two dispatch paths are genuinely different, live shaders,
//       not one silently always winning regardless of the flag.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    const Vector3 kAmbient(0.02f, 0.02f, 0.02f);
    const Vector3 kMaterialDiffuse(0.4f, 0.4f, 0.4f);
    const Vector3 kLightDiffuse(0.5f, 0.5f, 0.5f);
    const Vector3 kLightSpecular(1.0f, 1.0f, 1.0f);
    const Vector3 kSpecularColor(1.0f, 1.0f, 1.0f);
    const Vector3 kNormal(0.0f, 0.0f, 1.0f);
    const Vector3 kEyeStraightOn(0.0f, 0.0f, 3.0f);
    constexpr float kSpecularPower = 32.0f;

    bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    bool matches(const Color& c, int r, int g, int b, int tol = 10)
    {
        return closeTo(c.getRProperty(), r, tol)
            && closeTo(c.getGProperty(), g, tol)
            && closeTo(c.getBProperty(), b, tol);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    VertexBuffer MakeQuad(GraphicsDevice& dev)
    {
        VertexBuffer vb(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        const VertexPositionNormalTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), kNormal, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), kNormal, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), kNormal, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), kNormal, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), kNormal, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), kNormal, Vector2(1.0f, 1.0f) },
        };
        vb.SetData(verts, 0, 6);
        return vb;
    }
}

class WebGpuBasicEffectPreferPerPixelLightingTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    Color renderWith(GraphicsDevice& dev, bool preferPerPixelLighting)
    {
        VertexBuffer vb = MakeQuad(dev);
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&whiteTex_);
        fx.setLightingEnabledProperty(true);
        fx.setPreferPerPixelLightingProperty(preferPerPixelLighting);
        fx.setAmbientLightColorProperty(kAmbient);
        fx.setDiffuseColorProperty(kMaterialDiffuse);
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setSpecularColorProperty(kSpecularColor);
        fx.setSpecularPowerProperty(kSpecularPower);

        Vector3 lightDir(0.5f, 0.0f, -1.0f);
        lightDir.Normalize();
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(lightDir);
        fx.DirectionalLight0.setDiffuseColorProperty(kLightDiffuse);
        fx.DirectionalLight0.setSpecularColorProperty(kLightSpecular);

        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(kEyeStraightOn, Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));

        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        return readCenter(dev);
    }

protected:
    void LoadContent() override
    {
        whiteTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                 std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // REMED-GFX-131: the expected values are now the raw linear ~127/~152 that EasyGL, Vulkan
        // and Bgfx read for this identical scene. They used to be 187/203 -- the sRGB encode of
        // exactly those numbers -- because this renderer rendered into an *UnormSrgb attachment.
        // Measured post-fix as 127/152, so all four renderers agree on the same shader math.
        const Color vertexLit = renderWith(dev, false);
        std::printf("[INFO] vertex-lit (PreferPerPixelLighting=false) centre = (%d,%d,%d)\n",
                    vertexLit.getRProperty(), vertexLit.getGProperty(), vertexLit.getBProperty());
        check(matches(vertexLit, 127, 127, 127),
              "(a) PreferPerPixelLighting=false renders the Gouraud-averaged vertex-lit result");

        const Color pixelLit = renderWith(dev, true);
        std::printf("[INFO] pixel-lit (PreferPerPixelLighting=true) centre = (%d,%d,%d)\n",
                    pixelLit.getRProperty(), pixelLit.getGProperty(), pixelLit.getBProperty());
        check(matches(pixelLit, 152, 152, 152),
              "(b) PreferPerPixelLighting=true renders the fresh per-fragment pixel-lit result");

        check(!matches(vertexLit, pixelLit.getRProperty(), pixelLit.getGProperty(), pixelLit.getBProperty(), 3),
              "(c) vertex-lit and pixel-lit results genuinely differ");

        std::printf("=== %d/3 PASS ===\n", passCount_);
        result_ = (passCount_ == 3) ? 0 : 1;
        Exit();
    }

public:
    WebGpuBasicEffectPreferPerPixelLightingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuBasicEffectPreferPerPixelLightingTest game;
    game.Run();
    return game.getResult();
}
