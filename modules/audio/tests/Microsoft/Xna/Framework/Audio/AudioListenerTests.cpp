// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Audio::AudioListener;

// TQ-062: Refresh AudioListener.set_{Position,Velocity,Forward,Up} IL_0000-0011
// and getters IL_0000-0010 only store/load (with inverse handedness flips).
// Four independent vectors survive together; setters do not normalize orientation.
TEST(AudioListenerTest, VectorPropertiesKeepIndependentUnnormalizedValues)
{
    AudioListener value;
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

TEST(AudioListenerTest, DefaultValues)
{
    AudioListener l;
    EXPECT_EQ(l.getForwardProperty(), Vector3::Forward);
    EXPECT_EQ(l.getPositionProperty(), Vector3::Zero);
    EXPECT_EQ(l.getUpProperty(), Vector3::Up);
    EXPECT_EQ(l.getVelocityProperty(), Vector3::Zero);
}

TEST(AudioListenerTest, ForwardRoundTrip)
{
    AudioListener l;
    const Vector3 v(1.0f, 2.0f, 3.0f);
    l.setForwardProperty(v);
    EXPECT_EQ(l.getForwardProperty(), v);
}

TEST(AudioListenerTest, PositionRoundTrip)
{
    AudioListener l;
    const Vector3 v(-4.0f, 5.0f, -6.0f);
    l.setPositionProperty(v);
    EXPECT_EQ(l.getPositionProperty(), v);
}

TEST(AudioListenerTest, UpRoundTrip)
{
    AudioListener l;
    const Vector3 v(7.0f, -8.0f, 9.0f);
    l.setUpProperty(v);
    EXPECT_EQ(l.getUpProperty(), v);
}

TEST(AudioListenerTest, VelocityRoundTrip)
{
    AudioListener l;
    const Vector3 v(0.5f, -0.5f, 1.5f);
    l.setVelocityProperty(v);
    EXPECT_EQ(l.getVelocityProperty(), v);
}
