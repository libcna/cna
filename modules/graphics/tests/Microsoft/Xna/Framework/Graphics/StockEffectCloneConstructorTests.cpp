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

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
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
}

TEST(StockEffectCloneConstructorTest, EveryStockEffectExposesTheCloneConstructorToASubclass)
{
    GraphicsDevice device;

    // One instantiation per type: if any of the five were still private, this would not compile.
    const CloneProbe<AlphaTestEffect> alphaTest(device);
    const CloneProbe<BasicEffect> basic(device);
    const CloneProbe<DualTextureEffect> dualTexture(device);
    const CloneProbe<EnvironmentMapEffect> environmentMap(device);
    const CloneProbe<SkinnedEffect> skinned(device);

    EXPECT_NO_THROW({ CloneProbe<AlphaTestEffect> copy(alphaTest); });
    EXPECT_NO_THROW({ CloneProbe<BasicEffect> copy(basic); });
    EXPECT_NO_THROW({ CloneProbe<DualTextureEffect> copy(dualTexture); });
    EXPECT_NO_THROW({ CloneProbe<EnvironmentMapEffect> copy(environmentMap); });
    EXPECT_NO_THROW({ CloneProbe<SkinnedEffect> copy(skinned); });
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
    EXPECT_ANY_THROW((void)source.Clone());
}
