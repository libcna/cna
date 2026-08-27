// SPDX-License-Identifier: MS-PL
// WEBGPU-148: SkinnedEffect fog on the WebGPU renderer. FNA's SkinnedEffect skins the vertex
// position BEFORE computing the fog factor (Skin() then ComputeCommonVSOutput), so the WGSL
// computes fogFactor from the skinned view-space position -- matching Vulkan's skinned3d.vert.glsl.
// This exercises all four skinned WGSL modules: {per-pixel, per-vertex} lighting x {stride-52,
// stride-56 (per-vertex Color)}. FogStart==FogEnd (keep=0) collapses each to FogColor -- the
// discriminator that fails if the WGSL fog blend or the fog uniforms are removed.

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

    VertexBuffer MakeQuad(GraphicsDevice& dev, bool colorVariant)
    {
        if (!colorVariant)
        {
            const SkinnedVertex v[] = {
                { -1, 1, 0, 0,0,-1, 0,0, 1,0,0,0, 0,0,0,0 }, { -1,-1, 0, 0,0,-1, 0,1, 1,0,0,0, 0,0,0,0 },
                { 1,-1, 0, 0,0,-1, 1,1, 1,0,0,0, 0,0,0,0 }, { -1, 1, 0, 0,0,-1, 0,0, 1,0,0,0, 0,0,0,0 },
                { 1,-1, 0, 0,0,-1, 1,1, 1,0,0,0, 0,0,0,0 }, { 1, 1, 0, 0,0,-1, 1,0, 1,0,0,0, 0,0,0,0 },
            };
            VertexBuffer vb(dev, 6);
            vb.SetDataRaw(v, 6, static_cast<int>(sizeof(SkinnedVertex)));
            return vb;
        }
        const std::uint8_t w = 255; // white vertex colour -> tint leaves base unchanged
        const SkinnedColorVertex v[] = {
            { -1, 1, 0, 0,0,-1, 0,0, 1,0,0,0, 0,0,0,0, w,w,w,w }, { -1,-1, 0, 0,0,-1, 0,1, 1,0,0,0, 0,0,0,0, w,w,w,w },
            { 1,-1, 0, 0,0,-1, 1,1, 1,0,0,0, 0,0,0,0, w,w,w,w }, { -1, 1, 0, 0,0,-1, 0,0, 1,0,0,0, 0,0,0,0, w,w,w,w },
            { 1,-1, 0, 0,0,-1, 1,1, 1,0,0,0, 0,0,0,0, w,w,w,w }, { 1, 1, 0, 0,0,-1, 1,0, 1,0,0,0, 0,0,0,0, w,w,w,w },
        };
        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(v, 6, static_cast<int>(sizeof(SkinnedColorVertex)));
        return vb;
    }

    // Ambient-only (no directional light) so the lit base == DiffuseColor (SkinnedEffect folds
    // Ambient*Diffuse into emissive), keeping the pre-fog colour deterministic for every module.
    Color Render(bool perPixel, bool colorVariant, const Matrix& view, const Vector3& diffuse,
                 bool fogEnabled, const Vector3& fogColor, float fogStart, float fogEnd)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetRenderTarget(target_.get());
        dev.Clear(Color(0, 0, 0, 255));
        VertexBuffer vb = MakeQuad(dev, colorVariant);
        SkinnedEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(view);
        fx.setProjectionProperty(Ortho());
        fx.setTextureProperty(white_.get());
        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.setDiffuseColorProperty(diffuse);
        fx.setPreferPerPixelLightingProperty(perPixel);
        fx.VertexColorEnabled = colorVariant;
        fx.setAmbientLightColorProperty(Vector3(1, 1, 1));
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setFogEnabledProperty(fogEnabled);
        fx.setFogColorProperty(fogColor);
        fx.setFogStartProperty(fogStart);
        fx.setFogEndProperty(fogEnd);
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return ReadCentre();
    }

    Color Calibrate(bool perPixel, bool colorVariant, const Vector3& linearRgb)
    {
        return Render(perPixel, colorVariant, Matrix::CreateTranslation(0, 0, -4.0f), linearRgb,
                      false, Vector3::Zero, kFogStart, kFogEnd);
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
        const Matrix viewFar = Matrix::CreateTranslation(0, 0, -4.0f); // keep=0.5
        const Color base = Render(perPixel, colorVariant, viewFar, kBase, false, kFogColor, kFogStart, kFogEnd);
        Check(RgbNear(base, FromLinear(kBase)), std::string(name) + " base (ambient-only) = DiffuseColor",
              base, FromLinear(kBase));

        const Color full = Render(perPixel, colorVariant, viewFar, kBase, true, kFogColor, 4.0f, 4.0f);
        Check(RgbNear(full, FromLinear(kFogColor)),
              std::string(name) + " FogStart==FogEnd collapses to FogColor", full, FromLinear(kFogColor));

        const Color halfExpected = Calibrate(perPixel, colorVariant, MixLin(kFogColor, kBase, 0.5f));
        const Color half = Render(perPixel, colorVariant, viewFar, kBase, true, kFogColor, kFogStart, kFogEnd);
        Check(RgbNear(half, halfExpected), std::string(name) + " half fog matches lerp(FogColor,base,keep)",
              half, halfExpected);
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

        std::printf("=== WEBGPU-148 SkinnedEffect fog: %d/%d PASS ===\n", passed_, total_);
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
