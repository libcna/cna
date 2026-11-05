// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <cmath>
#include <limits>

using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Audio::AudioEmitter;

// TQ-062: Refresh AudioEmitter.set_{Position,Velocity,Forward,Up} IL_0000-0011
// and getters IL_0000-0010 only store/load (with inverse handedness flips).
// Four independent vectors survive together; setters do not normalize orientation.
TEST(AudioEmitterTest, VectorPropertiesKeepIndependentUnnormalizedValues)
{
    AudioEmitter value;
    Vector3 position(1.0f, -2.0f, 3.0f);
    value.setPositionProperty(position);
    value.setVelocityProperty({-4.0f, 5.0f, -6.0f});
    value.setForwardProperty({2.0f, 0.0f, -2.0f});
    value.setUpProperty({0.0f, 3.0f, 1.0f});
    position = Vector3::Zero;
    EXPECT_EQ(value.getPositionProperty(), Vector3(1.0f, -2.0f, 3.0f));
    EXPECT_EQ(value.getVelocityProperty(), Vector3(-4.0f, 5.0f, -6.0f));
    EXPECT_EQ(value.getForwardProperty(), Vector3(2.0f, 0.0f, -2.0f));
    EXPECT_EQ(value.getUpProperty(), Vector3(0.0f, 3.0f, 1.0f));
    value.setForwardProperty(Vector3::Zero);
    EXPECT_EQ(value.getForwardProperty(), Vector3::Zero);
    EXPECT_EQ(value.getUpProperty(), Vector3(0.0f, 3.0f, 1.0f));
}

TEST(AudioEmitterTest, DefaultValues)
{
    AudioEmitter e;
    EXPECT_FLOAT_EQ(e.getDopplerScaleProperty(), 1.0f);
    EXPECT_EQ(e.getForwardProperty(), Vector3::Forward);
    EXPECT_EQ(e.getPositionProperty(), Vector3::Zero);
    EXPECT_EQ(e.getUpProperty(), Vector3::Up);
    EXPECT_EQ(e.getVelocityProperty(), Vector3::Zero);
}

TEST(AudioEmitterTest, DopplerScaleRoundTrip)
{
    AudioEmitter e;
    e.setDopplerScaleProperty(2.5f);
    EXPECT_FLOAT_EQ(e.getDopplerScaleProperty(), 2.5f);
}

TEST(AudioEmitterTest, DopplerScaleZeroAllowed)
{
    AudioEmitter e;
    EXPECT_NO_THROW(e.setDopplerScaleProperty(0.0f));
    EXPECT_FLOAT_EQ(e.getDopplerScaleProperty(), 0.0f);
}

TEST(AudioEmitterTest, DopplerScaleNegativeThrows)
{
    AudioEmitter e;
    EXPECT_THROW(e.setDopplerScaleProperty(-0.1f), System::ArgumentOutOfRangeException);
}

// XNA AudioEmitter.set_DopplerScale (IL_0000-IL_0017) throws ArgumentOutOfRangeException with
// ParamName "value" for a negative scale; its `bge.un` comparison lets NaN and -0.0 through, and
// a refusal leaves the stored scale alone.
TEST(AudioEmitterTest, DopplerScaleRefusalNamesValueAndUnorderedValuesPass)
{
    AudioEmitter e;
    e.setDopplerScaleProperty(3.0f);
    try { e.setDopplerScaleProperty(-std::numeric_limits<float>::denorm_min()); FAIL(); }
    catch (const System::ArgumentOutOfRangeException& error) { EXPECT_EQ(error.getParamNameProperty(), "value"); }
    EXPECT_EQ(e.getDopplerScaleProperty(), 3.0f);
    e.setDopplerScaleProperty(-0.0f);
    EXPECT_EQ(e.getDopplerScaleProperty(), 0.0f);
    e.setDopplerScaleProperty(std::numeric_limits<float>::quiet_NaN());
    EXPECT_TRUE(std::isnan(e.getDopplerScaleProperty()));
}

TEST(AudioEmitterTest, ForwardRoundTrip)
{
    AudioEmitter e;
    const Vector3 v(1.0f, 2.0f, 3.0f);
    e.setForwardProperty(v);
    EXPECT_EQ(e.getForwardProperty(), v);
}

TEST(AudioEmitterTest, PositionRoundTrip)
{
    AudioEmitter e;
    const Vector3 v(-4.0f, 5.0f, -6.0f);
    e.setPositionProperty(v);
    EXPECT_EQ(e.getPositionProperty(), v);
}

TEST(AudioEmitterTest, UpRoundTrip)
{
    AudioEmitter e;
    const Vector3 v(7.0f, -8.0f, 9.0f);
    e.setUpProperty(v);
    EXPECT_EQ(e.getUpProperty(), v);
}

TEST(AudioEmitterTest, VelocityRoundTrip)
{
    AudioEmitter e;
    const Vector3 v(0.5f, -0.5f, 1.5f);
    e.setVelocityProperty(v);
    EXPECT_EQ(e.getVelocityProperty(), v);
}

// TQ-062: Refresh AudioEmitter.set_DopplerScale IL_0008-0017 loads
// FrameworkResources.InvalidEmitterDopplerScale (literal below). The exception
// constructor supplies this runtime's parameter suffix, not an audio oracle.
TEST(AudioEmitterTest, NegativeDopplerScaleHasRefreshMessageAndKeepsValue)
{
    AudioEmitter emitter;
    emitter.setDopplerScaleProperty(2.5f);
    for (float value : {-1.0f, -std::numeric_limits<float>::infinity()})
    {
        try { emitter.setDopplerScaleProperty(value); FAIL() << "accepted " << value; }
        catch (const System::ArgumentOutOfRangeException& error)
        {
            EXPECT_EQ(error.getParamNameProperty(), "value");
            EXPECT_EQ(error.getMessageProperty(), System::ArgumentOutOfRangeException(
                "value", "The doppler scale of an audio emitter must be greater than or equal to zero.").getMessageProperty());
        }
        EXPECT_EQ(emitter.getDopplerScaleProperty(), 2.5f);
    }
}
