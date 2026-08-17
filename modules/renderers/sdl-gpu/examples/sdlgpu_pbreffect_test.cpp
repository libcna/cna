// SPDX-License-Identifier: MS-PL
// PbrEffect proof for the SDL_GPU graphics renderer (SdlGpuRenderer::CreatePbrResources()/
// GetOrCreatePipelinePbr3D(), pbr3d.vert.glsl/pbr3d.frag.glsl) -- proves the stride-48
// VertexPositionNormalTangentTexture layout, TBN construction, and the metallic-roughness BRDF
// itself all work end-to-end via a real GPU draw.
//
// Unlike easygl_pbreffect_golden_test.cpp's own scene (View=Identity places the eye exactly
// inside the quad's own z=0 plane, a genuinely degenerate grazing-angle view direction that
// renderer's own header comment says makes exact hand-derivation "impractical"), this test moves
// the quad to z=-0.5 while keeping View=Identity (eye at object-space origin) and points
// DirectionalLight0 straight down -Z -- N=V=L=H=(0,0,1) exactly at the sampled center pixel, a
// fully analytic case. Expected values below are independently reproduced by a standalone Python
// re-implementation of this exact BRDF formula (see this task's own PR notes), not just
// "run once and paste":
//   Quad A: white albedo, fully rough (Roughness=1), non-metallic (Metallic=0), default (flat)
//     normal map -> (79,79,79,255).
//   Quad B: red albedo, fully metallic (Metallic=1), same roughness -> (20,0,0,255) (metallic's
//     BRDF diffuse term is exactly zero; only a red-tinted specular lobe plus zero ambient
//     remain), proving MetallicFactor genuinely changes the BRDF, not just the base color.
//   Quad C: same material as A, but with an explicit normal map tilting the tangent-space normal
//     ~90 degrees so the perturbed world normal is (numerically) perpendicular to the light
//     direction -> NdotL collapses to ~0, rendering as (0,0,0,255) -- proves the normal map is
//     actually sampled and perturbs the lighting response, not silently ignored.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRTSize = 64;

    // Stride-48 GPU-compact PBR vertex: matches GetOrCreatePipelinePbr3D's stride==48 layout
    // (Position+Normal+Tangent+TextureCoordinate).
    struct PbrGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrGpuVertex) == 48, "PBR vertex must be 48 bytes");

    // A small quad at z=-0.5 (in front of the object-space-origin eye along +Z), Normal hardcoded
    // to (0,0,1) and Tangent to (1,0,0,1) regardless of geometry -- these are independent vertex
    // attributes, not derived from position, exactly matching how this test's BRDF derivation
    // treats them.
    void BuildQuad(std::vector<PbrGpuVertex>& out)
    {
        const PbrGpuVertex tl{-0.3f,  0.3f, -0.5f,  0,0,1,  1,0,0,1,  0,0};
        const PbrGpuVertex bl{-0.3f, -0.3f, -0.5f,  0,0,1,  1,0,0,1,  0,1};
        const PbrGpuVertex br{ 0.3f, -0.3f, -0.5f,  0,0,1,  1,0,0,1,  1,1};
        const PbrGpuVertex tr{ 0.3f,  0.3f, -0.5f,  0,0,1,  1,0,0,1,  1,0};
        out.push_back(tl); out.push_back(bl); out.push_back(br);
        out.push_back(tl); out.push_back(br); out.push_back(tr);
    }

    bool WithinTolerance(const Color& actual, const Color& expected, int tolerance)
    {
        return std::abs(static_cast<int>(actual.getRProperty()) - static_cast<int>(expected.getRProperty())) <= tolerance
            && std::abs(static_cast<int>(actual.getGProperty()) - static_cast<int>(expected.getGProperty())) <= tolerance
            && std::abs(static_cast<int>(actual.getBProperty()) - static_cast<int>(expected.getBProperty())) <= tolerance
            && std::abs(static_cast<int>(actual.getAProperty()) - static_cast<int>(expected.getAProperty())) <= tolerance;
    }
}

class SdlGpuPbrEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> rt_;
    std::unique_ptr<Texture2D> whiteTex_;
    std::unique_ptr<Texture2D> redTex_;
    std::unique_ptr<Texture2D> tiltedNormalTex_;
    std::unique_ptr<VertexBuffer> vb_;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label, const Color& actual, const Color& expected)
    {
        std::printf("[%s] %s: actual=(%d,%d,%d,%d) expected=(%d,%d,%d,%d)\n",
                    ok ? "PASS" : "FAIL", label,
                    actual.getRProperty(), actual.getGProperty(), actual.getBProperty(), actual.getAProperty(),
                    expected.getRProperty(), expected.getGProperty(), expected.getBProperty(), expected.getAProperty());
        if (ok) ++passCount_;
    }

    Color RenderAndSampleCenter(GraphicsDevice& dev, PbrEffect& fx)
    {
        dev.SetRenderTarget(rt_.get());
        dev.Clear(Color(0, 255, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetVertexBuffer(vb_.get());
        fx.Apply();
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const Rectangle region(kRTSize / 2, kRTSize / 2, 1, 1);
        Color out(0, 0, 0, 0);
        rt_->GetData(0, &region, &out, 0, 1);
        return out;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        rt_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);

        const std::vector<std::uint8_t> white = {255, 255, 255, 255};
        whiteTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 1, 1, white));
        const std::vector<std::uint8_t> red = {255, 0, 0, 255};
        redTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 1, 1, red));
        // Tangent-space normal tilted ~90 degrees toward +X: encoded (255,128,128) decodes to
        // ~(1,0,0) after the shader's rgb*2-1 -- same encoding easygl_pbreffect_golden_test.cpp
        // already established for this exact purpose.
        const std::vector<std::uint8_t> tiltedNormal = {255, 128, 128, 255};
        tiltedNormalTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 1, 1, tiltedNormal));

        std::vector<PbrGpuVertex> verts;
        BuildQuad(verts);
        vb_ = std::make_unique<VertexBuffer>(dev, static_cast<int>(verts.size()));
        vb_->SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(PbrGpuVertex)));
    }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();

        PbrEffect fx(dev);
        fx.setBaseColorTextureIsSrgbEXTProperty(false);
        fx.setEmissiveTextureIsSrgbEXTProperty(false);
        fx.setEncodeOutputToSrgbEXTProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        // Straight-on key light: Direction=(0,0,-1) means light travels toward -Z, so the
        // surface-to-light vector L=-Direction=(0,0,1) exactly matches the quad's (0,0,1) normal
        // and the (0,0,1) eye-to-surface view direction this scene's geometry produces --
        // N=V=L=H=(0,0,1), the fully analytic case this test's header comment derives by hand.
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        // DirectionalLight1/2 default Enabled=false; AmbientLightColor/EmissiveFactor default to
        // Vector3::Zero -- left untouched so this test's own light math has no hidden extra terms.

        // Quad A: white, fully rough, non-metallic, default (flat) normal.
        fx.setTextureProperty(whiteTex_.get());
        fx.setNormalMapProperty(nullptr);
        fx.setRoughnessFactorProperty(1.0f);
        fx.setMetallicFactorProperty(0.0f);
        const Color a = RenderAndSampleCenter(dev, fx);
        Check(WithinTolerance(a, Color(79, 79, 79, 255), 10), "quadA-dielectric-white", a, Color(79, 79, 79, 255));

        // Quad B: red, fully metallic, same roughness, default (flat) normal.
        fx.setTextureProperty(redTex_.get());
        fx.setMetallicFactorProperty(1.0f);
        const Color b = RenderAndSampleCenter(dev, fx);
        Check(WithinTolerance(b, Color(20, 0, 0, 255), 10), "quadB-metallic-red", b, Color(20, 0, 0, 255));

        // Quad C: same as A, but with a normal map tilting the perturbed world normal to
        // (numerically) perpendicular to the light direction -- NdotL collapses to ~0.
        fx.setTextureProperty(whiteTex_.get());
        fx.setNormalMapProperty(tiltedNormalTex_.get());
        fx.setMetallicFactorProperty(0.0f);
        const Color c = RenderAndSampleCenter(dev, fx);
        Check(WithinTolerance(c, Color(0, 0, 0, 255), 10), "quadC-tilted-normal-zeroed", c, Color(0, 0, 0, 255));

        std::printf("=== %d/3 PASS ===\n", passCount_);
        result_ = (passCount_ == 3) ? 0 : 1;
        Exit();
    }

public:
    SdlGpuPbrEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuPbrEffectTest game;
    game.Run();
    return game.getResult();
}
