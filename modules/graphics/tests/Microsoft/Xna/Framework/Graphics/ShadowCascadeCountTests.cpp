// SPDX-License-Identifier: MS-PL
// ISSUE-102: safe setter-boundary reproduction; never draw an invalid count.
#include <gtest/gtest.h>
#include <limits>
#include <string>
#include <typeinfo>
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IShadowReceiverEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

namespace {
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

ShadowCascadeStateEXT DistinctState(int count)
{
    ShadowCascadeStateEXT state;
    state.Count = count;
    for (int i = 0; i < ShadowCascadeStateEXT::kMaxCascades; ++i)
    {
        state.WorldToAtlas[i] = Matrix::CreateTranslation(float(i + 1), 2.0f, 3.0f);
        state.SplitDistance[i] = float(10 * (i + 1));
    }
    state.CameraView = Matrix::CreateTranslation(4.0f, 5.0f, 6.0f);
    state.BlendBand = 2.5f;
    state.DebugTint = true;
    return state;
}

void ExpectSameState(const ShadowCascadeStateEXT& actual, const ShadowCascadeStateEXT& expected)
{
    EXPECT_EQ(actual.Count, expected.Count);
    for (int i = 0; i < ShadowCascadeStateEXT::kMaxCascades; ++i)
    {
        EXPECT_EQ(actual.WorldToAtlas[i], expected.WorldToAtlas[i]);
        EXPECT_EQ(actual.SplitDistance[i], expected.SplitDistance[i]);
    }
    EXPECT_EQ(actual.CameraView, expected.CameraView);
    EXPECT_EQ(actual.BlendBand, expected.BlendBand);
    EXPECT_EQ(actual.DebugTint, expected.DebugTint);
}

template<class EffectType>
class ShadowCascadeCountTest : public testing::Test
{
protected:
    GraphicsDevice device;
    EffectType effect{device};

    void SetUp() override
    {
        this->RecordProperty("renderer", std::string(device.GetGraphicsRendererName()));
    }

    void RejectWithoutMutation(int invalidCount)
    {
        SCOPED_TRACE(invalidCount);
        const auto valid = DistinctState(ShadowCascadeStateEXT::kMaxCascades);
        effect.setShadowCascadesEXT(valid);
        auto invalid = DistinctState(invalidCount);
        invalid.WorldToAtlas[0] = Matrix::CreateScale(9.0f);
        invalid.SplitDistance[0] = 123.0f;
        invalid.CameraView = Matrix::CreateScale(8.0f);
        invalid.BlendBand = 9.0f;
        invalid.DebugTint = false;
        IShadowReceiverEXT& receiver = effect;
        bool exactRefusal = false;
        try { receiver.setShadowCascadesEXT(invalid); }
        catch (const System::ArgumentOutOfRangeException& error)
        { exactRefusal = typeid(error) == typeid(System::ArgumentOutOfRangeException); }
        catch (...) { }
        EXPECT_TRUE(exactRefusal);
        ExpectSameState(receiver.getShadowCascadesEXT(), valid);
        // Do NOT call FillGpuDrawParams on a count the setter incorrectly accepted.
    }
};
using EffectFamilies = testing::Types<BasicEffect, SkinnedEffect, PbrEffect, SkinnedPbrEffect>;
TYPED_TEST_SUITE(ShadowCascadeCountTest, EffectFamilies);

TYPED_TEST(ShadowCascadeCountTest, EverySupportedCountRoundTripsWithoutClamping)
{
    for (int count = 0; count <= ShadowCascadeStateEXT::kMaxCascades; ++count)
    {
        SCOPED_TRACE(count);
        const auto state = DistinctState(count);
        ASSERT_NO_THROW(this->effect.setShadowCascadesEXT(state));
        ExpectSameState(this->effect.getShadowCascadesEXT(), state);
    }
}

TYPED_TEST(ShadowCascadeCountTest, NegativeCountsAreRefusedBeforeMutation)
{
    this->RejectWithoutMutation(-1);
    this->RejectWithoutMutation(std::numeric_limits<int>::min());
}

TYPED_TEST(ShadowCascadeCountTest, OversizedCountsAreRefusedBeforeMutation)
{
    this->RejectWithoutMutation(ShadowCascadeStateEXT::kMaxCascades + 1);
    this->RejectWithoutMutation(std::numeric_limits<int>::max());
}

TYPED_TEST(ShadowCascadeCountTest, OwnValidStateCanBeAssignedBackThroughTheInterface)
{
    const auto state = DistinctState(ShadowCascadeStateEXT::kMaxCascades);
    this->effect.setShadowCascadesEXT(state);
    IShadowReceiverEXT& receiver = this->effect;
    ASSERT_NO_THROW(receiver.setShadowCascadesEXT(receiver.getShadowCascadesEXT()));
    ExpectSameState(receiver.getShadowCascadesEXT(), state);
}

TYPED_TEST(ShadowCascadeCountTest, MaximumCountReachesAllFourDrawSlotsAndZeroDisablesCascades)
{
    // The public receiver accepts a texture; no framebuffer capability is needed
    // to test its common CPU draw-parameter copy.
    Texture2D atlas(this->device, 8, 8);
    this->effect.setShadowMapEXT(&atlas);
    this->effect.setShadowsEnabledEXT(true);
    auto state = DistinctState(ShadowCascadeStateEXT::kMaxCascades);
    this->effect.setShadowCascadesEXT(state);
    CNA::Internal::Renderers::GpuDrawParams params;
    this->effect.FillGpuDrawParams(params);
    ASSERT_TRUE(params.shadowsEnabled);
    ASSERT_EQ(params.cascadeCount, ShadowCascadeStateEXT::kMaxCascades);
    for (int i = 0; i < ShadowCascadeStateEXT::kMaxCascades; ++i)
    {
        EXPECT_EQ(params.cascadeMatricesColMajor[i * 16 + 12], float(i + 1));
        EXPECT_EQ(params.cascadeSplits[i], state.SplitDistance[i]);
    }
    state.Count = 0;
    this->effect.setShadowCascadesEXT(state);
    CNA::Internal::Renderers::GpuDrawParams disabled;
    this->effect.FillGpuDrawParams(disabled);
    EXPECT_TRUE(disabled.shadowsEnabled);
    EXPECT_EQ(disabled.cascadeCount, 0);
}
} // namespace
