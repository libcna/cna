// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Audio::AudioEmitter;

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
