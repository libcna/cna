// SPDX-License-Identifier: MS-PL
// ISSUE-038: disposal guards, including sample helpers with the same missing guard.
#include <gtest/gtest.h>
#include <typeinfo>
#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/TimeSpan.hpp"

namespace
{
    using Microsoft::Xna::Framework::Audio::AudioChannels;
    using Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstance;
    using Microsoft::Xna::Framework::Audio::SoundEffectInstance;

    template<class Action>
    void ExpectDisposed(Action&& action)
    {
        try
        {
            action();
            FAIL() << "Disposed dynamic audio must refuse this operation";
        }
        catch (const System::ObjectDisposedException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ObjectDisposedException));
            EXPECT_EQ(error.getObjectNameProperty(), "DynamicSoundEffectInstance");
        }
        catch (...)
        {
            FAIL() << "Expected exact ObjectDisposedException before argument validation";
        }
    }

    TEST(DynamicAudioDisposedTest, PendingCountRefusesAfterDispose)
    {
        DynamicSoundEffectInstance instance(44100, AudioChannels::Stereo);
        EXPECT_EQ(instance.getPendingBufferCountProperty(), 0);
        instance.Dispose();
        ASSERT_TRUE(instance.getIsDisposedProperty());
        ExpectDisposed([&] { (void)instance.getPendingBufferCountProperty(); });
    }

    TEST(DynamicAudioDisposedTest, IsLoopedRefusesThroughBothDynamicAndBaseViews)
    {
        DynamicSoundEffectInstance instance(44100, AudioChannels::Stereo);
        SoundEffectInstance& base = instance;
        EXPECT_FALSE(instance.getIsLoopedProperty());
        base.Dispose();
        ASSERT_TRUE(instance.getIsDisposedProperty());
        ExpectDisposed([&] { (void)instance.getIsLoopedProperty(); });
        ExpectDisposed([&] { (void)base.getIsLoopedProperty(); });
    }

    TEST(DynamicAudioDisposedTest, SampleDurationChecksDisposalBeforeZeroOrInvalidSize)
    {
        DynamicSoundEffectInstance instance(44100, AudioChannels::Stereo);
        EXPECT_EQ(instance.GetSampleDuration(0), System::TimeSpan::Zero);
        instance.Dispose();
        for (const int bytes : {0, 4000, -1})
            ExpectDisposed([&] { (void)instance.GetSampleDuration(bytes); });
    }

    TEST(DynamicAudioDisposedTest, SampleSizeChecksDisposalBeforeZeroOrInvalidDuration)
    {
        DynamicSoundEffectInstance instance(44100, AudioChannels::Stereo);
        EXPECT_EQ(instance.GetSampleSizeInBytes(System::TimeSpan::Zero), 0);
        instance.Dispose();
        for (const int seconds : {0, 1, -1})
            ExpectDisposed([&] {
                (void)instance.GetSampleSizeInBytes(System::TimeSpan::FromSeconds(seconds));
            });
    }

    TEST(DynamicAudioDisposedTest, ExistingLoopSetterDisposalGuardIsPreserved)
    {
        DynamicSoundEffectInstance instance(44100, AudioChannels::Stereo);
        instance.Dispose();
        const bool value = false;
        ExpectDisposed([&] { instance.setIsLoopedProperty(value); });
        ExpectDisposed([&] { instance.setIsLoopedProperty(true); });
    }
}
