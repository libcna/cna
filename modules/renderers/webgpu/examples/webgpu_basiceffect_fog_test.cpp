// SPDX-License-Identifier: MS-PL
// WEBGPU-145: BasicEffect fog on the WebGPU renderer. Fog is the FNA CPU-prepared World*View fog
// vector (EffectHelpers.SetFogVector), dotted with the OBJECT-space vertex position in the vertex
// shader to give fogFactor = 1 - saturate(dot(pos, FogVector)); the fragment then lerps the composed
// RGB toward FogColor (ApplyFog). This exercises every BasicEffect WGSL family that gained fog:
// colored3d (stride 16), textured3d (stride 20), colored_textured3d (stride 24), and both
// lit_textured3d variants (stride 32, per-pixel and per-vertex).
//
// The render target is a Color RenderTarget2D; WebGPU may pick an sRGB storage format and GetData
// returns stored bytes, so expected values are calibrated by rendering the known linear result
// through the SAME BasicEffect path with fog OFF -- the assertions therefore never treat encoded
// bytes as linear values. FogStart==FogEnd (keep=0) collapses to exactly FogColor regardless of the
// base shading, which is the discriminator that fails the moment the WGSL fog blend (or the fog
// uniforms) are removed.

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
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

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

    // Chosen far apart in R and G so a fog blend visibly moves both channels (a weak "between" check
    // would be meaningless if base and fog were close).
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
        return Vector3(fog.X * (1.0f - keep) + base.X * keep,
                       fog.Y * (1.0f - keep) + base.Y * keep,
                       fog.Z * (1.0f - keep) + base.Z * keep);
    }

    // FNA keep factor: keep = 1 - saturate(dot(objPos, FogVector)); the centre vertex is object
    // position (0,0,0), so the whole term reduces to the M43/M33 view-Z form below.
    float ViewSpaceKeep(const Matrix& view, bool fogEnabled, float fogStart, float fogEnd)
    {
        if (!fogEnabled) return 1.0f;
        if (fogStart == fogEnd) return 0.0f;
        const float viewZ = Vector3::Transform(Vector3::Zero, view).Z;
        return 1.0f - Clamp01((viewZ + fogStart) / (fogStart - fogEnd));
    }

    Matrix Ortho() { return Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 100.0f); }
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

    enum class Family { Colored, Textured, LitPerPixel, LitPerVertex };

    Color ReadCentre()
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        target_->GetData(0, &region, &pixel, 0, 1);
        return pixel;
    }

    void ConfigureCommon(BasicEffect& e, const Matrix& view, const Vector3& diffuse,
                         bool fogEnabled, const Vector3& fogColor, float fogStart, float fogEnd)
    {
        e.setWorldProperty(Matrix::getIdentityProperty());
        e.setViewProperty(view);
        e.setProjectionProperty(Ortho());
        e.setDiffuseColorProperty(diffuse);
        e.setAlphaProperty(1.0f);
        e.VertexColorEnabled = false;
        e.setFogEnabledProperty(fogEnabled);
        e.setFogColorProperty(fogColor);
        e.setFogStartProperty(fogStart);
        e.setFogEndProperty(fogEnd);
    }

    // Renders one full-target quad through the requested BasicEffect family and returns the centre
    // pixel. Lighting is set up so a lit surface is fully illuminated head-on (N=-Z, key light +Z),
    // which keeps the lit base colour deterministic for the "between" checks.
    Color Render(Family family, const Matrix& view, const Vector3& diffuse, bool fogEnabled,
                 const Vector3& fogColor, float fogStart, float fogEnd)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(target_.get());
        device.Clear(Color(0, 0, 0, 255));
        BasicEffect effect(device);
        ConfigureCommon(effect, view, diffuse, fogEnabled, fogColor, fogStart, fogEnd);

        if (family == Family::Colored)
        {
            effect.setTextureEnabledProperty(false);
            effect.setLightingEnabledProperty(false);
            const Color c = Color(static_cast<int>(std::lround(Clamp01(diffuse.X) * 255.0f)),
                                  static_cast<int>(std::lround(Clamp01(diffuse.Y) * 255.0f)),
                                  static_cast<int>(std::lround(Clamp01(diffuse.Z) * 255.0f)), 255);
            // Diffuse drives the colour; vertex colour is disabled so it is not multiplied in.
            const VertexPositionColor verts[] = {
                {Vector3(-1, 1, 0), c}, {Vector3(-1, -1, 0), c}, {Vector3(1, -1, 0), c},
                {Vector3(-1, 1, 0), c}, {Vector3(1, -1, 0), c}, {Vector3(1, 1, 0), c},
            };
            VertexBuffer vb(device, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(verts, 0, 6);
            effect.Apply();
            device.SetVertexBuffer(&vb);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            device.SetVertexBuffer(nullptr);
        }
        else if (family == Family::Textured)
        {
            effect.setTextureEnabledProperty(true);
            effect.setTextureProperty(white_.get());
            effect.setLightingEnabledProperty(false);
            const VertexPositionTexture verts[] = {
                {Vector3(-1, 1, 0), Vector2(0, 0)}, {Vector3(-1, -1, 0), Vector2(0, 1)},
                {Vector3(1, -1, 0), Vector2(1, 1)}, {Vector3(-1, 1, 0), Vector2(0, 0)},
                {Vector3(1, -1, 0), Vector2(1, 1)}, {Vector3(1, 1, 0), Vector2(1, 0)},
            };
            VertexBuffer vb(device, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(verts, 0, 6);
            effect.Apply();
            device.SetVertexBuffer(&vb);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            device.SetVertexBuffer(nullptr);
        }
        else
        {
            effect.setTextureEnabledProperty(true);
            effect.setTextureProperty(white_.get());
            effect.setLightingEnabledProperty(true);
            effect.setPreferPerPixelLightingProperty(family == Family::LitPerPixel);
            effect.DirectionalLight0.setEnabledProperty(true);
            effect.DirectionalLight0.setDirectionProperty(Vector3(0, 0, 1)); // faces -Z normal head-on
            effect.DirectionalLight0.setDiffuseColorProperty(Vector3(1, 1, 1));
            effect.DirectionalLight0.setSpecularColorProperty(Vector3::Zero);
            effect.DirectionalLight1.setEnabledProperty(false);
            effect.DirectionalLight2.setEnabledProperty(false);
            effect.setAmbientLightColorProperty(Vector3::Zero);
            effect.setEmissiveColorProperty(Vector3::Zero);
            const Vector3 n(0, 0, -1);
            const VertexPositionNormalTexture verts[] = {
                {Vector3(-1, 1, 0), n, Vector2(0, 0)}, {Vector3(-1, -1, 0), n, Vector2(0, 1)},
                {Vector3(1, -1, 0), n, Vector2(1, 1)}, {Vector3(-1, 1, 0), n, Vector2(0, 0)},
                {Vector3(1, -1, 0), n, Vector2(1, 1)}, {Vector3(1, 1, 0), n, Vector2(1, 0)},
            };
            VertexBuffer vb(device, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(verts, 0, 6);
            effect.Apply();
            device.SetVertexBuffer(&vb);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            device.SetVertexBuffer(nullptr);
        }

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return ReadCentre();
    }

    // Renders a solid linear RGB through the fog-off Colored path to get its encoded bytes. The view
    // pushes the z=0 quad in front of the near plane (an identity view would clip it at the camera).
    Color Calibrate(const Vector3& linearRgb)
    {
        return Render(Family::Colored, Matrix::CreateTranslation(0, 0, -4.0f), linearRgb, false,
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

        // Base (fog off) -- captured through the family's own path.
        const Color base = Render(family, viewFar, kBase, false, kFogColor, kFogStart, kFogEnd);
        // Fully fogged: FogStart==FogEnd => keep=0 => output is exactly FogColor for EVERY family,
        // independent of lighting/texture. This is the discriminator that fails if fog is dropped.
        const Color full = Render(family, viewFar, kBase, true, kFogColor, 4.0f, 4.0f);
        const Color fogCal = Calibrate(kFogColor);
        Check(RgbNear(full, fogCal), std::string(name) + " FogStart==FogEnd collapses to FogColor",
              full, fogCal);

        // Half fog (keep=0.5): each channel must land between the fog-off base and FogColor.
        const Color half = Render(family, viewFar, kBase, true, kFogColor, kFogStart, kFogEnd);
        const bool between =
            ChannelBetween(half.getRProperty(), base.getRProperty(), fogCal.getRProperty()) &&
            ChannelBetween(half.getGProperty(), base.getGProperty(), fogCal.getGProperty()) &&
            ChannelBetween(half.getBProperty(), base.getBProperty(), fogCal.getBProperty());
        // ...and must be strictly moved from the base toward the fog on the two far-apart channels.
        const bool movedR = std::abs(half.getRProperty() - base.getRProperty()) > 8;
        const bool movedG = std::abs(half.getGProperty() - base.getGProperty()) > 8;
        Check(between && movedR && movedG, std::string(name) + " half fog lies between base and FogColor",
              half, base);

        // Fog OFF is unaffected by FogColor (keep=1 -> base).
        const Color off = Render(family, viewFar, kBase, false, kFogColor, kFogStart, kFogEnd);
        Check(RgbNear(off, base), std::string(name) + " fog disabled preserves base", off, base);
    }

    void RunColoredExact()
    {
        // The Colored/Textured families let us predict the exact fogged colour via calibration.
        const Matrix viewFar = Matrix::CreateTranslation(0, 0, -4.0f);
        const float keep = ViewSpaceKeep(viewFar, true, kFogStart, kFogEnd);
        const Color expected = Calibrate(MixLinear(kFogColor, kBase, keep));
        const Color gotColored = Render(Family::Colored, viewFar, kBase, true, kFogColor, kFogStart, kFogEnd);
        Check(RgbNear(gotColored, expected), "Colored half fog matches lerp(FogColor,base,keep)",
              gotColored, expected);
        const Color gotTextured = Render(Family::Textured, viewFar, kBase, true, kFogColor, kFogStart, kFogEnd);
        Check(RgbNear(gotTextured, expected), "Textured half fog matches lerp(FogColor,base,keep)",
              gotTextured, expected);

        // Boundaries: before FogStart keeps base; at/after FogEnd is fully fogged.
        const struct { const char* label; float viewZ; float keep; } bounds[] = {
            {"before FogStart", -1.0f, 1.0f}, {"at FogEnd", -6.0f, 0.0f}, {"beyond FogEnd", -8.0f, 0.0f},
        };
        for (const auto& b : bounds)
        {
            const Matrix v = Matrix::CreateTranslation(0, 0, b.viewZ);
            const Color exp = Calibrate(MixLinear(kFogColor, kBase, b.keep));
            const Color got = Render(Family::Colored, v, kBase, true, kFogColor, kFogStart, kFogEnd);
            Check(RgbNear(got, exp), std::string("boundary ") + b.label, got, exp);
        }

        // A different FogColor changes the fogged result (proves FogColor actually drives the blend).
        const Vector3 blueFog(0.1f, 0.2f, 0.9f);
        const Color expBlue = Calibrate(MixLinear(blueFog, kBase, keep));
        const Color gotBlue = Render(Family::Colored, viewFar, kBase, true, blueFog, kFogStart, kFogEnd);
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
        RunFamily("LitPerPixel", Family::LitPerPixel);
        RunFamily("LitPerVertex", Family::LitPerVertex);

        std::printf("=== WEBGPU-145 BasicEffect fog: %d/%d PASS ===\n", passed_, total_);
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
