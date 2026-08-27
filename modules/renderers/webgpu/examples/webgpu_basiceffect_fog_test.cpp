// SPDX-License-Identifier: MS-PL
// WEBGPU-145/149: BasicEffect fog on the WebGPU renderer. Fog is the FNA CPU-prepared World*View fog
// vector (EffectHelpers.SetFogVector), dotted with the OBJECT-space vertex position in the vertex
// shader; the fragment then lerps the composed RGB toward FogColor * OUTPUT ALPHA (FNA Common.fxh:
// color.rgb = lerp(color.rgb, FogColor * color.a, fogFactor)). This exercises every BasicEffect WGSL
// family: colored3d (stride 16), textured3d (stride 20), colored_textured3d (stride 24, both a per-
// vertex Color and a texture), and both lit_textured3d variants (stride 32, per-pixel and per-vertex).
//
// Colour target RenderTarget2D + calibration through the same BasicEffect path (sRGB-safe). The
// discriminators that fail if fog (or the FNA alpha-premultiply) is dropped: FogStart==FogEnd (keep=0)
// collapses to FogColor, and with Alpha<1 it collapses to FogColor * Alpha (NOT plain FogColor).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
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
    const Vector3 kBase{0.20f, 0.65f, 0.30f};   // green-ish diffuse
    const Vector3 kFogColor{0.80f, 0.10f, 0.20f}; // red-ish fog

    bool RgbNear(const Color& a, const Color& b, int tolerance = 3)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tolerance &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tolerance &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tolerance;
    }
    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + "," + std::to_string(c.getAProperty()) + ")";
    }
    float Clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
    Vector3 MixLinear(const Vector3& fog, const Vector3& base, float keep)
    {
        return Vector3(fog.X * (1 - keep) + base.X * keep, fog.Y * (1 - keep) + base.Y * keep,
                       fog.Z * (1 - keep) + base.Z * keep);
    }
    Vector3 Scale(const Vector3& v, float s) { return Vector3(v.X * s, v.Y * s, v.Z * s); }

    // FNA keep factor: the centre vertex is object position (0,0,0), so it reduces to the view-Z form.
    float ViewSpaceKeep(const Matrix& view, bool fogEnabled, float fogStart, float fogEnd)
    {
        if (!fogEnabled) return 1.0f;
        if (fogStart == fogEnd) return 0.0f;
        const float viewZ = Vector3::Transform(Vector3::Zero, view).Z;
        return 1.0f - Clamp01((viewZ + fogStart) / (fogStart - fogEnd));
    }
    Matrix Ortho() { return Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 100.0f); }
    Color FromLinear(const Vector3& v)
    {
        return Color(static_cast<int>(std::lround(Clamp01(v.X) * 255.0f)),
                     static_cast<int>(std::lround(Clamp01(v.Y) * 255.0f)),
                     static_cast<int>(std::lround(Clamp01(v.Z) * 255.0f)), 255);
    }
}

class WebGpuBasicEffectFogTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<Texture2D> white_;
    std::unique_ptr<RenderTarget2D> target_;
    bool done_ = false;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    enum class Family { Colored, Textured, ColoredTextured, LitPerPixel, LitPerVertex };

    Color ReadCentre()
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        target_->GetData(0, &region, &pixel, 0, 1);
        return pixel;
    }

    // Renders one full-target quad through the requested BasicEffect family with the given material
    // alpha, and returns the centre pixel. Lighting (for the Lit families) is head-on so the lit base
    // colour is deterministic. `indexed` draws the same two triangles via DrawIndexedPrimitives.
    Color Render(Family family, const Matrix& view, const Vector3& diffuse, float alpha,
                 bool fogEnabled, const Vector3& fogColor, float fogStart, float fogEnd,
                 bool indexed = false)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(target_.get());
        device.Clear(Color(0, 0, 0, 255));
        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(view);
        effect.setProjectionProperty(Ortho());
        effect.setDiffuseColorProperty(diffuse);
        effect.setAlphaProperty(alpha);
        effect.setFogEnabledProperty(fogEnabled);
        effect.setFogColorProperty(fogColor);
        effect.setFogStartProperty(fogStart);
        effect.setFogEndProperty(fogEnd);

        const std::uint16_t idx[] = {0, 1, 2, 3, 4, 5};
        IndexBuffer ib(device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
        ib.SetData(idx, 0, 6);
        const auto draw = [&](VertexBuffer& vb) {
            effect.Apply();
            device.SetVertexBuffer(&vb);
            if (indexed) { device.SetIndexBuffer(&ib); device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 6, 0, 2); device.SetIndexBuffer(nullptr); }
            else device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            device.SetVertexBuffer(nullptr);
        };

        if (family == Family::Colored)
        {
            effect.setTextureEnabledProperty(false);
            effect.setLightingEnabledProperty(false);
            effect.VertexColorEnabled = false;
            const Color c = FromLinear(diffuse); // vertex colour ignored (VertexColorEnabled=false)
            const VertexPositionColor v[] = {
                {Vector3(-1, 1, 0), c}, {Vector3(-1, -1, 0), c}, {Vector3(1, -1, 0), c},
                {Vector3(-1, 1, 0), c}, {Vector3(1, -1, 0), c}, {Vector3(1, 1, 0), c},
            };
            VertexBuffer vb(device, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(v, 0, 6);
            draw(vb);
        }
        else if (family == Family::Textured)
        {
            effect.setTextureEnabledProperty(true);
            effect.setTextureProperty(white_.get());
            effect.setLightingEnabledProperty(false);
            effect.VertexColorEnabled = false;
            const VertexPositionTexture v[] = {
                {Vector3(-1, 1, 0), Vector2(0, 0)}, {Vector3(-1, -1, 0), Vector2(0, 1)},
                {Vector3(1, -1, 0), Vector2(1, 1)}, {Vector3(-1, 1, 0), Vector2(0, 0)},
                {Vector3(1, -1, 0), Vector2(1, 1)}, {Vector3(1, 1, 0), Vector2(1, 0)},
            };
            VertexBuffer vb(device, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(v, 0, 6);
            draw(vb);
        }
        else if (family == Family::ColoredTextured)
        {
            // stride 24: a per-vertex Color AND a texture -> colored_textured3d.wgsl. White vertex
            // colour + white texture leaves the output equal to the (premultiplied) DiffuseColor.
            effect.setTextureEnabledProperty(true);
            effect.setTextureProperty(white_.get());
            effect.setLightingEnabledProperty(false);
            effect.VertexColorEnabled = true;
            const Color w(255, 255, 255, 255);
            const VertexPositionColorTexture v[] = {
                {Vector3(-1, 1, 0), w, Vector2(0, 0)}, {Vector3(-1, -1, 0), w, Vector2(0, 1)},
                {Vector3(1, -1, 0), w, Vector2(1, 1)}, {Vector3(-1, 1, 0), w, Vector2(0, 0)},
                {Vector3(1, -1, 0), w, Vector2(1, 1)}, {Vector3(1, 1, 0), w, Vector2(1, 0)},
            };
            VertexBuffer vb(device, VertexPositionColorTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(v, 0, 6);
            draw(vb);
        }
        else
        {
            effect.setTextureEnabledProperty(true);
            effect.setTextureProperty(white_.get());
            effect.setLightingEnabledProperty(true);
            effect.VertexColorEnabled = false;
            effect.setPreferPerPixelLightingProperty(family == Family::LitPerPixel);
            effect.DirectionalLight0.setEnabledProperty(true);
            effect.DirectionalLight0.setDirectionProperty(Vector3(0, 0, 1));
            effect.DirectionalLight0.setDiffuseColorProperty(Vector3(1, 1, 1));
            effect.DirectionalLight0.setSpecularColorProperty(Vector3::Zero);
            effect.DirectionalLight1.setEnabledProperty(false);
            effect.DirectionalLight2.setEnabledProperty(false);
            effect.setAmbientLightColorProperty(Vector3::Zero);
            effect.setEmissiveColorProperty(Vector3::Zero);
            const Vector3 n(0, 0, -1);
            const VertexPositionNormalTexture v[] = {
                {Vector3(-1, 1, 0), n, Vector2(0, 0)}, {Vector3(-1, -1, 0), n, Vector2(0, 1)},
                {Vector3(1, -1, 0), n, Vector2(1, 1)}, {Vector3(-1, 1, 0), n, Vector2(0, 0)},
                {Vector3(1, -1, 0), n, Vector2(1, 1)}, {Vector3(1, 1, 0), n, Vector2(1, 0)},
            };
            VertexBuffer vb(device, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(v, 0, 6);
            draw(vb);
        }

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return ReadCentre();
    }

    // Renders a solid linear RGB through the fog-off Colored path (alpha=1) to get its encoded bytes.
    Color Calibrate(const Vector3& linearRgb)
    {
        return Render(Family::Colored, Matrix::CreateTranslation(0, 0, -4.0f), linearRgb, 1.0f, false,
                      Vector3::Zero, kFogStart, kFogEnd);
    }

    void Check(bool ok, const std::string& label, const Color& got, const Color& expected)
    {
        ++total_;
        if (ok) ++passed_;
        std::printf("[%s] %s got=%s expected~=%s\n", ok ? "PASS" : "FAIL", label.c_str(),
                    ColorText(got).c_str(), ColorText(expected).c_str());
    }

    static bool ChannelBetween(int got, int a, int b, int slack = 2)
    {
        const int lo = std::min(a, b) - slack;
        const int hi = std::max(a, b) + slack;
        return got >= lo && got <= hi;
    }

    void RunFamily(const char* name, Family family)
    {
        const Matrix viewFar = Matrix::CreateTranslation(0, 0, -4.0f); // keep=0.5 with start2/end6

        const Color base = Render(family, viewFar, kBase, 1.0f, false, kFogColor, kFogStart, kFogEnd);
        const Color full = Render(family, viewFar, kBase, 1.0f, true, kFogColor, 4.0f, 4.0f);
        const Color fogCal = Calibrate(kFogColor);
        Check(RgbNear(full, fogCal), std::string(name) + " FogStart==FogEnd collapses to FogColor",
              full, fogCal);

        const Color half = Render(family, viewFar, kBase, 1.0f, true, kFogColor, kFogStart, kFogEnd);
        const bool between =
            ChannelBetween(half.getRProperty(), base.getRProperty(), fogCal.getRProperty()) &&
            ChannelBetween(half.getGProperty(), base.getGProperty(), fogCal.getGProperty()) &&
            ChannelBetween(half.getBProperty(), base.getBProperty(), fogCal.getBProperty());
        const bool movedR = std::abs(half.getRProperty() - base.getRProperty()) > 8;
        const bool movedG = std::abs(half.getGProperty() - base.getGProperty()) > 8;
        Check(between && movedR && movedG, std::string(name) + " half fog lies between base and FogColor",
              half, base);

        const Color off = Render(family, viewFar, kBase, 1.0f, false, kFogColor, kFogStart, kFogEnd);
        Check(RgbNear(off, base), std::string(name) + " fog disabled preserves base", off, base);

        // Alpha < 1: FNA lerps toward FogColor * OUTPUT ALPHA, not plain FogColor. With a white
        // texture and Alpha=0.5 the output alpha is 0.5, so full fog -> FogColor*0.5. This FAILS if the
        // shader drops the alpha-premultiply (it would give plain FogColor), and it also proves the
        // scaled result differs from the unscaled one.
        const Color fullA = Render(family, viewFar, kBase, 0.5f, true, kFogColor, 4.0f, 4.0f);
        const Color expScaled = Calibrate(Scale(kFogColor, 0.5f));
        const Color expUnscaled = Calibrate(kFogColor);
        Check(RgbNear(fullA, expScaled) && !RgbNear(fullA, expUnscaled),
              std::string(name) + " Alpha<1 full fog = FogColor*alpha (not plain FogColor)", fullA, expScaled);
    }

    void RunColoredExact()
    {
        const Matrix viewFar = Matrix::CreateTranslation(0, 0, -4.0f);
        const float keep = ViewSpaceKeep(viewFar, true, kFogStart, kFogEnd);
        const Color expected = Calibrate(MixLinear(kFogColor, kBase, keep));
        const Color gotColored = Render(Family::Colored, viewFar, kBase, 1.0f, true, kFogColor, kFogStart, kFogEnd);
        Check(RgbNear(gotColored, expected), "Colored half fog matches lerp(FogColor,base,keep)", gotColored, expected);
        const Color gotTextured = Render(Family::Textured, viewFar, kBase, 1.0f, true, kFogColor, kFogStart, kFogEnd);
        Check(RgbNear(gotTextured, expected), "Textured half fog matches lerp(FogColor,base,keep)", gotTextured, expected);
        const Color gotCT = Render(Family::ColoredTextured, viewFar, kBase, 1.0f, true, kFogColor, kFogStart, kFogEnd);
        Check(RgbNear(gotCT, expected), "ColoredTextured (stride 24) half fog matches lerp", gotCT, expected);

        // Indexed draw path (DrawIndexedPrimitives) honours fog identically.
        const Color idxFull = Render(Family::Colored, viewFar, kBase, 1.0f, true, kFogColor, 4.0f, 4.0f, /*indexed*/true);
        Check(RgbNear(idxFull, Calibrate(kFogColor)), "indexed draw: full fog collapses to FogColor", idxFull, Calibrate(kFogColor));
        const Color idxHalf = Render(Family::Colored, viewFar, kBase, 1.0f, true, kFogColor, kFogStart, kFogEnd, true);
        Check(RgbNear(idxHalf, expected), "indexed draw: half fog matches lerp", idxHalf, expected);

        const struct { const char* label; float viewZ; float keep; } bounds[] = {
            {"before FogStart", -1.0f, 1.0f}, {"at FogEnd", -6.0f, 0.0f}, {"beyond FogEnd", -8.0f, 0.0f},
        };
        for (const auto& b : bounds)
        {
            const Matrix v = Matrix::CreateTranslation(0, 0, b.viewZ);
            const Color exp = Calibrate(MixLinear(kFogColor, kBase, b.keep));
            const Color got = Render(Family::Colored, v, kBase, 1.0f, true, kFogColor, kFogStart, kFogEnd);
            Check(RgbNear(got, exp), std::string("boundary ") + b.label, got, exp);
        }

        const Vector3 blueFog(0.1f, 0.2f, 0.9f);
        const Color expBlue = Calibrate(MixLinear(blueFog, kBase, keep));
        const Color gotBlue = Render(Family::Colored, viewFar, kBase, 1.0f, true, blueFog, kFogStart, kFogEnd);
        Check(RgbNear(gotBlue, expBlue) && !RgbNear(gotBlue, expected),
              "FogColor drives the blend (blue fog != red fog)", gotBlue, expBlue);
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

        RunColoredExact();
        RunFamily("Colored", Family::Colored);
        RunFamily("Textured", Family::Textured);
        RunFamily("ColoredTextured", Family::ColoredTextured);
        RunFamily("LitPerPixel", Family::LitPerPixel);
        RunFamily("LitPerVertex", Family::LitPerVertex);

        std::printf("=== WEBGPU-149 BasicEffect fog: %d/%d PASS ===\n", passed_, total_);
        result_ = (passed_ == total_) ? 0 : 1;
        Exit();
    }

public:
    WebGpuBasicEffectFogTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(kSize);
        manager_->setPreferredBackBufferHeightProperty(kSize);
    }
    int getResult() const { return result_; }
};

int main()
{
    WebGpuBasicEffectFogTest game;
    game.Run();
    return game.getResult();
}
