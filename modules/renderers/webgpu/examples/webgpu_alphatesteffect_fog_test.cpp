// SPDX-License-Identifier: MS-PL
// WEBGPU-146/149: AlphaTestEffect fog on the WebGPU renderer. FNA applies fog to the pixel that
// SURVIVES the alpha-test discard, lerping toward FogColor * OUTPUT ALPHA (Common.fxh). This drives
// all three vertex layouts AlphaTestEffect can use: the uncolored stride-20 module
// (VertexPositionTexture), the vertex-colour stride-24 module (VertexPositionColorTexture), and the
// stride-32 module (VertexPositionNormalTexture -- the normal is unread, same uncolored shader).
// Discriminators: FogStart==FogEnd -> FogColor at alpha 1, and -> FogColor*alpha at alpha<1.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

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
    Vector3 Scale(const Vector3& v, float s) { return Vector3(v.X * s, v.Y * s, v.Z * s); }
    Matrix Ortho() { return Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 100.0f); }
    Color FromLinear(const Vector3& v)
    {
        return Color(static_cast<int>(std::lround(Clamp01(v.X) * 255.0f)),
                     static_cast<int>(std::lround(Clamp01(v.Y) * 255.0f)),
                     static_cast<int>(std::lround(Clamp01(v.Z) * 255.0f)), 255);
    }
}

class WebGpuAlphaTestEffectFogTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<Texture2D> white_;
    std::unique_ptr<RenderTarget2D> target_;
    bool done_ = false;
    int passed_ = 0, total_ = 0, result_ = 1;

    enum class Layout { Tex20, ColorTex24, NormalTex32 };

    Color ReadCentre()
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        target_->GetData(0, &region, &pixel, 0, 1);
        return pixel;
    }

    // The alpha always passes (opaque-enough texture, Greater than ReferenceAlpha 0), so a pixel
    // survives to be fogged. `alpha` is the material alpha; the output alpha is that value (white tex).
    Color Render(Layout layout, const Matrix& view, const Vector3& diffuse, float alpha, bool fogEnabled,
                 const Vector3& fogColor, float fogStart, float fogEnd)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(target_.get());
        device.Clear(Color(0, 0, 0, 255));
        AlphaTestEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(view);
        effect.setProjectionProperty(Ortho());
        effect.setTextureProperty(white_.get());
        effect.setDiffuseColorProperty(diffuse);
        effect.setAlphaProperty(alpha);
        effect.setAlphaFunctionProperty(CompareFunction::Greater);
        effect.setReferenceAlphaProperty(0);
        effect.setVertexColorEnabledProperty(layout == Layout::ColorTex24);
        effect.setFogEnabledProperty(fogEnabled);
        effect.setFogColorProperty(fogColor);
        effect.setFogStartProperty(fogStart);
        effect.setFogEndProperty(fogEnd);
        effect.Apply();

        if (layout == Layout::Tex20)
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
        else if (layout == Layout::ColorTex24)
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
        else // NormalTex32 -- the uncolored module serves strides 20 and 32 (the normal is unread).
        {
            const Vector3 n(0, 0, -1);
            const VertexPositionNormalTexture v[] = {
                {Vector3(-1, 1, 0), n, Vector2(0, 0)}, {Vector3(-1, -1, 0), n, Vector2(0, 1)},
                {Vector3(1, -1, 0), n, Vector2(1, 1)}, {Vector3(-1, 1, 0), n, Vector2(0, 0)},
                {Vector3(1, -1, 0), n, Vector2(1, 1)}, {Vector3(1, 1, 0), n, Vector2(1, 0)},
            };
            VertexBuffer vb(device, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(v, 0, 6);
            device.SetVertexBuffer(&vb);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            device.SetVertexBuffer(nullptr);
        }

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return ReadCentre();
    }

    Color Calibrate(const Vector3& linearRgb)
    {
        return Render(Layout::Tex20, Matrix::CreateTranslation(0, 0, -4.0f), linearRgb, 1.0f, false,
                      Vector3::Zero, kFogStart, kFogEnd);
    }

    void Check(bool ok, const std::string& label, const Color& got, const Color& expected)
    {
        ++total_;
        if (ok) ++passed_;
        std::printf("[%s] %s got=%s expected~=%s\n", ok ? "PASS" : "FAIL", label.c_str(),
                    Txt(got).c_str(), Txt(expected).c_str());
    }

    void RunLayout(const char* name, Layout layout)
    {
        const Matrix viewFar = Matrix::CreateTranslation(0, 0, -4.0f); // keep=0.5
        const Color base = Render(layout, viewFar, kBase, 1.0f, false, kFogColor, kFogStart, kFogEnd);
        Check(RgbNear(base, FromLinear(kBase)), std::string(name) + " base = DiffuseColor", base, FromLinear(kBase));

        const Color full = Render(layout, viewFar, kBase, 1.0f, true, kFogColor, 4.0f, 4.0f);
        Check(RgbNear(full, FromLinear(kFogColor)),
              std::string(name) + " FogStart==FogEnd collapses to FogColor", full, FromLinear(kFogColor));

        const Color half = Render(layout, viewFar, kBase, 1.0f, true, kFogColor, kFogStart, kFogEnd);
        Check(RgbNear(half, Calibrate(MixLin(kFogColor, kBase, 0.5f))),
              std::string(name) + " half fog matches lerp(FogColor,base,keep)", half, Calibrate(MixLin(kFogColor, kBase, 0.5f)));

        // Alpha < 1: full fog -> FogColor * outputAlpha (0.5), NOT plain FogColor.
        const Color fullA = Render(layout, viewFar, kBase, 0.5f, true, kFogColor, 4.0f, 4.0f);
        const Color expScaled = FromLinear(Scale(kFogColor, 0.5f));
        Check(RgbNear(fullA, expScaled) && !RgbNear(fullA, FromLinear(kFogColor)),
              std::string(name) + " Alpha<1 full fog = FogColor*alpha (not plain FogColor)", fullA, expScaled);
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

        RunLayout("AlphaTest(stride20)", Layout::Tex20);
        RunLayout("AlphaTest(stride24,color)", Layout::ColorTex24);
        RunLayout("AlphaTest(stride32,normal)", Layout::NormalTex32);

        std::printf("=== WEBGPU-149 AlphaTestEffect fog: %d/%d PASS ===\n", passed_, total_);
        result_ = (passed_ == total_) ? 0 : 1;
        Exit();
    }

public:
    WebGpuAlphaTestEffectFogTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(kSize);
        manager_->setPreferredBackBufferHeightProperty(kSize);
    }
    int getResult() const { return result_; }
};

int main()
{
    WebGpuAlphaTestEffectFogTest game;
    game.Run();
    return game.getResult();
}
