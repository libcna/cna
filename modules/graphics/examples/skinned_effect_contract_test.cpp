// SPDX-License-Identifier: MS-PL
// SOFTWARE-115: renderer-neutral FNA/XNA SkinnedEffect skinning and lighting contract.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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

    struct SkinVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinVertex) == 52);

    struct Light
    {
        bool enabled = false;
        Vector3 direction{0.0f, 0.0f, -1.0f};
        Vector3 diffuse = Vector3::Zero;
        Vector3 specular = Vector3::Zero;
    };

    struct Scene
    {
        bool bindTexture = true;
        bool preferPerPixel = false;
        Vector3 ambient = Vector3::Zero;
        Vector3 diffuse = Vector3::One;
        Vector3 emissive = Vector3::Zero;
        Vector3 specular = Vector3::Zero;
        float specularPower = 16.0f;
        float alpha = 1.0f;
        std::array<Light, 3> lights{};
        Vector3 normal{0.0f, 0.0f, 1.0f};
        Color textureColor{255, 255, 255, 255};
        Matrix world = Matrix::getIdentityProperty();
        Matrix view = Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 4.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
        Matrix projection = Matrix::CreateOrthographic(6.0f, 6.0f, 0.1f, 2000.0f);
        std::vector<Matrix> bones{Matrix::getIdentityProperty()};
        int weightsPerVertex = 1;
        Vector4 weights{1.0f, 0.0f, 0.0f, 0.0f};
        std::array<std::uint8_t, 4> indices{0, 0, 0, 0};
    };

    int Byte(float value)
    {
        return std::clamp(static_cast<int>(std::lround(value * 255.0f)), 0, 255);
    }

    Color FloatColor(float r, float g, float b, float a = 1.0f)
    {
        return Color(Byte(r), Byte(g), Byte(b), Byte(a));
    }

    std::string ColorText(const Color& color)
    {
        return "(" + std::to_string(static_cast<int>(color.getRProperty())) + "," +
               std::to_string(static_cast<int>(color.getGProperty())) + "," +
               std::to_string(static_cast<int>(color.getBProperty())) + "," +
               std::to_string(static_cast<int>(color.getAProperty())) + ")";
    }

    bool CloseColor(const Color& actual, const Color& expected, int tolerance = 2)
    {
        const auto close = [&](int a, int b) { return std::abs(a - b) <= tolerance; };
        return close(actual.getRProperty(), expected.getRProperty()) &&
               close(actual.getGProperty(), expected.getGProperty()) &&
               close(actual.getBProperty(), expected.getBProperty()) &&
               close(actual.getAProperty(), expected.getAProperty());
    }

    const VertexDeclaration& SkinDeclaration()
    {
        static const VertexDeclaration declaration(sizeof(SkinVertex), {
            VertexElement(offsetof(SkinVertex, px), VertexElementFormat::Vector3,
                          VertexElementUsage::Position, 0),
            VertexElement(offsetof(SkinVertex, nx), VertexElementFormat::Vector3,
                          VertexElementUsage::Normal, 0),
            VertexElement(offsetof(SkinVertex, u), VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
            VertexElement(offsetof(SkinVertex, w0), VertexElementFormat::Vector4,
                          VertexElementUsage::BlendWeight, 0),
            VertexElement(offsetof(SkinVertex, i0), VertexElementFormat::Byte4,
                          VertexElementUsage::BlendIndices, 0),
        });
        return declaration;
    }
}

class SkinnedEffectContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(const std::string& label, const Color& actual, const Color& expected,
               int tolerance = 2)
    {
        const bool ok = CloseColor(actual, expected, tolerance);
        std::printf("[%s] %s: got=%s expected=%s tolerance=%d\n", ok ? "PASS" : "FAIL",
                    label.c_str(), ColorText(actual).c_str(), ColorText(expected).c_str(),
                    tolerance);
        ++total_;
        if (ok)
            ++passed_;
    }

    void Check(const std::string& label, bool condition, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", condition ? "PASS" : "FAIL", label.c_str(),
                    detail.c_str());
        ++total_;
        if (condition)
            ++passed_;
    }

    static void ConfigureLight(DirectionalLight& light, const Light& source)
    {
        light.setEnabledProperty(source.enabled);
        light.setDirectionProperty(source.direction);
        light.setDiffuseColorProperty(source.diffuse);
        light.setSpecularColorProperty(source.specular);
    }

    static void ConfigureEffect(SkinnedEffect& effect, Texture2D& texture,
                                const Scene& scene, bool enableDefaultLighting = false)
    {
        if (scene.bindTexture)
            effect.setTextureProperty(&texture);
        effect.setPreferPerPixelLightingProperty(scene.preferPerPixel);
        effect.setDiffuseColorProperty(scene.diffuse);
        effect.setEmissiveColorProperty(scene.emissive);
        effect.setSpecularColorProperty(scene.specular);
        effect.setSpecularPowerProperty(scene.specularPower);
        effect.setAlphaProperty(scene.alpha);
        if (enableDefaultLighting)
            effect.EnableDefaultLighting();
        else
        {
            effect.setAmbientLightColorProperty(scene.ambient);
            ConfigureLight(effect.DirectionalLight0, scene.lights[0]);
            ConfigureLight(effect.DirectionalLight1, scene.lights[1]);
            ConfigureLight(effect.DirectionalLight2, scene.lights[2]);
        }
        effect.setWorldProperty(scene.world);
        effect.setViewProperty(scene.view);
        effect.setProjectionProperty(scene.projection);
        effect.SetBoneTransforms(scene.bones);
        effect.setWeightsPerVertexProperty(scene.weightsPerVertex);
        effect.setFogEnabledProperty(false);
    }

    static SkinVertex Vertex(float x, float y, const Scene& scene)
    {
        return {
            x, y, 0.0f,
            scene.normal.X, scene.normal.Y, scene.normal.Z,
            (x + 1.0f) * 0.5f, (1.0f - y) * 0.5f,
            scene.weights.X, scene.weights.Y, scene.weights.Z, scene.weights.W,
            scene.indices[0], scene.indices[1], scene.indices[2], scene.indices[3],
        };
    }

    Color Render(GraphicsDevice& device, RenderTarget2D& target, Texture2D& texture,
                 const Scene& scene, bool enableDefaultLighting = false)
    {
        device.SetRenderTarget(&target);
        device.setViewportProperty(Viewport(0, 0, kSize, kSize));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.Clear(Color(0, 0, 0, 255));

        texture.SetData(&scene.textureColor, 1);
        SkinnedEffect effect(device);
        ConfigureEffect(effect, texture, scene, enableDefaultLighting);

        const SkinVertex vertices[6] = {
            Vertex(-1,  1, scene), Vertex(-1, -1, scene), Vertex( 1, -1, scene),
            Vertex(-1,  1, scene), Vertex( 1, -1, scene), Vertex( 1,  1, scene),
        };
        VertexBuffer buffer(device, SkinDeclaration(), 6, BufferUsage::None);
        buffer.SetData(vertices, 6);
        device.SetVertexBuffer(&buffer);
        effect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const Rectangle center(kSize / 2, kSize / 2, 1, 1);
        Color pixel;
        target.GetData(0, &center, &pixel, 0, 1);
        return pixel;
    }

    std::array<Color, 3> RenderShift(GraphicsDevice& device, RenderTarget2D& target,
                                     Texture2D& texture, const Scene& scene)
    {
        device.SetRenderTarget(&target);
        device.setViewportProperty(Viewport(0, 0, kSize, kSize));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.Clear(Color(0, 255, 0, 255));

        texture.SetData(&scene.textureColor, 1);
        SkinnedEffect effect(device);
        ConfigureEffect(effect, texture, scene);
        const SkinVertex vertices[6] = {
            Vertex(-1,  1, scene), Vertex(-1, -1, scene), Vertex(0, -1, scene),
            Vertex(-1,  1, scene), Vertex(0, -1, scene), Vertex(0,  1, scene),
        };
        VertexBuffer buffer(device, SkinDeclaration(), 6, BufferUsage::None);
        buffer.SetData(vertices, 6);
        device.SetVertexBuffer(&buffer);
        effect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const auto read = [&](int x)
        {
            const Rectangle region(x, kSize / 2, 1, 1);
            Color pixel;
            target.GetData(0, &region, &pixel, 0, 1);
            return pixel;
        };
        return {read(kSize / 8), read(kSize / 2), read(7 * kSize / 8)};
    }

    void CheckShift(GraphicsDevice& device, RenderTarget2D& target, Texture2D& texture,
                    const std::string& label, const Scene& scene)
    {
        const std::array<Color, 3> pixels = RenderShift(device, target, texture, scene);
        const bool leftGreen = pixels[0].getGProperty() > pixels[0].getRProperty();
        const bool centerRed = pixels[1].getRProperty() > pixels[1].getGProperty() &&
                               pixels[1].getRProperty() > 100;
        const bool rightGreen = pixels[2].getGProperty() > pixels[2].getRProperty();
        Check(label, leftGreen && centerRed && rightGreen,
              "left=" + ColorText(pixels[0]) + " center=" + ColorText(pixels[1]) +
                  " right=" + ColorText(pixels[2]));
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        Texture2D texture(device, 1, 1);

        Scene composition;
        composition.textureColor = Color(200, 100, 50, 128);
        composition.ambient = Vector3(0.1f, 0.1f, 0.1f);
        composition.diffuse = Vector3(0.5f, 0.8f, 0.4f);
        composition.emissive = Vector3(0.05f, 0.02f, 0.1f);
        composition.alpha = 0.5f;
        composition.lights[0] = {
            true, Vector3(0, 0, -1), Vector3(0.6f, 0.25f, 0.5f), Vector3::Zero};
        Check("texture, material, light and alpha use FNA ordering",
              Render(device, target, texture, composition),
              FloatColor(0.2f * (200.0f / 255.0f), 0.15f * (100.0f / 255.0f),
                         0.17f * (50.0f / 255.0f), 0.5f * (128.0f / 255.0f)));

        Scene threeLights;
        threeLights.lights[0] = {
            true, Vector3(0, 0, -1), Vector3(0.25f, 0, 0), Vector3::Zero};
        threeLights.lights[1] = {
            true, Vector3(0.8660254f, 0, -0.5f), Vector3(0, 0.5f, 0), Vector3::Zero};
        threeLights.lights[2] = {
            true, Vector3(0.9682458f, 0, -0.25f), Vector3(0, 0, 0.8f), Vector3::Zero};
        Check("all three directional lights contribute independently",
              Render(device, target, texture, threeLights),
              FloatColor(0.25f, 0.25f, 0.2f));

        Scene backFace = threeLights;
        backFace.normal = Vector3(0, 0, -1);
        Check("negative NdotL is clamped for every light",
              Render(device, target, texture, backFace), Color(0, 0, 0, 255));

        Scene nonUniformWorld;
        nonUniformWorld.normal = Vector3(0.70710678f, 0.70710678f, 0.0f);
        nonUniformWorld.world = Matrix::CreateScale(2.0f, 1.0f, 1.0f);
        nonUniformWorld.lights[0] = {
            true, Vector3(0, -1, 0), Vector3::One, Vector3::Zero};
        Check("vertex lighting applies inverse-transpose World after skinning",
              Render(device, target, texture, nonUniformWorld),
              FloatColor(0.8944272f, 0.8944272f, 0.8944272f));
        nonUniformWorld.preferPerPixel = true;
        Check("pixel lighting applies inverse-transpose World after skinning",
              Render(device, target, texture, nonUniformWorld),
              FloatColor(0.8944272f, 0.8944272f, 0.8944272f));

        // FNA SkinnedEffect.fx Skin() deliberately multiplies a normal by the weighted bone 3x3
        // directly. Only the subsequent World transform uses inverse-transpose.
        Scene boneNormal;
        boneNormal.normal = Vector3(0.0f, 0.6f, 0.8f);
        boneNormal.bones = {Matrix::CreateScale(1.0f, 2.0f, 1.0f)};
        boneNormal.lights[0] = {
            true, Vector3(0, -1, 0), Vector3::One, Vector3::Zero};
        constexpr float kDirectBoneNormalY = 1.2f / 1.4422205f;
        Check("vertex lighting uses FNA's direct weighted bone normal transform",
              Render(device, target, texture, boneNormal),
              FloatColor(kDirectBoneNormalY, kDirectBoneNormalY, kDirectBoneNormalY));
        boneNormal.preferPerPixel = true;
        Check("pixel lighting uses FNA's direct weighted bone normal transform",
              Render(device, target, texture, boneNormal),
              FloatColor(kDirectBoneNormalY, kDirectBoneNormalY, kDirectBoneNormalY));

        Scene lowPower;
        lowPower.preferPerPixel = true;
        lowPower.view = Matrix::CreateLookAt(
            Vector3(0, 0, 1000), Vector3::Zero, Vector3(0, 1, 0));
        lowPower.diffuse = Vector3::Zero;
        lowPower.specular = Vector3::One;
        lowPower.specularPower = 4.0f;
        lowPower.lights[0] = {
            true, Vector3(0.6f, 0, -0.8f), Vector3::Zero, Vector3::One};
        const Color lowPowerPixel = Render(device, target, texture, lowPower);
        Check("SpecularPower=4 follows the FNA half-vector exponent", lowPowerPixel,
              FloatColor(0.81f, 0.81f, 0.81f));
        Scene highPower = lowPower;
        highPower.specularPower = 32.0f;
        const Color highPowerPixel = Render(device, target, texture, highPower);
        Check("SpecularPower=32 narrows the highlight", highPowerPixel,
              FloatColor(std::pow(0.9486833f, 32.0f),
                         std::pow(0.9486833f, 32.0f),
                         std::pow(0.9486833f, 32.0f)));
        Check("SpecularPower changes output materially",
              lowPowerPixel.getRProperty() > highPowerPixel.getRProperty() + 100,
              "low=" + ColorText(lowPowerPixel) + " high=" + ColorText(highPowerPixel));

        Scene preference;
        preference.ambient = Vector3(0.02f, 0.02f, 0.02f);
        preference.diffuse = Vector3(0.4f, 0.4f, 0.4f);
        preference.specular = Vector3::One;
        preference.specularPower = 32.0f;
        preference.lights[0] = {
            true, Vector3(0.4472136f, 0, -0.8944272f),
            Vector3(0.5f, 0.5f, 0.5f), Vector3::One};
        preference.view = Matrix::CreateLookAt(
            Vector3(0, 0, 3), Vector3::Zero, Vector3(0, 1, 0));
        preference.projection = Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, 1.0f, 0.1f, 100.0f);
        const Color vertexLit = Render(device, target, texture, preference);
        Check("PreferPerPixelLighting=false is the XNA Gouraud path", vertexLit,
              Color(127, 127, 127, 255));
        preference.preferPerPixel = true;
        const Color pixelLit = Render(device, target, texture, preference);
        Check("PreferPerPixelLighting=true evaluates the fragment", pixelLit,
              Color(155, 155, 155, 255));
        Check("lighting preference is a real dispatch selector",
              pixelLit.getRProperty() > vertexLit.getRProperty() + 15,
              "vertex=" + ColorText(vertexLit) + " pixel=" + ColorText(pixelLit));

        Scene noTexture;
        noTexture.bindTexture = false;
        noTexture.diffuse = Vector3(0.2f, 0.3f, 0.4f);
        noTexture.emissive = Vector3::One;
        noTexture.alpha = 0.5f;
        Check("unbound optional texture contributes opaque white",
              Render(device, target, texture, noTexture),
              FloatColor(0.5f, 0.5f, 0.5f, 0.5f));

        Scene defaultFront;
        Check("EnableDefaultLighting front normal uses the standard rig",
              Render(device, target, texture, defaultFront, true),
              FloatColor(0.6808402f, 0.7017885f, 0.6888710f));
        Scene defaultBack = defaultFront;
        defaultBack.normal = Vector3(0, 0, -1);
        Check("EnableDefaultLighting back normal uses fill/back lights",
              Render(device, target, texture, defaultBack, true),
              FloatColor(0.7828587f, 0.7219990f, 0.6072770f));

        Scene oneWeight;
        oneWeight.textureColor = Color(255, 0, 0, 255);
        oneWeight.emissive = Vector3::One;
        oneWeight.weightsPerVertex = 1;
        oneWeight.weights = Vector4(1, 1, 1, 1);
        oneWeight.indices = {0, 1, 2, 3};
        oneWeight.bones = {
            Matrix::CreateTranslation(0.5f, 0, 0),
            Matrix::CreateTranslation(100.0f, 0, 0),
            Matrix::CreateTranslation(100.0f, 0, 0),
            Matrix::CreateTranslation(100.0f, 0, 0),
        };
        CheckShift(device, target, texture,
                   "WeightsPerVertex=1 ignores populated slots 1..3", oneWeight);

        Scene twoWeights = oneWeight;
        twoWeights.weightsPerVertex = 2;
        twoWeights.weights = Vector4(0.5f, 0.5f, 0.5f, 0.5f);
        twoWeights.bones[0] = Matrix::CreateTranslation(-0.5f, 0, 0);
        twoWeights.bones[1] = Matrix::CreateTranslation(1.5f, 0, 0);
        CheckShift(device, target, texture,
                   "WeightsPerVertex=2 blends two bones and ignores slots 2..3", twoWeights);

        Scene fourWeights = oneWeight;
        fourWeights.weightsPerVertex = 4;
        fourWeights.weights = Vector4(0.25f, 0.25f, 0.25f, 0.25f);
        fourWeights.bones = {
            Matrix::CreateTranslation(-1.0f, 0, 0),
            Matrix::CreateTranslation(0.0f, 0, 0),
            Matrix::CreateTranslation(1.0f, 0, 0),
            Matrix::CreateTranslation(2.0f, 0, 0),
        };
        CheckShift(device, target, texture,
                   "WeightsPerVertex=4 blends all four weighted bones", fourWeights);

        std::printf("\nResult: %d/%d PASS\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    SkinnedEffectContractTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kSize);
        graphics_->setPreferredBackBufferHeightProperty(kSize);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int Result() const { return result_; }
};

int main()
{
    SkinnedEffectContractTest game;
    game.Run();
    return game.Result();
}
