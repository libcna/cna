// SPDX-License-Identifier: MS-PL
//
// TQ-041: the reflected shape of XNA's five stock effects -- parameter tables, technique and pass --
// and the texture parameters that back the Texture/Texture2 properties.
//
// Oracle: the effect bytecode embedded in the shipped Microsoft.Xna.Framework.Graphics.dll
// (BasicEffectCode, SkinnedEffectCode, DualTextureEffectCode, AlphaTestEffectCode and
// EnvironmentMapEffectCode, each `::Code`), whose D3DX parameter and technique tables were read
// directly, filtered as XNA builds Effect.Parameters: EffectParameterCollection::.ctor IL_0071-0093
// skips a parameter whose class or type has no XNA enum value (the samplers and the VSArray/PSArray
// shader arrays) and asks the effect's WantParameter, which drops VSIndices and PSIndices for every
// stock effect (e.g. BasicEffect::WantParameter IL_0000-0024). Rows/Columns are the D3DX
// descriptor's (EffectParameter::.ctor IL_0087-009a); every table's initial numeric value is zero.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <stdexcept>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechniqueCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using namespace Microsoft::Xna::Framework::Graphics;

static EffectParameter& RequireReflectedParameter(Effect& effect, const char* name)
{
    auto* parameter = effect.getParametersProperty()[name];
    if (parameter == nullptr)
        throw std::runtime_error(std::string("missing stock parameter ") + name);
    return *parameter;
}

namespace
{
    constexpr int kNotCheckedHere = -1;

    struct ExpectedParameter
    {
        const char* name;
        EffectParameterClass parameterClass;
        EffectParameterType parameterType;
        int rows;
        int columns;
        int elements;
    };

    constexpr EffectParameterClass kObject = EffectParameterClass::Object;
    constexpr EffectParameterClass kScalar = EffectParameterClass::Scalar;
    constexpr EffectParameterClass kVector = EffectParameterClass::Vector;
    constexpr EffectParameterClass kMatrix = EffectParameterClass::Matrix;
    constexpr EffectParameterType kSingle = EffectParameterType::Single;
    constexpr EffectParameterType kInt32 = EffectParameterType::Int32;
    constexpr EffectParameterType kTexture2D = EffectParameterType::Texture2D;
    constexpr EffectParameterType kTextureCube = EffectParameterType::TextureCube;

    ExpectedParameter Float3(const char* name) { return {name, kVector, kSingle, 1, 3, 0}; }
    ExpectedParameter Float4(const char* name) { return {name, kVector, kSingle, 1, 4, 0}; }
    ExpectedParameter Float1(const char* name) { return {name, kScalar, kSingle, 1, 1, 0}; }
    ExpectedParameter Float4x4(const char* name) { return {name, kMatrix, kSingle, 4, 4, 0}; }
    ExpectedParameter Float3x3(const char* name) { return {name, kMatrix, kSingle, 3, 3, 0}; }
    ExpectedParameter TextureParameter(const char* name) { return {name, kObject, kTexture2D, 0, 0, 0}; }
    ExpectedParameter ShaderIndex() { return {"ShaderIndex", kScalar, kInt32, 1, 1, 0}; }

    void ExpectParameterTable(const Effect& effect, const std::vector<ExpectedParameter>& expected)
    {
        const EffectParameterCollection& parameters = effect.getParametersProperty();
        ASSERT_EQ(parameters.getCountProperty(), static_cast<int>(expected.size()));
        for (int i = 0; i < parameters.getCountProperty(); ++i)
        {
            const EffectParameter& actual = *parameters[i];
            const ExpectedParameter& wanted = expected[static_cast<std::size_t>(i)];
            SCOPED_TRACE(wanted.name);
            EXPECT_EQ(actual.getNameProperty(), wanted.name) << "parameter " << i;
            EXPECT_EQ(actual.getSemanticProperty(), "");
            EXPECT_EQ(actual.getParameterClassProperty(), wanted.parameterClass);
            EXPECT_EQ(actual.getParameterTypeProperty(), wanted.parameterType);
            if (wanted.rows != kNotCheckedHere)
            {
                EXPECT_EQ(actual.getRowCountProperty(), wanted.rows);
                EXPECT_EQ(actual.getColumnCountProperty(), wanted.columns);
            }
            EXPECT_EQ(actual.getElementsProperty().getCountProperty(), wanted.elements);
            EXPECT_EQ(parameters[wanted.name], parameters[i]) << "lookup by name";
        }
    }

    // Each embedded table has one technique, named after the effect, holding one pass whose name
    // string is empty (a one-byte NUL string); EffectPass::.ctor IL_004a-005a copies it with
    // PtrToStringAnsi, so EffectPass.Name is "".
    void ExpectStockTechnique(const Effect& effect, const std::string& name)
    {
        const EffectTechniqueCollection& techniques = effect.getTechniquesProperty();
        ASSERT_EQ(techniques.getCountProperty(), 1);
        EXPECT_EQ(effect.getCurrentTechniqueProperty(), techniques[0]);
        EXPECT_EQ(techniques[0]->getNameProperty(), name);
        EXPECT_EQ(techniques[name], techniques[0]);
        const EffectPassCollection& passes = techniques[0]->getPassesProperty();
        ASSERT_EQ(passes.getCountProperty(), 1);
        EXPECT_EQ(passes[0]->getNameProperty(), "");
    }

    class StockEffectReflectionTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;
    };
}

TEST_F(StockEffectReflectionTest, SkinnedEffectParametersMatchItsEmbeddedTable)
{
    SkinnedEffect effect(device);
    // Bones' Rows/Columns are TQ-041 HOLD item D10 (StockParameterContractTests owns them).
    ExpectParameterTable(effect, {
        TextureParameter("Texture"),
        Float4("DiffuseColor"), Float3("EmissiveColor"), Float3("SpecularColor"),
        Float1("SpecularPower"),
        Float3("DirLight0Direction"), Float3("DirLight0DiffuseColor"), Float3("DirLight0SpecularColor"),
        Float3("DirLight1Direction"), Float3("DirLight1DiffuseColor"), Float3("DirLight1SpecularColor"),
        Float3("DirLight2Direction"), Float3("DirLight2DiffuseColor"), Float3("DirLight2SpecularColor"),
        Float3("EyePosition"), Float3("FogColor"), Float4("FogVector"),
        Float4x4("World"), Float3x3("WorldInverseTranspose"),
        {"Bones", kMatrix, kSingle, kNotCheckedHere, kNotCheckedHere, 72},
        Float4x4("WorldViewProj"), ShaderIndex(),
    });
}

TEST_F(StockEffectReflectionTest, EnvironmentMapEffectParametersMatchItsEmbeddedTable)
{
    EnvironmentMapEffect effect(device);
    // No DirLight*SpecularColor: the environment-map shader has no specular light term.
    ExpectParameterTable(effect, {
        TextureParameter("Texture"), {"EnvironmentMap", kObject, kTextureCube, 0, 0, 0},
        Float3("EnvironmentMapSpecular"), Float1("FresnelFactor"), Float1("EnvironmentMapAmount"),
        Float4("DiffuseColor"), Float3("EmissiveColor"),
        Float3("DirLight0Direction"), Float3("DirLight0DiffuseColor"),
        Float3("DirLight1Direction"), Float3("DirLight1DiffuseColor"),
        Float3("DirLight2Direction"), Float3("DirLight2DiffuseColor"),
        Float3("EyePosition"), Float3("FogColor"), Float4("FogVector"),
        Float4x4("World"), Float3x3("WorldInverseTranspose"), Float4x4("WorldViewProj"),
        ShaderIndex(),
    });
}

TEST_F(StockEffectReflectionTest, DualTextureEffectParametersMatchItsEmbeddedTable)
{
    DualTextureEffect effect(device);
    ExpectParameterTable(effect, {
        TextureParameter("Texture"), TextureParameter("Texture2"),
        Float4("DiffuseColor"), Float3("FogColor"), Float4("FogVector"),
        Float4x4("WorldViewProj"), ShaderIndex(),
    });
}

TEST_F(StockEffectReflectionTest, AlphaTestEffectParametersMatchItsEmbeddedTable)
{
    AlphaTestEffect effect(device);
    ExpectParameterTable(effect, {
        TextureParameter("Texture"),
        Float4("DiffuseColor"), Float4("AlphaTest"), Float3("FogColor"), Float4("FogVector"),
        Float4x4("WorldViewProj"), ShaderIndex(),
    });
}

TEST_F(StockEffectReflectionTest, EveryStockEffectHasOneTechniqueWithOneUnnamedPass)
{
    BasicEffect basic(device);
    SkinnedEffect skinned(device);
    EnvironmentMapEffect environmentMap(device);
    DualTextureEffect dualTexture(device);
    AlphaTestEffect alphaTest(device);

    ExpectStockTechnique(basic, "BasicEffect");
    ExpectStockTechnique(skinned, "SkinnedEffect");
    ExpectStockTechnique(environmentMap, "EnvironmentMapEffect");
    ExpectStockTechnique(dualTexture, "DualTextureEffect");
    ExpectStockTechnique(alphaTest, "AlphaTestEffect");

    // Effect::.ctor(Effect) clones the technique table too (IL_0048-0085, IL_00be-00cb).
    const std::unique_ptr<Effect> skinnedClone(skinned.Clone());
    const std::unique_ptr<Effect> alphaTestClone(alphaTest.Clone());
    ExpectStockTechnique(*skinnedClone, "SkinnedEffect");
    ExpectStockTechnique(*alphaTestClone, "AlphaTestEffect");
}

// get_Texture/set_Texture read and write the Texture parameter (AlphaTestEffect get_Texture
// IL_0000-000b, set_Texture IL_0000-000c; SkinnedEffect and DualTextureEffect alike, and Texture2
// through its own parameter). There is no separate field, so a raw parameter write is what the
// property returns.
TEST_F(StockEffectReflectionTest, TexturePropertiesAreTheirParameters)
{
    Texture2D first(device, 1, 1);
    Texture2D second(device, 1, 1);

    AlphaTestEffect alphaTest(device);
    SkinnedEffect skinned(device);
    DualTextureEffect dualTexture(device);

    alphaTest.setTextureProperty(&first);
    EXPECT_EQ(RequireReflectedParameter(alphaTest, "Texture").GetValueTexture2D(), &first);
    RequireReflectedParameter(alphaTest, "Texture").SetValue(&second);
    EXPECT_EQ(alphaTest.getTextureProperty(), &second);

    skinned.setTextureProperty(&first);
    EXPECT_EQ(RequireReflectedParameter(skinned, "Texture").GetValueTexture2D(), &first);
    RequireReflectedParameter(skinned, "Texture").SetValue(static_cast<Texture2D*>(nullptr));
    EXPECT_EQ(skinned.getTextureProperty(), nullptr);

    dualTexture.setTextureProperty(&first);
    dualTexture.setTexture2Property(&second);
    EXPECT_EQ(RequireReflectedParameter(dualTexture, "Texture").GetValueTexture2D(), &first);
    EXPECT_EQ(RequireReflectedParameter(dualTexture, "Texture2").GetValueTexture2D(), &second);
    RequireReflectedParameter(dualTexture, "Texture2").SetValue(&first);
    EXPECT_EQ(dualTexture.getTexture2Property(), &first);
    EXPECT_EQ(dualTexture.getTextureProperty(), &first);
}

// The Texture parameters are what a pass samples, so Apply binds a texture that only a raw
// parameter write supplied (sampler 0, and sampler 1 for Texture2).
TEST_F(StockEffectReflectionTest, ApplyBindsTheTextureParametersValue)
{
    Texture2D first(device, 1, 1);
    Texture2D second(device, 1, 1);

    AlphaTestEffect alphaTest(device);
    RequireReflectedParameter(alphaTest, "Texture").SetValue(&first);
    alphaTest.Apply();
    EXPECT_EQ(device.getTexturesProperty()[0], &first);

    DualTextureEffect dualTexture(device);
    RequireReflectedParameter(dualTexture, "Texture").SetValue(&second);
    RequireReflectedParameter(dualTexture, "Texture2").SetValue(&first);
    dualTexture.Apply();
    EXPECT_EQ(device.getTexturesProperty()[0], &second);
    EXPECT_EQ(device.getTexturesProperty()[1], &first);

    SkinnedEffect skinned(device);
    RequireReflectedParameter(skinned, "Texture").SetValue(&first);
    skinned.Apply();
    EXPECT_EQ(device.getTexturesProperty()[0], &first);
}

// SetOwnedTexture (CNAEXT) keeps the texture alive and stores it where the property reads it.
TEST_F(StockEffectReflectionTest, OwnedTexturesAreVisibleThroughTheirParameters)
{
    auto owned = std::make_shared<Texture2D>(device, 1, 1);
    auto owned2 = std::make_shared<Texture2D>(device, 1, 1);

    AlphaTestEffect alphaTest(device);
    alphaTest.SetOwnedTexture(owned);
    EXPECT_EQ(RequireReflectedParameter(alphaTest, "Texture").GetValueTexture2D(), owned.get());

    SkinnedEffect skinned(device);
    skinned.SetOwnedTexture(owned);
    EXPECT_EQ(RequireReflectedParameter(skinned, "Texture").GetValueTexture2D(), owned.get());

    DualTextureEffect dualTexture(device);
    dualTexture.SetOwnedTexture(owned);
    dualTexture.SetOwnedTexture2(owned2);
    EXPECT_EQ(RequireReflectedParameter(dualTexture, "Texture").GetValueTexture2D(), owned.get());
    EXPECT_EQ(RequireReflectedParameter(dualTexture, "Texture2").GetValueTexture2D(), owned2.get());
}
