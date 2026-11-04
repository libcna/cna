// SPDX-License-Identifier: MS-PL
//
// TQ-041: what Apply writes into a stock effect's parameters, and what it must leave alone.
//
// Oracle: XNA 4.0's Microsoft.Xna.Framework.Graphics.dll.
//  * EffectHelpers.SetWorldViewProjAndFog IL_0000-005e: when WorldViewProj (1) is dirty,
//    worldView = World * View and WorldViewProj = worldView * Projection; with fog on, Fog|FogEnable
//    (16|32) dirty -> SetFogVector; with fog off, only FogEnable (32) dirty -> FogVector = 0 (the Fog
//    bit is left set).
//  * EffectHelpers.SetFogVector IL_0000-0073: FogStart == FogEnd -> (0, 0, 0, 1); otherwise
//    s = 1 / (FogStart - FogEnd) and (wv.M13 s, wv.M23 s, wv.M33 s, (wv.M43 + FogStart) s).
//  * EffectHelpers.SetLightingMatrices IL_0000-0052: World (2) -> World and the transposed inverse;
//    EyePosition (4) -> Invert(View).Translation.
//  * EffectHelpers.SetMaterialColor IL_0006-00a5 (lit): Diffuse = (d * a, a),
//    Emissive = (e + ambient * d) * a. AlphaTestEffect/DualTextureEffect OnApply IL_0051-0073:
//    Diffuse = (d * a, a).
//  * Setter dirty bits: World 19 (WorldViewProj|World|Fog) on the lit effects, 17 on
//    AlphaTest/DualTexture; View 21 (WorldViewProj|EyePosition|Fog), 17 on AlphaTest/DualTexture;
//    Projection 1; DiffuseColor/EmissiveColor/Alpha/AmbientLightColor 8; FogStart/FogEnd 16;
//    FogEnabled 0xa0 on change; VertexColorEnabled/PreferPerPixelLighting 0x80 on change;
//    WeightsPerVertex 0x80 always; AlphaFunction/ReferenceAlpha 64 (AlphaTest).
//
// Every expected value below is hand-derived from those formulas with exactly representable
// operands (the arithmetic is written next to each constant).

#include <gtest/gtest.h>

#include <initializer_list>
#include <stdexcept>
#include <string>

#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectFog.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // ---- Scenario -------------------------------------------------------------------------------
    // W has off-diagonal M13/M23 so a transposed fog vector or WIT shows; V is a pure translation.
    const Matrix kWorld(2, 0, 0.5f, 0,
                        0, 2, 0.25f, 0,
                        0, 0, 2, 0,
                        1, 2, 3, 1);
    const Matrix kView(1, 0, 0, 0,
                       0, 1, 0, 0,
                       0, 0, 1, 0,
                       0, 0, -10, 1);  // CreateTranslation(0, 0, -10)
    const Matrix kProjection(1, 0, 0, 0,
                             0, 2, 0, 0,
                             0, 0, 0.5f, 0,
                             0, 0, 0, 1);
    // W*V: rows 1-3 of W, row 4 (1, 2, 3 - 10, 1). (W*V)*P scales columns by (1, 2, 0.5, 1).
    const Matrix kWorldViewProj(2, 0, 0.25f, 0,
                                0, 4, 0.125f, 0,
                                0, 0, 1, 0,
                                1, 4, -3.5f, 1);
    // FogStart 2, FogEnd 10: s = 1 / (2 - 10) = -0.125;
    // (0.5 s, 0.25 s, 2 s, (-7 + 2) s) = (-0.0625, -0.03125, -0.25, 0.625).
    constexpr float kFogStart = 2.0f;
    constexpr float kFogEnd = 10.0f;
    const Vector4 kFogVector(-0.0625f, -0.03125f, -0.25f, 0.625f);

    // Material: d = (0.5, 0.25, 0.75), e = (0.125, 0.25, 0.0625), ambient = (0.25, 0.5, 0.125), a = 0.5.
    const Vector3 kDiffuse(0.5f, 0.25f, 0.75f);
    const Vector3 kEmissive(0.125f, 0.25f, 0.0625f);
    const Vector3 kAmbient(0.25f, 0.5f, 0.125f);
    constexpr float kAlpha = 0.5f;
    // (d * a, a)
    const Vector4 kDiffuseParameter(0.25f, 0.125f, 0.375f, 0.5f);
    // ((0.125 + 0.25*0.5) * 0.5, (0.25 + 0.5*0.25) * 0.5, (0.0625 + 0.125*0.75) * 0.5)
    const Vector3 kEmissiveParameter(0.125f, 0.1875f, 0.078125f);

    // Second World: Scale(2). W2*V = diag(2, 2, 2) with row 4 (0, 0, -10, 1);
    // *P -> diag(2, 4, 1), row 4 (0, 0, -5, 1); fog (0, 0, 2 s, (-10 + 2) s) = (0, 0, -0.25, 1).
    const Matrix kWorld2(2, 0, 0, 0,
                         0, 2, 0, 0,
                         0, 0, 2, 0,
                         0, 0, 0, 1);  // CreateScale(2)
    const Matrix kWorldViewProj2(2, 0, 0, 0,
                                 0, 4, 0, 0,
                                 0, 0, 1, 0,
                                 0, 0, -5, 1);
    const Vector4 kFogVector2(0.0f, 0.0f, -0.25f, 1.0f);

    // Second View: translation (0, 0, -6). W*V2 row 4 = (1, 2, -3, 1); *P -> (1, 4, -1.5, 1);
    // fog W = (-3 + 2) s = 0.125; EyePosition = Invert(V2).Translation = (0, 0, 6).
    const Matrix kView2(1, 0, 0, 0,
                        0, 1, 0, 0,
                        0, 0, 1, 0,
                        0, 0, -6, 1);  // CreateTranslation(0, 0, -6)
    const Matrix kWorldViewProjView2(2, 0, 0.25f, 0,
                                     0, 4, 0.125f, 0,
                                     0, 0, 1, 0,
                                     1, 4, -1.5f, 1);
    const Vector4 kFogVectorView2(-0.0625f, -0.03125f, -0.25f, 0.125f);

    // ---- Poisoning: a raw value that no Apply in these scenarios can produce ---------------------
    const Matrix kPoisonMatrix(9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9);
    const Vector4 kPoison4(9.0f, 9.0f, 9.0f, 9.0f);
    const Vector3 kPoison3(9.0f, 9.0f, 9.0f);
    constexpr int kPoisonIndex = 99;

    EffectParameter& Parameter(Effect& effect, const std::string& name)
    {
        EffectParameter* parameter = effect.getParametersProperty()[name];
        if (parameter == nullptr)
            throw std::runtime_error("missing stock parameter " + name);
        return *parameter;
    }

    void Poison(Effect& effect, std::initializer_list<const char*> names)
    {
        for (const char* name : names)
        {
            EffectParameter& parameter = Parameter(effect, name);
            if (parameter.getParameterClassProperty() == EffectParameterClass::Matrix)
                parameter.SetValue(kPoisonMatrix);
            else if (parameter.getParameterTypeProperty() == EffectParameterType::Int32)
                parameter.SetValue(kPoisonIndex);
            else if (parameter.getParameterClassProperty() == EffectParameterClass::Scalar)
                parameter.SetValue(9.0f);
            else if (parameter.getColumnCountProperty() == 4)
                parameter.SetValue(kPoison4);
            else
                parameter.SetValue(kPoison3);
        }
    }

    bool IsPoisoned(Effect& effect, const char* name)
    {
        EffectParameter& parameter = Parameter(effect, name);
        if (parameter.getParameterClassProperty() == EffectParameterClass::Matrix)
        {
            // The 3x3 part is all a 3x3 parameter defines.
            const Matrix value = parameter.GetValueMatrix();
            return value.M11 == 9.0f && value.M22 == 9.0f && value.M33 == 9.0f && value.M12 == 9.0f;
        }
        if (parameter.getParameterTypeProperty() == EffectParameterType::Int32)
            return parameter.GetValueInt32() == kPoisonIndex;
        if (parameter.getParameterClassProperty() == EffectParameterClass::Scalar)
            return parameter.GetValueSingle() == 9.0f;
        if (parameter.getColumnCountProperty() == 4)
            return parameter.GetValueVector4() == kPoison4;
        return parameter.GetValueVector3() == kPoison3;
    }

    void ExpectUntouched(Effect& effect, std::initializer_list<const char*> names)
    {
        for (const char* name : names)
            EXPECT_TRUE(IsPoisoned(effect, name)) << name << " was rewritten";
    }

    void ExpectMatrix3x3(const Matrix& actual, const Matrix& expected)
    {
        EXPECT_FLOAT_EQ(actual.M11, expected.M11);
        EXPECT_FLOAT_EQ(actual.M12, expected.M12);
        EXPECT_FLOAT_EQ(actual.M13, expected.M13);
        EXPECT_FLOAT_EQ(actual.M21, expected.M21);
        EXPECT_FLOAT_EQ(actual.M22, expected.M22);
        EXPECT_FLOAT_EQ(actual.M23, expected.M23);
        EXPECT_FLOAT_EQ(actual.M31, expected.M31);
        EXPECT_FLOAT_EQ(actual.M32, expected.M32);
        EXPECT_FLOAT_EQ(actual.M33, expected.M33);
    }

    // ---- Per-type facts ---------------------------------------------------------------------------
    template <typename TEffect> struct ApplyTraits;
    template <> struct ApplyTraits<SkinnedEffect>
    {
        static constexpr bool kLit = true;           // SetLightingMatrices + lit SetMaterialColor
        static constexpr int kDefaultShaderIndex = 11; // fog off 1 + four weights 4 + one light 6
    };
    template <> struct ApplyTraits<EnvironmentMapEffect>
    {
        static constexpr bool kLit = true;
        static constexpr int kDefaultShaderIndex = 11; // fog off 1 + Fresnel 2 + one light 8
    };
    template <> struct ApplyTraits<AlphaTestEffect>
    {
        static constexpr bool kLit = false;
        static constexpr int kDefaultShaderIndex = 1;  // fog off 1 (Greater is not Eq/Ne)
    };
    template <> struct ApplyTraits<DualTextureEffect>
    {
        static constexpr bool kLit = false;
        static constexpr int kDefaultShaderIndex = 1;  // fog off 1
    };

    template <typename TEffect>
    class StockEffectApplyTest : public ::testing::Test
    {
    protected:
        void SetUpScenario(TEffect& effect, bool fog)
        {
            IEffectMatrices& matrices = effect;
            matrices.setWorldProperty(kWorld);
            matrices.setViewProperty(kView);
            matrices.setProjectionProperty(kProjection);
            IEffectFog& fogState = effect;
            fogState.setFogEnabledProperty(fog);
            fogState.setFogStartProperty(kFogStart);
            fogState.setFogEndProperty(kFogEnd);
            effect.setDiffuseColorProperty(kDiffuse);
            effect.setAlphaProperty(kAlpha);
            if constexpr (ApplyTraits<TEffect>::kLit)
            {
                effect.setEmissiveColorProperty(kEmissive);
                effect.setAmbientLightColorProperty(kAmbient);
            }
        }

        // Every parameter OnApply derives, for this type.
        static void PoisonDerived(TEffect& effect)
        {
            Poison(effect, {"WorldViewProj", "FogVector", "DiffuseColor", "ShaderIndex"});
            if constexpr (ApplyTraits<TEffect>::kLit)
                Poison(effect, {"World", "WorldInverseTranspose", "EyePosition", "EmissiveColor"});
        }

        GraphicsDevice device;
    };

    using FlaggedStockEffects = ::testing::Types<SkinnedEffect, EnvironmentMapEffect,
                                                 AlphaTestEffect, DualTextureEffect>;
}

TYPED_TEST_SUITE(StockEffectApplyTest, FlaggedStockEffects);

// ---- First Apply: everything is dirty (constructors set dirtyFlags = All) -------------------------

TYPED_TEST(StockEffectApplyTest, FirstApplyDerivesWorldViewProjFogAndMaterial)
{
    TypeParam effect(this->device);
    this->SetUpScenario(effect, true);
    effect.Apply();

    EXPECT_EQ(Parameter(effect, "WorldViewProj").GetValueMatrix(), kWorldViewProj);
    EXPECT_EQ(Parameter(effect, "FogVector").GetValueVector4(), kFogVector);
    EXPECT_EQ(Parameter(effect, "DiffuseColor").GetValueVector4(), kDiffuseParameter);
    EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(),
              ApplyTraits<TypeParam>::kDefaultShaderIndex - 1) << "fog on removes the +1";
    if constexpr (ApplyTraits<TypeParam>::kLit)
    {
        EXPECT_EQ(Parameter(effect, "EmissiveColor").GetValueVector3(), kEmissiveParameter);
        EXPECT_EQ(Parameter(effect, "World").GetValueMatrix(), kWorld);
        // Invert(W): upper 3x3 [[0.5, 0, -0.125], [0, 0.5, -0.0625], [0, 0, 0.5]]; transposed.
        ExpectMatrix3x3(Parameter(effect, "WorldInverseTranspose").GetValueMatrix(),
                        Matrix(0.5f, 0, 0, 0,
                               0, 0.5f, 0, 0,
                               -0.125f, -0.0625f, 0.5f, 0,
                               0, 0, 0, 1));
        EXPECT_EQ(Parameter(effect, "EyePosition").GetValueVector3(), Vector3(0.0f, 0.0f, 10.0f));
    }
}

TYPED_TEST(StockEffectApplyTest, EqualFogStartAndEndFogEverything)
{
    TypeParam effect(this->device);
    this->SetUpScenario(effect, true);
    IEffectFog& fog = effect;
    fog.setFogStartProperty(5.0f);
    fog.setFogEndProperty(5.0f);
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "FogVector").GetValueVector4(), Vector4(0.0f, 0.0f, 0.0f, 1.0f));
}

TYPED_TEST(StockEffectApplyTest, DisabledFogZeroesTheFogVectorOnTheFirstApply)
{
    TypeParam effect(this->device);
    this->SetUpScenario(effect, false);
    Poison(effect, {"FogVector"});
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "FogVector").GetValueVector4(), Vector4::Zero);
}

// ---- The dirty-flag rules -------------------------------------------------------------------------

TYPED_TEST(StockEffectApplyTest, ApplyWithNothingChangedRewritesNothing)
{
    TypeParam effect(this->device);
    this->SetUpScenario(effect, true);
    effect.Apply();
    this->PoisonDerived(effect);
    effect.Apply();
    ExpectUntouched(effect, {"WorldViewProj", "FogVector", "DiffuseColor", "ShaderIndex"});
    if constexpr (ApplyTraits<TypeParam>::kLit)
        ExpectUntouched(effect, {"World", "WorldInverseTranspose", "EyePosition", "EmissiveColor"});
}

TYPED_TEST(StockEffectApplyTest, ProjectionRecomputesOnlyWorldViewProj)
{
    TypeParam effect(this->device);
    this->SetUpScenario(effect, true);
    effect.Apply();
    this->PoisonDerived(effect);
    IEffectMatrices& matrices = effect;
    matrices.setProjectionProperty(Matrix::getIdentityProperty());
    effect.Apply();

    // W*V*I = W*V: rows 1-3 of W, row 4 (1, 2, -7, 1).
    EXPECT_EQ(Parameter(effect, "WorldViewProj").GetValueMatrix(),
              Matrix(2, 0, 0.5f, 0, 0, 2, 0.25f, 0, 0, 0, 2, 0, 1, 2, -7, 1));
    ExpectUntouched(effect, {"FogVector", "DiffuseColor", "ShaderIndex"});
    if constexpr (ApplyTraits<TypeParam>::kLit)
        ExpectUntouched(effect, {"World", "WorldInverseTranspose", "EyePosition", "EmissiveColor"});
}

TYPED_TEST(StockEffectApplyTest, WorldRecomputesWorldViewProjFogAndTheLightingMatrices)
{
    TypeParam effect(this->device);
    this->SetUpScenario(effect, true);
    effect.Apply();
    this->PoisonDerived(effect);
    IEffectMatrices& matrices = effect;
    matrices.setWorldProperty(kWorld2);
    effect.Apply();

    EXPECT_EQ(Parameter(effect, "WorldViewProj").GetValueMatrix(), kWorldViewProj2);
    EXPECT_EQ(Parameter(effect, "FogVector").GetValueVector4(), kFogVector2);
    ExpectUntouched(effect, {"DiffuseColor", "ShaderIndex"});
    if constexpr (ApplyTraits<TypeParam>::kLit)
    {
        EXPECT_EQ(Parameter(effect, "World").GetValueMatrix(), kWorld2);
        ExpectMatrix3x3(Parameter(effect, "WorldInverseTranspose").GetValueMatrix(),
                        Matrix::CreateScale(0.5f));
        ExpectUntouched(effect, {"EyePosition", "EmissiveColor"});
    }
}

TYPED_TEST(StockEffectApplyTest, ViewRecomputesWorldViewProjFogAndTheEyePosition)
{
    TypeParam effect(this->device);
    this->SetUpScenario(effect, true);
    effect.Apply();
    this->PoisonDerived(effect);
    IEffectMatrices& matrices = effect;
    matrices.setViewProperty(kView2);
    effect.Apply();

    EXPECT_EQ(Parameter(effect, "WorldViewProj").GetValueMatrix(), kWorldViewProjView2);
    EXPECT_EQ(Parameter(effect, "FogVector").GetValueVector4(), kFogVectorView2);
    ExpectUntouched(effect, {"DiffuseColor", "ShaderIndex"});
    if constexpr (ApplyTraits<TypeParam>::kLit)
    {
        EXPECT_EQ(Parameter(effect, "EyePosition").GetValueVector3(), Vector3(0.0f, 0.0f, 6.0f));
        ExpectUntouched(effect, {"World", "WorldInverseTranspose", "EmissiveColor"});
    }
}

TYPED_TEST(StockEffectApplyTest, MaterialSettersRecomputeOnlyTheColours)
{
    TypeParam effect(this->device);
    this->SetUpScenario(effect, true);
    effect.Apply();
    this->PoisonDerived(effect);
    effect.setAlphaProperty(kAlpha);  // the same value still marks MaterialColor dirty
    effect.Apply();

    EXPECT_EQ(Parameter(effect, "DiffuseColor").GetValueVector4(), kDiffuseParameter);
    ExpectUntouched(effect, {"WorldViewProj", "FogVector", "ShaderIndex"});
    if constexpr (ApplyTraits<TypeParam>::kLit)
    {
        EXPECT_EQ(Parameter(effect, "EmissiveColor").GetValueVector3(), kEmissiveParameter);
        ExpectUntouched(effect, {"World", "WorldInverseTranspose", "EyePosition"});
    }
}

// With fog off, a FogStart/FogEnd change sets the Fog bit, which only a fog-on Apply consumes; the
// FogVector is left alone until fog is enabled again, and then reflects the current range.
TYPED_TEST(StockEffectApplyTest, AFogRangeChangeWaitsUntilFogIsEnabled)
{
    TypeParam effect(this->device);
    this->SetUpScenario(effect, false);
    effect.Apply();
    Poison(effect, {"FogVector", "ShaderIndex"});
    IEffectFog& fog = effect;
    fog.setFogStartProperty(6.0f);
    effect.Apply();
    ExpectUntouched(effect, {"FogVector", "ShaderIndex"});

    fog.setFogEnabledProperty(true);
    effect.Apply();
    // s = 1 / (6 - 10) = -0.25: (0.5 s, 0.25 s, 2 s, (-7 + 6) s).
    EXPECT_EQ(Parameter(effect, "FogVector").GetValueVector4(),
              Vector4(-0.125f, -0.0625f, -0.5f, 0.25f));
    EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(),
              ApplyTraits<TypeParam>::kDefaultShaderIndex - 1);
}

TYPED_TEST(StockEffectApplyTest, DisablingFogRewritesOnlyTheFogVectorAndShaderIndex)
{
    TypeParam effect(this->device);
    this->SetUpScenario(effect, true);
    effect.Apply();
    this->PoisonDerived(effect);
    IEffectFog& fog = effect;
    fog.setFogEnabledProperty(true);  // unchanged: no dirty bit
    effect.Apply();
    ExpectUntouched(effect, {"FogVector", "ShaderIndex"});

    fog.setFogEnabledProperty(false);
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "FogVector").GetValueVector4(), Vector4::Zero);
    EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(),
              ApplyTraits<TypeParam>::kDefaultShaderIndex);
    ExpectUntouched(effect, {"WorldViewProj", "DiffuseColor"});
}

// ---- Lit effects: the one-light test runs on every Apply (OnApply IL_00b9-00f5) -------------------

TEST(StockEffectLightingApplyTest, EnablingASecondLightRewritesOnlyTheShaderIndex)
{
    GraphicsDevice device;
    SkinnedEffect skinned(device);
    EnvironmentMapEffect environmentMap(device);
    for (Effect* effect : {static_cast<Effect*>(&skinned), static_cast<Effect*>(&environmentMap)})
    {
        effect->Apply();
        Poison(*effect, {"WorldViewProj", "DiffuseColor", "ShaderIndex", "World", "EyePosition"});
    }
    skinned.getDirectionalLight1Property().setEnabledProperty(true);
    environmentMap.getDirectionalLight2Property().setEnabledProperty(true);
    skinned.Apply();
    environmentMap.Apply();

    EXPECT_EQ(Parameter(skinned, "ShaderIndex").GetValueInt32(), 5) << "fog off 1 + four weights 4";
    EXPECT_EQ(Parameter(environmentMap, "ShaderIndex").GetValueInt32(), 3) << "fog off 1 + Fresnel 2";
    for (Effect* effect : {static_cast<Effect*>(&skinned), static_cast<Effect*>(&environmentMap)})
        ExpectUntouched(*effect, {"WorldViewProj", "DiffuseColor", "World", "EyePosition"});
}

// Invert(View).Translation, not the View's own translation: View is a quarter turn about Y
// ([[0,0,-1],[0,1,0],[1,0,0]]) then a translation by (0, 0, -10); its inverse moves the eye to
// -(0, 0, -10) * R^T = (-10, 0, 0).
TEST(StockEffectLightingApplyTest, EyePositionIsTheInverseViewsTranslation)
{
    const Matrix view(0, 0, -1, 0,
                      0, 1, 0, 0,
                      1, 0, 0, 0,
                      0, 0, -10, 1);
    GraphicsDevice device;
    SkinnedEffect skinned(device);
    EnvironmentMapEffect environmentMap(device);
    skinned.setViewProperty(view);
    environmentMap.setViewProperty(view);
    skinned.Apply();
    environmentMap.Apply();
    for (Effect* effect : {static_cast<Effect*>(&skinned), static_cast<Effect*>(&environmentMap)})
    {
        const Vector3 eye = Parameter(*effect, "EyePosition").GetValueVector3();
        EXPECT_FLOAT_EQ(eye.X, -10.0f);
        EXPECT_FLOAT_EQ(eye.Y, 0.0f);
        EXPECT_FLOAT_EQ(eye.Z, 0.0f);
    }
}

// SpecularColor, SpecularPower and FogColor live in their parameters (set_SpecularColor
// IL_0000-000c, set_SpecularPower IL_0000-000c, set_FogColor IL_0000-000c) and OnApply never writes
// them, so a raw write survives Apply whatever else changed.
TEST(StockEffectLightingApplyTest, ApplyNeverWritesTheParameterBackedProperties)
{
    GraphicsDevice device;
    SkinnedEffect skinned(device);
    skinned.Apply();
    Poison(skinned, {"SpecularColor", "SpecularPower", "FogColor"});
    skinned.setAlphaProperty(0.25f);
    skinned.setFogEnabledProperty(true);
    skinned.setWorldProperty(kWorld2);
    skinned.Apply();
    ExpectUntouched(skinned, {"SpecularColor", "SpecularPower", "FogColor"});
    EXPECT_EQ(skinned.getSpecularColorProperty(), kPoison3);
    EXPECT_FLOAT_EQ(skinned.getSpecularPowerProperty(), 9.0f);
}

// ---- Shader-index tables ---------------------------------------------------------------------------

// SkinnedEffect::OnApply IL_0108-014c: fog off +1; WeightsPerVertex 2 -> +2, 4 -> +4;
// PreferPerPixelLighting +12, else one light +6. All 18 indices 0..17 (VSIndices has 18 entries).
TEST(StockEffectShaderIndexTest, SkinnedEffectSelectsAllEighteenVariants)
{
    GraphicsDevice device;
    bool seen[18] = {};
    for (const bool fog : {false, true})
        for (const int weights : {1, 2, 4})
            for (const bool perPixel : {false, true})
                for (const bool oneLight : {false, true})
                {
                    SkinnedEffect effect(device);
                    effect.setFogEnabledProperty(fog);
                    effect.setWeightsPerVertexProperty(weights);
                    effect.setPreferPerPixelLightingProperty(perPixel);
                    effect.getDirectionalLight1Property().setEnabledProperty(!oneLight);
                    effect.Apply();
                    const int expected = (fog ? 0 : 1) + (weights == 2 ? 2 : weights == 4 ? 4 : 0) +
                                         (perPixel ? 12 : oneLight ? 6 : 0);
                    SCOPED_TRACE(testing::Message() << "fog=" << fog << " weights=" << weights
                                                    << " perPixel=" << perPixel << " oneLight=" << oneLight);
                    EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(), expected);
                    seen[expected] = true;
                }
    for (int index = 0; index < 18; ++index)
        EXPECT_TRUE(seen[index]) << index;
}

// EnvironmentMapEffect::OnApply IL_0108-0137: fog off +1, Fresnel (FresnelFactor != 0) +2,
// specular (EnvironmentMapSpecular != 0) +4, one light +8. All 16 (VSIndices has 16 entries).
TEST(StockEffectShaderIndexTest, EnvironmentMapEffectSelectsAllSixteenVariants)
{
    GraphicsDevice device;
    for (const bool fog : {false, true})
        for (const bool fresnel : {false, true})
            for (const bool specular : {false, true})
                for (const bool oneLight : {false, true})
                {
                    EnvironmentMapEffect effect(device);
                    effect.setFogEnabledProperty(fog);
                    effect.setFresnelFactorProperty(fresnel ? 0.5f : 0.0f);
                    effect.setEnvironmentMapSpecularProperty(specular ? Vector3(0.0f, 0.0f, 0.25f)
                                                                      : Vector3::Zero);
                    effect.getDirectionalLight2Property().setEnabledProperty(!oneLight);
                    effect.Apply();
                    SCOPED_TRACE(testing::Message() << "fog=" << fog << " fresnel=" << fresnel
                                                    << " specular=" << specular << " oneLight=" << oneLight);
                    EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(),
                              (fog ? 0 : 1) + (fresnel ? 2 : 0) + (specular ? 4 : 0) + (oneLight ? 8 : 0));
                }
}

// set_FresnelFactor and set_EnvironmentMapSpecular dirty ShaderIndex only when the enable state
// flips (IL_000c-0035 each): a new non-zero factor rewrites nothing.
TEST(StockEffectShaderIndexTest, EnvironmentMapEffectRewritesTheIndexOnlyWhenAnEnableFlips)
{
    GraphicsDevice device;
    EnvironmentMapEffect effect(device);
    effect.Apply();
    Poison(effect, {"ShaderIndex"});
    effect.setFresnelFactorProperty(0.5f);
    effect.setEnvironmentMapSpecularProperty(Vector3::Zero);
    effect.Apply();
    ExpectUntouched(effect, {"ShaderIndex"});

    effect.setFresnelFactorProperty(0.0f);
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(), 9);  // 11 - Fresnel 2
    effect.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.125f, 0.0f));
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(), 13); // 9 + specular 4
}

// DualTextureEffect::OnApply IL_0095-00b6: fog off +1, vertex colour +2.
TEST(StockEffectShaderIndexTest, DualTextureEffectSelectsAllFourVariants)
{
    GraphicsDevice device;
    for (const bool fog : {false, true})
        for (const bool vertexColor : {false, true})
        {
            DualTextureEffect effect(device);
            effect.setFogEnabledProperty(fog);
            effect.setVertexColorEnabledProperty(vertexColor);
            effect.Apply();
            EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(),
                      (fog ? 0 : 1) + (vertexColor ? 2 : 0)) << fog << vertexColor;
        }
}

// AlphaTestEffect::OnApply IL_0269-0296: fog off +1, vertex colour +2, Equal/NotEqual +4.
TEST(StockEffectShaderIndexTest, AlphaTestEffectSelectsAllEightVariants)
{
    GraphicsDevice device;
    for (const bool fog : {false, true})
        for (const bool vertexColor : {false, true})
            for (const bool eqNe : {false, true})
            {
                AlphaTestEffect effect(device);
                effect.setFogEnabledProperty(fog);
                effect.setVertexColorEnabledProperty(vertexColor);
                effect.setAlphaFunctionProperty(eqNe ? CompareFunction::NotEqual
                                                     : CompareFunction::LessEqual);
                effect.Apply();
                EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(),
                          (fog ? 0 : 1) + (vertexColor ? 2 : 0) + (eqNe ? 4 : 0))
                    << fog << vertexColor << eqNe;
            }
}

// set_VertexColorEnabled IL_0000-0007 returns early for the current value (AlphaTestEffect and
// DualTextureEffect alike), so only a real change rewrites the index.
TEST(StockEffectShaderIndexTest, ReassigningVertexColourRewritesNothing)
{
    GraphicsDevice device;
    AlphaTestEffect alphaTest(device);
    DualTextureEffect dualTexture(device);
    alphaTest.Apply();
    dualTexture.Apply();
    Poison(alphaTest, {"ShaderIndex"});
    Poison(dualTexture, {"ShaderIndex"});
    alphaTest.setVertexColorEnabledProperty(false);
    dualTexture.setVertexColorEnabledProperty(false);
    alphaTest.Apply();
    dualTexture.Apply();
    ExpectUntouched(alphaTest, {"ShaderIndex"});
    ExpectUntouched(dualTexture, {"ShaderIndex"});
}

// set_WeightsPerVertex IL_001c-0030 marks ShaderIndex dirty with no equality check, unlike the
// boolean setters, so even the current value rewrites it.
TEST(StockEffectShaderIndexTest, SkinnedWeightsPerVertexAlwaysRewritesTheIndex)
{
    GraphicsDevice device;
    SkinnedEffect effect(device);
    effect.Apply();
    Poison(effect, {"ShaderIndex"});
    effect.setPreferPerPixelLightingProperty(false);  // unchanged: no dirty bit
    effect.Apply();
    ExpectUntouched(effect, {"ShaderIndex"});
    effect.setWeightsPerVertexProperty(4);
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(), 11);
}

// ---- AlphaTestEffect's compare vector for every CompareFunction -----------------------------------

// AlphaTestEffect::OnApply IL_0095-0225 with reference = ReferenceAlpha / 255f and the threshold
// literal 0.00196078443 (0.5 / 255). The switch is over XNA's CompareFunction order (Always, Never,
// Less, LessEqual, Equal, GreaterEqual, Greater, NotEqual); anything else takes Always' branch
// (IL_00da). Equal and NotEqual select the Eq/Ne shaders (+4).
TEST(AlphaTestEffectCompareVectorTest, EveryCompareFunctionProducesItsXnaVector)
{
    constexpr float t = 0.00196078443f;
    const float reference = 51.0f / 255.0f;
    struct Case
    {
        CompareFunction function;
        Vector4 expected;
        bool eqNe;
    };
    const Case cases[] = {
        {CompareFunction::Always, Vector4(0.0f, 0.0f, 1.0f, 1.0f), false},
        {CompareFunction::Never, Vector4(0.0f, 0.0f, -1.0f, -1.0f), false},
        {CompareFunction::Less, Vector4(reference - t, 0.0f, 1.0f, -1.0f), false},
        {CompareFunction::LessEqual, Vector4(reference + t, 0.0f, 1.0f, -1.0f), false},
        {CompareFunction::Equal, Vector4(reference, t, 1.0f, -1.0f), true},
        {CompareFunction::GreaterEqual, Vector4(reference - t, 0.0f, -1.0f, 1.0f), false},
        {CompareFunction::Greater, Vector4(reference + t, 0.0f, -1.0f, 1.0f), false},
        {CompareFunction::NotEqual, Vector4(reference, t, -1.0f, 1.0f), true},
        {static_cast<CompareFunction>(42), Vector4(0.0f, 0.0f, 1.0f, 1.0f), false},
    };

    GraphicsDevice device;
    for (const Case& c : cases)
    {
        SCOPED_TRACE(static_cast<int>(c.function));
        AlphaTestEffect effect(device);
        effect.setReferenceAlphaProperty(51);
        effect.setAlphaFunctionProperty(c.function);
        effect.Apply();
        const Vector4 actual = Parameter(effect, "AlphaTest").GetValueVector4();
        EXPECT_FLOAT_EQ(actual.X, c.expected.X);
        EXPECT_FLOAT_EQ(actual.Y, c.expected.Y);
        EXPECT_FLOAT_EQ(actual.Z, c.expected.Z);
        EXPECT_FLOAT_EQ(actual.W, c.expected.W);
        EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(), c.eqNe ? 5 : 1);
    }
}

// ReferenceAlpha and AlphaFunction mark only AlphaTest (64) dirty; ShaderIndex follows only when
// the Eq/Ne class flips (IL_0239-0256).
TEST(AlphaTestEffectCompareVectorTest, ReferenceChangesRewriteOnlyTheCompareVector)
{
    constexpr float t = 0.00196078443f;
    GraphicsDevice device;
    AlphaTestEffect effect(device);
    effect.Apply();
    Poison(effect, {"AlphaTest", "ShaderIndex", "WorldViewProj", "DiffuseColor", "FogVector"});

    effect.setReferenceAlphaProperty(102);
    effect.Apply();
    Vector4 actual = Parameter(effect, "AlphaTest").GetValueVector4();
    EXPECT_FLOAT_EQ(actual.X, 102.0f / 255.0f + t);  // Greater, the default
    EXPECT_FLOAT_EQ(actual.Z, -1.0f);
    ExpectUntouched(effect, {"ShaderIndex", "WorldViewProj", "DiffuseColor", "FogVector"});

    effect.setAlphaFunctionProperty(CompareFunction::GreaterEqual);
    effect.Apply();
    ExpectUntouched(effect, {"ShaderIndex"});

    effect.setAlphaFunctionProperty(CompareFunction::Equal);
    effect.Apply();
    actual = Parameter(effect, "AlphaTest").GetValueVector4();
    EXPECT_FLOAT_EQ(actual.X, 102.0f / 255.0f);
    EXPECT_FLOAT_EQ(actual.Y, t);
    EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(), 5);
}

// ---- BasicEffect --------------------------------------------------------------------------------
//
// BasicEffect::OnApply IL_0000-0183 follows the same rules, with lighting optional: the lit
// material form and SetLightingMatrices run only when LightingEnabled (IL_008f-0095), the unlit
// form folds emissive into diffuse and writes no EmissiveColor (SetMaterialColor IL_00a6-00f8),
// set_LightingEnabled marks MaterialColor|ShaderIndex (0x88) on change, set_TextureEnabled and
// set_VertexColorEnabled 0x80 on change. World, View and Projection are also public fields in
// CNA; a changed field counts as the property set (TQ-041 decision row).

namespace
{
    // Unlit DiffuseColor: ((d + e) * a, a) = ((0.5+0.125)*0.5, (0.25+0.25)*0.5, (0.75+0.0625)*0.5, 0.5).
    const Vector4 kUnlitDiffuseParameter(0.3125f, 0.25f, 0.40625f, 0.5f);

    class BasicEffectApplyTest : public ::testing::Test
    {
    protected:
        void SetUpScenario(bool lighting)
        {
            effect.setWorldProperty(kWorld);
            effect.setViewProperty(kView);
            effect.setProjectionProperty(kProjection);
            effect.setFogEnabledProperty(true);
            effect.setFogStartProperty(kFogStart);
            effect.setFogEndProperty(kFogEnd);
            effect.setDiffuseColorProperty(kDiffuse);
            effect.setEmissiveColorProperty(kEmissive);
            effect.setAmbientLightColorProperty(kAmbient);
            effect.setAlphaProperty(kAlpha);
            effect.setLightingEnabledProperty(lighting);
        }

        void PoisonDerived()
        {
            Poison(effect, {"WorldViewProj", "FogVector", "DiffuseColor", "EmissiveColor", "ShaderIndex",
                            "World", "WorldInverseTranspose", "EyePosition"});
        }

        GraphicsDevice device;
        BasicEffect effect{device};
    };
}

TEST_F(BasicEffectApplyTest, ApplyWithNothingChangedRewritesNothing)
{
    for (const bool lighting : {false, true})
    {
        SCOPED_TRACE(lighting ? "lit" : "unlit");
        SetUpScenario(lighting);
        effect.Apply();
        PoisonDerived();
        effect.Apply();
        ExpectUntouched(effect, {"WorldViewProj", "FogVector", "DiffuseColor", "EmissiveColor",
                                 "ShaderIndex", "World", "WorldInverseTranspose", "EyePosition"});
    }
}

TEST_F(BasicEffectApplyTest, FirstLitApplyDerivesEverything)
{
    SetUpScenario(true);
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "WorldViewProj").GetValueMatrix(), kWorldViewProj);
    EXPECT_EQ(Parameter(effect, "FogVector").GetValueVector4(), kFogVector);
    EXPECT_EQ(Parameter(effect, "DiffuseColor").GetValueVector4(), kDiffuseParameter);
    EXPECT_EQ(Parameter(effect, "EmissiveColor").GetValueVector3(), kEmissiveParameter);
    EXPECT_EQ(Parameter(effect, "World").GetValueMatrix(), kWorld);
    ExpectMatrix3x3(Parameter(effect, "WorldInverseTranspose").GetValueMatrix(),
                    Matrix(0.5f, 0, 0, 0, 0, 0.5f, 0, 0, -0.125f, -0.0625f, 0.5f, 0, 0, 0, 0, 1));
    EXPECT_EQ(Parameter(effect, "EyePosition").GetValueVector3(), Vector3(0.0f, 0.0f, 10.0f));
    EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(), 16) << "fog on, one light";
}

// Unlit: diffuse folds the emissive term in; EmissiveColor, World, WorldInverseTranspose and
// EyePosition are not written at all.
TEST_F(BasicEffectApplyTest, AnUnlitApplyWritesNoLightingParameter)
{
    SetUpScenario(false);
    Poison(effect, {"EmissiveColor", "World", "WorldInverseTranspose", "EyePosition"});
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "DiffuseColor").GetValueVector4(), kUnlitDiffuseParameter);
    ExpectUntouched(effect, {"EmissiveColor", "World", "WorldInverseTranspose", "EyePosition"});
    EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(), 0) << "fog on, unlit";
}

TEST_F(BasicEffectApplyTest, ProjectionRecomputesOnlyWorldViewProj)
{
    SetUpScenario(true);
    effect.Apply();
    PoisonDerived();
    effect.setProjectionProperty(Matrix::getIdentityProperty());
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "WorldViewProj").GetValueMatrix(),
              Matrix(2, 0, 0.5f, 0, 0, 2, 0.25f, 0, 0, 0, 2, 0, 1, 2, -7, 1));
    ExpectUntouched(effect, {"FogVector", "DiffuseColor", "EmissiveColor", "ShaderIndex", "World",
                             "WorldInverseTranspose", "EyePosition"});
}

TEST_F(BasicEffectApplyTest, WorldRecomputesWorldViewProjFogAndTheLightingMatrices)
{
    SetUpScenario(true);
    effect.Apply();
    PoisonDerived();
    effect.setWorldProperty(kWorld2);
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "WorldViewProj").GetValueMatrix(), kWorldViewProj2);
    EXPECT_EQ(Parameter(effect, "FogVector").GetValueVector4(), kFogVector2);
    EXPECT_EQ(Parameter(effect, "World").GetValueMatrix(), kWorld2);
    ExpectMatrix3x3(Parameter(effect, "WorldInverseTranspose").GetValueMatrix(),
                    Matrix::CreateScale(0.5f));
    ExpectUntouched(effect, {"DiffuseColor", "EmissiveColor", "ShaderIndex", "EyePosition"});
}

TEST_F(BasicEffectApplyTest, ViewRecomputesWorldViewProjFogAndTheEyePosition)
{
    SetUpScenario(true);
    effect.Apply();
    PoisonDerived();
    effect.setViewProperty(kView2);
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "WorldViewProj").GetValueMatrix(), kWorldViewProjView2);
    EXPECT_EQ(Parameter(effect, "FogVector").GetValueVector4(), kFogVectorView2);
    EXPECT_EQ(Parameter(effect, "EyePosition").GetValueVector3(), Vector3(0.0f, 0.0f, 6.0f));
    ExpectUntouched(effect, {"DiffuseColor", "EmissiveColor", "ShaderIndex", "World",
                             "WorldInverseTranspose"});
}

TEST_F(BasicEffectApplyTest, MaterialSettersRecomputeOnlyTheColours)
{
    SetUpScenario(true);
    effect.Apply();
    PoisonDerived();
    effect.setAmbientLightColorProperty(kAmbient);
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "DiffuseColor").GetValueVector4(), kDiffuseParameter);
    EXPECT_EQ(Parameter(effect, "EmissiveColor").GetValueVector3(), kEmissiveParameter);
    ExpectUntouched(effect, {"WorldViewProj", "FogVector", "ShaderIndex", "World",
                             "WorldInverseTranspose", "EyePosition"});
}

// The World and EyePosition bits set at construction are consumed only by a lit Apply, so turning
// lighting on later writes World, WorldInverseTranspose and EyePosition even though neither matrix
// changed; turning it off rewrites only the material and the index.
TEST_F(BasicEffectApplyTest, LightingTogglesRewriteMaterialIndexAndPendingLightingMatrices)
{
    SetUpScenario(false);
    effect.Apply();
    PoisonDerived();
    effect.setLightingEnabledProperty(true);
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "DiffuseColor").GetValueVector4(), kDiffuseParameter);
    EXPECT_EQ(Parameter(effect, "EmissiveColor").GetValueVector3(), kEmissiveParameter);
    EXPECT_EQ(Parameter(effect, "World").GetValueMatrix(), kWorld);
    EXPECT_EQ(Parameter(effect, "EyePosition").GetValueVector3(), Vector3(0.0f, 0.0f, 10.0f));
    EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(), 16);
    ExpectUntouched(effect, {"WorldViewProj", "FogVector"});

    PoisonDerived();
    effect.setLightingEnabledProperty(false);
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "DiffuseColor").GetValueVector4(), kUnlitDiffuseParameter);
    EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(), 0);
    ExpectUntouched(effect, {"WorldViewProj", "FogVector", "EmissiveColor", "World",
                             "WorldInverseTranspose", "EyePosition"});
}

// IL_00c6-0102: a lit Apply recomputes the one-light test and rewrites ShaderIndex when it flips,
// with no effect setter involved; an unlit Apply never looks at the lights.
TEST_F(BasicEffectApplyTest, TheOneLightTestRunsOnlyOnLitApplies)
{
    SetUpScenario(true);
    effect.Apply();
    Poison(effect, {"ShaderIndex"});
    effect.getDirectionalLight1Property().setEnabledProperty(true);
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(), 8) << "fog on, three lights";

    effect.setLightingEnabledProperty(false);
    effect.Apply();
    Poison(effect, {"ShaderIndex"});
    effect.getDirectionalLight1Property().setEnabledProperty(false);
    effect.Apply();
    ExpectUntouched(effect, {"ShaderIndex"});
}

TEST_F(BasicEffectApplyTest, BooleanSettersRewriteTheIndexOnlyOnChange)
{
    SetUpScenario(false);
    effect.Apply();
    Poison(effect, {"ShaderIndex"});
    effect.setTextureEnabledProperty(false);
    effect.setVertexColorEnabledProperty(false);
    effect.setPreferPerPixelLightingProperty(false);
    effect.setLightingEnabledProperty(false);
    effect.setFogEnabledProperty(true);
    effect.Apply();
    ExpectUntouched(effect, {"ShaderIndex"});

    effect.setTextureEnabledProperty(true);
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(), 4);
    effect.setVertexColorEnabledProperty(true);
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(), 6);
}

TEST_F(BasicEffectApplyTest, AFogRangeChangeWaitsUntilFogIsEnabled)
{
    SetUpScenario(true);
    effect.setFogEnabledProperty(false);
    effect.Apply();
    Poison(effect, {"FogVector"});
    effect.setFogStartProperty(6.0f);
    effect.Apply();
    ExpectUntouched(effect, {"FogVector"});
    effect.setFogEnabledProperty(true);
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "FogVector").GetValueVector4(),
              Vector4(-0.125f, -0.0625f, -0.5f, 0.25f));
}

// set_World IL_0000-0016 marks its bits with no equality check, so the property recomputes even
// for the current value. The CNA-only public fields have no setter to observe; a field counts as
// set when its value differs from the one the previous Apply used.
TEST_F(BasicEffectApplyTest, PropertiesDirtyAlwaysAndFieldsDirtyOnChange)
{
    SetUpScenario(true);
    effect.Apply();
    PoisonDerived();
    effect.setWorldProperty(kWorld);
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "WorldViewProj").GetValueMatrix(), kWorldViewProj);
    EXPECT_EQ(Parameter(effect, "World").GetValueMatrix(), kWorld);

    PoisonDerived();
    effect.World = kWorld;
    effect.Apply();
    ExpectUntouched(effect, {"WorldViewProj", "World", "FogVector"});

    effect.World = kWorld2;
    effect.Projection = kProjection;
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "WorldViewProj").GetValueMatrix(), kWorldViewProj2);
    EXPECT_EQ(Parameter(effect, "World").GetValueMatrix(), kWorld2);
    EXPECT_EQ(Parameter(effect, "FogVector").GetValueVector4(), kFogVector2);
    ExpectUntouched(effect, {"EyePosition", "ShaderIndex"});

    effect.VertexColorEnabled = true;
    effect.Apply();
    EXPECT_EQ(Parameter(effect, "ShaderIndex").GetValueInt32(), 18) << "fog on, vertex colour, one light";
}
