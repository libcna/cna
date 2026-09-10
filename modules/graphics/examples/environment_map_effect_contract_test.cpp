// SPDX-License-Identifier: MS-PL
// SOFTWARE-114/303: renderer-neutral EnvironmentMapEffect lighting, reflection, Fresnel, and
// null-sampler contract.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 256;

    struct Light
    {
        bool enabled = false;
        Vector3 direction{0.0f, 0.0f, -1.0f};
        Vector3 diffuse = Vector3::Zero;
    };

    struct Scene
    {
        bool bindTexture = true;
        bool bindEnvironmentMap = true;
        Vector3 ambient = Vector3::Zero;
        Vector3 diffuse = Vector3::One;
        Vector3 emissive = Vector3::Zero;
        float alpha = 1.0f;
        std::array<Light, 3> lights{};
        Vector3 normal{0.0f, 0.0f, 1.0f};
        Color textureColor{255, 255, 255, 255};
        float environmentAmount = 0.0f;
        float fresnelFactor = 0.0f;
        Vector3 environmentSpecular = Vector3::Zero;
        Matrix world = Matrix::getIdentityProperty();
        Matrix view = Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 4.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
        Matrix projection = Matrix::CreateOrthographic(6.0f, 6.0f, 0.1f, 2000.0f);
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

    void SetSolidCube(TextureCube& cube, const Color& color)
    {
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (const CubeMapFace face : faces)
            cube.SetData(face, &color, 1);
    }

    void SetDistinctCube(TextureCube& cube)
    {
        const Color positiveX(255, 0, 0, 255);
        const Color negativeX(0, 255, 255, 255);
        const Color positiveY(0, 255, 0, 255);
        const Color negativeY(255, 0, 255, 255);
        const Color positiveZ(0, 0, 255, 255);
        const Color negativeZ(255, 255, 0, 255);
        cube.SetData(CubeMapFace::PositiveX, &positiveX, 1);
        cube.SetData(CubeMapFace::NegativeX, &negativeX, 1);
        cube.SetData(CubeMapFace::PositiveY, &positiveY, 1);
        cube.SetData(CubeMapFace::NegativeY, &negativeY, 1);
        cube.SetData(CubeMapFace::PositiveZ, &positiveZ, 1);
        cube.SetData(CubeMapFace::NegativeZ, &negativeZ, 1);
    }
}

class EnvironmentMapEffectContractTest final : public Game
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
    }

    static void ConfigureEffect(EnvironmentMapEffect& effect, Texture2D& texture,
                                TextureCube& cube, const Scene& scene)
    {
        if (scene.bindTexture)
            effect.setTextureProperty(&texture);
        if (scene.bindEnvironmentMap)
            effect.setEnvironmentMapProperty(&cube);
        effect.setLightingEnabledProperty(true);
        effect.setAmbientLightColorProperty(scene.ambient);
        effect.setDiffuseColorProperty(scene.diffuse);
        effect.setEmissiveColorProperty(scene.emissive);
        effect.setAlphaProperty(scene.alpha);
        ConfigureLight(effect.DirectionalLight0, scene.lights[0]);
        ConfigureLight(effect.DirectionalLight1, scene.lights[1]);
        ConfigureLight(effect.DirectionalLight2, scene.lights[2]);
        effect.setEnvironmentMapAmountProperty(scene.environmentAmount);
        effect.setFresnelFactorProperty(scene.fresnelFactor);
        effect.setEnvironmentMapSpecularProperty(scene.environmentSpecular);
        effect.setWorldProperty(scene.world);
        effect.setViewProperty(scene.view);
        effect.setProjectionProperty(scene.projection);
        effect.setFogEnabledProperty(false);
    }

    Color Render(GraphicsDevice& device, RenderTarget2D& target, Texture2D& texture,
                 TextureCube& cube, const Scene& scene)
    {
        device.SetRenderTarget(&target);
        device.setViewportProperty(Viewport(0, 0, kSize, kSize));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.Clear(Color(0, 0, 0, 255));

        texture.SetData(&scene.textureColor, 1);
        EnvironmentMapEffect effect(device);
        ConfigureEffect(effect, texture, cube, scene);

        const float z = 0.0f;
        const VertexPositionNormalTexture vertices[6] = {
            {Vector3(-1,  1, z), scene.normal, Vector2(0, 0)},
            {Vector3(-1, -1, z), scene.normal, Vector2(0, 1)},
            {Vector3( 1, -1, z), scene.normal, Vector2(1, 1)},
            {Vector3(-1,  1, z), scene.normal, Vector2(0, 0)},
            {Vector3( 1, -1, z), scene.normal, Vector2(1, 1)},
            {Vector3( 1,  1, z), scene.normal, Vector2(1, 0)},
        };
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const Rectangle center(kSize / 2, kSize / 2, 1, 1);
        Color pixel;
        target.GetData(0, &center, &pixel, 0, 1);
        return pixel;
    }

    std::array<Color, 3> RenderFresnelGradient(GraphicsDevice& device, RenderTarget2D& target,
                                                Texture2D& texture, TextureCube& cube)
    {
        device.SetRenderTarget(&target);
        device.setViewportProperty(Viewport(0, 0, kSize, kSize));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.Clear(Color(0, 0, 0, 255));

        const Color white(255, 255, 255, 255);
        texture.SetData(&white, 1);
        Scene scene;
        scene.environmentAmount = 1.0f;
        scene.fresnelFactor = 1.0f;
        scene.world = Matrix::getIdentityProperty();
        scene.view = Matrix::getIdentityProperty();
        scene.projection = Matrix::getIdentityProperty();

        EnvironmentMapEffect effect(device);
        ConfigureEffect(effect, texture, cube, scene);

        const Vector3 topNormal(0.0f, 0.0f, 1.0f);
        const Vector3 bottomNormal(1.0f, 0.0f, 0.0f);
        const VertexPositionNormalTexture vertices[6] = {
            {Vector3(-0.6f,  0.6f, 0), topNormal,    Vector2(0, 0)},
            {Vector3( 0.6f,  0.6f, 0), topNormal,    Vector2(1, 0)},
            {Vector3(-0.6f, -0.6f, 0), bottomNormal, Vector2(0, 1)},
            {Vector3( 0.6f,  0.6f, 0), topNormal,    Vector2(1, 0)},
            {Vector3( 0.6f, -0.6f, 0), bottomNormal, Vector2(1, 1)},
            {Vector3(-0.6f, -0.6f, 0), bottomNormal, Vector2(0, 1)},
        };
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const auto readNdc = [&](float x, float y)
        {
            const int pixelX = static_cast<int>((x + 1.0f) * 0.5f * kSize);
            const int pixelY = static_cast<int>((1.0f - y) * 0.5f * kSize);
            const Rectangle region(pixelX, pixelY, 1, 1);
            Color pixel;
            target.GetData(0, &region, &pixel, 0, 1);
            return pixel;
        };
        return {readNdc(0.0f, 0.5f), readNdc(0.0f, 0.3f), readNdc(0.0f, -0.5f)};
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        // Cube filtering/addressing has its own shared contract. Point sampling keeps this
        // lighting/reflection test independent of implementation-specific seamless edge blends.
        device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        Texture2D texture(device, 1, 1);
        TextureCube cube(device, 1, false, SurfaceFormat::Color);
        SetSolidCube(cube, Color(64, 128, 192, 255));

        Scene ambientEmissive;
        ambientEmissive.ambient = Vector3(0.2f, 0.1f, 0.3f);
        ambientEmissive.diffuse = Vector3(0.5f, 0.75f, 0.25f);
        ambientEmissive.emissive = Vector3(0.05f, 0.1f, 0.02f);
        ambientEmissive.alpha = 0.8f;
        Check("ambient/emissive/diffuse/alpha use FNA material ordering",
              Render(device, target, texture, cube, ambientEmissive),
              FloatColor(0.12f, 0.14f, 0.076f, 0.8f));

        Scene threeLights;
        threeLights.normal = Vector3(0.8660254f, 0.0f, -0.5f);
        threeLights.lights[0] = {
            true, Vector3(0, 0, 1), Vector3(0.6f, 0, 0)};
        threeLights.lights[1] = {
            true, Vector3(0, 0, 1), Vector3(0, 0.6f, 0)};
        threeLights.lights[2] = {
            true, Vector3(0, 0, 1), Vector3(0, 0, 0.6f)};
        Check("all three directional lights contribute independently",
              Render(device, target, texture, cube, threeLights),
              FloatColor(0.3f, 0.3f, 0.3f));

        Scene lightDisabled = threeLights;
        lightDisabled.lights[2].enabled = false;
        Check("disabled directional light contributes zero",
              Render(device, target, texture, cube, lightDisabled),
              FloatColor(0.3f, 0.3f, 0.0f));

        Scene ownDirection = threeLights;
        ownDirection.lights[1].direction = Vector3(1.0f, 0.0f, 0.0f);
        Check("each directional light uses its own direction",
              Render(device, target, texture, cube, ownDirection),
              FloatColor(0.3f, 0.0f, 0.3f));

        Scene backFace = threeLights;
        backFace.normal = Vector3(0.0f, 0.0f, 1.0f);
        Check("negative NdotL is clamped",
              Render(device, target, texture, cube, backFace),
              Color(0, 0, 0, 255));

        SetSolidCube(cube, Color(128, 128, 128, 255));
        Scene highAmount;
        highAmount.emissive = Vector3::One;
        highAmount.environmentAmount = 2.0f;
        Check("COLOR1 saturates EnvironmentMapAmount above one before interpolation",
              Render(device, target, texture, cube, highAmount),
              Color(128, 128, 128, 255));

        Scene negativeAmount = highAmount;
        negativeAmount.environmentAmount = -1.0f;
        Check("COLOR1 saturates negative EnvironmentMapAmount to zero",
              Render(device, target, texture, cube, negativeAmount),
              Color::White);

        SetSolidCube(cube, Color(200, 100, 50, 255));
        Scene noBaseTexture;
        noBaseTexture.bindTexture = false;
        noBaseTexture.emissive = Vector3::One;
        noBaseTexture.environmentAmount = 0.0f;
        Check("null base Texture samples XNA opaque black",
              Render(device, target, texture, cube, noBaseTexture),
              Color(0, 0, 0, 255));

        Scene noEnvironmentMap;
        noEnvironmentMap.bindEnvironmentMap = false;
        noEnvironmentMap.emissive = Vector3::One;
        noEnvironmentMap.textureColor = Color(80, 40, 120, 255);
        noEnvironmentMap.environmentAmount = 1.0f;
        Check("null EnvironmentMap samples XNA opaque black",
              Render(device, target, texture, cube, noEnvironmentMap),
              Color(0, 0, 0, 255));

        Scene nonUniformLighting;
        nonUniformLighting.normal = Vector3(0.70710678f, 0.0f, 0.70710678f);
        nonUniformLighting.world = Matrix::CreateScale(2.0f, 1.0f, 0.5f);
        nonUniformLighting.lights[0] = {
            true, Vector3(0, 0, -1), Vector3::One};
        Check("non-uniform World uses inverse-transpose for lighting",
              Render(device, target, texture, cube, nonUniformLighting),
              FloatColor(0.9701425f, 0.9701425f, 0.9701425f));

        SetDistinctCube(cube);
        Scene nonUniformReflection;
        nonUniformReflection.normal = Vector3(0.0f, 0.70710678f, 0.70710678f);
        nonUniformReflection.world = Matrix::CreateScale(1.0f, 1.0f, 20.0f);
        nonUniformReflection.environmentAmount = 1.0f;
        Check("non-uniform World uses inverse-transpose for reflection",
              Render(device, target, texture, cube, nonUniformReflection),
              Color(255, 255, 0, 255));

        const Color translucentCube(80, 160, 240, 128);
        SetSolidCube(cube, translucentCube);
        Scene composition;
        composition.emissive = Vector3::One;
        composition.alpha = 0.5f;
        composition.textureColor = Color(200, 100, 50, 128);
        composition.environmentAmount = 0.25f;
        composition.environmentSpecular = Vector3(0.2f, 0.1f, 0.05f);
        Check("lerp target and environment specular use completed output alpha",
              Render(device, target, texture, cube, composition),
              FloatColor(0.338985f, 0.199058f, 0.138936f, 0.25098f));

        SetSolidCube(cube, Color(200, 100, 50, 255));
        const std::array<Color, 3> gradient =
            RenderFresnelGradient(device, target, texture, cube);
        Check("Fresnel is evaluated per vertex: upper probe", gradient[0],
              Color(188, 94, 47, 255));
        Check("Fresnel is evaluated per vertex: discriminating middle probe", gradient[1],
              Color(165, 82, 41, 255));
        Check("Fresnel is evaluated per vertex: lower probe", gradient[2],
              Color(70, 35, 18, 255));
        Check("Fresnel gradient is monotonic",
              gradient[0].getRProperty() > gradient[1].getRProperty() &&
                  gradient[1].getRProperty() > gradient[2].getRProperty(),
              ColorText(gradient[0]) + " > " + ColorText(gradient[1]) + " > " +
                  ColorText(gradient[2]));

        std::printf("\nResult: %d/%d PASS\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    EnvironmentMapEffectContractTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kSize);
        graphics_->setPreferredBackBufferHeightProperty(kSize);
    }

    int Result() const { return result_; }
};

int main()
{
    EnvironmentMapEffectContractTest game;
    game.Run();
    return game.Result();
}
