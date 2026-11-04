// SPDX-License-Identifier: MS-PL
//
// XNA-MISSING-005: the documented protected clone constructors of the five stock effects.
//
// Microsoft declares `protected AlphaTestEffect(AlphaTestEffect cloneSource)` and the equivalent on
// BasicEffect, DualTextureEffect, EnvironmentMapEffect and SkinnedEffect, and `Clone()` is what
// calls it. CNA already had each one and implemented it correctly, but declared it private, so it
// was not part of the API a subclass could reach. The cases below exercise the protected
// accessibility through a small derived type -- which is the only way to reach a protected
// constructor at all -- and the independence and resource-sharing the clone has to provide.

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechniqueCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    /// Reaching a protected constructor requires a subclass, which is exactly the accessibility
    /// this package changed: these would not compile against the previous private declarations.
    template <typename TEffect>
    class CloneProbe final : public TEffect
    {
    public:
        explicit CloneProbe(GraphicsDevice& device) : TEffect(device) {}
        explicit CloneProbe(const TEffect& cloneSource) : TEffect(cloneSource) {}
    };

    // A copy reached through the protected constructor is a distinct effect of the same type with
    // the same parameter graph (XNA's clone constructors all start with Effect::.ctor(Effect)).
    template <typename TEffect>
    void ExpectASeparateCopy(const CloneProbe<TEffect>& source)
    {
        const CloneProbe<TEffect> copy(source);
        EXPECT_NE(static_cast<const Effect*>(&copy), static_cast<const Effect*>(&source));
        EXPECT_EQ(copy.GetTypeName(), source.GetTypeName());
        EXPECT_EQ(copy.getParametersProperty().getCountProperty(),
                  source.getParametersProperty().getCountProperty());
        EXPECT_FALSE(copy.getIsDisposedProperty());
    }

    // Scenario matrices with exactly representable products (hand-derived in TQ-041 DESIGN.md):
    // WorldViewProj = World * View * Projection, View a pure translation by (0, 0, -10).
    Matrix ScenarioWorld()
    {
        return Matrix(2, 0, 0.5f, 0,
                      0, 2, 0.25f, 0,
                      0, 0, 2, 0,
                      1, 2, 3, 1);
    }
    Matrix ScenarioView() { return Matrix::CreateTranslation(0.0f, 0.0f, -10.0f); }
    Matrix ScenarioProjection()
    {
        return Matrix(1, 0, 0, 0,
                      0, 2, 0, 0,
                      0, 0, 0.5f, 0,
                      0, 0, 0, 1);
    }
    Matrix ScenarioWorldViewProj()
    {
        return Matrix(2, 0, 0.25f, 0,
                      0, 4, 0.125f, 0,
                      0, 0, 1, 0,
                      1, 4, -3.5f, 1);
    }

    // ShaderIndex a freshly constructed stock effect selects, from each type's OnApply:
    // fog off (+1) everywhere; SkinnedEffect four weights (+4) and one light (+6) (OnApply
    // IL_0108-014c); EnvironmentMapEffect Fresnel on (+2) and one light (+8) (OnApply IL_0108-0137).
    template <typename TEffect> int DefaultShaderIndex() { return 1; }
    template <> int DefaultShaderIndex<SkinnedEffect>() { return 11; }
    template <> int DefaultShaderIndex<EnvironmentMapEffect>() { return 11; }

    template <typename TEffect>
    class StockEffectCloneTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;
    };

    using StockEffectTypes = ::testing::Types<AlphaTestEffect, BasicEffect, DualTextureEffect,
                                              EnvironmentMapEffect, SkinnedEffect>;
}

TYPED_TEST_SUITE(StockEffectCloneTest, StockEffectTypes);

TEST(StockEffectCloneConstructorTest, EveryStockEffectExposesTheCloneConstructorToASubclass)
{
    GraphicsDevice device;

    // One instantiation per type: if any of the five were still private, this would not compile.
    const CloneProbe<AlphaTestEffect> alphaTest(device);
    const CloneProbe<BasicEffect> basic(device);
    const CloneProbe<DualTextureEffect> dualTexture(device);
    const CloneProbe<EnvironmentMapEffect> environmentMap(device);
    const CloneProbe<SkinnedEffect> skinned(device);

    ExpectASeparateCopy(alphaTest);
    ExpectASeparateCopy(basic);
    ExpectASeparateCopy(dualTexture);
    ExpectASeparateCopy(environmentMap);
    ExpectASeparateCopy(skinned);
}

TEST(StockEffectCloneConstructorTest, BasicEffectCloneCarriesStateAndIsIndependent)
{
    GraphicsDevice device;
    BasicEffect source(device);
    source.setAlphaProperty(0.25f);
    source.setDiffuseColorProperty(Vector3(0.5f, 0.25f, 0.125f));
    source.setFogEnabledProperty(true);
    source.setFogStartProperty(3.0f);
    source.setFogEndProperty(9.0f);
    source.setLightingEnabledProperty(true);
    source.setWorldProperty(Matrix::CreateScale(2.0f));

    const CloneProbe<BasicEffect> clone(source);
    EXPECT_FLOAT_EQ(clone.getAlphaProperty(), 0.25f);
    EXPECT_FLOAT_EQ(clone.getDiffuseColorProperty().X, 0.5f);
    EXPECT_TRUE(clone.getFogEnabledProperty());
    EXPECT_FLOAT_EQ(clone.getFogStartProperty(), 3.0f);
    EXPECT_FLOAT_EQ(clone.getFogEndProperty(), 9.0f);
    EXPECT_TRUE(clone.getLightingEnabledProperty());

    // Mutating the source afterwards must not reach the clone, which is the whole point of a clone
    // rather than a reference.
    source.setAlphaProperty(1.0f);
    source.setFogEnabledProperty(false);
    EXPECT_FLOAT_EQ(clone.getAlphaProperty(), 0.25f);
    EXPECT_TRUE(clone.getFogEnabledProperty());
}

TEST(StockEffectCloneConstructorTest, AlphaTestEffectCloneCarriesItsOwnSettings)
{
    GraphicsDevice device;
    AlphaTestEffect source(device);
    source.setAlphaFunctionProperty(CompareFunction::Greater);
    source.setReferenceAlphaProperty(128);
    source.setVertexColorEnabledProperty(true);

    const CloneProbe<AlphaTestEffect> clone(source);
    EXPECT_EQ(clone.getAlphaFunctionProperty(), CompareFunction::Greater);
    EXPECT_EQ(clone.getReferenceAlphaProperty(), 128);
    EXPECT_TRUE(clone.getVertexColorEnabledProperty());

    source.setReferenceAlphaProperty(0);
    EXPECT_EQ(clone.getReferenceAlphaProperty(), 128);
}

TEST(StockEffectCloneConstructorTest, ACloneSharesTheSourcesTextureReference)
{
    // XNA's clone points at the same texture object; it does not copy the image or the renderer
    // handle. Sharing the reference is the documented ownership.
    GraphicsDevice device;
    Texture2D texture(device, 4, 4);
    AlphaTestEffect source(device);
    source.setTextureProperty(&texture);

    const CloneProbe<AlphaTestEffect> clone(source);
    EXPECT_EQ(clone.getTextureProperty(), &texture);
    EXPECT_EQ(clone.getTextureProperty(), source.getTextureProperty());
}

TEST(StockEffectCloneConstructorTest, CloneUsesTheSameConstructorAndSurvivesSourceDisposal)
{
    GraphicsDevice device;
    auto source = std::make_unique<BasicEffect>(device);
    source->setAlphaProperty(0.5f);

    // Clone() is the public route to the same constructor; both must produce the same state.
    Effect* viaClone = source->Clone();
    ASSERT_NE(viaClone, nullptr);
    auto* clonedBasic = dynamic_cast<BasicEffect*>(viaClone);
    ASSERT_NE(clonedBasic, nullptr);
    EXPECT_FLOAT_EQ(clonedBasic->getAlphaProperty(), 0.5f);

    const CloneProbe<BasicEffect> viaConstructor(*source);
    EXPECT_FLOAT_EQ(viaConstructor.getAlphaProperty(), clonedBasic->getAlphaProperty());

    // Disposing the source leaves both clones usable: they are separate effects.
    source->Dispose();
    source.reset();
    EXPECT_FLOAT_EQ(clonedBasic->getAlphaProperty(), 0.5f);
    EXPECT_FLOAT_EQ(viaConstructor.getAlphaProperty(), 0.5f);
    EXPECT_FALSE(clonedBasic->getIsDisposedProperty());

    delete viaClone;
}

TEST(StockEffectCloneConstructorTest, CloneOfADisposedEffectIsRefused)
{
    GraphicsDevice device;
    BasicEffect source(device);
    source.Dispose();
    EXPECT_THROW((void)source.Clone(), System::ObjectDisposedException);
}

// XNA's stock clone constructors start with Effect::.ctor(Effect) (BasicEffect IL_0061,
// SkinnedEffect IL_0068, AlphaTestEffect IL_0052, DualTextureEffect IL_004b, EnvironmentMapEffect
// IL_0061), which clones the native effect -- every parameter with its current value
// (Effect::.ctor(Effect) IL_0048-0085). A value written straight into Parameters therefore reaches
// both Clone() and the protected constructor.
TYPED_TEST(StockEffectCloneTest, ARawParameterWriteReachesBothCloneRoutes)
{
    TypeParam source(this->device);
    const Vector4 raw(2.0f, 3.0f, 5.0f, 7.0f);
    source.getParametersProperty()["DiffuseColor"]->SetValue(raw);

    std::unique_ptr<Effect> viaClone(source.Clone());
    const CloneProbe<TypeParam> viaConstructor(source);

    EXPECT_EQ(viaClone->getParametersProperty()["DiffuseColor"]->GetValueVector4(), raw);
    EXPECT_EQ(viaConstructor.getParametersProperty()["DiffuseColor"]->GetValueVector4(), raw);
}

// The same native clone carries what the source's last Apply wrote, so the clone's WorldViewProj
// is World * View * Projection before the clone is ever applied.
TYPED_TEST(StockEffectCloneTest, TheCloneHoldsWhatTheSourcesLastApplyWrote)
{
    TypeParam source(this->device);
    IEffectMatrices& matrices = source;
    matrices.setWorldProperty(ScenarioWorld());
    matrices.setViewProperty(ScenarioView());
    matrices.setProjectionProperty(ScenarioProjection());
    source.Apply();
    ASSERT_EQ(source.getParametersProperty()["WorldViewProj"]->GetValueMatrix(),
              ScenarioWorldViewProj());

    const CloneProbe<TypeParam> clone(source);
    EXPECT_EQ(clone.getParametersProperty()["WorldViewProj"]->GetValueMatrix(),
              ScenarioWorldViewProj());
}

// Every stock clone constructor sets dirtyFlags = All before cloning (e.g. BasicEffect
// IL_0058-005a, AlphaTestEffect IL_0049-004b), so the clone's first Apply recomputes the derived
// parameters even though the cloned values look current.
TYPED_TEST(StockEffectCloneTest, TheClonesFirstApplyRecomputesTheDerivedParameters)
{
    TypeParam source(this->device);
    IEffectMatrices& matrices = source;
    matrices.setWorldProperty(ScenarioWorld());
    matrices.setViewProperty(ScenarioView());
    matrices.setProjectionProperty(ScenarioProjection());
    source.Apply();
    const Matrix sentinel(9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9);
    source.getParametersProperty()["WorldViewProj"]->SetValue(sentinel);
    source.getParametersProperty()["ShaderIndex"]->SetValue(99);

    CloneProbe<TypeParam> clone(source);
    ASSERT_EQ(clone.getParametersProperty()["WorldViewProj"]->GetValueMatrix(), sentinel);
    clone.Apply();

    EXPECT_EQ(clone.getParametersProperty()["WorldViewProj"]->GetValueMatrix(),
              ScenarioWorldViewProj());
    EXPECT_EQ(clone.getParametersProperty()["ShaderIndex"]->GetValueInt32(),
              DefaultShaderIndex<TypeParam>());
}

// Effect::.ctor(Effect) IL_0019-0029 calls Helpers.CheckDisposed on the source before anything
// else, so the protected constructor refuses a disposed source exactly as Clone() does.
TYPED_TEST(StockEffectCloneTest, ADisposedSourceIsRefusedByBothCloneRoutes)
{
    TypeParam source(this->device);
    source.Dispose();
    EXPECT_THROW((void)source.Clone(), System::ObjectDisposedException);
    EXPECT_THROW({ CloneProbe<TypeParam> clone(source); }, System::ObjectDisposedException);
}

// The clone's parameters are storage of its own, one for one and in the same order.
TYPED_TEST(StockEffectCloneTest, TheCloneOwnsItsParameterStorage)
{
    TypeParam source(this->device);
    CloneProbe<TypeParam> clone(source);
    auto& sourceParameters = source.getParametersProperty();
    auto& cloneParameters = clone.getParametersProperty();
    ASSERT_EQ(cloneParameters.getCountProperty(), sourceParameters.getCountProperty());
    for (int i = 0; i < cloneParameters.getCountProperty(); ++i)
    {
        EXPECT_NE(cloneParameters[i], sourceParameters[i]);
        EXPECT_EQ(cloneParameters[i]->getNameProperty(), sourceParameters[i]->getNameProperty());
    }

    const Vector4 cloneOnly(0.5f, 0.25f, 0.125f, 1.0f);
    cloneParameters["DiffuseColor"]->SetValue(cloneOnly);
    EXPECT_NE(sourceParameters["DiffuseColor"]->GetValueVector4(), cloneOnly);
}

// The clone selects the source's technique graph (Effect::.ctor(Effect) IL_00be-00cb selects
// Techniques[0]); names and pass names are copied with it.
TYPED_TEST(StockEffectCloneTest, TheCloneHasTheSourcesTechniqueAndPasses)
{
    TypeParam source(this->device);
    const CloneProbe<TypeParam> clone(source);
    const auto& techniques = clone.getTechniquesProperty();
    ASSERT_EQ(techniques.getCountProperty(), source.getTechniquesProperty().getCountProperty());
    ASSERT_NE(clone.getCurrentTechniqueProperty(), nullptr);
    EXPECT_EQ(clone.getCurrentTechniqueProperty(), techniques[0]);
    const EffectTechnique& expected = *source.getTechniquesProperty()[0];
    EXPECT_EQ(techniques[0]->getNameProperty(), expected.getNameProperty());
    ASSERT_EQ(techniques[0]->getPassesProperty().getCountProperty(),
              expected.getPassesProperty().getCountProperty());
    EXPECT_EQ(techniques[0]->getPassesProperty()[0]->getNameProperty(),
              expected.getPassesProperty()[0]->getNameProperty());
}

// SetBoneTransforms writes the Bones array parameter; the native clone copies it, so a bone written
// through a raw element view reaches the clone, and GetBoneTransforms restores M44 = 1
// (SkinnedEffect.GetBoneTransforms IL_004d-006a).
TEST(StockEffectCloneConstructorTest, SkinnedEffectCloneKeepsARawBoneElementWrite)
{
    GraphicsDevice device;
    SkinnedEffect source(device);
    const Matrix bone = Matrix::CreateScale(2.0f, 3.0f, 4.0f) *
                        Matrix::CreateTranslation(5.0f, 6.0f, 7.0f);
    source.getParametersProperty()["Bones"]->getElementsProperty()[3]->SetValue(bone);

    const CloneProbe<SkinnedEffect> clone(source);
    const std::vector<Matrix> bones = clone.GetBoneTransforms(4);
    ASSERT_EQ(bones.size(), 4u);
    EXPECT_EQ(bones[3], bone);
    EXPECT_EQ(bones[2], Matrix::getIdentityProperty());
}
