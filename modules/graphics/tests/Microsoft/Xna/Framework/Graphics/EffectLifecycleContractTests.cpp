// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <utility>
#include "CNA/GraphicsCapability.hpp"
#include "CNA/TestSupport/TestPaths.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidCastException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
namespace CNA::TestSupport
{
    std::unique_ptr<Effect> CreateLifecycleTableEffect(GraphicsDevice& device);
}
namespace
{
    class EffectLifecycleContractTest : public testing::Test
    {
    protected:
        GraphicsDevice device;
        std::unique_ptr<Effect> effect;
        void SetUp() override
        {
            if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
            {
                // HEADLESS cannot load .fxb: exercise the same public object-graph
                // contracts with literal reflection and state-change inputs.
                RecordProperty("effect_fixture", "literal reflected table");
                effect = CNA::TestSupport::CreateLifecycleTableEffect(device);
                return;
            }
            RecordProperty("effect_fixture", "CnaConformanceEffect.fxb");
            std::ifstream input(CNA::TestSupport::CompiledEffectDirectory() /
                                "CnaConformanceEffect.fxb", std::ios::binary);
            ASSERT_TRUE(input.is_open());
            const std::vector<SharpRuntime::bytecs> bytes{
                std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            ASSERT_FALSE(bytes.empty());
            effect = std::make_unique<Effect>(device, bytes);
        }
    };
}

TEST_F(EffectLifecycleContractTest, AnnotationsRemainAttachedToTheirDeclaredOwners)
{
    // CnaConformanceEffect.fx:23..26, 97..100, 109..113 supplies literal declarations.
    // XNA constructors populate their own annotation collections; getters IL_0000..0006.
    auto* gain = effect->getParametersProperty()["Gain"];
    auto* first = effect->getTechniquesProperty()["FirstTechnique"];
    auto* second = effect->getTechniquesProperty()["SecondTechnique"];
    ASSERT_NE(gain, nullptr); ASSERT_NE(first, nullptr); ASSERT_NE(second, nullptr);
    auto* statePass = first->getPassesProperty()["StatePass"];
    ASSERT_NE(statePass, nullptr);
    const auto& ga = std::as_const(*gain).getAnnotationsProperty();
    const auto& ta = std::as_const(*first).getAnnotationsProperty();
    const auto& pa = std::as_const(*statePass).getAnnotationsProperty();
    ASSERT_EQ(ga.getCountProperty(), 1);
    ASSERT_EQ(ta.getCountProperty(), 1);
    ASSERT_EQ(pa.getCountProperty(), 1);
    ASSERT_NE(ga["Visible"], nullptr); ASSERT_NE(ta["Quality"], nullptr);
    ASSERT_NE(pa["PassTag"], nullptr);
    EXPECT_TRUE(ga["Visible"]->GetValueBoolean());
    EXPECT_EQ(ta["Quality"]->GetValueInt32(), 7);
    EXPECT_EQ(pa["PassTag"]->GetValueInt32(), 3);
    EXPECT_EQ(ga["Quality"], nullptr);
    EXPECT_EQ(ta["PassTag"], nullptr);
    EXPECT_EQ(pa["Visible"], nullptr);
    EXPECT_EQ(second->getAnnotationsProperty().getCountProperty(), 0);
    EXPECT_EQ(first->getPassesProperty()[0]->getAnnotationsProperty().getCountProperty(), 0);
    EXPECT_THROW((void)ga[0]->GetValueString(), System::InvalidCastException);
    effect->setCurrentTechniqueProperty(second);
    EXPECT_EQ(pa["PassTag"]->GetValueInt32(), 3);
    EXPECT_EQ(ta["Quality"]->GetValueInt32(), 7);
}

TEST_F(EffectLifecycleContractTest, CloneStartsAtFirstTechniqueAndCopiesNestedValuesIndependently)
{
    // XNA Effect..ctor(Effect) IL_00b8..00cb explicitly selects Techniques[0].
    // IL_0048..007b clones native values; IL_0122..015a additionally copies numeric arrays.
    auto* second = effect->getTechniquesProperty()["SecondTechnique"];
    ASSERT_NE(second, nullptr);
    effect->setCurrentTechniqueProperty(second);
    auto* weights = effect->getParametersProperty()["Weights"];
    auto* lighting = effect->getParametersProperty()["Lighting"];
    ASSERT_NE(weights, nullptr); ASSERT_NE(lighting, nullptr);
    auto* thresholds = lighting->getStructureMembersProperty()["Thresholds"];
    ASSERT_NE(thresholds, nullptr);
    weights->SetValue(std::vector<float>{3,7});
    thresholds->SetValue(std::vector<float>{11,13});
    std::unique_ptr<Effect> clone(effect->Clone());
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->getCurrentTechniqueProperty(), clone->getTechniquesProperty()[0]);
    EXPECT_EQ(clone->getCurrentTechniqueProperty()->getNameProperty(), "FirstTechnique");
    EXPECT_EQ(effect->getCurrentTechniqueProperty(), second);
    auto* cw = clone->getParametersProperty()["Weights"];
    auto* cl = clone->getParametersProperty()["Lighting"];
    ASSERT_NE(cw, nullptr); ASSERT_NE(cl, nullptr);
    auto* ct = cl->getStructureMembersProperty()["Thresholds"];
    ASSERT_NE(ct, nullptr);
    EXPECT_EQ(cw->GetValueSingleArray(2), (std::vector<float>{3,7}));
    EXPECT_EQ(ct->GetValueSingleArray(2), (std::vector<float>{11,13}));
    cw->getElementsProperty()[1]->SetValue(17.0f);
    thresholds->getElementsProperty()[0]->SetValue(19.0f);
    EXPECT_EQ(weights->GetValueSingleArray(2), (std::vector<float>{3,7}));
    EXPECT_EQ(cw->GetValueSingleArray(2), (std::vector<float>{3,17}));
    EXPECT_EQ(thresholds->GetValueSingleArray(2), (std::vector<float>{19,13}));
    EXPECT_EQ(ct->GetValueSingleArray(2), (std::vector<float>{11,13}));
    effect.reset();
    EXPECT_EQ(cw->GetValueSingleArray(2), (std::vector<float>{3,17}));
    EXPECT_EQ(ct->GetValueSingleArray(2), (std::vector<float>{11,13}));
    EXPECT_EQ(clone->getCurrentTechniqueProperty()->getNameProperty(), "FirstTechnique");
}

TEST_F(EffectLifecycleContractTest, RejectedPassLeavesDeviceStateAndTechniqueUnchanged)
{
    auto* first = effect->getTechniquesProperty()["FirstTechnique"];
    auto* second = effect->getTechniquesProperty()["SecondTechnique"];
    ASSERT_NE(first, nullptr); ASSERT_NE(second, nullptr);
    auto* statePass = first->getPassesProperty()["StatePass"];
    ASSERT_NE(statePass, nullptr);
    device.setDepthStencilStateProperty(DepthStencilState::Default);
    device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
    const auto* depth = &device.getDepthStencilStateProperty();
    const auto* rasterizer = &device.getRasterizerStateProperty();
    effect->setCurrentTechniqueProperty(second);
    // EffectPass.Apply IL_0021..0039 refuses before OnApply/native pass/state assignment.
    // Index zero exists in both techniques: a later pass-index bounds check cannot
    // substitute for validating that this pass belongs to CurrentTechnique.
    ASSERT_NE(first->getPassesProperty()[0], nullptr);
    ASSERT_NE(second->getPassesProperty()[0], nullptr);
    EXPECT_THROW(first->getPassesProperty()[0]->Apply(), System::InvalidOperationException);
    EXPECT_THROW(statePass->Apply(), System::InvalidOperationException);
    EXPECT_EQ(&device.getDepthStencilStateProperty(), depth);
    EXPECT_TRUE(device.getDepthStencilStateProperty().getDepthBufferEnableProperty());
    EXPECT_EQ(&device.getRasterizerStateProperty(), rasterizer);
    EXPECT_EQ(device.getRasterizerStateProperty().getCullModeProperty(), CullMode::CullCounterClockwiseFace);
    EXPECT_EQ(effect->getCurrentTechniqueProperty(), second);
    effect->setCurrentTechniqueProperty(first);
    statePass->Apply();
    // CnaConformanceEffect.fx StatePass: ZEnable=false, ZWriteEnable=false, CullMode=None.
    EXPECT_FALSE(device.getDepthStencilStateProperty().getDepthBufferEnableProperty());
    EXPECT_FALSE(device.getDepthStencilStateProperty().getDepthBufferWriteEnableProperty());
    EXPECT_EQ(device.getRasterizerStateProperty().getCullModeProperty(), CullMode::None);
    EXPECT_EQ(effect->getCurrentTechniqueProperty(), first);
    effect->Dispose();
    // Effect.set_CurrentTechnique IL_0000..0025: disposal precedes even null validation.
    EXPECT_THROW(effect->setCurrentTechniqueProperty(nullptr), System::ObjectDisposedException);
    EXPECT_THROW(second->getPassesProperty()[0]->Apply(), System::ObjectDisposedException);
    EXPECT_THROW((void)effect->Clone(), System::ObjectDisposedException);
    EXPECT_EQ(effect->getCurrentTechniqueProperty(), first);
}
