// SPDX-License-Identifier: MS-PL
// SOFTWARE-113: renderer-neutral BasicEffect lighting contract.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
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
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    struct Light
    {
        bool enabled = false;
        Vector3 direction{0.0f, 0.0f, -1.0f};
        Vector3 diffuse = Vector3::Zero;
        Vector3 specular = Vector3::Zero;
    };

    struct Scene
    {
        bool lighting = true;
        bool preferPerPixel = false;
        bool textureEnabled = false;
        bool vertexColorEnabled = false;
        Vector3 ambient = Vector3::Zero;
        Vector3 diffuse = Vector3::One;
        Vector3 emissive = Vector3::Zero;
        Vector3 specular = Vector3::Zero;
        float specularPower = 16.0f;
        float alpha = 1.0f;
        std::array<Light, 3> lights{};
        Vector3 normal{0.0f, 0.0f, 1.0f};
        Color vertexColor{255, 255, 255, 255};
        Color textureColor{255, 255, 255, 255};
        bool fogEnabled = false;
        Vector3 fogColor = Vector3::Zero;
        float fogStart = 0.0f;
        float fogEnd = 1.0f;
        Matrix world = Matrix::getIdentityProperty();
        Matrix view = Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 4.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
        Matrix projection = Matrix::CreateOrthographic(6.0f, 6.0f, 0.1f, 2000.0f);
    };

    struct LitVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(LitVertex) == 36);

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
}

class BasicEffectLightingContractTest final : public Game
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

    Color Render(GraphicsDevice& device, RenderTarget2D& target, Texture2D& texture,
                 const VertexDeclaration& declaration, const Scene& scene,
                 bool enableDefaultLighting = false)
    {
        device.SetRenderTarget(&target);
        device.setViewportProperty(Viewport(0, 0, kSize, kSize));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.Clear(Color(0, 0, 0, 255));

        texture.SetData(&scene.textureColor, 1);
        BasicEffect effect(device);
        if (enableDefaultLighting)
            effect.EnableDefaultLighting();
        else
        {
            effect.setLightingEnabledProperty(scene.lighting);
            ConfigureLight(effect.DirectionalLight0, scene.lights[0]);
            ConfigureLight(effect.DirectionalLight1, scene.lights[1]);
            ConfigureLight(effect.DirectionalLight2, scene.lights[2]);
        }
        effect.setPreferPerPixelLightingProperty(scene.preferPerPixel);
        effect.setTextureEnabledProperty(scene.textureEnabled);
        if (scene.textureEnabled)
            effect.setTextureProperty(&texture);
        effect.VertexColorEnabled = scene.vertexColorEnabled;
        if (!enableDefaultLighting)
            effect.setAmbientLightColorProperty(scene.ambient);
        effect.setDiffuseColorProperty(scene.diffuse);
        effect.setEmissiveColorProperty(scene.emissive);
        effect.setSpecularColorProperty(scene.specular);
        effect.setSpecularPowerProperty(scene.specularPower);
        effect.setAlphaProperty(scene.alpha);
        effect.setWorldProperty(scene.world);
        effect.setViewProperty(scene.view);
        effect.setProjectionProperty(scene.projection);
        effect.setFogEnabledProperty(scene.fogEnabled);
        effect.setFogColorProperty(scene.fogColor);
        effect.setFogStartProperty(scene.fogStart);
        effect.setFogEndProperty(scene.fogEnd);

        const auto component = [](std::uint8_t value) { return value; };
        const Color& color = scene.vertexColor;
        const float z = 0.0f;
        const LitVertex vertices[6] = {
            {-1,  1, z, scene.normal.X, scene.normal.Y, scene.normal.Z, 0, 0,
                component(color.getRProperty()), component(color.getGProperty()),
                component(color.getBProperty()), component(color.getAProperty())},
            {-1, -1, z, scene.normal.X, scene.normal.Y, scene.normal.Z, 0, 1,
                component(color.getRProperty()), component(color.getGProperty()),
                component(color.getBProperty()), component(color.getAProperty())},
            { 1, -1, z, scene.normal.X, scene.normal.Y, scene.normal.Z, 1, 1,
                component(color.getRProperty()), component(color.getGProperty()),
                component(color.getBProperty()), component(color.getAProperty())},
            {-1,  1, z, scene.normal.X, scene.normal.Y, scene.normal.Z, 0, 0,
                component(color.getRProperty()), component(color.getGProperty()),
                component(color.getBProperty()), component(color.getAProperty())},
            { 1, -1, z, scene.normal.X, scene.normal.Y, scene.normal.Z, 1, 1,
                component(color.getRProperty()), component(color.getGProperty()),
                component(color.getBProperty()), component(color.getAProperty())},
            { 1,  1, z, scene.normal.X, scene.normal.Y, scene.normal.Z, 1, 0,
                component(color.getRProperty()), component(color.getGProperty()),
                component(color.getBProperty()), component(color.getAProperty())},
        };
        VertexBuffer buffer(device, declaration, 6, BufferUsage::None);
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

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        Texture2D texture(device, 1, 1);
        const VertexDeclaration declaration(36, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });

        Scene unlit;
        unlit.lighting = false;
        unlit.diffuse = Vector3(0.2f, 0.3f, 0.4f);
        unlit.emissive = Vector3(0.1f, 0.05f, 0.2f);
        unlit.ambient = Vector3::One;
        unlit.alpha = 0.5f;
        unlit.lights[0] = {true, Vector3(0, 0, -1), Vector3::One, Vector3::One};
        unlit.specular = Vector3::One;
        Check("lighting disabled ignores lights/ambient/specular and adds emissive",
              Render(device, target, texture, declaration, unlit),
              FloatColor(0.15f, 0.175f, 0.3f, 0.5f));

        Scene ambientEmissive;
        ambientEmissive.ambient = Vector3(0.2f, 0.1f, 0.3f);
        ambientEmissive.diffuse = Vector3(0.5f, 0.75f, 0.25f);
        ambientEmissive.emissive = Vector3(0.05f, 0.1f, 0.02f);
        ambientEmissive.alpha = 0.8f;
        Check("lit ambient and emissive use FNA material ordering",
              Render(device, target, texture, declaration, ambientEmissive),
              FloatColor(0.12f, 0.14f, 0.076f, 0.8f));

        Scene threeLights;
        threeLights.diffuse = Vector3::One;
        threeLights.lights[0] = {
            true, Vector3(0, 0, -1), Vector3(0.25f, 0, 0), Vector3::Zero};
        threeLights.lights[1] = {
            true, Vector3(0.8660254f, 0, -0.5f), Vector3(0, 0.5f, 0), Vector3::Zero};
        threeLights.lights[2] = {
            true, Vector3(0.9682458f, 0, -0.25f), Vector3(0, 0, 0.8f), Vector3::Zero};
        Check("all three directional lights contribute independently",
              Render(device, target, texture, declaration, threeLights),
              FloatColor(0.25f, 0.25f, 0.2f));

        Scene backFace = threeLights;
        backFace.normal = Vector3(0, 0, -1);
        Check("negative NdotL is clamped for every directional light",
              Render(device, target, texture, declaration, backFace), Color(0, 0, 0, 255));

        Scene nonUniform;
        nonUniform.diffuse = Vector3::One;
        nonUniform.lights[0] = {
            true, Vector3(0, 0, -1), Vector3::One, Vector3::Zero};
        nonUniform.normal = Vector3(0.70710678f, 0, 0.70710678f);
        nonUniform.world = Matrix::CreateScale(2.0f, 1.0f, 0.5f);
        Check("non-uniform World uses inverse-transpose normal",
              Render(device, target, texture, declaration, nonUniform),
              FloatColor(0.9701425f, 0.9701425f, 0.9701425f));

        Scene composition;
        composition.textureEnabled = true;
        composition.vertexColorEnabled = true;
        composition.textureColor = Color(255, 128, 204, 128);
        composition.vertexColor = Color(128, 255, 64, 128);
        composition.ambient = Vector3(0.1f, 0.1f, 0.1f);
        composition.diffuse = Vector3(0.5f, 0.8f, 0.4f);
        composition.emissive = Vector3(0.05f, 0.02f, 0.1f);
        composition.alpha = 0.5f;
        composition.lights[0] = {
            true, Vector3(0, 0, -1), Vector3(0.6f, 0.25f, 0.5f), Vector3::Zero};
        const float vertexR = 128.0f / 255.0f;
        const float vertexB = 64.0f / 255.0f;
        const float textureG = 128.0f / 255.0f;
        const float textureB = 204.0f / 255.0f;
        Check("texture, vertex color, material and alpha compose before specular",
              Render(device, target, texture, declaration, composition),
              FloatColor(0.2f * vertexR, 0.15f * textureG,
                         0.17f * vertexB * textureB,
                         0.5f * (128.0f / 255.0f) * (128.0f / 255.0f)));

        Scene lowPower;
        lowPower.preferPerPixel = true;
        lowPower.view = Matrix::CreateLookAt(
            Vector3(0, 0, 1000), Vector3::Zero, Vector3(0, 1, 0));
        lowPower.diffuse = Vector3::Zero;
        lowPower.specular = Vector3::One;
        lowPower.specularPower = 4.0f;
        lowPower.lights[0] = {
            true, Vector3(0.6f, 0, -0.8f), Vector3::Zero, Vector3::One};
        const Color lowPowerPixel = Render(device, target, texture, declaration, lowPower);
        Check("SpecularPower=4 follows the FNA half-vector exponent", lowPowerPixel,
              FloatColor(0.81f, 0.81f, 0.81f));

        Scene highPower = lowPower;
        highPower.specularPower = 32.0f;
        const Color highPowerPixel = Render(device, target, texture, declaration, highPower);
        Check("SpecularPower=32 narrows the highlight", highPowerPixel,
              FloatColor(std::pow(0.9486833f, 32.0f),
                         std::pow(0.9486833f, 32.0f),
                         std::pow(0.9486833f, 32.0f)));
        Check("SpecularPower changes output materially",
              lowPowerPixel.getRProperty() > highPowerPixel.getRProperty() + 100,
              "low=" + ColorText(lowPowerPixel) + " high=" + ColorText(highPowerPixel));

        Scene preference;
        preference.textureEnabled = true;
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
        const Color vertexLit = Render(device, target, texture, declaration, preference);
        Check("PreferPerPixelLighting=false is the XNA Gouraud path", vertexLit,
              Color(127, 127, 127, 255));
        preference.preferPerPixel = true;
        const Color pixelLit = Render(device, target, texture, declaration, preference);
        Check("PreferPerPixelLighting=true evaluates the fragment", pixelLit,
              Color(155, 155, 155, 255));
        Check("lighting preference is a real dispatch selector",
              std::abs(static_cast<int>(pixelLit.getRProperty()) -
                       static_cast<int>(vertexLit.getRProperty())) > 15,
              "vertex=" + ColorText(vertexLit) + " pixel=" + ColorText(pixelLit));

        Scene litFog;
        litFog.preferPerPixel = true;
        litFog.diffuse = Vector3::Zero;
        litFog.emissive = Vector3(0, 0, 1);
        litFog.specular = Vector3(0, 1, 0);
        litFog.alpha = 0.5f;
        litFog.lights[0] = {
            true, Vector3(0, 0, -1), Vector3::Zero, Vector3::One};
        litFog.fogEnabled = true;
        litFog.fogColor = Vector3(1, 0, 0);
        litFog.fogStart = 2.0f;
        litFog.fogEnd = 6.0f;
        Check("fog follows diffuse, emissive and specular and targets FogColor*alpha",
              Render(device, target, texture, declaration, litFog),
              FloatColor(0.25f, 0.25f, 0.25f, 0.5f));

        Scene defaultFront;
        defaultFront.specular = Vector3::Zero;
        const Color defaultFrontPixel =
            Render(device, target, texture, declaration, defaultFront, true);
        Check("EnableDefaultLighting front normal renders its key light",
              defaultFrontPixel, FloatColor(0.6808402f, 0.7017885f, 0.6888710f));

        Scene defaultBack = defaultFront;
        defaultBack.normal = Vector3(0, 0, -1);
        const Color defaultBackPixel =
            Render(device, target, texture, declaration, defaultBack, true);
        Check("EnableDefaultLighting back normal renders fill and back lights",
              defaultBackPixel, FloatColor(0.7828587f, 0.7219990f, 0.6072770f));

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    BasicEffectLightingContractTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(64);
        graphics_->setPreferredBackBufferHeightProperty(64);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int Result() const { return result_; }
};

int main()
{
    BasicEffectLightingContractTest game;
    game.Run();
    return game.Result();
}
