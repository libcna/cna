// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnj.md CNB-45/CNB-46: .cnj JSON support for the 5 stock effects (BasicEffect/
// AlphaTestEffect/DualTextureEffect/EnvironmentMapEffect/SkinnedEffect), dispatched from inside
// the pre-existing EffectTypeReader (RegisterTypeReader<T>() allows only one reader per
// std::shared_ptr<Effect>, so these cannot get their own registrations). Field lists are a direct
// JSON port of StockEffectContentTypeReaders.cpp's already-FNA-verified per-effect field order.
// The last test in this file confirms the pre-existing custom-GLSL "Effect" .cnj shape still
// works unchanged (regression).

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "CNA/GraphicsCapability.hpp"

using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::AlphaTestEffect;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::CompareFunction;
using Microsoft::Xna::Framework::Graphics::DualTextureEffect;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::SkinnedEffect;

namespace
{
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_cnj_stock_effect_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }

        ~ScratchContentRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        ScratchContentRoot(const ScratchContentRoot&) = delete;
        ScratchContentRoot& operator=(const ScratchContentRoot&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)";

    const char* kFragSrc = R"(#version 300 es
precision mediump float;
out vec4 FragColor;
void main() { FragColor = vec4(1.0); }
)";
}

class CnjStockEffectTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(CnjStockEffectTest, LoadsBasicEffectWithAllFields)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "basic.cnj", R"({
        "cnjVersion": 1,
        "type": "BasicEffect",
        "diffuseColor": [0.1, 0.2, 0.3],
        "emissiveColor": [0.4, 0.5, 0.6],
        "specularColor": [0.7, 0.8, 0.9],
        "specularPower": 32.0,
        "alpha": 0.5,
        "vertexColorEnabled": true
    })");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    std::shared_ptr<Effect> loaded = cm.Load<std::shared_ptr<Effect>>("basic");
    ASSERT_NE(loaded, nullptr);
    auto* basic = dynamic_cast<BasicEffect*>(loaded.get());
    ASSERT_NE(basic, nullptr);

    EXPECT_EQ(basic->getDiffuseColorProperty(), Vector3(0.1f, 0.2f, 0.3f));
    EXPECT_EQ(basic->getEmissiveColorProperty(), Vector3(0.4f, 0.5f, 0.6f));
    EXPECT_FLOAT_EQ(basic->getSpecularPowerProperty(), 32.0f);
    EXPECT_FLOAT_EQ(basic->getAlphaProperty(), 0.5f);
    EXPECT_TRUE(basic->VertexColorEnabled);
}

TEST_F(CnjStockEffectTest, BasicEffectDefaultsMatchEngineDefaults)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "defaults.cnj", R"({"cnjVersion": 1, "type": "BasicEffect"})");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    std::shared_ptr<Effect> loaded = cm.Load<std::shared_ptr<Effect>>("defaults");
    auto* basic = dynamic_cast<BasicEffect*>(loaded.get());
    ASSERT_NE(basic, nullptr);

    BasicEffect freshlyConstructed(gd);
    EXPECT_EQ(basic->getDiffuseColorProperty(), freshlyConstructed.getDiffuseColorProperty());
    EXPECT_EQ(basic->getEmissiveColorProperty(), freshlyConstructed.getEmissiveColorProperty());
    EXPECT_FLOAT_EQ(basic->getSpecularPowerProperty(), freshlyConstructed.getSpecularPowerProperty());
    EXPECT_FLOAT_EQ(basic->getAlphaProperty(), freshlyConstructed.getAlphaProperty());
    EXPECT_EQ(basic->VertexColorEnabled, freshlyConstructed.VertexColorEnabled);
}

TEST_F(CnjStockEffectTest, LoadsAlphaTestEffect)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "alphatest.cnj", R"({
        "cnjVersion": 1,
        "type": "AlphaTestEffect",
        "alphaFunction": "LessEqual",
        "referenceAlpha": 128,
        "diffuseColor": [1.0, 0.0, 0.0],
        "alpha": 0.9,
        "vertexColorEnabled": true
    })");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    std::shared_ptr<Effect> loaded = cm.Load<std::shared_ptr<Effect>>("alphatest");
    auto* effect = dynamic_cast<AlphaTestEffect*>(loaded.get());
    ASSERT_NE(effect, nullptr);

    EXPECT_EQ(effect->getAlphaFunctionProperty(), CompareFunction::LessEqual);
    EXPECT_EQ(effect->getReferenceAlphaProperty(), 128);
    EXPECT_EQ(effect->getDiffuseColorProperty(), Vector3(1.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(effect->getAlphaProperty(), 0.9f);
    EXPECT_TRUE(effect->getVertexColorEnabledProperty());
}

TEST_F(CnjStockEffectTest, LoadsDualTextureEffect)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "dual.cnj", R"({
        "cnjVersion": 1,
        "type": "DualTextureEffect",
        "diffuseColor": [0.2, 0.2, 0.2],
        "alpha": 0.8,
        "vertexColorEnabled": false
    })");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    std::shared_ptr<Effect> loaded = cm.Load<std::shared_ptr<Effect>>("dual");
    auto* effect = dynamic_cast<DualTextureEffect*>(loaded.get());
    ASSERT_NE(effect, nullptr);
    EXPECT_EQ(effect->getDiffuseColorProperty(), Vector3(0.2f, 0.2f, 0.2f));
    EXPECT_FLOAT_EQ(effect->getAlphaProperty(), 0.8f);
}

TEST_F(CnjStockEffectTest, LoadsEnvironmentMapEffect)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "envmap.cnj", R"({
        "cnjVersion": 1,
        "type": "EnvironmentMapEffect",
        "environmentMapAmount": 0.75,
        "environmentMapSpecular": [0.1, 0.1, 0.1],
        "fresnelFactor": 2.0,
        "diffuseColor": [0.3, 0.3, 0.3],
        "alpha": 1.0
    })");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    std::shared_ptr<Effect> loaded = cm.Load<std::shared_ptr<Effect>>("envmap");
    auto* effect = dynamic_cast<EnvironmentMapEffect*>(loaded.get());
    ASSERT_NE(effect, nullptr);
    EXPECT_FLOAT_EQ(effect->getEnvironmentMapAmountProperty(), 0.75f);
    EXPECT_FLOAT_EQ(effect->getFresnelFactorProperty(), 2.0f);
    EXPECT_EQ(effect->getDiffuseColorProperty(), Vector3(0.3f, 0.3f, 0.3f));
}

TEST_F(CnjStockEffectTest, LoadsSkinnedEffect)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "skinned.cnj", R"({
        "cnjVersion": 1,
        "type": "SkinnedEffect",
        "weightsPerVertex": 2,
        "diffuseColor": [0.6, 0.6, 0.6],
        "specularPower": 64.0,
        "alpha": 1.0
    })");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    std::shared_ptr<Effect> loaded = cm.Load<std::shared_ptr<Effect>>("skinned");
    auto* effect = dynamic_cast<SkinnedEffect*>(loaded.get());
    ASSERT_NE(effect, nullptr);
    EXPECT_EQ(effect->getWeightsPerVertexProperty(), 2);
    EXPECT_EQ(effect->getDiffuseColorProperty(), Vector3(0.6f, 0.6f, 0.6f));
    EXPECT_FLOAT_EQ(effect->getSpecularPowerProperty(), 64.0f);
}

TEST_F(CnjStockEffectTest, UnrecognizedTypeThrows)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "wrong.cnj", R"({"cnjVersion": 1, "type": "NotARealEffectType"})");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<std::shared_ptr<Effect>>("wrong"), ContentLoadException);
}

// Found by adversarial review, 2026-07-17: an unrecognized 'type' combined with a 'sourceFile'
// field must report the *type* problem, not the (also-true, but less useful) sourceFile
// rejection -- the type check must run first.
TEST_F(CnjStockEffectTest, UnrecognizedTypeWithSourceFileReportsTypeProblem)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "wrong.cnj",
              R"({"cnjVersion": 1, "type": "NotARealEffectType", "sourceFile": "x.bin"})");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    try
    {
        (void)cm.Load<std::shared_ptr<Effect>>("wrong");
        FAIL() << "expected ContentLoadException";
    }
    catch (const ContentLoadException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(message.find("NotARealEffectType"), std::string::npos) << message;
        EXPECT_EQ(message.find("sourceFile"), std::string::npos) << message;
    }
}

TEST_F(CnjStockEffectTest, SourceFileRejected)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "wrong.cnj",
              R"({"cnjVersion": 1, "type": "BasicEffect", "sourceFile": "foo.bin"})");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<std::shared_ptr<Effect>>("wrong"), ContentLoadException);
}

// Regression: the pre-existing custom-GLSL "Effect" .cnj shape (EffectTypeReader's original,
// only behavior before this task) must still work unchanged now that Read() dispatches across 6
// type names instead of validating against a single fixed one.
TEST_F(CnjStockEffectTest, CustomGlslEffectStillWorks)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "passthrough.vert.glsl", kVertSrc);
    WriteFile(root.path() / "passthrough.frag.glsl", kFragSrc);
    WriteFile(root.path() / "passthrough.cnj", R"({
        "cnjVersion": 1,
        "type": "Effect",
        "vertex": "passthrough.vert.glsl",
        "fragment": "passthrough.frag.glsl"
    })");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    std::shared_ptr<Effect> loaded = cm.Load<std::shared_ptr<Effect>>("passthrough");
    ASSERT_NE(loaded, nullptr);
    auto* shaderEffect = dynamic_cast<ShaderEffect*>(loaded.get());
    ASSERT_NE(shaderEffect, nullptr);
    // A renderer with no shader compiler (OpenGL ES 1.1's fixed-function pipeline has none at all)
    // cannot produce a valid custom-GLSL effect. Gate on the capability rather than the renderer
    // name, matching CnjTexture3DTests' own convention for the same situation.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::CustomEffects))
    {
        EXPECT_FALSE(shaderEffect->IsEffectValid());
        return;
    }
    EXPECT_TRUE(shaderEffect->IsEffectValid());
}
