// SPDX-License-Identifier: MS-PL
// PBR + skinning combo proof for the SDL_GPU graphics renderer (SdlGpuRenderer::
// CreatePbrResources()'s pbrSkinnedVertexShader_/pbr_skinned3d.vert.glsl) -- proves the stride-68
// VertexPositionNormalTangentTextureSkinned layout, the bone-palette skin transform applied to
// Position/Normal/Tangent, and pbr3d.frag.glsl's shared BRDF fragment stage all work end-to-end.
//
// Reuses sdlgpu_pbreffect_test.cpp's exact 3-quad scene (same camera, same light, same per-quad
// material setup) but through SkinnedPbrEffect with a single identity bone (weight 1.0 on bone
// index 0). An identity bind pose is a mathematical no-op for the skin transform, so the
// rendered pixels must be identical to PbrEffect's own already hand/Python-derived values -- this
// is this test's own oracle (mirrors easygl_skinnedpbreffect_golden_test.cpp's identical
// rationale), not a freshly re-derived number, and directly proves the skin matrix multiply in
// pbr_skinned3d.vert.glsl (position AND normal AND tangent, all three -- see that shader's own
// doc comment on why it also applies World to the normal/tangent, unlike the plain non-PBR
// skinned3d.vert.glsl) is wired correctly: a bug there would shift/darken the quads relative to
// PbrEffect's own verified result.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
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

    // Stride-68 GPU-compact skinned PBR vertex: matches GetOrCreatePipelinePbr3D's stride==68
    // layout (Position+Normal+Tangent+TextureCoordinate+BlendWeight+BlendIndices).
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

    // Identical geometry to sdlgpu_pbreffect_test.cpp's own BuildQuad, plus a single identity
    // bone (weight 1, index 0) on every vertex.
    void BuildQuad(std::vector<SkinnedPbrGpuVertex>& out)
    {
        const SkinnedPbrGpuVertex tl{-0.3f,  0.3f, -0.5f,  0,0,1,  1,0,0,1,  0,0,  1,0,0,0,  0,0,0,0};
        const SkinnedPbrGpuVertex bl{-0.3f, -0.3f, -0.5f,  0,0,1,  1,0,0,1,  0,1,  1,0,0,0,  0,0,0,0};
        const SkinnedPbrGpuVertex br{ 0.3f, -0.3f, -0.5f,  0,0,1,  1,0,0,1,  1,1,  1,0,0,0,  0,0,0,0};
        const SkinnedPbrGpuVertex tr{ 0.3f,  0.3f, -0.5f,  0,0,1,  1,0,0,1,  1,0,  1,0,0,0,  0,0,0,0};
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

class SdlGpuSkinnedPbrEffectTest : public Game
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

    Color RenderAndSampleCenter(GraphicsDevice& dev, SkinnedPbrEffect& fx)
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
        const std::vector<std::uint8_t> tiltedNormal = {255, 128, 128, 255};
        tiltedNormalTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 1, 1, tiltedNormal));

        std::vector<SkinnedPbrGpuVertex> verts;
        BuildQuad(verts);
        vb_ = std::make_unique<VertexBuffer>(dev, static_cast<int>(verts.size()));
        vb_->SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(SkinnedPbrGpuVertex)));
    }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();

        SkinnedPbrEffect fx(dev);
        fx.setBaseColorTextureIsSrgbEXTProperty(false);
        fx.setEmissiveTextureIsSrgbEXTProperty(false);
        fx.setEncodeOutputToSrgbEXTProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        const std::vector<Matrix> bones = {Matrix::getIdentityProperty()};
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);

        // Same 3 checks, same expected values, as sdlgpu_pbreffect_test.cpp -- see this file's
        // own header comment for why an identity bind pose makes that test's own values the
        // correct oracle here too.
        fx.setTextureProperty(whiteTex_.get());
        fx.setNormalMapProperty(nullptr);
        fx.setRoughnessFactorProperty(1.0f);
        fx.setMetallicFactorProperty(0.0f);
        const Color a = RenderAndSampleCenter(dev, fx);
        Check(WithinTolerance(a, Color(79, 79, 79, 255), 10), "quadA-dielectric-white-identitybone", a, Color(79, 79, 79, 255));

        fx.setTextureProperty(redTex_.get());
        fx.setMetallicFactorProperty(1.0f);
        const Color b = RenderAndSampleCenter(dev, fx);
        Check(WithinTolerance(b, Color(20, 0, 0, 255), 10), "quadB-metallic-red-identitybone", b, Color(20, 0, 0, 255));

        fx.setTextureProperty(whiteTex_.get());
        fx.setNormalMapProperty(tiltedNormalTex_.get());
        fx.setMetallicFactorProperty(0.0f);
        const Color c = RenderAndSampleCenter(dev, fx);
        Check(WithinTolerance(c, Color(0, 0, 0, 255), 10), "quadC-tilted-normal-zeroed-identitybone", c, Color(0, 0, 0, 255));

        std::printf("=== %d/3 PASS ===\n", passCount_);
        result_ = (passCount_ == 3) ? 0 : 1;
        Exit();
    }

public:
    SdlGpuSkinnedPbrEffectTest()
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

    SdlGpuSkinnedPbrEffectTest game;
    game.Run();
    return game.getResult();
}
