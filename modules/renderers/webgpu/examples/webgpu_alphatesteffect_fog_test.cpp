// SPDX-License-Identifier: MS-PL
// WEBGPU-146: AlphaTestEffect fog on the WebGPU renderer. FNA's AlphaTestEffect applies fog to the
// pixel that SURVIVES the alpha-test discard (fog does not affect the discard decision). This
// exercises both alpha_test3d WGSL modules -- the uncolored stride-20 module (VertexPositionTexture)
// and the vertex-colour stride-24 module (VertexPositionColorTexture). FogStart==FogEnd (keep=0)
// collapses the surviving colour to exactly FogColor, the discriminator that fails if the WGSL fog
// blend (or the fog uniforms) are removed.

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

class WebGpuAlphaTestEffectFogTest final : public Game
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

    void ConfigureFog(AlphaTestEffect& e, bool fogEnabled, const Vector3& fogColor,
                      float fogStart, float fogEnd)
    {
        e.setFogEnabledProperty(fogEnabled);
        e.setFogColorProperty(fogColor);
        e.setFogStartProperty(fogStart);
        e.setFogEndProperty(fogEnd);
    }

    // colored=false -> stride-20 uncolored module; colored=true -> stride-24 vertex-colour module.
    // The alpha always passes (opaque texture, Greater than ReferenceAlpha 0), so a pixel survives.
    Color Render(bool colored, const Matrix& view, const Vector3& diffuse, bool fogEnabled,
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
        effect.setAlphaProperty(1.0f);
        effect.setAlphaFunctionProperty(CompareFunction::Greater);
        effect.setReferenceAlphaProperty(0);
        effect.setVertexColorEnabledProperty(colored);
        ConfigureFog(effect, fogEnabled, fogColor, fogStart, fogEnd);
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
            const Color w(255, 255, 255, 255); // white vertex colour: tint = diffuse
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

    Color Calibrate(const Vector3& linearRgb)
    {
        return Render(false, Matrix::CreateTranslation(0, 0, -4.0f), linearRgb, false,
                      Vector3::Zero, kFogStart, kFogEnd);
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
        const Color base = Render(colored, viewFar, kBase, false, kFogColor, kFogStart, kFogEnd);
        const Color full = Render(colored, viewFar, kBase, true, kFogColor, 4.0f, 4.0f);
        Check(RgbNear(full, FromLinear(kFogColor)),
              std::string(name) + " FogStart==FogEnd collapses to FogColor", full, FromLinear(kFogColor));

        const float keep = 0.5f;
        const Color halfExpected = Calibrate(MixLin(kFogColor, kBase, keep));
        const Color half = Render(colored, viewFar, kBase, true, kFogColor, kFogStart, kFogEnd);
        Check(RgbNear(half, halfExpected), std::string(name) + " half fog matches lerp(FogColor,base,keep)",
              half, halfExpected);

        const Color off = Render(colored, viewFar, kBase, false, kFogColor, kFogStart, kFogEnd);
        Check(RgbNear(off, base), std::string(name) + " fog disabled preserves base", off, base);
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

        RunModule("AlphaTest(uncolored)", false);
        RunModule("AlphaTest(colored)", true);

        std::printf("=== WEBGPU-146 AlphaTestEffect fog: %d/%d PASS ===\n", passed_, total_);
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
