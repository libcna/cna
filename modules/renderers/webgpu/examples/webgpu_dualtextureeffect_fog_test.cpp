// SPDX-License-Identifier: MS-PL
// WEBGPU-147: DualTextureEffect fog on the WebGPU renderer. Exercises both dual_texture3d WGSL
// modules (uncolored stride-20 and vertex-colour stride-24). The dual-texture output is
// sample0.rgb*2 * sample1 * tint; with tex0 = mid-grey (doubled -> white) and diffuse = white the
// surviving colour is tex1, so a known base can be fed through the shader and then fogged.
// FogStart==FogEnd (keep=0) collapses to FogColor -- the discriminator that fails if the WGSL fog
// blend or the fog uniforms are removed.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr float kFogStart = 2.0f;
    constexpr float kFogEnd = 6.0f;
    const Vector3 kBase{0.20f, 0.65f, 0.30f};
    const Vector3 kFogColor{0.80f, 0.10f, 0.20f};

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

class WebGpuDualTextureEffectFogTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<Texture2D> grey_;    // tex0: doubled -> white
    std::unique_ptr<Texture2D> baseTex_; // tex1: the base colour under test
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

    // colored=false -> stride-20 module; colored=true -> stride-24 (vertex colour) module.
    Color Render(bool colored, Texture2D* tex1, const Matrix& view, bool fogEnabled,
                 const Vector3& fogColor, float fogStart, float fogEnd, float alpha = 1.0f)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(target_.get());
        device.Clear(Color(0, 0, 0, 255));
        DualTextureEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(view);
        effect.setProjectionProperty(Ortho());
        effect.setTextureProperty(grey_.get());
        effect.setTexture2Property(tex1);
        effect.setDiffuseColorProperty(Vector3(1, 1, 1));
        effect.setAlphaProperty(alpha);
        effect.setVertexColorEnabledProperty(colored);
        effect.setFogEnabledProperty(fogEnabled);
        effect.setFogColorProperty(fogColor);
        effect.setFogStartProperty(fogStart);
        effect.setFogEndProperty(fogEnd);
        effect.Apply();

        if (!colored)
        {
            const VertexPositionTexture v[] = {
                {Vector3(-1, 1, 0), Vector2(0, 0)}, {Vector3(-1, -1, 0), Vector2(0, 1)},
                {Vector3(1, -1, 0), Vector2(1, 1)}, {Vector3(-1, 1, 0), Vector2(0, 0)},
                {Vector3(1, -1, 0), Vector2(1, 1)}, {Vector3(1, 1, 0), Vector2(1, 0)},
            };
            VertexBuffer vb(device, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(v, 0, 6);
            device.SetVertexBuffer(&vb);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            device.SetVertexBuffer(nullptr);
        }
        else
        {
            const Color w(255, 255, 255, 255);
            const VertexPositionColorTexture v[] = {
                {Vector3(-1, 1, 0), w, Vector2(0, 0)}, {Vector3(-1, -1, 0), w, Vector2(0, 1)},
                {Vector3(1, -1, 0), w, Vector2(1, 1)}, {Vector3(-1, 1, 0), w, Vector2(0, 0)},
                {Vector3(1, -1, 0), w, Vector2(1, 1)}, {Vector3(1, 1, 0), w, Vector2(1, 0)},
            };
            VertexBuffer vb(device, VertexPositionColorTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(v, 0, 6);
            device.SetVertexBuffer(&vb);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            device.SetVertexBuffer(nullptr);
        }

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return ReadCentre();
    }

    // Renders a solid linear RGB through the fog-off uncolored path (tex1 = that colour).
    Color Calibrate(const Vector3& linearRgb)
    {
        auto& device = getGraphicsDeviceProperty();
        const Color c = FromLinear(linearRgb);
        Texture2D tex(device, 1, 1);
        tex.SetData(&c, 1);
        return Render(false, &tex, Matrix::CreateTranslation(0, 0, -4.0f), false, Vector3::Zero,
                      kFogStart, kFogEnd);
    }

    void Check(bool ok, const std::string& label, const Color& got, const Color& expected)
    {
        ++total_;
        if (ok) ++passed_;
        std::printf("[%s] %s got=%s expected~=%s\n", ok ? "PASS" : "FAIL", label.c_str(),
                    Txt(got).c_str(), Txt(expected).c_str());
    }

    void RunModule(const char* name, bool colored)
    {
        const Matrix viewFar = Matrix::CreateTranslation(0, 0, -4.0f); // keep=0.5
        const Color base = Render(colored, baseTex_.get(), viewFar, false, kFogColor, kFogStart, kFogEnd);
        Check(RgbNear(base, FromLinear(kBase)), std::string(name) + " base (grey*2 x tex1) = tex1",
              base, FromLinear(kBase));

        const Color full = Render(colored, baseTex_.get(), viewFar, true, kFogColor, 4.0f, 4.0f);
        Check(RgbNear(full, FromLinear(kFogColor)),
              std::string(name) + " FogStart==FogEnd collapses to FogColor", full, FromLinear(kFogColor));

        const Color halfExpected = Calibrate(MixLin(kFogColor, kBase, 0.5f));
        const Color half = Render(colored, baseTex_.get(), viewFar, true, kFogColor, kFogStart, kFogEnd);
        Check(RgbNear(half, halfExpected), std::string(name) + " half fog matches lerp(FogColor,base,keep)",
              half, halfExpected);

        // Alpha < 1: full fog -> FogColor * outputAlpha (0.5), NOT plain FogColor. (grey*2=white and
        // tex1.a=1, so the output alpha is the material alpha.)
        const Color fullA = Render(colored, baseTex_.get(), viewFar, true, kFogColor, 4.0f, 4.0f, /*alpha*/0.5f);
        const Color expScaled = FromLinear(Vector3(kFogColor.X * 0.5f, kFogColor.Y * 0.5f, kFogColor.Z * 0.5f));
        Check(RgbNear(fullA, expScaled) && !RgbNear(fullA, FromLinear(kFogColor)),
              std::string(name) + " Alpha<1 full fog = FogColor*alpha (not plain FogColor)", fullA, expScaled);
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        const Color grey(128, 128, 128, 255);
        grey_ = std::make_unique<Texture2D>(device, 1, 1);
        grey_->SetData(&grey, 1);
        const Color baseColor = FromLinear(kBase);
        baseTex_ = std::make_unique<Texture2D>(device, 1, 1);
        baseTex_->SetData(&baseColor, 1);
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
        device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;

        RunModule("DualTexture(uncolored)", false);
        RunModule("DualTexture(colored)", true);

        std::printf("=== WEBGPU-147 DualTextureEffect fog: %d/%d PASS ===\n", passed_, total_);
        result_ = (passed_ == total_) ? 0 : 1;
        Exit();
    }

public:
    WebGpuDualTextureEffectFogTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(kSize);
        manager_->setPreferredBackBufferHeightProperty(kSize);
    }
    int getResult() const { return result_; }
};

int main()
{
    WebGpuDualTextureEffectFogTest game;
    game.Run();
    return game.getResult();
}
