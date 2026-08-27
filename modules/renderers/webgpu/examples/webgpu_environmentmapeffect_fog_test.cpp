// SPDX-License-Identifier: MS-PL
// REMED-GFX-100: EnvironmentMapEffect fog is the FNA CPU-prepared World*View fog vector,
// not a scalar range evaluated from raw object-space Z.
//
// This deliberately uses an off-screen Color target.  WebGPU may select an sRGB target and
// GetData returns stored bytes, so expected RGB values are calibrated by rendering the known
// linear result through EnvironmentMapEffect's full env-map replacement path.  The assertions
// therefore prove the fog semantics without treating encoded storage bytes as linear values.
//
// The fixture uses the public EnvironmentMapEffect API only.  Its fixed object-space Z is zero:
// World or View translation to view Z=-4 must produce keep=.5 for FogStart=2/FogEnd=6, whereas
// the old WGSL scalar expression always kept the unfogged result.  Orthographic projections keep
// coverage stable while proving that projection is not part of fog preparation.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 72;
    constexpr float kFogStart = 2.0f;
    constexpr float kFogEnd = 6.0f;
    constexpr float kAlpha = 0.5f;

    bool RgbNear(const Color& a, const Color& b, int tolerance = 3)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tolerance &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tolerance &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tolerance;
    }

    std::string ColorText(const Color& color)
    {
        return "(" + std::to_string(color.getRProperty()) + "," +
               std::to_string(color.getGProperty()) + "," +
               std::to_string(color.getBProperty()) + "," +
               std::to_string(color.getAProperty()) + ")";
    }

    float Clamp01(float value)
    {
        return std::max(0.0f, std::min(1.0f, value));
    }

    Vector3 Add(const Vector3& a, const Vector3& b)
    {
        return Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    }

    Vector3 Multiply(const Vector3& a, const Vector3& b)
    {
        return Vector3(a.X * b.X, a.Y * b.Y, a.Z * b.Z);
    }

    Vector3 Scale(const Vector3& value, float scalar)
    {
        return Vector3(value.X * scalar, value.Y * scalar, value.Z * scalar);
    }

    Color ColorFromLinear(const Vector3& value)
    {
        const auto channel = [](float v)
        {
            return static_cast<int>(std::lround(Clamp01(v) * 255.0f));
        };
        return Color(channel(value.X), channel(value.Y), channel(value.Z), 255);
    }

    Matrix Ortho(float width = 2.0f)
    {
        return Matrix::CreateOrthographic(width, width, 0.1f, 100.0f);
    }

    // The exact public FNA contract: keep=1-saturate(dot(float4(objectPosition,1), FogVector)),
    // where FogVector is formed from World*View.  The centre vertex has object position (0,0,0).
    float ViewSpaceKeep(const Matrix& world, const Matrix& view, bool fogEnabled,
                        float fogStart = kFogStart, float fogEnd = kFogEnd)
    {
        if (!fogEnabled)
            return 1.0f;
        if (fogStart == fogEnd)
            return 0.0f;

        Matrix worldView;
        Matrix::Multiply(world, view, worldView);
        const float viewZ = Vector3::Transform(Vector3::Zero, worldView).Z;
        const float fog = Clamp01((viewZ + fogStart) / (fogStart - fogEnd));
        return 1.0f - fog;
    }

    std::unique_ptr<TextureCube> MakeSolidCube(GraphicsDevice& device, const Color& color)
    {
        auto cube = std::make_unique<TextureCube>(device, 1, false, SurfaceFormat::Color);
        constexpr CubeMapFace faces[] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (const CubeMapFace face : faces)
            cube->SetData(face, &color, 1);
        return cube;
    }

    VertexBuffer MakeQuad(GraphicsDevice& device, float xMin = -1.0f, float xMax = 1.0f)
    {
        VertexBuffer buffer(device, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6,
                            BufferUsage::None);
        // The object-space z coordinate deliberately remains zero in every transform test.
        // A -Z normal and a +Z light direction yield N dot -L = 1.
        const Vector3 normal(0.0f, 0.0f, -1.0f);
        const VertexPositionNormalTexture vertices[] = {
            {Vector3(xMin,  1.0f, 0.0f), normal, Vector2(0.0f, 0.0f)},
            {Vector3(xMin, -1.0f, 0.0f), normal, Vector2(0.0f, 1.0f)},
            {Vector3(xMax, -1.0f, 0.0f), normal, Vector2(1.0f, 1.0f)},
            {Vector3(xMin,  1.0f, 0.0f), normal, Vector2(0.0f, 0.0f)},
            {Vector3(xMax, -1.0f, 0.0f), normal, Vector2(1.0f, 1.0f)},
            {Vector3(xMax,  1.0f, 0.0f), normal, Vector2(1.0f, 0.0f)},
        };
        buffer.SetData(vertices, 0, 6);
        return buffer;
    }
}

class WebGpuEnvironmentMapEffectFogTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<Texture2D> texture_;
    std::unique_ptr<Texture2D> whiteTexture_;
    std::unique_ptr<TextureCube> environment_;
    std::unique_ptr<RenderTarget2D> target_;
    bool done_ = false;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    const Vector3 diffuse_{0.6f, 0.4f, 0.8f};
    const Vector3 ambient_{0.1f, 0.2f, 0.1f};
    const Vector3 emissive_{0.2f, 0.1f, 0.25f};
    const Vector3 directional_{0.4f, 0.5f, 0.6f};
    const Vector3 fogColor_{0.8f, 0.1f, 0.2f};
    const Vector3 environmentRgb_{51.0f / 255.0f, 204.0f / 255.0f, 102.0f / 255.0f};
    const float textureAlpha_ = 160.0f / 255.0f;
    const float environmentAlpha_ = 192.0f / 255.0f;
    const float environmentAmount_ = 0.35f;

    void Check(bool ok, const std::string& label, const Color& got, const Color& expected)
    {
        ++total_;
        if (ok)
            ++passed_;
        std::printf("[%s] %s got=%s expected~=%s\n", ok ? "PASS" : "FAIL", label.c_str(),
                    ColorText(got).c_str(), ColorText(expected).c_str());
    }

    Color ReadTarget(int x = kSize / 2, int y = kSize / 2)
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        target_->GetData(0, &region, &pixel, 0, 1);
        return pixel;
    }

    Vector3 ComposedRgb() const
    {
        // EnvironmentMapEffect's established ordering, matching the already-correct renderers:
        // (directional*Diffuse*Alpha + (Emissive + Ambient*Diffuse)*Alpha) * Texture;
        // then environment blend/specular.  Fog is deliberately not included here.
        const Vector3 lit = Scale(Add(Multiply(directional_, diffuse_),
                                      Add(emissive_, Multiply(ambient_, diffuse_))), kAlpha);
        const Vector3 base = Multiply(lit, Vector3(128.0f / 255.0f, 64.0f / 255.0f, 192.0f / 255.0f));
        const float combinedAlpha = kAlpha * textureAlpha_;
        const Vector3 environmentBase = Scale(environmentRgb_, combinedAlpha);
        const Vector3 mixed = Add(Scale(base, 1.0f - environmentAmount_),
                                  Scale(environmentBase, environmentAmount_));
        const Vector3 specular(0.1f, 0.05f, 0.2f);
        return Add(mixed, Scale(specular, environmentAlpha_ * combinedAlpha));
    }

    Vector3 FoggedRgb(float keep) const
    {
        // WEBGPU-149: FNA ApplyFog lerps toward FogColor * OUTPUT ALPHA, not plain FogColor
        // (Common.fxh: color.rgb = lerp(color.rgb, FogColor * color.a, fogFactor)). The env-map
        // output alpha here is combinedAlpha = kAlpha * textureAlpha_, so the fog target is scaled
        // by it. keep = 1 - fogFactor.
        const float outputAlpha = kAlpha * textureAlpha_;
        return Add(Scale(fogColor_, outputAlpha * (1.0f - keep)), Scale(ComposedRgb(), keep));
    }

    void Configure(EnvironmentMapEffect& effect, const Matrix& world, const Matrix& view,
                   const Matrix& projection, bool fogEnabled, float fogStart = kFogStart,
                   float fogEnd = kFogEnd)
    {
        effect.setWorldProperty(world);
        effect.setViewProperty(view);
        effect.setProjectionProperty(projection);
        effect.setTextureProperty(texture_.get());
        effect.setEnvironmentMapProperty(environment_.get());
        effect.setDiffuseColorProperty(diffuse_);
        effect.setAmbientLightColorProperty(ambient_);
        effect.setEmissiveColorProperty(emissive_);
        effect.setAlphaProperty(kAlpha);
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, 1.0f));
        effect.DirectionalLight0.setDiffuseColorProperty(directional_);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);
        effect.setEnvironmentMapAmountProperty(environmentAmount_);
        effect.setEnvironmentMapSpecularProperty(Vector3(0.1f, 0.05f, 0.2f));
        effect.setFresnelFactorProperty(0.0f);
        effect.setFogEnabledProperty(fogEnabled);
        effect.setFogColorProperty(fogColor_);
        effect.setFogStartProperty(fogStart);
        effect.setFogEndProperty(fogEnd);
    }

    Color Render(const Matrix& world, const Matrix& view, const Matrix& projection,
                 bool fogEnabled, float fogStart = kFogStart, float fogEnd = kFogEnd)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(target_.get());
        device.Clear(Color(0, 0, 0, 0));
        VertexBuffer buffer = MakeQuad(device);
        EnvironmentMapEffect effect(device);
        Configure(effect, world, view, projection, fogEnabled, fogStart, fogEnd);
        effect.Apply();
        device.SetVertexBuffer(&buffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return ReadTarget();
    }

    // Calibrates T(linearRgb) through the exact EnvironmentMapEffect render target path.  The
    // full environment-map replacement has combinedAlpha=1 and FogVector=0, so RGB is exactly the
    // supplied cube colour independent of lighting or reflection direction.
    Color Calibrate(const Vector3& linearRgb)
    {
        auto& device = getGraphicsDeviceProperty();
        auto calibrationCube = MakeSolidCube(device, ColorFromLinear(linearRgb));
        device.SetRenderTarget(target_.get());
        device.Clear(Color(0, 0, 0, 0));
        VertexBuffer buffer = MakeQuad(device);
        EnvironmentMapEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::CreateTranslation(0.0f, 0.0f, -4.0f));
        effect.setProjectionProperty(Ortho());
        effect.setTextureProperty(whiteTexture_.get());
        effect.setEnvironmentMapProperty(calibrationCube.get());
        effect.setEnvironmentMapAmountProperty(1.0f);
        effect.setEnvironmentMapSpecularProperty(Vector3::Zero);
        effect.setFresnelFactorProperty(0.0f);
        effect.setFogEnabledProperty(false);
        effect.Apply();
        device.SetVertexBuffer(&buffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return ReadTarget();
    }

    void CheckRender(const char* label, const Matrix& world, const Matrix& view,
                     const Matrix& projection, bool fogEnabled, float fogStart = kFogStart,
                     float fogEnd = kFogEnd)
    {
        const float keep = ViewSpaceKeep(world, view, fogEnabled, fogStart, fogEnd);
        const Color expected = Calibrate(FoggedRgb(keep));
        const Color got = Render(world, view, projection, fogEnabled, fogStart, fogEnd);
        Check(RgbNear(got, expected), std::string(label) + " RGB", got, expected);

        const int expectedAlpha = static_cast<int>(std::lround(kAlpha * textureAlpha_ * 255.0f));
        const Color alphaExpected(0, 0, 0, expectedAlpha);
        Check(std::abs(got.getAProperty() - expectedAlpha) <= 1,
              std::string(label) + " preserves Alpha while fogging RGB", got, alphaExpected);
    }

    void CheckEnabledAtoBtoA()
    {
        auto& device = getGraphicsDeviceProperty();
        const Matrix world = Matrix::getIdentityProperty();
        const Matrix view = Matrix::CreateTranslation(0.0f, 0.0f, -4.0f);
        const Color expectedA = Calibrate(FoggedRgb(1.0f));
        const Color expectedB = Calibrate(FoggedRgb(0.5f));

        device.SetRenderTarget(target_.get());
        device.Clear(Color(0, 0, 0, 0));
        std::vector<VertexPositionNormalTexture> vertices;
        const auto append = [&](float x0, float x1)
        {
            const Vector3 normal(0.0f, 0.0f, -1.0f);
            vertices.insert(vertices.end(), {
                {Vector3(x0,  1, 0), normal, Vector2(0, 0)}, {Vector3(x0, -1, 0), normal, Vector2(0, 1)},
                {Vector3(x1, -1, 0), normal, Vector2(1, 1)}, {Vector3(x0,  1, 0), normal, Vector2(0, 0)},
                {Vector3(x1, -1, 0), normal, Vector2(1, 1)}, {Vector3(x1,  1, 0), normal, Vector2(1, 0)},
            });
        };
        append(-1.0f, -1.0f / 3.0f); append(-1.0f / 3.0f, 1.0f / 3.0f); append(1.0f / 3.0f, 1.0f);
        VertexBuffer buffer(device, VertexPositionNormalTexture::getVertexDeclarationStatic(),
                            static_cast<int>(vertices.size()), BufferUsage::None);
        buffer.SetData(vertices.data(), 0, static_cast<int>(vertices.size()));
        device.SetVertexBuffer(&buffer);
        EnvironmentMapEffect effect(device);
        Configure(effect, world, view, Ortho(), false);
        effect.Apply(); device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        Configure(effect, world, view, Ortho(), true);
        effect.Apply(); device.DrawPrimitives(PrimitiveType::TriangleList, 6, 2);
        Configure(effect, world, view, Ortho(), false);
        effect.Apply(); device.DrawPrimitives(PrimitiveType::TriangleList, 12, 2);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const Color a1 = ReadTarget(kSize / 6, kSize / 2);
        const Color b = ReadTarget(kSize / 2, kSize / 2);
        const Color a2 = ReadTarget((kSize * 5) / 6, kSize / 2);
        Check(RgbNear(a1, expectedA), "FogEnabled A=false uses zero-vector/unfogged state", a1, expectedA);
        Check(RgbNear(b, expectedB), "FogEnabled B=true uses prepared fog vector", b, expectedB);
        Check(RgbNear(a2, expectedA) && RgbNear(a2, a1),
              "FogEnabled A->B->A restores zero-vector state", a2, expectedA);
    }

    void CheckTransformAtoBtoA()
    {
        auto& device = getGraphicsDeviceProperty();
        const Matrix world = Matrix::getIdentityProperty();
        const Matrix viewA = Matrix::CreateTranslation(0.0f, 0.0f, -2.0f);
        const Matrix viewB = Matrix::CreateTranslation(0.0f, 0.0f, -6.0f);
        const Color expectedA = Calibrate(FoggedRgb(1.0f));
        const Color expectedB = Calibrate(FoggedRgb(0.0f));

        device.SetRenderTarget(target_.get());
        device.Clear(Color(0, 0, 0, 0));
        std::vector<VertexPositionNormalTexture> vertices;
        for (const auto& range : {std::pair<float, float>{-1.0f, -1.0f / 3.0f},
                                  {-1.0f / 3.0f, 1.0f / 3.0f}, {1.0f / 3.0f, 1.0f}})
        {
            const Vector3 normal(0, 0, -1);
            vertices.insert(vertices.end(), {
                {Vector3(range.first,  1, 0), normal, Vector2(0, 0)}, {Vector3(range.first, -1, 0), normal, Vector2(0, 1)},
                {Vector3(range.second, -1, 0), normal, Vector2(1, 1)}, {Vector3(range.first,  1, 0), normal, Vector2(0, 0)},
                {Vector3(range.second, -1, 0), normal, Vector2(1, 1)}, {Vector3(range.second,  1, 0), normal, Vector2(1, 0)},
            });
        }
        VertexBuffer buffer(device, VertexPositionNormalTexture::getVertexDeclarationStatic(),
                            static_cast<int>(vertices.size()), BufferUsage::None);
        buffer.SetData(vertices.data(), 0, static_cast<int>(vertices.size()));
        device.SetVertexBuffer(&buffer);
        EnvironmentMapEffect effect(device);
        Configure(effect, world, viewA, Ortho(), true);
        effect.Apply(); device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        Configure(effect, world, viewB, Ortho(), true);
        effect.Apply(); device.DrawPrimitives(PrimitiveType::TriangleList, 6, 2);
        Configure(effect, world, viewA, Ortho(), true);
        effect.Apply(); device.DrawPrimitives(PrimitiveType::TriangleList, 12, 2);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const Color a1 = ReadTarget(kSize / 6, kSize / 2);
        const Color b = ReadTarget(kSize / 2, kSize / 2);
        const Color a2 = ReadTarget((kSize * 5) / 6, kSize / 2);
        Check(RgbNear(a1, expectedA), "transform A is unfogged", a1, expectedA);
        Check(RgbNear(b, expectedB), "transform B is fully fogged", b, expectedB);
        Check(RgbNear(a2, expectedA) && RgbNear(a2, a1),
              "transform A->B->A captures per-draw prepared vectors", a2, expectedA);
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        const Color textureColor(128, 64, 192, 160);
        const Color white(255, 255, 255, 255);
        texture_ = std::make_unique<Texture2D>(device, 1, 1);
        texture_->SetData(&textureColor, 1);
        whiteTexture_ = std::make_unique<Texture2D>(device, 1, 1);
        whiteTexture_->SetData(&white, 1);
        environment_ = MakeSolidCube(device, Color(51, 204, 102, 192));
        target_ = std::make_unique<RenderTarget2D>(device, kSize, kSize, false, SurfaceFormat::Color,
                                                    DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        RasterizerState scissored = RasterizerState::CullNone;
        scissored.setScissorTestEnableProperty(true);
        device.setRasterizerStateProperty(scissored);
        device.setScissorRectangleProperty(Rectangle(0, 0, kSize, kSize));
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setViewportProperty(Viewport(0, 0, kSize, kSize));
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;

        const Matrix identity = Matrix::getIdentityProperty();
        const Matrix halfWorld = Matrix::CreateTranslation(0.0f, 0.0f, -4.0f);
        const Matrix halfView = Matrix::CreateTranslation(0.0f, 0.0f, -4.0f);

        // Mandatory discriminators: identical object-space Z, but independently transformed
        // World and View depths.  The old `input.position.z` scalar path returns the same result.
        CheckRender("World translation to view-space depth -4", halfWorld, identity, Ortho(), true);
        CheckRender("View translation to view-space depth -4", identity, halfView, Ortho(), true);

        // A nontrivial view rotation/translation keeps the on-axis origin at depth -4.
        CheckRender("rotated LookAt view changes view-space fog", identity,
                    Matrix::CreateLookAt(Vector3(2.4f, 0.0f, 3.2f), Vector3::Zero, Vector3(0, 1, 0)),
                    Ortho(), true);

        // Both projections cover the centre sample, yet their fog expectation is identical.
        CheckRender("projection A excluded from fog", halfWorld, identity, Ortho(2.0f), true);
        CheckRender("projection B excluded from fog", halfWorld, identity, Ortho(3.0f), true);

        CheckEnabledAtoBtoA();
        CheckTransformAtoBtoA();

        const struct Boundary { const char* label; float viewZ; float keep; } boundaries[] = {
            {"before FogStart", -1.0f, 1.0f}, {"at FogStart", -2.0f, 1.0f},
            {"between FogStart/FogEnd", -4.0f, 0.5f}, {"at FogEnd", -6.0f, 0.0f},
            {"beyond FogEnd", -7.0f, 0.0f},
        };
        for (const Boundary& boundary : boundaries)
        {
            const Matrix view = Matrix::CreateTranslation(0.0f, 0.0f, boundary.viewZ);
            const float computed = ViewSpaceKeep(identity, view, true);
            const Color expected = Calibrate(FoggedRgb(boundary.keep));
            const Color got = Render(identity, view, Ortho(), true);
            Check(std::abs(computed - boundary.keep) < 0.0001f && RgbNear(got, expected),
                  std::string("boundary ") + boundary.label, got, expected);
        }

        CheckRender("FogStart == FogEnd is fully fogged", identity, halfView, Ortho(), true, 4.0f, 4.0f);

        // This exact half-fog case has non-white texture/cube/material, nonzero ambient/emissive,
        // directional light, environment/specular amount, and Alpha.  It proves that fog receives
        // the complete EnvironmentMapEffect RGB and preserves the established output alpha.
        CheckRender("complete material/environment composition is fogged last", halfWorld, identity, Ortho(), true);

        std::printf("=== REMED-GFX-100 WebGPU EnvironmentMap fog: %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    WebGpuEnvironmentMapEffectFogTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(kSize);
        manager_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuEnvironmentMapEffectFogTest game;
    game.Run();
    return game.getResult();
}
