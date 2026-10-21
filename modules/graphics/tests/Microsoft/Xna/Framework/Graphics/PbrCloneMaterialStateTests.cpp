// SPDX-License-Identifier: MS-PL
// ISSUE-070: public clone material state and independent lifetime.
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"

namespace
{
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::GpuDrawParams;

template<class EffectType>
void ConfigureMaterial(EffectType& effect, AlphaModeEXT mode, float cutoff)
{
    effect.VertexColorEnabledEXT = true;
    effect.setAlphaModeEXTProperty(mode);
    effect.setAlphaCutoffEXTProperty(cutoff);
    effect.setDoubleSidedEXTProperty(true);
    effect.setEncodeOutputToSrgbEXTProperty(false);
    effect.setMetallicFactorProperty(0.25f);
}

template<class EffectType>
void ExpectMaterial(const EffectType& effect, AlphaModeEXT mode, float cutoff)
{
    EXPECT_TRUE(effect.VertexColorEnabledEXT);
    EXPECT_EQ(effect.getAlphaModeEXTProperty(), mode);
    EXPECT_EQ(effect.getAlphaCutoffEXTProperty(), cutoff);
    EXPECT_TRUE(effect.getDoubleSidedEXTProperty());
    EXPECT_FALSE(effect.getEncodeOutputToSrgbEXTProperty());
    EXPECT_EQ(effect.getMetallicFactorProperty(), 0.25f);
}

template<class EffectType>
class PbrCloneMaterialStateTest : public testing::Test
{
protected:
    GraphicsDevice device;
    void SetUp() override
    {
        this->RecordProperty("renderer", std::string(device.GetGraphicsRendererName()));
    }
};
using PbrFamilies = testing::Types<PbrEffect, SkinnedPbrEffect>;
TYPED_TEST_SUITE(PbrCloneMaterialStateTest, PbrFamilies);

TYPED_TEST(PbrCloneMaterialStateTest, AuthoredStateAndDrawParametersSurviveSourceDestruction)
{
    auto source = std::make_unique<TypeParam>(this->device);
    ConfigureMaterial(*source, AlphaModeEXT::Mask, 0.125f);
    GpuDrawParams expected;
    source->FillGpuDrawParams(expected);
    std::unique_ptr<Effect> owner(source->Clone());
    auto* clone = dynamic_cast<TypeParam*>(owner.get());
    ASSERT_NE(clone, nullptr);
    source.reset();
    ExpectMaterial(*clone, AlphaModeEXT::Mask, 0.125f);
    GpuDrawParams actual;
    clone->FillGpuDrawParams(actual);
    EXPECT_EQ(actual.vertexColorEnabled, expected.vertexColorEnabled);
    EXPECT_EQ(actual.pbrDoubleSided, expected.pbrDoubleSided);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(actual.alphaTest[i], expected.alphaTest[i]);
}

TYPED_TEST(PbrCloneMaterialStateTest, EveryAlphaModeKeepsEvenItsDormantCutoff)
{
    TypeParam source(this->device);
    for (const auto mode : {AlphaModeEXT::Opaque, AlphaModeEXT::Mask, AlphaModeEXT::Blend})
    {
        SCOPED_TRACE(static_cast<int>(mode));
        for (const float cutoff : {0.125f, 0.75f})
        {
            SCOPED_TRACE(cutoff);
            ConfigureMaterial(source, mode, cutoff);
            std::unique_ptr<Effect> owner(source.Clone());
            auto* clone = dynamic_cast<TypeParam*>(owner.get());
            ASSERT_NE(clone, nullptr);
            ExpectMaterial(*clone, mode, cutoff);
        }
    }
}

TYPED_TEST(PbrCloneMaterialStateTest, SourceAndCloneMutateIndependently)
{
    TypeParam source(this->device);
    ConfigureMaterial(source, AlphaModeEXT::Blend, 0.125f);
    std::unique_ptr<Effect> owner(source.Clone());
    auto* clone = dynamic_cast<TypeParam*>(owner.get());
    ASSERT_NE(clone, nullptr);
    source.VertexColorEnabledEXT = false;
    source.setAlphaModeEXTProperty(AlphaModeEXT::Opaque);
    source.setAlphaCutoffEXTProperty(0.875f);
    source.setDoubleSidedEXTProperty(false);
    ExpectMaterial(*clone, AlphaModeEXT::Blend, 0.125f);
    clone->setAlphaModeEXTProperty(AlphaModeEXT::Mask);
    clone->setAlphaCutoffEXTProperty(0.25f);
    EXPECT_FALSE(source.VertexColorEnabledEXT);
    EXPECT_EQ(source.getAlphaModeEXTProperty(), AlphaModeEXT::Opaque);
    EXPECT_EQ(source.getAlphaCutoffEXTProperty(), 0.875f);
    EXPECT_FALSE(source.getDoubleSidedEXTProperty());
}

TYPED_TEST(PbrCloneMaterialStateTest, DefaultMaterialRemainsDefault)
{
    TypeParam source(this->device);
    std::unique_ptr<Effect> owner(source.Clone());
    auto* clone = dynamic_cast<TypeParam*>(owner.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_FALSE(clone->VertexColorEnabledEXT);
    EXPECT_EQ(clone->getAlphaModeEXTProperty(), AlphaModeEXT::Opaque);
    EXPECT_EQ(clone->getAlphaCutoffEXTProperty(), 0.5f);
    EXPECT_FALSE(clone->getDoubleSidedEXTProperty());
    EXPECT_EQ(clone->getEncodeOutputToSrgbEXTProperty(), source.getEncodeOutputToSrgbEXTProperty());
}
} // namespace
