// SPDX-License-Identifier: MS-PL
// WEBGPU-148/149: SkinnedEffect fog on the WebGPU renderer. FNA skins the vertex position BEFORE
// computing the fog factor, so the WGSL dots the SKINNED view-space position with FogVector, and the
// fragment lerps toward FogColor * OUTPUT ALPHA (Common.fxh). This exercises all four skinned WGSL
// modules ({per-pixel, per-vertex} x {stride-52, stride-56 vertex colour}) AND all three
// WeightsPerVertex values (1/2/4).
//
// The discriminator for "fog uses the SKINNED position, not the original": bone0 = identity, bone1 =
// Translate(0,0,+4). With View=Translate(0,0,-4) and FogStart=2/FogEnd=6 an unskinned z=0 quad sits at
// keep=0.5 (half fog), but blending in bone1 pushes the skinned z so the fog changes -- WeightsPerVertex
// 2 (skinned z=2) lands at keep=1 (NO fog -> base colour), which a shader that fogged the ORIGINAL
// position (still z=0 -> keep=0.5) could never produce. The alpha<1 check proves the FNA
// FogColor*outputAlpha premultiply.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr float kFogStart = 2.0f;
    constexpr float kFogEnd = 6.0f;
    const Vector3 kBase{0.20f, 0.65f, 0.30f};
    const Vector3 kFogColor{0.80f, 0.10f, 0.20f};

    struct SkinnedVertex { float px, py, pz, nx, ny, nz, u, v, w0, w1, w2, w3;
                           std::uint8_t i0, i1, i2, i3; };
    static_assert(sizeof(SkinnedVertex) == 52, "stride 52");
    struct SkinnedColorVertex { float px, py, pz, nx, ny, nz, u, v, w0, w1, w2, w3;
                                std::uint8_t i0, i1, i2, i3; std::uint8_t r, g, b, a; };
    static_assert(sizeof(SkinnedColorVertex) == 56, "stride 56");

    bool RgbNear(const Color& a, const Color& b, int t = 3)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= t &&
               std::abs(a.getGProperty() - b.getGProperty()) <= t &&
               std::abs(a.getBProperty() - b.getBProperty()) <= t;
    }
    std::string Txt(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + "," + std::to_string(c.getAProperty()) + ")";
    }
    float Clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
    Vector3 MixLin(const Vector3& f, const Vector3& b, float keep)
    {
        return Vector3(f.X * (1 - keep) + b.X * keep, f.Y * (1 - keep) + b.Y * keep,
                       f.Z * (1 - keep) + b.Z * keep);
    }
    Matrix Ortho() { return Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 100.0f); }
    Color FromLinear(const Vector3& v)
    {
        return Color(static_cast<int>(std::lround(Clamp01(v.X) * 255.0f)),
                     static_cast<int>(std::lround(Clamp01(v.Y) * 255.0f)),
                     static_cast<int>(std::lround(Clamp01(v.Z) * 255.0f)), 255);
    }

    // The skinned z these bones/weights produce (bone0=identity, bone1=Translate z+4, bones2/3=identity).
    float SkinnedZ(int weightsPerVertex)
    {
        if (weightsPerVertex == 1) return 0.0f;                 // bone0 only
        if (weightsPerVertex == 2) return 0.5f * 4.0f;          // 0.5*bone0 + 0.5*bone1 -> z=2
        return 0.25f * 4.0f;                                    // 0.25 each, only bone1 shifts -> z=1
    }
    // keep = 1 - saturate(dot(vec4(skinnedPos,1), FogVector)); FogVector from World*View=Translate(0,0,-4).
    float KeepForZ(float skinnedZ)
    {
        return 1.0f - Clamp01((skinnedZ - 4.0f + kFogStart) / (kFogStart - kFogEnd));
    }
}

class WebGpuSkinnedEffectFogTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<Texture2D> white_;
    std::unique_ptr<RenderTarget2D> target_;
    bool done_ = false;
    int passed_ = 0, total_ = 0, result_ = 1;

    Color ReadCentre()
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        target_->GetData(0, &region, &pixel, 0, 1);
        return pixel;
    }

    // A z=0 quad with the given per-vertex bone weights (indices always 0,1,2,3).
    VertexBuffer MakeQuad(GraphicsDevice& dev, bool colorVariant, float w0, float w1, float w2, float w3)
    {
        if (!colorVariant)
        {
            const SkinnedVertex q[] = {
                {-1, 1, 0, 0,0,-1, 0,0, w0,w1,w2,w3, 0,1,2,3}, {-1,-1, 0, 0,0,-1, 0,1, w0,w1,w2,w3, 0,1,2,3},
                { 1,-1, 0, 0,0,-1, 1,1, w0,w1,w2,w3, 0,1,2,3}, {-1, 1, 0, 0,0,-1, 0,0, w0,w1,w2,w3, 0,1,2,3},
                { 1,-1, 0, 0,0,-1, 1,1, w0,w1,w2,w3, 0,1,2,3}, { 1, 1, 0, 0,0,-1, 1,0, w0,w1,w2,w3, 0,1,2,3},
            };
            VertexBuffer vb(dev, 6);
            vb.SetDataRaw(q, 6, static_cast<int>(sizeof(SkinnedVertex)));
            return vb;
        }
        const std::uint8_t c = 255;
        const SkinnedColorVertex q[] = {
            {-1, 1, 0, 0,0,-1, 0,0, w0,w1,w2,w3, 0,1,2,3, c,c,c,c}, {-1,-1, 0, 0,0,-1, 0,1, w0,w1,w2,w3, 0,1,2,3, c,c,c,c},
            { 1,-1, 0, 0,0,-1, 1,1, w0,w1,w2,w3, 0,1,2,3, c,c,c,c}, {-1, 1, 0, 0,0,-1, 0,0, w0,w1,w2,w3, 0,1,2,3, c,c,c,c},
            { 1,-1, 0, 0,0,-1, 1,1, w0,w1,w2,w3, 0,1,2,3, c,c,c,c}, { 1, 1, 0, 0,0,-1, 1,0, w0,w1,w2,w3, 0,1,2,3, c,c,c,c},
        };
        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(q, 6, static_cast<int>(sizeof(SkinnedColorVertex)));
        return vb;
    }

    // Ambient-only so the lit base == DiffuseColor (SkinnedEffect folds Ambient*Diffuse into emissive).
    Color Render(bool perPixel, bool colorVariant, int weightsPerVertex, const Vector3& diffuse,
                 float alpha, bool fogEnabled)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetRenderTarget(target_.get());
        dev.Clear(Color(0, 0, 0, 255));

        float w0 = 1, w1 = 0, w2 = 0, w3 = 0;
        if (weightsPerVertex == 2) { w0 = 0.5f; w1 = 0.5f; }
        else if (weightsPerVertex == 4) { w0 = w1 = w2 = w3 = 0.25f; }
        VertexBuffer vb = MakeQuad(dev, colorVariant, w0, w1, w2, w3);

        SkinnedEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateTranslation(0, 0, -4.0f));
        fx.setProjectionProperty(Ortho());
        fx.setTextureProperty(white_.get());
        // bone0 identity, bone1 shifts z by +4, bones 2/3 identity.
        std::vector<Matrix> bones = {
            Matrix::getIdentityProperty(), Matrix::CreateTranslation(0, 0, 4.0f),
            Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
        };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(weightsPerVertex);
        fx.setDiffuseColorProperty(diffuse);
        fx.setAlphaProperty(alpha);
        fx.setPreferPerPixelLightingProperty(perPixel);
        fx.VertexColorEnabled = colorVariant;
        fx.setAmbientLightColorProperty(Vector3(1, 1, 1));
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setFogEnabledProperty(fogEnabled);
        fx.setFogColorProperty(kFogColor);
        fx.setFogStartProperty(kFogStart);
        fx.setFogEndProperty(kFogEnd);
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return ReadCentre();
    }

    // Renders a solid linear RGB (fog off, alpha 1, WPV 1) to get its encoded bytes.
    Color Calibrate(const Vector3& linearRgb)
    {
        return Render(false, false, 1, linearRgb, 1.0f, false);
    }

    void Check(bool ok, const std::string& label, const Color& got, const Color& expected)
    {
        ++total_;
        if (ok) ++passed_;
        std::printf("[%s] %s got=%s expected~=%s\n", ok ? "PASS" : "FAIL", label.c_str(),
                    Txt(got).c_str(), Txt(expected).c_str());
    }

    void RunModule(const char* name, bool perPixel, bool colorVariant)
    {
        // WeightsPerVertex 1/2/4: each produces a distinct SKINNED z, hence a distinct fog keep. A
        // shader fogging the ORIGINAL (z=0) position would give keep=0.5 for all three.
        for (int wpv : {1, 2, 4})
        {
            const float keep = KeepForZ(SkinnedZ(wpv));
            const Color expected = Calibrate(MixLin(kFogColor, kBase, keep));
            const Color got = Render(perPixel, colorVariant, wpv, kBase, 1.0f, true);
            Check(RgbNear(got, expected),
                  std::string(name) + " WPV=" + std::to_string(wpv) + " fog uses skinned z (keep=" +
                      std::to_string(keep).substr(0, 4) + ")", got, expected);
        }

        // Full fog (WPV=1 lands at keep=0.5, so shift the quad fully into fog with a far view? Instead
        // use FogStart==FogEnd is not available here (fixed 2/6); rely on the WPV=1 half-fog above and
        // add an explicit fully-fogged check by putting the quad far: bone1-only via WPV=2 gives keep=1
        // (no fog), and WPV=1 gives keep=0.5. For a keep=0 anchor, reuse the base (no fog) below.)
        const Color base = Render(perPixel, colorVariant, 2, kBase, 1.0f, true); // WPV=2 -> keep=1 -> base
        Check(RgbNear(base, FromLinear(kBase)),
              std::string(name) + " skinned z moved out of fog range => no fog (base)", base, FromLinear(kBase));

        // Alpha < 1 with WPV=1 (half fog): the fogged pixel is mix(FogColor*alpha, base*alpha, keep).
        // Check the fully-fogged limit is FogColor*alpha, not FogColor: pull the quad deep into fog by
        // using WPV=1 but a start/end that gives keep~0 is fixed; instead compare the half-fog fogged R
        // against the alpha-scaled expectation.
        const float keep1 = KeepForZ(SkinnedZ(1)); // 0.5
        const float a = 0.5f;
        const Color gotA = Render(perPixel, colorVariant, 1, kBase, a, true);
        // base is premultiplied by alpha (SkinnedEffect folds alpha), fog target = FogColor*alpha.
        const Vector3 baseA = Vector3(kBase.X * a, kBase.Y * a, kBase.Z * a);
        const Vector3 fogA = Vector3(kFogColor.X * a, kFogColor.Y * a, kFogColor.Z * a);
        const Color expA = Calibrate(MixLin(fogA, baseA, keep1));
        const Color expWrong = Calibrate(MixLin(kFogColor, baseA, keep1)); // if alpha-multiply dropped
        Check(RgbNear(gotA, expA) && !RgbNear(gotA, expWrong),
              std::string(name) + " Alpha<1: fog target is FogColor*alpha (not plain FogColor)", gotA, expA);
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        const Color white(255, 255, 255, 255);
        white_ = std::make_unique<Texture2D>(device, 1, 1);
        white_->SetData(&white, 1);
        target_ = std::make_unique<RenderTarget2D>(device, kSize, kSize, false, SurfaceFormat::Color,
                                                   DepthFormat::Depth24Stencil8, 0,
                                                   RenderTargetUsage::DiscardContents);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;

        RunModule("Skinned(perVertex,stride52)", false, false);
        RunModule("Skinned(perPixel,stride52)", true, false);
        RunModule("Skinned(perVertex,stride56)", false, true);
        RunModule("Skinned(perPixel,stride56)", true, true);

        std::printf("=== WEBGPU-149 SkinnedEffect fog: %d/%d PASS ===\n", passed_, total_);
        result_ = (passed_ == total_) ? 0 : 1;
        Exit();
    }

public:
    WebGpuSkinnedEffectFogTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(kSize);
        manager_->setPreferredBackBufferHeightProperty(kSize);
    }
    int getResult() const { return result_; }
};

int main()
{
    WebGpuSkinnedEffectFogTest game;
    game.Run();
    return game.getResult();
}
