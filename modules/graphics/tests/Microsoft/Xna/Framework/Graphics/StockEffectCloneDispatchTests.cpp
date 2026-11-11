// SPDX-License-Identifier: MS-PL
// HND-TQ-25 D9: a base-Effect or EffectMaterial clone of a stock effect draws with the cloned stock
// shaders. XNA's Effect::.ctor(Effect) IL_0048-0085 clones the native stock effect; the clone is
// not the stock class, so EffectPass.Apply IL_003b runs the clone's own (empty) OnApply and the
// shaders read the clone's raw parameter values. Each stock technique's single pass selects its
// shaders by ShaderIndex through the VSIndices/PSIndices tables of the stock .fx files, which the
// tables below transcribe (BasicEffect.fx, AlphaTestEffect.fx, DualTextureEffect.fx,
// EnvironmentMapEffect.fx, SkinnedEffect.fx of the XNA Stock Effects sample).
//
// These cases check what a clone hands the renderer (GpuDrawParams and the draw matrices); the
// pixels each renderer family draws from it are StockEffectCloneRenderingTests.

#include <gtest/gtest.h>

#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectMaterial.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "System/InvalidOperationException.hpp"

using CNA::Internal::Renderers::GpuDrawParams;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    GpuDrawParams DrawParamsOf(const Effect& effect)
    {
        GpuDrawParams params;
        effect.FillGpuDrawParams(params);
        return params;
    }

    void SetShaderIndex(Effect& effect, int index)
    {
        effect.getParametersProperty()["ShaderIndex"]->SetValue(index);
    }

    bool IsBlack(const float (&rgb)[3])
    {
        return rgb[0] == 0.0f && rgb[1] == 0.0f && rgb[2] == 0.0f;
    }

    // Every light parameter of a lit clone gets a distinct non-black value, so a light the selected
    // vertex shader does not read shows up as a zero in the draw parameters.
    void LightAllThree(Effect& effect, bool withSpecular)
    {
        auto& parameters = effect.getParametersProperty();
        for (int light = 0; light < 3; ++light)
        {
            const std::string prefix = "DirLight" + std::to_string(light);
            parameters[prefix + "Direction"]->SetValue(Vector3(0.0f, 0.0f, -1.0f));
            parameters[prefix + "DiffuseColor"]->SetValue(Vector3(0.25f * (light + 1), 0.5f, 0.75f));
            if (withSpecular)
                parameters[prefix + "SpecularColor"]->SetValue(Vector3(0.5f, 0.25f * (light + 1), 0.125f));
        }
    }

    // BasicEffect.fx: VSArray (texture, vertex colour, lighting) and PSArray (fogs?), indexed through
    // VSIndices/PSIndices by ShaderIndex.
    enum class Lighting { None, ThreeLights, OneLight, PerPixel };
    struct BasicVertexShader { bool texture; bool vertexColor; Lighting lighting; };
    constexpr BasicVertexShader kBasicVs[20] = {
        {false, false, Lighting::None}, {false, false, Lighting::None},   // VSBasic, NoFog
        {false, true, Lighting::None},  {false, true, Lighting::None},    // Vc, VcNoFog
        {true, false, Lighting::None},  {true, false, Lighting::None},    // Tx, TxNoFog
        {true, true, Lighting::None},   {true, true, Lighting::None},     // TxVc, TxVcNoFog
        {false, false, Lighting::ThreeLights}, {false, true, Lighting::ThreeLights},
        {true, false, Lighting::ThreeLights},  {true, true, Lighting::ThreeLights},
        {false, false, Lighting::OneLight},    {false, true, Lighting::OneLight},
        {true, false, Lighting::OneLight},     {true, true, Lighting::OneLight},
        {false, false, Lighting::PerPixel},    {false, true, Lighting::PerPixel},
        {true, false, Lighting::PerPixel},     {true, true, Lighting::PerPixel},
    };
    constexpr int kBasicVsIndices[32] = {0, 1, 2, 3, 4, 5, 6, 7,
                                         8, 8, 9, 9, 10, 10, 11, 11,
                                         12, 12, 13, 13, 14, 14, 15, 15,
                                         16, 16, 17, 17, 18, 18, 19, 19};
    // PSBasic, PSBasicNoFog, PSBasicTx, PSBasicTxNoFog, PSBasicVertexLighting, ...NoFog,
    // ...Tx, ...TxNoFog, PSBasicPixelLighting, PSBasicPixelLightingTx.
    constexpr bool kBasicPsFogs[10] = {true, false, true, false, true, false, true, false, true, true};
    constexpr int kBasicPsIndices[32] = {0, 1, 0, 1, 2, 3, 2, 3,
                                         4, 5, 4, 5, 6, 7, 6, 7,
                                         4, 5, 4, 5, 6, 7, 6, 7,
                                         8, 8, 8, 8, 9, 9, 9, 9};
}

TEST(StockEffectCloneDispatchTest, EveryBasicEffectShaderIndexSelectsItsStockShaders)
{
    GraphicsDevice device;
    BasicEffect source(device);
    EffectMaterial clone(source);
    LightAllThree(clone, true);
    clone.getParametersProperty()["FogVector"]->SetValue(Vector4(0.125f, 0.25f, 0.5f, 1.0f));

    for (int index = 0; index < 32; ++index)
    {
        SCOPED_TRACE("ShaderIndex " + std::to_string(index));
        SetShaderIndex(clone, index);
        clone.Apply();
        const GpuDrawParams p = DrawParamsOf(clone);
        const BasicVertexShader& vs = kBasicVs[kBasicVsIndices[index]];
        const bool fogs = kBasicPsFogs[kBasicPsIndices[index]];

        EXPECT_TRUE(p.rawStockTransforms);
        EXPECT_FALSE(p.alphaTestEffect || p.dualTexture || p.envMapping || p.skinned || p.pbr);
        EXPECT_EQ(p.textureEnabled, vs.texture);
        EXPECT_EQ(p.vertexColorEnabled, vs.vertexColor);
        EXPECT_EQ(p.lightingEnabled, vs.lighting != Lighting::None);
        EXPECT_EQ(p.preferPerPixelLighting, vs.lighting == Lighting::PerPixel);
        EXPECT_EQ(p.fogEnabled, fogs);
        // A pixel shader that does not fog reaches renderers that fog by the vector alone as zero.
        EXPECT_EQ(p.fogVector[3], fogs ? 1.0f : 0.0f);
        if (vs.lighting != Lighting::None)
        {
            EXPECT_FALSE(IsBlack(p.light0Diffuse));
            EXPECT_FALSE(IsBlack(p.light0Specular));
            // ComputeLights(..., 1) in the one-light vertex shaders reads DirLight0 only.
            const bool oneLight = vs.lighting == Lighting::OneLight;
            EXPECT_EQ(IsBlack(p.light1Diffuse), oneLight);
            EXPECT_EQ(IsBlack(p.light2Diffuse), oneLight);
            EXPECT_EQ(IsBlack(p.light1Specular), oneLight);
            EXPECT_EQ(IsBlack(p.light2Specular), oneLight);
        }
    }
}

TEST(StockEffectCloneDispatchTest, EveryAlphaTestEffectShaderIndexSelectsItsStockShaders)
{
    // VSAlphaTest, NoFog, Vc, VcNoFog through VSIndices; PSAlphaTestLtGt, LtGtNoFog, EqNe,
    // EqNeNoFog through PSIndices.
    constexpr bool vsVertexColor[4] = {false, false, true, true};
    constexpr int vsIndices[8] = {0, 1, 2, 3, 0, 1, 2, 3};
    constexpr bool psEqNe[4] = {false, false, true, true};
    constexpr bool psFogs[4] = {true, false, true, false};
    constexpr int psIndices[8] = {0, 1, 0, 1, 2, 3, 2, 3};

    GraphicsDevice device;
    AlphaTestEffect source(device);
    EffectMaterial clone(source);
    // A positive y, which the less/greater pixel shaders never read.
    const Vector4 alphaTest(0.5f, 0.25f, 1.0f, -1.0f);
    clone.getParametersProperty()["AlphaTest"]->SetValue(alphaTest);

    for (int index = 0; index < 8; ++index)
    {
        SCOPED_TRACE("ShaderIndex " + std::to_string(index));
        SetShaderIndex(clone, index);
        clone.Apply();
        const GpuDrawParams p = DrawParamsOf(clone);
        const bool eqNe = psEqNe[psIndices[index]];
        EXPECT_TRUE(p.alphaTestEffect);
        EXPECT_TRUE(p.textureEnabled);
        EXPECT_FALSE(p.lightingEnabled);
        EXPECT_EQ(p.vertexColorEnabled, vsVertexColor[vsIndices[index]]);
        EXPECT_EQ(p.fogEnabled, psFogs[psIndices[index]]);
        // The renderers choose equal/not-equal by y > 0.
        EXPECT_EQ(p.alphaTest[1] > 0.0f, eqNe);
        EXPECT_EQ(p.alphaTest[0], 0.5f);
        EXPECT_EQ(p.alphaTest[1], eqNe ? 0.25f : 0.0f);
        EXPECT_EQ(p.alphaTest[2], 1.0f);
        EXPECT_EQ(p.alphaTest[3], -1.0f);
    }
}

// PSAlphaTestEqNe evaluates clip((abs(a - x) < y) ? z : w). With y not positive no alpha passes the
// comparison, so every pixel takes w; the renderers' less/greater form gives that for x = -FLT_MAX.
TEST(StockEffectCloneDispatchTest, AnEqualAlphaTestWithANonPositiveToleranceAlwaysTakesW)
{
    GraphicsDevice device;
    AlphaTestEffect source(device);
    EffectMaterial clone(source);
    SetShaderIndex(clone, 4);
    for (const float tolerance : {0.0f, -0.25f, std::numeric_limits<float>::quiet_NaN()})
    {
        clone.getParametersProperty()["AlphaTest"]->SetValue(Vector4(0.5f, tolerance, 1.0f, -1.0f));
        clone.Apply();
        const GpuDrawParams p = DrawParamsOf(clone);
        EXPECT_EQ(p.alphaTest[0], -std::numeric_limits<float>::max());
        EXPECT_EQ(p.alphaTest[1], 0.0f);
        EXPECT_EQ(p.alphaTest[2], 1.0f);
        EXPECT_EQ(p.alphaTest[3], -1.0f);
    }
}

TEST(StockEffectCloneDispatchTest, EveryDualTextureEffectShaderIndexSelectsItsStockShaders)
{
    // VSArray[ShaderIndex] directly: VSDualTexture, NoFog, Vc, VcNoFog; PSIndices {0,1,0,1}.
    GraphicsDevice device;
    DualTextureEffect source(device);
    EffectMaterial clone(source);
    for (int index = 0; index < 4; ++index)
    {
        SCOPED_TRACE("ShaderIndex " + std::to_string(index));
        SetShaderIndex(clone, index);
        clone.Apply();
        const GpuDrawParams p = DrawParamsOf(clone);
        EXPECT_TRUE(p.dualTexture);
        EXPECT_TRUE(p.textureEnabled);
        EXPECT_FALSE(p.lightingEnabled);
        EXPECT_EQ(p.vertexColorEnabled, index >= 2);
        EXPECT_EQ(p.fogEnabled, index % 2 == 0);
    }
}

TEST(StockEffectCloneDispatchTest, EveryEnvironmentMapEffectShaderIndexSelectsItsStockShaders)
{
    // VSEnvMap, Fresnel, OneLight, OneLightFresnel; PSEnvMap, NoFog, Specular, SpecularNoFog.
    constexpr bool vsFresnel[4] = {false, true, false, true};
    constexpr bool vsOneLight[4] = {false, false, true, true};
    constexpr int vsIndices[16] = {0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 3, 3, 2, 2, 3, 3};
    constexpr bool psSpecular[4] = {false, false, true, true};
    constexpr bool psFogs[4] = {true, false, true, false};
    constexpr int psIndices[16] = {0, 1, 0, 1, 2, 3, 2, 3, 0, 1, 0, 1, 2, 3, 2, 3};

    GraphicsDevice device;
    EnvironmentMapEffect source(device);
    EffectMaterial clone(source);
    LightAllThree(clone, false);
    for (int index = 0; index < 16; ++index)
    {
        SCOPED_TRACE("ShaderIndex " + std::to_string(index));
        SetShaderIndex(clone, index);
        clone.Apply();
        const GpuDrawParams p = DrawParamsOf(clone);
        EXPECT_TRUE(p.envMapping);
        EXPECT_TRUE(p.lightingEnabled);
        EXPECT_FALSE(p.vertexColorEnabled);
        EXPECT_EQ(p.fresnelEnabled, vsFresnel[vsIndices[index]]);
        EXPECT_EQ(p.specularEnabled, psSpecular[psIndices[index]]);
        EXPECT_EQ(p.fogEnabled, psFogs[psIndices[index]]);
        EXPECT_FALSE(IsBlack(p.light0Diffuse));
        EXPECT_EQ(IsBlack(p.light1Diffuse), vsOneLight[vsIndices[index]]);
        EXPECT_EQ(IsBlack(p.light2Diffuse), vsOneLight[vsIndices[index]]);
    }
}

TEST(StockEffectCloneDispatchTest, EverySkinnedEffectShaderIndexSelectsItsStockShaders)
{
    // VSSkinned{VertexLighting,OneLight,PixelLighting}{One,Two,Four}Bone(s) through VSIndices;
    // PSSkinnedVertexLighting, ...NoFog, PSSkinnedPixelLighting through PSIndices.
    constexpr int vsWeights[9] = {1, 2, 4, 1, 2, 4, 1, 2, 4};
    constexpr Lighting vsLighting[9] = {
        Lighting::ThreeLights, Lighting::ThreeLights, Lighting::ThreeLights,
        Lighting::OneLight, Lighting::OneLight, Lighting::OneLight,
        Lighting::PerPixel, Lighting::PerPixel, Lighting::PerPixel};
    constexpr int vsIndices[18] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8};
    constexpr bool psFogs[3] = {true, false, true};
    constexpr int psIndices[18] = {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2, 2, 2};

    GraphicsDevice device;
    SkinnedEffect source(device);
    EffectMaterial clone(source);
    LightAllThree(clone, true);
    for (int index = 0; index < 18; ++index)
    {
        SCOPED_TRACE("ShaderIndex " + std::to_string(index));
        SetShaderIndex(clone, index);
        clone.Apply();
        const GpuDrawParams p = DrawParamsOf(clone);
        const int vs = vsIndices[index];
        EXPECT_TRUE(p.skinned);
        EXPECT_TRUE(p.lightingEnabled);
        // XNA's SkinnedEffect has no vertex-colour shader.
        EXPECT_FALSE(p.vertexColorEnabled);
        EXPECT_EQ(p.weightsPerVertex, vsWeights[vs]);
        EXPECT_EQ(p.preferPerPixelLighting, vsLighting[vs] == Lighting::PerPixel);
        EXPECT_EQ(p.fogEnabled, psFogs[psIndices[index]]);
        EXPECT_EQ(IsBlack(p.light1Diffuse), vsLighting[vs] == Lighting::OneLight);
        EXPECT_EQ(IsBlack(p.light2Specular), vsLighting[vs] == Lighting::OneLight);
        EXPECT_EQ(p.boneCount, 72);
    }
}

// What the native effect does with an index outside its shader tables is not established, so CNA
// refuses it at Apply.
TEST(StockEffectCloneDispatchTest, AShaderIndexNoStockShaderAnswersIsRefusedAtApply)
{
    GraphicsDevice device;
    BasicEffect source(device);
    EffectMaterial clone(source);
    for (const int index : {-1, 32, 99})
    {
        SetShaderIndex(clone, index);
        EXPECT_THROW(clone.Apply(), System::InvalidOperationException);
    }

    SkinnedEffect skinned(device);
    EffectMaterial skinnedClone(skinned);
    SetShaderIndex(skinnedClone, 18);
    EXPECT_THROW(skinnedClone.Apply(), System::InvalidOperationException);
    SetShaderIndex(skinnedClone, 17);
    EXPECT_NO_THROW(skinnedClone.Apply());
}

// XNA latches the constants when the pass begins: a raw write after Apply reaches the draw only
// through the next Apply.
TEST(StockEffectCloneDispatchTest, TheDrawUsesWhatTheLastApplyRead)
{
    GraphicsDevice device;
    BasicEffect source(device);
    source.Apply();
    EffectMaterial clone(source);
    auto& diffuse = *clone.getParametersProperty()["DiffuseColor"];
    diffuse.SetValue(Vector4(0.25f, 0.5f, 0.75f, 1.0f));
    clone.Apply();

    diffuse.SetValue(Vector4(1.0f, 0.0f, 0.0f, 1.0f));
    Matrix world, view, projection;
    clone.getParametersProperty()["WorldViewProj"]->SetValue(Matrix::CreateScale(3.0f));
    ASSERT_TRUE(clone.GetStockCloneDrawMatricesInternal(world, view, projection));
    EXPECT_EQ(projection, Matrix::getIdentityProperty());
    GpuDrawParams p = DrawParamsOf(clone);
    EXPECT_EQ(p.diffuseColor[0], 0.25f);
    EXPECT_EQ(p.diffuseColor[1], 0.5f);

    clone.Apply();
    p = DrawParamsOf(clone);
    EXPECT_EQ(p.diffuseColor[0], 1.0f);
    EXPECT_EQ(p.diffuseColor[1], 0.0f);
    ASSERT_TRUE(clone.GetStockCloneDrawMatricesInternal(world, view, projection));
    EXPECT_EQ(projection, Matrix::CreateScale(3.0f));
}

// The stock vertex shaders place a vertex with WorldViewProj and light it with World and
// WorldInverseTranspose. A clone's three are raw and independent: the renderer gets identity,
// identity and WorldViewProj, plus World and WorldInverseTranspose unchanged.
TEST(StockEffectCloneDispatchTest, TheRawTransformsReachTheRendererUnchanged)
{
    GraphicsDevice device;
    BasicEffect source(device);
    EffectMaterial clone(source);
    auto& parameters = clone.getParametersProperty();
    const Matrix worldViewProj(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    const Matrix world(2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 5, 6, 7, 1);
    const Matrix worldInverseTranspose(-1, 2, 3, 0, 4, -5, 6, 0, 7, 8, -9, 0, 0, 0, 0, 1);
    parameters["WorldViewProj"]->SetValue(worldViewProj);
    parameters["World"]->SetValue(world);
    parameters["WorldInverseTranspose"]->SetValue(worldInverseTranspose);
    SetShaderIndex(clone, 24);
    clone.Apply();

    Matrix drawWorld, drawView, drawProjection;
    ASSERT_TRUE(clone.GetStockCloneDrawMatricesInternal(drawWorld, drawView, drawProjection));
    EXPECT_EQ(drawWorld, Matrix::getIdentityProperty());
    EXPECT_EQ(drawView, Matrix::getIdentityProperty());
    EXPECT_EQ(drawProjection, worldViewProj);

    const GpuDrawParams p = DrawParamsOf(clone);
    ASSERT_TRUE(p.rawStockTransforms);
    float expectedWorld[16];
    world.ToColumnMajor(expectedWorld);
    for (int i = 0; i < 16; ++i)
        EXPECT_EQ(p.worldColMajor[i], expectedWorld[i]) << i;
    const float expectedNormal[9] = {-1, 2, 3, 4, -5, 6, 7, 8, -9};
    for (int i = 0; i < 9; ++i)
        EXPECT_EQ(p.worldInverseTransposeColMajor[i], expectedNormal[i]) << i;
}

// EmissiveColor already holds (emissive + ambient * diffuse) * alpha when a stock OnApply wrote it,
// so a clone's renderer-side ambient term is zero and its raw EmissiveColor is the whole constant.
TEST(StockEffectCloneDispatchTest, TheRawEmissiveColourIsTheWholeConstantTerm)
{
    GraphicsDevice device;
    BasicEffect source(device);
    source.setLightingEnabledProperty(true);
    source.setAmbientLightColorProperty(Vector3(0.5f, 0.5f, 0.5f));
    source.setDiffuseColorProperty(Vector3(0.5f, 0.25f, 1.0f));
    source.setEmissiveColorProperty(Vector3(0.125f, 0.0f, 0.0f));
    source.Apply();
    EffectMaterial clone(source);
    clone.Apply();

    const GpuDrawParams p = DrawParamsOf(clone);
    EXPECT_TRUE(IsBlack(p.ambientColor));
    EXPECT_EQ(p.emissiveColor[0], 0.125f + 0.5f * 0.5f);
    EXPECT_EQ(p.emissiveColor[1], 0.5f * 0.25f);
    EXPECT_EQ(p.emissiveColor[2], 0.5f * 1.0f);
}

// Apply binds the samplers the selected shaders read: BasicEffect's textured variants s0, the
// untextured ones none; DualTextureEffect s0 and s1.
TEST(StockEffectCloneDispatchTest, ApplyBindsTheTexturesTheSelectedShadersSample)
{
    GraphicsDevice device;
    Texture2D first(device, 1, 1, false, SurfaceFormat::Color);
    Texture2D second(device, 1, 1, false, SurfaceFormat::Color);
    Texture2D other(device, 1, 1, false, SurfaceFormat::Color);
    BasicEffect source(device);
    source.setTextureProperty(&first);
    EffectMaterial clone(source);

    device.getTexturesProperty()(0, &other);
    SetShaderIndex(clone, 1);
    clone.Apply();
    EXPECT_EQ(device.getTexturesProperty()[0], &other);
    SetShaderIndex(clone, 5);
    clone.Apply();
    EXPECT_EQ(device.getTexturesProperty()[0], &first);
    // A texture set on the device after Apply is the one drawn, as for a stock effect.
    device.getTexturesProperty()(0, &other);
    EXPECT_EQ(DrawParamsOf(clone).texture0, &other.GetRenderer());

    DualTextureEffect dual(device);
    dual.setTextureProperty(&first);
    dual.setTexture2Property(&second);
    EffectMaterial dualClone(dual);
    dualClone.Apply();
    EXPECT_EQ(device.getTexturesProperty()[0], &first);
    EXPECT_EQ(device.getTexturesProperty()[1], &second);
    const GpuDrawParams p = DrawParamsOf(dualClone);
    EXPECT_EQ(p.texture0, &first.GetRenderer());
    EXPECT_EQ(p.texture1, &second.GetRenderer());
}

// The clone's draw follows its own parameters only: a convenience property set on the source
// after cloning, and applied there, changes nothing the clone draws.
TEST(StockEffectCloneDispatchTest, TheSourcesLaterPropertiesDoNotReachTheClone)
{
    GraphicsDevice device;
    BasicEffect source(device);
    source.setDiffuseColorProperty(Vector3(0.25f, 0.5f, 0.75f));
    source.Apply();
    EffectMaterial clone(source);
    clone.Apply();

    source.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    source.setWorldProperty(Matrix::CreateScale(5.0f));
    source.setTextureEnabledProperty(true);
    source.Apply();
    clone.Apply();

    const GpuDrawParams p = DrawParamsOf(clone);
    EXPECT_EQ(p.diffuseColor[0], 0.25f);
    EXPECT_EQ(p.diffuseColor[2], 0.75f);
    EXPECT_FALSE(p.textureEnabled);
    Matrix world, view, projection;
    ASSERT_TRUE(clone.GetStockCloneDrawMatricesInternal(world, view, projection));
    EXPECT_EQ(projection, Matrix::getIdentityProperty());
}

// Which effects take the route: a base-Effect clone of a material keeps it, the stock class's own
// clone keeps the stock class's path, and an effect never cloned from a stock one has neither.
TEST(StockEffectCloneDispatchTest, OnlyCloneOfAStockEffectThatIsNotItselfStockTakesTheRoute)
{
    GraphicsDevice device;
    BasicEffect source(device);
    source.setWorldProperty(Matrix::CreateScale(2.0f));
    EffectMaterial material(source);
    std::unique_ptr<Effect> ofMaterial(material.Clone());
    std::unique_ptr<Effect> stockClone(source.Clone());
    Effect plain(device);

    EXPECT_TRUE(material.IsStockEffectEXT());
    EXPECT_TRUE(ofMaterial->IsStockEffectEXT());
    EXPECT_TRUE(stockClone->IsStockEffectEXT());
    EXPECT_FALSE(plain.IsStockEffectEXT());

    ofMaterial->Apply();
    EXPECT_TRUE(DrawParamsOf(*ofMaterial).rawStockTransforms);
    stockClone->Apply();
    EXPECT_FALSE(DrawParamsOf(*stockClone).rawStockTransforms);
    EXPECT_FALSE(DrawParamsOf(source).rawStockTransforms);
    plain.Apply();
    EXPECT_FALSE(DrawParamsOf(plain).rawStockTransforms);

    Matrix world, view, projection;
    // The stock class's own clone draws with its own World, View and Projection as its Apply
    // latched them (KI-GFX-STOCK-LATCH), not with a raw WorldViewProj.
    ASSERT_TRUE(stockClone->GetStockCloneDrawMatricesInternal(world, view, projection));
    EXPECT_EQ(world, Matrix::CreateScale(2.0f));
    EXPECT_EQ(projection, Matrix::getIdentityProperty());
    EXPECT_FALSE(plain.GetStockCloneDrawMatricesInternal(world, view, projection));
}

// The constants of the stock .fx files have no initializers, so XNA's embedded effect starts with
// every one of them zero: a parameter no constructor writes reads zero until the first OnApply
// writes it -- a zero matrix, not identity, and a Vector4 (0, 0, 0, 0).
TEST(StockEffectCloneDispatchTest, UnwrittenStockParametersStartAtZero)
{
    GraphicsDevice device;
    BasicEffect basic(device);
    AlphaTestEffect alphaTest(device);
    DualTextureEffect dualTexture(device);
    EnvironmentMapEffect environmentMap(device);
    SkinnedEffect skinned(device);
    SpriteEffect sprite(device);
    const Matrix zero;
    for (Effect* effect : std::initializer_list<Effect*>{&basic, &alphaTest, &dualTexture,
                                                         &environmentMap, &skinned})
    {
        SCOPED_TRACE(effect->GetTypeName());
        auto& parameters = effect->getParametersProperty();
        for (const char* name : {"World", "WorldInverseTranspose", "WorldViewProj"})
        {
            if (const EffectParameter* matrix = parameters[name])
            {
                SCOPED_TRACE(name);
                EXPECT_EQ(matrix->GetValueMatrix(), zero);
                EXPECT_EQ(matrix->GetValueSingleArray(16), std::vector<float>(16, 0.0f));
            }
        }
        ASSERT_NE(parameters["WorldViewProj"], nullptr);
        for (const char* name : {"DiffuseColor", "FogVector"})
        {
            SCOPED_TRACE(name);
            ASSERT_NE(parameters[name], nullptr);
            EXPECT_EQ(parameters[name]->GetValueVector4(), Vector4::Zero);
            EXPECT_EQ(parameters[name]->GetValueSingleArray(4), std::vector<float>(4, 0.0f));
        }
        EXPECT_EQ(parameters["ShaderIndex"]->GetValueInt32(), 0);
    }
    EXPECT_EQ(alphaTest.getParametersProperty()["AlphaTest"]->GetValueVector4(), Vector4::Zero);
    EXPECT_EQ(sprite.getParametersProperty()["MatrixTransform"]->GetValueMatrix(), zero);
}

// A clone made before its source's first Apply copies those zeros: the stock vertex shader places
// every vertex with a zero WorldViewProj, so XNA draws nothing through it. After the source's
// Apply the clone copies the WorldViewProj that Apply derived.
TEST(StockEffectCloneDispatchTest, ACloneMadeBeforeTheSourcesFirstApplyHasAZeroWorldViewProj)
{
    GraphicsDevice device;
    BasicEffect source(device);
    source.setWorldProperty(Matrix::CreateScale(0.5f));
    source.setViewProperty(Matrix::CreateTranslation(0.0f, 0.0f, -2.0f));
    EffectMaterial early(source);
    early.Apply();
    Matrix world, view, projection;
    ASSERT_TRUE(early.GetStockCloneDrawMatricesInternal(world, view, projection));
    EXPECT_EQ(projection, Matrix());
    const GpuDrawParams p = DrawParamsOf(early);
    EXPECT_EQ(p.diffuseColor[0], 0.0f);
    EXPECT_EQ(p.diffuseColor[3], 0.0f);

    source.Apply();
    EffectMaterial late(source);
    late.Apply();
    ASSERT_TRUE(late.GetStockCloneDrawMatricesInternal(world, view, projection));
    EXPECT_EQ(projection, Matrix::CreateScale(0.5f) * Matrix::CreateTranslation(0.0f, 0.0f, -2.0f));
    EXPECT_EQ(DrawParamsOf(late).diffuseColor[3], 1.0f);

    // Before any Apply of the clone itself, the draw matrices read the current raw value.
    BasicEffect fresh(device);
    EffectMaterial unappliedEarly(fresh);
    ASSERT_TRUE(unappliedEarly.GetStockCloneDrawMatricesInternal(world, view, projection));
    EXPECT_EQ(projection, Matrix());
}

// FNA's SpriteEffect (adopted by CNA; XNA 4.0 has none): an EffectMaterial or base-Effect clone of
// it runs the cloned sprite shaders with its own (empty) OnApply, so nothing recomputes
// MatrixTransform from the viewport. SpriteEffect.fx places a vertex with that raw matrix and
// draws the s0 texel times the vertex colour. SpriteEffect's own Clone is a SpriteEffect, which
// keeps deriving its matrix.
TEST(StockEffectCloneDispatchTest, ASpriteEffectCloneRunsTheSpriteShaderWithItsRawMatrixTransform)
{
    GraphicsDevice device;
    SpriteEffect source(device);
    source.Apply();
    EffectMaterial clone(source);
    std::unique_ptr<Effect> ofMaterial(clone.Clone());
    std::unique_ptr<Effect> spriteClone(source.Clone());
    EXPECT_FALSE(source.IsStockEffectEXT());
    EXPECT_TRUE(clone.IsStockEffectEXT());
    EXPECT_TRUE(ofMaterial->IsStockEffectEXT());
    EXPECT_FALSE(spriteClone->IsStockEffectEXT());

    const Matrix placement = Matrix::CreateScale(0.25f, 0.5f, 1.0f) * Matrix::CreateTranslation(-0.5f, 0.25f, 0.0f);
    clone.getParametersProperty()["MatrixTransform"]->SetValue(placement);
    clone.Apply();
    Matrix world, view, projection;
    ASSERT_TRUE(clone.GetStockCloneDrawMatricesInternal(world, view, projection));
    EXPECT_EQ(world, Matrix::getIdentityProperty());
    EXPECT_EQ(view, Matrix::getIdentityProperty());
    EXPECT_EQ(projection, placement);
    EXPECT_EQ(clone.getParametersProperty()["MatrixTransform"]->GetValueMatrix(), placement)
        << "the clone's Apply does not derive the matrix from the viewport";

    const GpuDrawParams p = DrawParamsOf(clone);
    EXPECT_TRUE(p.rawStockTransforms);
    EXPECT_TRUE(p.textureEnabled);
    EXPECT_TRUE(p.vertexColorEnabled);
    EXPECT_FALSE(p.lightingEnabled);
    EXPECT_FALSE(p.fogEnabled);
    EXPECT_FALSE(p.alphaTestEffect || p.dualTexture || p.envMapping || p.skinned || p.pbr);
    for (const float component : p.diffuseColor)
        EXPECT_EQ(component, 1.0f);
    for (const float component : p.fogVector)
        EXPECT_EQ(component, 0.0f);

    // The draw uses what the last Apply read.
    clone.getParametersProperty()["MatrixTransform"]->SetValue(Matrix::CreateScale(3.0f));
    ASSERT_TRUE(clone.GetStockCloneDrawMatricesInternal(world, view, projection));
    EXPECT_EQ(projection, placement);
    clone.Apply();
    ASSERT_TRUE(clone.GetStockCloneDrawMatricesInternal(world, view, projection));
    EXPECT_EQ(projection, Matrix::CreateScale(3.0f));

    EXPECT_FALSE(spriteClone->GetStockCloneDrawMatricesInternal(world, view, projection));
    EXPECT_FALSE(source.GetStockCloneDrawMatricesInternal(world, view, projection));

    // A clone of a SpriteEffect that was never applied starts from its zero MatrixTransform.
    SpriteEffect fresh(device);
    EffectMaterial early(fresh);
    early.Apply();
    ASSERT_TRUE(early.GetStockCloneDrawMatricesInternal(world, view, projection));
    EXPECT_EQ(projection, Matrix());
}

// XNA's stock effects are compiled effects: OnApply writes their derived parameters and the native
// pass latches them when it begins (EffectPass.Apply IL_003b, IL_009f). A property set after Apply
// reaches the draws only through the next Apply; the samplers still read the device's slots at the
// draw (KI-GFX-STOCK-LATCH).
TEST(StockEffectCloneDispatchTest, AStockEffectsDrawUsesWhatItsLastApplyLatched)
{
    GraphicsDevice device;
    BasicEffect effect(device);
    Texture2D first(device, 1, 1, false, SurfaceFormat::Color);
    Texture2D second(device, 1, 1, false, SurfaceFormat::Color);
    effect.setTextureEnabledProperty(true);
    effect.setTextureProperty(&first);
    effect.setDiffuseColorProperty(Vector3(0.25f, 0.5f, 0.75f));
    effect.setWorldProperty(Matrix::CreateScale(2.0f));
    Matrix world, view, projection;
    EXPECT_FALSE(effect.GetStockCloneDrawMatricesInternal(world, view, projection))
        << "before its first Apply a stock effect has nothing latched";
    effect.Apply();

    effect.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
    effect.setWorldProperty(Matrix::CreateScale(5.0f));
    effect.setTextureEnabledProperty(false);
    effect.setVertexColorEnabledProperty(true);
    GpuDrawParams p = DrawParamsOf(effect);
    EXPECT_EQ(p.diffuseColor[0], 0.25f);
    EXPECT_EQ(p.diffuseColor[2], 0.75f);
    EXPECT_TRUE(p.textureEnabled);
    EXPECT_FALSE(p.vertexColorEnabled);
    ASSERT_TRUE(effect.GetStockCloneDrawMatricesInternal(world, view, projection));
    EXPECT_EQ(world, Matrix::CreateScale(2.0f));
    EXPECT_EQ(p.texture0, &first.GetRenderer());

    // The sampler reads the slot the draw finds: a texture set on the device after Apply is drawn.
    device.getTexturesProperty()(0, &second);
    EXPECT_EQ(DrawParamsOf(effect).texture0, &second.GetRenderer());

    effect.Apply();
    p = DrawParamsOf(effect);
    EXPECT_EQ(p.diffuseColor[0], 1.0f);
    EXPECT_EQ(p.diffuseColor[1], 0.0f);
    EXPECT_FALSE(p.textureEnabled);
    EXPECT_TRUE(p.vertexColorEnabled);
    ASSERT_TRUE(effect.GetStockCloneDrawMatricesInternal(world, view, projection));
    EXPECT_EQ(world, Matrix::CreateScale(5.0f));
}

// The same latch for the other four stock effects' own state.
TEST(StockEffectCloneDispatchTest, EveryStockEffectLatchesItsDrawAtApply)
{
    GraphicsDevice device;
    AlphaTestEffect alphaTest(device);
    DualTextureEffect dualTexture(device);
    EnvironmentMapEffect environmentMap(device);
    SkinnedEffect skinned(device);
    alphaTest.setReferenceAlphaProperty(32);
    dualTexture.setAlphaProperty(0.5f);
    environmentMap.setEnvironmentMapAmountProperty(0.25f);
    skinned.setWeightsPerVertexProperty(2);
    for (Effect* effect : std::initializer_list<Effect*>{&alphaTest, &dualTexture, &environmentMap, &skinned})
    {
        effect->Apply();
        dynamic_cast<IEffectMatrices&>(*effect).setWorldProperty(Matrix::CreateScale(3.0f));
    }
    alphaTest.setReferenceAlphaProperty(200);
    dualTexture.setAlphaProperty(1.0f);
    environmentMap.setEnvironmentMapAmountProperty(1.0f);
    skinned.setWeightsPerVertexProperty(4);

    AlphaTestEffect at32(device);
    at32.setReferenceAlphaProperty(32);
    at32.Apply();
    AlphaTestEffect at200(device);
    at200.setReferenceAlphaProperty(200);
    at200.Apply();
    ASSERT_NE(DrawParamsOf(at32).alphaTest[0], DrawParamsOf(at200).alphaTest[0]);
    EXPECT_EQ(DrawParamsOf(alphaTest).alphaTest[0], DrawParamsOf(at32).alphaTest[0]);
    alphaTest.Apply();
    EXPECT_EQ(DrawParamsOf(alphaTest).alphaTest[0], DrawParamsOf(at200).alphaTest[0]);
    EXPECT_EQ(DrawParamsOf(dualTexture).diffuseColor[3], 0.5f);
    EXPECT_EQ(DrawParamsOf(environmentMap).envMapAmount, 0.25f);
    EXPECT_EQ(DrawParamsOf(skinned).weightsPerVertex, 2);
    for (Effect* effect : std::initializer_list<Effect*>{&dualTexture, &environmentMap, &skinned})
    {
        Matrix world, view, projection;
        ASSERT_TRUE(effect->GetStockCloneDrawMatricesInternal(world, view, projection));
        EXPECT_EQ(world, Matrix::getIdentityProperty());
        effect->Apply();
        ASSERT_TRUE(effect->GetStockCloneDrawMatricesInternal(world, view, projection));
        EXPECT_EQ(world, Matrix::CreateScale(3.0f));
    }
    EXPECT_EQ(DrawParamsOf(dualTexture).diffuseColor[3], 1.0f);
    EXPECT_EQ(DrawParamsOf(environmentMap).envMapAmount, 1.0f);
    EXPECT_EQ(DrawParamsOf(skinned).weightsPerVertex, 4);
}

// Genuine XNA (Microsoft's d3dx9_41 under Wine; tests/reference/xna40/stock-clone-shader-index)
// fails the pass for a ShaderIndex outside the stock tables, and EffectPass.Apply IL_0112-011c
// throws GetExceptionFromResult's InvalidOperationException(FrameworkResources.UnexpectedError)
// (KI-GFX-SHADERINDEX).
TEST(StockEffectCloneDispatchTest, AnOutOfTableShaderIndexThrowsXnasUnexpectedError)
{
    GraphicsDevice device;
    DualTextureEffect dual(device);
    BasicEffect basic(device);
    EffectMaterial dualClone(dual);
    EffectMaterial basicClone(basic);
    const auto expectRefusal = [](Effect& clone, int index)
    {
        SCOPED_TRACE(index);
        SetShaderIndex(clone, index);
        try
        {
            clone.Apply();
            ADD_FAILURE() << "Expected InvalidOperationException";
        }
        catch (const System::InvalidOperationException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::InvalidOperationException));
            EXPECT_EQ(error.getMessageProperty(), "An unexpected error has occurred.");
        }
    };
    for (const int index : {4, 5, 8, 1000, -2, -4, std::numeric_limits<int>::max()})
        expectRefusal(dualClone, index);
    for (const int index : {32, 33, 1000, -2})
        expectRefusal(basicClone, index);
    SetShaderIndex(dualClone, 3);
    EXPECT_NO_THROW(dualClone.Apply());
}
