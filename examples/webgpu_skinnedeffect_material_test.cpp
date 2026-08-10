// SPDX-License-Identifier: MS-PL
// REMED-GFX-090: WebGPU SkinnedEffect must consume the CPU-pre-folded material exactly like
// FNA/XNA and CNA's correct renderers:
//
//   DiffuseShader.rgb = DiffuseColor * Alpha
//   DiffuseShader.a   = Alpha
//   EmissiveShader    = (EmissiveColor + AmbientLightColor * DiffuseColor) * Alpha
//   baseRGB           = directionalLightSum * DiffuseShader.rgb + EmissiveShader
//   outputRGB         = baseRGB * texture.rgb * optionalVertexColor.rgb
//   outputA           = Alpha * texture.a * optionalVertexColor.a
//
// The old four embedded WGSL variants instead computed
// `(EmissiveShader + directionalLightSum) * DiffuseShader.rgb`. That re-multiplied both the
// ambient-prefolded and pure-emissive terms by DiffuseColor, and also applied Alpha a second time.
//
// VARIANT COVERAGE. The core ambient/emissive/directional matrix runs through all four shader
// modules selected by GetOrCreatePipelineSkinned3D:
//   - per-pixel lighting, stride 52 (plain);
//   - per-pixel lighting, stride 56 (vertex colour);
//   - per-vertex lighting, stride 52 (plain);
//   - per-vertex lighting, stride 56 (vertex colour).
// Vertex-colour variants use a white enabled vertex colour in the core matrix, then both get a
// separate non-white vertex-colour discriminator.
//
// COLOR SPACE. WebGPU RenderTarget2D mirrors the chosen swapchain format, normally *_Srgb, and
// GetData returns stored bytes without decoding. Every algebra assertion therefore compares the
// discriminating non-white-Diffuse draw against a calibration draw through the SAME shader module,
// texture, vertex-colour, target, and output encoding. The calibration uses Diffuse=white,
// Alpha=1, no light, and Emissive=the analytically expected pre-texture base RGB; old and correct
// formulas coincide for that control. Equality to the calibration proves the material algebra
// without assuming linear or sRGB target encoding.
//
// Fog is deliberately not fabricated here: WebGPU's non-environment-map 3D shaders do not yet
// upload or consume GpuDrawParams::fogVector, an existing renderer capability gap recorded before
// GFX-090. This test changes and verifies only the reachable material path.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
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

    struct SkinnedGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedGpuVertex) == 52, "skinned vertex must be 52 bytes");

    struct SkinnedColorGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(SkinnedColorGpuVertex) == 56, "skinned+color vertex must be 56 bytes");

    struct Variant
    {
        const char* name;
        bool preferPerPixel;
        bool vertexColor;
    };

    constexpr Variant kVariants[] = {
        {"per-pixel/plain",        true,  false},
        {"per-pixel/vertex-color", true,  true},
        {"per-vertex/plain",       false, false},
        {"per-vertex/vertex-color",false, true},
    };

    struct Material
    {
        Vector3 diffuse;
        Vector3 ambient;
        Vector3 emissive;
        Vector3 lightDiffuse;
        float alpha = 1.0f;
    };

    Vector3 Add(const Vector3& a, const Vector3& b)
    {
        return Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    }

    Vector3 Multiply(const Vector3& a, const Vector3& b)
    {
        return Vector3(a.X * b.X, a.Y * b.Y, a.Z * b.Z);
    }

    Vector3 Scale(const Vector3& v, float s)
    {
        return Vector3(v.X * s, v.Y * s, v.Z * s);
    }

    Vector3 ExpectedBase(const Material& m)
    {
        // N dot -L is exactly one in this fixture.
        return Scale(Add(Add(Multiply(m.ambient, m.diffuse),
                             Multiply(m.lightDiffuse, m.diffuse)),
                         m.emissive),
                     m.alpha);
    }

    bool RgbNear(const Color& a, const Color& b, int tolerance = 3)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tolerance
            && std::abs(a.getGProperty() - b.getGProperty()) <= tolerance
            && std::abs(a.getBProperty() - b.getBProperty()) <= tolerance;
    }

    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + ","
                   + std::to_string(c.getGProperty()) + ","
                   + std::to_string(c.getBProperty()) + ","
                   + std::to_string(c.getAProperty()) + ")";
    }

    void AppendPlainQuad(std::vector<SkinnedGpuVertex>& out, float xMin, float xMax)
    {
        // Normal=-Z, light direction=+Z => dot(N,-L)=1. Position z=.5 keeps the eye vector
        // non-zero for the shader's (zero-colour) specular calculation.
        const SkinnedGpuVertex tl{xMin,  1.0f, .5f, 0,0,-1, 0,0, 1,0,0,0, 0,0,0,0};
        const SkinnedGpuVertex bl{xMin, -1.0f, .5f, 0,0,-1, 0,1, 1,0,0,0, 0,0,0,0};
        const SkinnedGpuVertex br{xMax, -1.0f, .5f, 0,0,-1, 1,1, 1,0,0,0, 0,0,0,0};
        const SkinnedGpuVertex tr{xMax,  1.0f, .5f, 0,0,-1, 1,0, 1,0,0,0, 0,0,0,0};
        out.insert(out.end(), {tl, bl, br, tl, br, tr});
    }

    void AppendColorQuad(std::vector<SkinnedColorGpuVertex>& out, float xMin, float xMax,
                         const Color& color)
    {
        const auto r = static_cast<std::uint8_t>(color.getRProperty());
        const auto g = static_cast<std::uint8_t>(color.getGProperty());
        const auto b = static_cast<std::uint8_t>(color.getBProperty());
        const auto a = static_cast<std::uint8_t>(color.getAProperty());
        const SkinnedColorGpuVertex tl{xMin,  1.0f, .5f, 0,0,-1, 0,0, 1,0,0,0, 0,0,0,0, r,g,b,a};
        const SkinnedColorGpuVertex bl{xMin, -1.0f, .5f, 0,0,-1, 0,1, 1,0,0,0, 0,0,0,0, r,g,b,a};
        const SkinnedColorGpuVertex br{xMax, -1.0f, .5f, 0,0,-1, 1,1, 1,0,0,0, 0,0,0,0, r,g,b,a};
        const SkinnedColorGpuVertex tr{xMax,  1.0f, .5f, 0,0,-1, 1,0, 1,0,0,0, 0,0,0,0, r,g,b,a};
        out.insert(out.end(), {tl, bl, br, tl, br, tr});
    }
}

class WebGpuSkinnedEffectMaterialTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> whiteTexture_;
    std::unique_ptr<Texture2D> coloredTexture_;
    std::unique_ptr<RenderTarget2D> target_;
    bool done_ = false;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label, const Color& got, const Color& expected)
    {
        ++total_;
        if (ok) ++passed_;
        std::printf("[%s] %s got=%s expected~=%s\n",
                    ok ? "PASS" : "FAIL", label.c_str(),
                    ColorText(got).c_str(), ColorText(expected).c_str());
    }

    Color ReadTarget(int x = kSize / 2, int y = kSize / 2)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        target_->GetData(0, &region, &pixel, 0, 1);
        return pixel;
    }

    void ConfigureEffect(SkinnedEffect& fx, Texture2D& texture, const Variant& variant,
                         const Material& material)
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(&texture);
        fx.SetBoneTransforms({Matrix::getIdentityProperty()});
        fx.setWeightsPerVertexProperty(1);
        fx.setPreferPerPixelLightingProperty(variant.preferPerPixel);
        fx.VertexColorEnabled = variant.vertexColor;
        fx.setDiffuseColorProperty(material.diffuse);
        fx.setAmbientLightColorProperty(material.ambient);
        fx.setEmissiveColorProperty(material.emissive);
        fx.setAlphaProperty(material.alpha);
        fx.setSpecularColorProperty(Vector3::Zero);
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, 1.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(material.lightDiffuse);
        fx.DirectionalLight0.setSpecularColorProperty(Vector3::Zero);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setFogEnabledProperty(false);
    }

    VertexBuffer MakeQuad(GraphicsDevice& dev, const Variant& variant, const Color& vertexColor,
                          float xMin = -1.0f, float xMax = 1.0f)
    {
        if (variant.vertexColor)
        {
            std::vector<SkinnedColorGpuVertex> vertices;
            AppendColorQuad(vertices, xMin, xMax, vertexColor);
            VertexBuffer vb(dev, static_cast<int>(vertices.size()));
            vb.SetDataRaw(vertices.data(), static_cast<int>(vertices.size()),
                          static_cast<int>(sizeof(SkinnedColorGpuVertex)));
            return vb;
        }

        std::vector<SkinnedGpuVertex> vertices;
        AppendPlainQuad(vertices, xMin, xMax);
        VertexBuffer vb(dev, static_cast<int>(vertices.size()));
        vb.SetDataRaw(vertices.data(), static_cast<int>(vertices.size()),
                      static_cast<int>(sizeof(SkinnedGpuVertex)));
        return vb;
    }

    Color RenderMaterial(const Variant& variant, const Material& material, Texture2D& texture,
                         const Color& vertexColor = Color::White)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetRenderTarget(target_.get());
        dev.Clear(Color(0, 0, 0, 0));

        VertexBuffer vb = MakeQuad(dev, variant, vertexColor);
        SkinnedEffect fx(dev);
        ConfigureEffect(fx, texture, variant, material);
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return ReadTarget();
    }

    Color RenderCalibration(const Variant& variant, const Material& material, Texture2D& texture,
                            const Color& vertexColor = Color::White)
    {
        // With Diffuse=white and Alpha=1 the old and corrected expressions coincide, so this is
        // an independent encoding/path oracle for the analytically expected base RGB.
        const Material calibration{
            Vector3(1.0f, 1.0f, 1.0f),
            Vector3::Zero,
            ExpectedBase(material),
            Vector3::Zero,
            1.0f,
        };
        return RenderMaterial(variant, calibration, texture, vertexColor);
    }

    void CheckMaterialCase(const Variant& variant, const char* caseName, const Material& material,
                           Texture2D& texture, const Color& vertexColor = Color::White)
    {
        const Color expected = RenderCalibration(variant, material, texture, vertexColor);
        const Color got = RenderMaterial(variant, material, texture, vertexColor);
        Check(RgbNear(got, expected),
              std::string(variant.name) + " / " + caseName,
              got, expected);
    }

    void CheckAlphaCase(const Variant& variant, const char* caseName, const Material& material,
                        Texture2D& texture, int textureAlpha, const Color& vertexColor)
    {
        const Color expectedRgb = RenderCalibration(variant, material, texture, vertexColor);
        const Color got = RenderMaterial(variant, material, texture, vertexColor);
        const Color expectedRgbDisplay(
            static_cast<int>(expectedRgb.getRProperty()),
            static_cast<int>(expectedRgb.getGProperty()),
            static_cast<int>(expectedRgb.getBProperty()),
            static_cast<int>(got.getAProperty()));
        Check(RgbNear(got, expectedRgb),
              std::string(variant.name) + " / " + caseName + " RGB",
              got, expectedRgbDisplay);

        const int vcAlpha = variant.vertexColor ? vertexColor.getAProperty() : 255;
        const int expectedAlpha = static_cast<int>(
            std::lround(material.alpha * static_cast<float>(textureAlpha)
                        * static_cast<float>(vcAlpha) / 255.0f));
        const Color expectedA(0, 0, 0, expectedAlpha);
        Check(std::abs(got.getAProperty() - expectedAlpha) <= 1,
              std::string(variant.name) + " / " + caseName
                  + " output Alpha is Alpha*texture.a*vertexColor.a",
              Color(0, 0, 0, static_cast<int>(got.getAProperty())), expectedA);
    }

    void CheckAtoBtoA(const Material& materialA, const Material& materialB)
    {
        auto& dev = getGraphicsDeviceProperty();
        const Variant variant{"per-pixel/plain", true, false};
        const Color expectedA = RenderCalibration(variant, materialA, *whiteTexture_);
        const Color expectedB = RenderCalibration(variant, materialB, *whiteTexture_);

        std::vector<SkinnedGpuVertex> vertices;
        AppendPlainQuad(vertices, -1.0f, -1.0f / 3.0f);
        AppendPlainQuad(vertices, -1.0f / 3.0f, 1.0f / 3.0f);
        AppendPlainQuad(vertices, 1.0f / 3.0f, 1.0f);
        VertexBuffer vb(dev, static_cast<int>(vertices.size()));
        vb.SetDataRaw(vertices.data(), static_cast<int>(vertices.size()),
                      static_cast<int>(sizeof(SkinnedGpuVertex)));

        dev.SetRenderTarget(target_.get());
        dev.Clear(Color(0, 0, 0, 0));
        dev.SetVertexBuffer(&vb);
        SkinnedEffect fx(dev);

        ConfigureEffect(fx, *whiteTexture_, variant, materialA);
        fx.Apply();
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        ConfigureEffect(fx, *whiteTexture_, variant, materialB);
        fx.Apply();
        dev.DrawPrimitives(PrimitiveType::TriangleList, 6, 2);

        ConfigureEffect(fx, *whiteTexture_, variant, materialA);
        fx.Apply();
        dev.DrawPrimitives(PrimitiveType::TriangleList, 12, 2);

        dev.SetVertexBuffer(nullptr);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const Color gotA1 = ReadTarget(kSize / 6, kSize / 2);
        const Color gotB  = ReadTarget(kSize / 2, kSize / 2);
        const Color gotA2 = ReadTarget((kSize * 5) / 6, kSize / 2);
        Check(RgbNear(gotA1, expectedA), "A->B->A first A uses captured material A", gotA1, expectedA);
        Check(RgbNear(gotB, expectedB), "A->B->A middle draw uses captured material B", gotB, expectedB);
        Check(RgbNear(gotA2, expectedA) && RgbNear(gotA2, gotA1),
              "A->B->A final A restores A (no last-state-wins)", gotA2, expectedA);
    }

    void CheckPostDrawMutation(const Material& materialA, const Material& materialB)
    {
        auto& dev = getGraphicsDeviceProperty();
        const Variant variant{"per-vertex/plain", false, false};
        // Compare against the same actual A path, so this lifetime check is independent of whether
        // the material expression itself is in its pre-fix or post-fix form.
        const Color expectedA = RenderMaterial(variant, materialA, *whiteTexture_);

        dev.SetRenderTarget(target_.get());
        dev.Clear(Color(0, 0, 0, 0));
        VertexBuffer vb = MakeQuad(dev, variant, Color::White);
        dev.SetVertexBuffer(&vb);
        SkinnedEffect fx(dev);
        ConfigureEffect(fx, *whiteTexture_, variant, materialA);
        fx.Apply();
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        // Mutate and apply the instance after the draw was queued, without issuing another draw.
        ConfigureEffect(fx, *whiteTexture_, variant, materialB);
        fx.Apply();

        dev.SetVertexBuffer(nullptr);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const Color got = ReadTarget();
        Check(RgbNear(got, expectedA),
              "post-draw effect mutation does not retroactively alter the queued draw",
              got, expectedA);
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        whiteTexture_ = std::make_unique<Texture2D>(dev, 1, 1);
        const Color white(255, 255, 255, 255);
        whiteTexture_->SetData(&white, 1);

        coloredTexture_ = std::make_unique<Texture2D>(dev, 1, 1);
        const Color colored(128, 64, 192, 160);
        coloredTexture_->SetData(&colored, 1);

        target_ = std::make_unique<RenderTarget2D>(
            dev, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::DiscardContents);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;

        const Vector3 nonWhiteDiffuse(0.25f, 0.5f, 0.75f);
        const Material ambientOnly{
            nonWhiteDiffuse,
            Vector3(0.5f, 0.25f, 0.125f),
            Vector3::Zero,
            Vector3::Zero,
            1.0f,
        };
        const Material emissiveOnly{
            nonWhiteDiffuse,
            Vector3::Zero,
            Vector3(0.5f, 0.25f, 0.125f),
            Vector3::Zero,
            1.0f,
        };
        const Material combined{
            nonWhiteDiffuse,
            Vector3(0.25f, 0.5f, 0.25f),
            Vector3(0.125f, 0.0625f, 0.25f),
            Vector3::Zero,
            1.0f,
        };
        const Material directional{
            nonWhiteDiffuse,
            Vector3::Zero,
            Vector3::Zero,
            Vector3(0.5f, 0.25f, 0.75f),
            1.0f,
        };

        // Core formula coverage: all four embedded WGSL modules.
        for (const Variant& variant : kVariants)
        {
            CheckMaterialCase(variant, "ambient only", ambientOnly, *whiteTexture_);
            CheckMaterialCase(variant, "pure emissive", emissiveOnly, *whiteTexture_);
            CheckMaterialCase(variant, "ambient + emissive", combined, *whiteTexture_);
            CheckMaterialCase(variant, "directional diffuse control", directional, *whiteTexture_);
        }

        // Non-white texture multiplies the completed material result, not only one term.
        CheckMaterialCase(kVariants[0], "non-white RGBA texture", combined, *coloredTexture_);

        // Non-trivial Alpha: RGB is pre-folded exactly once; output A follows the separate
        // Alpha*texture.a path.
        Material alphaCase = emissiveOnly;
        alphaCase.alpha = 0.5f;
        CheckAlphaCase(kVariants[2], "nontrivial Alpha", alphaCase,
                       *coloredTexture_, 160, Color::White);

        // Both vertex-colour shader families apply a non-white RGBA vertex colour to the final
        // material+texture result and to output alpha, preserving CNA's extension semantics.
        const Color vertexColor(128, 192, 64, 128);
        Material vertexCase = combined;
        vertexCase.alpha = 0.75f;
        CheckAlphaCase(kVariants[1], "non-white vertex color", vertexCase,
                       *coloredTexture_, 160, vertexColor);
        CheckAlphaCase(kVariants[3], "non-white vertex color", vertexCase,
                       *coloredTexture_, 160, vertexColor);

        CheckAtoBtoA(ambientOnly, emissiveOnly);
        CheckPostDrawMutation(ambientOnly, emissiveOnly);

        // A 0.5-linear calibration distinguishes the usual sRGB target (~188) from linear
        // (~128) without making either a prerequisite for the semantic comparisons above.
        const Material half{
            Vector3(1.0f, 1.0f, 1.0f), Vector3::Zero,
            Vector3(0.5f, 0.5f, 0.5f), Vector3::Zero, 1.0f,
        };
        const Color transferProbe = RenderMaterial(kVariants[0], half, *whiteTexture_);
        const char* targetEncoding = transferProbe.getRProperty() > 160 ? "sRGB" : "linear";
        std::printf("[INFO] target encoding probe linear(0.5) -> %d: %s attachment\n",
                    transferProbe.getRProperty(), targetEncoding);
        std::printf("=== %d/%d PASS ===\n", passed_, total_);

        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    WebGpuSkinnedEffectMaterialTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuSkinnedEffectMaterialTest game;
    game.Run();
    return game.getResult();
}
