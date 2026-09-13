// SPDX-License-Identifier: MS-PL
//
// plans/plan_fx.md FX-005: replay FNA's checked-in compiled-effect pixel observations through
// CNA's public Effect and GraphicsDevice APIs. The managed generator and this test deliberately
// use the same flat resources, vertex declaration, parameter values, and clean state baseline.

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Json.hpp"
#include "CNA/TestSupport/TestPaths.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using CNA::Internal::JsonValue;
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    constexpr int kTargetSize = 8;
    constexpr int kChannelTolerance = 3;
    const Color kBackground(9, 19, 29, 255);
    const Color kPrimaryTextureColor(160, 80, 40, 255);
    const Color kSecondaryTextureColor(64, 192, 128, 255);
    const Color kEnvironmentTextureColor(30, 100, 220, 255);

    struct OracleVertex
    {
        float position[3];
        std::uint32_t color;
        float normal[3];
        float textureCoordinate0[2];
        float textureCoordinate1[2];
        float blendWeight[4];
        std::uint32_t blendIndices;
    };

    static_assert(sizeof(OracleVertex) == 64);
    static_assert(offsetof(OracleVertex, color) == 12);
    static_assert(offsetof(OracleVertex, normal) == 16);
    static_assert(offsetof(OracleVertex, textureCoordinate0) == 28);
    static_assert(offsetof(OracleVertex, textureCoordinate1) == 36);
    static_assert(offsetof(OracleVertex, blendWeight) == 44);
    static_assert(offsetof(OracleVertex, blendIndices) == 60);

    JsonValue LoadOracle()
    {
        const auto path = CNA::TestSupport::CompiledEffectFixtureDirectory() /
            "fna-effect-pixels.json";
        std::ifstream input(path);
        if (!input) return {};
        std::ostringstream text;
        text << input.rdbuf();
        return CNA::Internal::ParseJson(text.str());
    }

    std::vector<SharpRuntime::bytecs> ReadEffect(const std::string& fileName)
    {
        const auto path = CNA::TestSupport::CompiledEffectDirectory() / fileName;
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    int OracleInt(const JsonValue& object, const char* key)
    {
        const JsonValue* member = object.FindMember(key);
        return member != nullptr ? static_cast<int>(member->numberValue) : -1;
    }

    void SetMatrix(Effect& effect, const char* name, const Matrix& value)
    {
        if (EffectParameter* parameter = effect.getParametersProperty()[name])
            parameter->SetValue(value);
    }

    void SetSingle(Effect& effect, const char* name, float value)
    {
        if (EffectParameter* parameter = effect.getParametersProperty()[name])
            parameter->SetValue(value);
    }

    void SetVector3(Effect& effect, const char* name, const Vector3& value)
    {
        if (EffectParameter* parameter = effect.getParametersProperty()[name])
            parameter->SetValue(value);
    }

    void SetVector4(Effect& effect, const char* name, const Vector4& value)
    {
        if (EffectParameter* parameter = effect.getParametersProperty()[name])
            parameter->SetValue(value);
    }

    void SetTexture(Effect& effect, const char* name, Texture* value)
    {
        if (EffectParameter* parameter = effect.getParametersProperty()[name])
            parameter->SetValue(value);
    }

    void ConfigureEffect(Effect& effect, const std::string& fileName,
                         Texture2D& primary, Texture2D& secondary,
                         TextureCube& environment)
    {
        SetMatrix(effect, "Transform", Matrix::getIdentityProperty());
        SetMatrix(effect, "MatrixTransform", Matrix::getIdentityProperty());
        SetMatrix(effect, "World", Matrix::getIdentityProperty());
        SetMatrix(effect, "WorldInverseTranspose", Matrix::getIdentityProperty());
        SetMatrix(effect, "WorldViewProj", Matrix::getIdentityProperty());

        if (EffectParameter* bones = effect.getParametersProperty()["Bones"])
        {
            bones->SetValue(std::vector<Matrix>(
                static_cast<std::size_t>(bones->getElementsProperty().getCountProperty()),
                Matrix::getIdentityProperty()));
        }

        SetVector4(effect, "DiffuseColor", Vector4(0.7f, 0.8f, 0.9f, 1.0f));
        SetVector3(effect, "EmissiveColor", Vector3(0.1f, 0.2f, 0.3f));
        SetVector3(effect, "SpecularColor", Vector3::Zero);
        SetSingle(effect, "SpecularPower", 4.0f);
        SetVector3(effect, "EyePosition", Vector3(0.0f, 0.0f, 1.0f));
        SetVector3(effect, "FogColor", Vector3::Zero);
        SetVector4(effect, "FogVector", Vector4::Zero);
        SetVector4(effect, "AlphaTest", Vector4(0.0f, 1.0f, 1.0f, 1.0f));

        for (int light = 0; light < 3; ++light)
        {
            const std::string prefix = "DirLight" + std::to_string(light);
            SetVector3(effect, (prefix + "Direction").c_str(), Vector3(0.0f, 0.0f, -1.0f));
            SetVector3(effect, (prefix + "DiffuseColor").c_str(), Vector3::Zero);
            SetVector3(effect, (prefix + "SpecularColor").c_str(), Vector3::Zero);
        }

        SetVector3(effect, "EnvironmentMapSpecular", Vector3::Zero);
        SetSingle(effect, "FresnelFactor", 1.0f);
        SetSingle(effect, "EnvironmentMapAmount", 0.5f);

        SetTexture(effect, "Texture", &primary);
        SetTexture(effect, "Texture2", &secondary);
        SetTexture(effect, "FxTexture", &primary);
        SetTexture(effect, "EnvironmentMap", &environment);

        if (EffectParameter* shaderIndex = effect.getParametersProperty()["ShaderIndex"])
        {
            int value = 0;
            if (fileName == "AlphaTestEffect.fxb") value = 3;
            else if (fileName == "BasicEffect.fxb") value = 7;
            else if (fileName == "DualTextureEffect.fxb") value = 3;
            else if (fileName == "EnvironmentMapEffect.fxb") value = 1;
            else if (fileName == "SkinnedEffect.fxb") value = 5;
            shaderIndex->SetValue(value);
        }
    }

    VertexDeclaration CreateVertexDeclaration()
    {
        return VertexDeclaration(64, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            VertexElement(16, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(28, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
            VertexElement(36, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 1),
            VertexElement(44, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(60, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0),
        });
    }

    OracleVertex MakeVertex(float x, float y)
    {
        return {
            {x, y, 0.5f},
            Color(200, 140, 80, 255).getPackedValueProperty(),
            {0.0f, 0.0f, 1.0f},
            {0.5f, 0.5f},
            {0.5f, 0.5f},
            {1.0f, 0.0f, 0.0f, 0.0f},
            0,
        };
    }

    std::array<OracleVertex, 6> CreateVertices()
    {
        return {
            MakeVertex(-1.0f, 1.0f), MakeVertex(-1.0f, -1.0f), MakeVertex(1.0f, -1.0f),
            MakeVertex(-1.0f, 1.0f), MakeVertex(1.0f, -1.0f), MakeVertex(1.0f, 1.0f),
        };
    }

    std::array<Color, kTargetSize * kTargetSize> RenderPass(
        GraphicsDevice& device, EffectPass& pass, const VertexDeclaration& declaration,
        const std::array<OracleVertex, 6>& vertices)
    {
        RenderTarget2D target(device, kTargetSize, kTargetSize, false,
                              SurfaceFormat::Color, DepthFormat::None);
        device.SetRenderTarget(&target);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
        device.Clear(kBackground);
        pass.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 2, declaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::array<Color, kTargetSize * kTargetSize> pixels{};
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels;
    }

    void ExpectColorNear(const Color& actual, const JsonValue& expected,
                         const std::string& where)
    {
        EXPECT_NEAR(actual.getRProperty(), OracleInt(expected, "r"), kChannelTolerance)
            << where << " red";
        EXPECT_NEAR(actual.getGProperty(), OracleInt(expected, "g"), kChannelTolerance)
            << where << " green";
        EXPECT_NEAR(actual.getBProperty(), OracleInt(expected, "b"), kChannelTolerance)
            << where << " blue";
        EXPECT_NEAR(actual.getAProperty(), OracleInt(expected, "a"), kChannelTolerance)
            << where << " alpha";
    }

    void ExpectColorExact(const Color& actual, const JsonValue* expected,
                          const std::string& where)
    {
        ASSERT_NE(expected, nullptr) << where << " metadata is missing";
        EXPECT_EQ(actual.getRProperty(), OracleInt(*expected, "r")) << where << " red";
        EXPECT_EQ(actual.getGProperty(), OracleInt(*expected, "g")) << where << " green";
        EXPECT_EQ(actual.getBProperty(), OracleInt(*expected, "b")) << where << " blue";
        EXPECT_EQ(actual.getAProperty(), OracleInt(*expected, "a")) << where << " alpha";
    }

    void ExpectPassMatches(const std::array<Color, kTargetSize * kTargetSize>& pixels,
                           const JsonValue& oracle, const std::string& where)
    {
        int changed = 0;
        for (const Color& pixel : pixels)
            if (pixel != kBackground) ++changed;
        EXPECT_EQ(changed, OracleInt(oracle, "changedPixelCount")) << where;

        const JsonValue* upperLeft = oracle.FindMember("upperLeft");
        const JsonValue* center = oracle.FindMember("center");
        const JsonValue* lowerRight = oracle.FindMember("lowerRight");
        ASSERT_NE(upperLeft, nullptr) << where;
        ASSERT_NE(center, nullptr) << where;
        ASSERT_NE(lowerRight, nullptr) << where;
        ExpectColorNear(pixels[2 * kTargetSize + 2], *upperLeft, where + " upperLeft");
        ExpectColorNear(pixels[4 * kTargetSize + 4], *center, where + " center");
        ExpectColorNear(pixels[6 * kTargetSize + 6], *lowerRight, where + " lowerRight");
    }
}

TEST(Fna3dEffectPixelOracleTest, EveryCompilerProducedPassMatchesFnaPixels)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    const JsonValue oracle = LoadOracle();
    const JsonValue* effects = oracle.FindMember("effects");
    ASSERT_NE(effects, nullptr) << "the FNA pixel oracle is missing or malformed";
    EXPECT_EQ(OracleInt(oracle, "renderTargetSize"), kTargetSize);
    ExpectColorExact(kBackground, oracle.FindMember("background"), "background");
    ExpectColorExact(kPrimaryTextureColor, oracle.FindMember("primaryTexture"), "primaryTexture");
    ExpectColorExact(kSecondaryTextureColor, oracle.FindMember("secondaryTexture"),
                     "secondaryTexture");
    ExpectColorExact(kEnvironmentTextureColor, oracle.FindMember("environmentTexture"),
                     "environmentTexture");

    Texture2D primary(device, 1, 1);
    primary.SetData(&kPrimaryTextureColor, 1);
    Texture2D secondary(device, 1, 1);
    secondary.SetData(&kSecondaryTextureColor, 1);
    TextureCube environment(device, 1, false, SurfaceFormat::Color);
    for (CubeMapFace face : {CubeMapFace::PositiveX, CubeMapFace::NegativeX,
                             CubeMapFace::PositiveY, CubeMapFace::NegativeY,
                             CubeMapFace::PositiveZ, CubeMapFace::NegativeZ})
        environment.SetData(face, &kEnvironmentTextureColor, 1);

    const VertexDeclaration declaration = CreateVertexDeclaration();
    const auto vertices = CreateVertices();
    int comparedEffects = 0;
    int comparedPasses = 0;

    for (const auto& effectEntry : effects->objectValue)
    {
        const auto bytes = ReadEffect(effectEntry.key);
        ASSERT_FALSE(bytes.empty()) << "cannot read fixture " << effectEntry.key;
        Effect effect(device, bytes);
        ConfigureEffect(effect, effectEntry.key, primary, secondary, environment);

        const JsonValue* techniques = effectEntry.value.FindMember("techniques");
        ASSERT_NE(techniques, nullptr) << effectEntry.key;
        for (const auto& techniqueEntry : techniques->objectValue)
        {
            EffectTechnique* technique = effect.getTechniquesProperty()[techniqueEntry.key];
            ASSERT_NE(technique, nullptr)
                << effectEntry.key << " has no technique " << techniqueEntry.key;
            effect.setCurrentTechniqueProperty(technique);

            int passIndex = 0;
            for (const auto& passEntry : techniqueEntry.value.objectValue)
            {
                ASSERT_LT(passIndex, technique->getPassesProperty().getCountProperty())
                    << effectEntry.key << " " << techniqueEntry.key;
                EffectPass& pass = technique->getPassesProperty()[passIndex];
                const std::string expectedName = pass.getNameProperty().empty()
                    ? "<unnamed:" + std::to_string(passIndex) + ">"
                    : pass.getNameProperty();
                EXPECT_EQ(passEntry.key, expectedName)
                    << effectEntry.key << " " << techniqueEntry.key;

                const std::string where = effectEntry.key + " " + techniqueEntry.key + "/" +
                    passEntry.key;
                ExpectPassMatches(RenderPass(device, pass, declaration, vertices),
                                  passEntry.value, where);
                ++passIndex;
                ++comparedPasses;
            }
            EXPECT_EQ(passIndex, technique->getPassesProperty().getCountProperty())
                << effectEntry.key << " " << techniqueEntry.key;
        }
        ++comparedEffects;
    }

    EXPECT_EQ(comparedEffects, 7);
    EXPECT_EQ(comparedPasses, 9);
}
