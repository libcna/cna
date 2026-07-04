// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Audio::AudioListener;

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
