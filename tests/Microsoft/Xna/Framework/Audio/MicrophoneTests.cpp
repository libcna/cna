// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/Microphone.hpp"
#include "Microsoft/Xna/Framework/Audio/MicrophoneState.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/TimeSpan.hpp"

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

using Microsoft::Xna::Framework::Audio::Microphone;
using Microsoft::Xna::Framework::Audio::MicrophoneState;

namespace Microsoft::Xna::Framework::Audio
{
    // Test-only accessor for Microphone's private constructor (see Microphone.hpp). Production
    // instances only ever come from a real capture backend, which does not exist yet.
    struct MicrophoneTestAccess
    {
        static Microphone Make(SharpRuntime::uintcs id, std::string name)
        {
            return Microphone(id, std::move(name));
        }
    };
}

namespace
{
    using Microsoft::Xna::Framework::Audio::MicrophoneTestAccess;

    Microphone MakeMic(const std::string& name = "Test Mic")
    {
        return MicrophoneTestAccess::Make(1, name);
    }

    // Microphone::getAllProperty() caches its result for the lifetime of the process (matching
    // FNA's `micList` caching), so the SDL audio driver must be pinned before the *first* call
    // anywhere in this binary. A static initializer runs before any TEST body, guaranteeing this
    // regardless of gtest run order -- unlike a fixture SetUp(), which only helps if that
    // fixture's test happens to run first.
    const bool g_forceDummyAudioDriver = []
    {
        ::setenv("SDL_AUDIODRIVER", "dummy", 1);
        return true;
    }();
}

// ===================== Static discovery (SDL dummy driver always reports one device) =====================

TEST(MicrophoneTest, AllIsNonEmptyUnderDummyAudioDriver)
{
    // The SDL "dummy" driver (forced above) always exposes exactly one recording device, so
    // real enumeration never sees zero microphones here, unlike a genuine headless machine.
    EXPECT_FALSE(Microphone::getAllProperty().empty());
}

TEST(MicrophoneTest, DefaultDeviceEntryIsNamedDefaultDevice)
{
    const auto& all = Microphone::getAllProperty();
    ASSERT_FALSE(all.empty());
    EXPECT_EQ(all[0]->Name, "Default Device");
}

TEST(MicrophoneTest, DefaultPropertyIsFirstEntryOfAll)
{
    const auto& all = Microphone::getAllProperty();
    ASSERT_FALSE(all.empty());
    EXPECT_EQ(Microphone::getDefaultProperty(), all[0]);
}

// ===================== Name / state =====================

TEST(MicrophoneTest, NameRoundTripsConstructorArgument)
{
    Microphone mic = MakeMic("Test Mic");
    EXPECT_EQ(mic.Name, "Test Mic");
}

TEST(MicrophoneTest, InitialStateIsStopped)
{
    Microphone mic = MakeMic();
    EXPECT_EQ(mic.getStateProperty(), MicrophoneState::Stopped);
}

TEST(MicrophoneTest, StartSetsStateToStarted)
{
    Microphone mic = MakeMic();
    mic.Start();
    EXPECT_EQ(mic.getStateProperty(), MicrophoneState::Started);
}

TEST(MicrophoneTest, StopSetsStateBackToStopped)
{
    Microphone mic = MakeMic();
    mic.Start();
    mic.Stop();
    EXPECT_EQ(mic.getStateProperty(), MicrophoneState::Stopped);
}

TEST(MicrophoneTest, IsHeadsetIsAlwaysFalse)
{
    Microphone mic = MakeMic();
    EXPECT_FALSE(mic.getIsHeadsetProperty());
}

TEST(MicrophoneTest, SampleRateIs44100)
{
    Microphone mic = MakeMic();
    EXPECT_EQ(mic.getSampleRateProperty(), 44100);
}

// ===================== BufferDuration =====================

TEST(MicrophoneTest, DefaultBufferDurationIsOneSecond)
{
    // The constructor assigns the backing field directly, bypassing the setter's validation
    // (matching FNA), so this is unaffected by the setter's range check.
    Microphone mic = MakeMic();
    EXPECT_DOUBLE_EQ(mic.getBufferDurationProperty().getTotalSecondsProperty(), 1.0);
}

TEST(MicrophoneTest, BufferDurationValidRoundTrip)
{
    Microphone mic = MakeMic();
    mic.setBufferDurationProperty(System::TimeSpan::FromMilliseconds(500));
    EXPECT_EQ(mic.getBufferDurationProperty().getMillisecondsProperty(), 500);
}

TEST(MicrophoneTest, BufferDurationTooSmallThrows)
{
    Microphone mic = MakeMic();
    EXPECT_THROW(mic.setBufferDurationProperty(System::TimeSpan::FromMilliseconds(50)),
                 System::ArgumentOutOfRangeException);
}

TEST(MicrophoneTest, BufferDurationNotMultipleOfTenThrows)
{
    // getMillisecondsProperty() is the sub-second component ([-999, 999]), so the setter's
    // ">1000" branch is unreachable — not tested here since no TimeSpan value can trigger it.
    Microphone mic = MakeMic();
    EXPECT_THROW(mic.setBufferDurationProperty(System::TimeSpan::FromMilliseconds(105)),
                 System::ArgumentOutOfRangeException);
}

// ===================== GetSampleDuration / GetSampleSizeInBytes =====================

TEST(MicrophoneTest, GetSampleDurationRoundTripsWithGetSampleSizeInBytes)
{
    Microphone mic = MakeMic();
    const auto bytes = mic.GetSampleSizeInBytes(System::TimeSpan::FromSeconds(1.0));
    EXPECT_EQ(bytes, 88200); // 44100 Hz * mono(1) * 2 bytes/sample
    EXPECT_DOUBLE_EQ(mic.GetSampleDuration(bytes).getTotalSecondsProperty(), 1.0);
}

TEST(MicrophoneTest, GetSampleSizeInBytesOfZeroDurationIsZero)
{
    Microphone mic = MakeMic();
    EXPECT_EQ(mic.GetSampleSizeInBytes(System::TimeSpan::Zero), 0);
}

TEST(MicrophoneTest, GetSampleDurationOfZeroBytesIsZero)
{
    Microphone mic = MakeMic();
    EXPECT_DOUBLE_EQ(mic.GetSampleDuration(0).getTotalSecondsProperty(), 0.0);
}

// ===================== GetData =====================

TEST(MicrophoneTest, GetDataSingleArgOverloadDelegatesAndReturnsZero)
{
    Microphone mic = MakeMic();
    std::vector<SharpRuntime::bytecs> buffer(10);
    EXPECT_EQ(mic.GetData(buffer), 0); // no capture backend, so 0 bytes are ever available
}

TEST(MicrophoneTest, GetDataValidRangeReturnsZero)
{
    Microphone mic = MakeMic();
    std::vector<SharpRuntime::bytecs> buffer(100);
    EXPECT_EQ(mic.GetData(buffer, 10, 50), 0);
}

TEST(MicrophoneTest, GetDataNegativeOffsetThrows)
{
    Microphone mic = MakeMic();
    std::vector<SharpRuntime::bytecs> buffer(10);
    EXPECT_THROW(mic.GetData(buffer, -1, 5), System::ArgumentException);
}

TEST(MicrophoneTest, GetDataOffsetBeyondBufferThrows)
{
    Microphone mic = MakeMic();
    std::vector<SharpRuntime::bytecs> buffer(10);
    EXPECT_THROW(mic.GetData(buffer, 11, 1), System::ArgumentException);
}

TEST(MicrophoneTest, GetDataZeroOrNegativeCountThrows)
{
    Microphone mic = MakeMic();
    std::vector<SharpRuntime::bytecs> buffer(10);
    EXPECT_THROW(mic.GetData(buffer, 0, 0), System::ArgumentException);
}

TEST(MicrophoneTest, GetDataCountBeyondBufferThrows)
{
    Microphone mic = MakeMic();
    std::vector<SharpRuntime::bytecs> buffer(10);
    EXPECT_THROW(mic.GetData(buffer, 0, 11), System::ArgumentException);
}

// ===================== CheckBuffer / CheckAllBuffers =====================

TEST(MicrophoneTest, CheckBufferDoesNotThrowWithNoSubscribers)
{
    Microphone mic = MakeMic();
    EXPECT_NO_THROW(mic.CheckBuffer());
}

TEST(MicrophoneTest, CheckAllBuffersDoesNotThrowWithEmptyList)
{
    EXPECT_NO_THROW(Microphone::CheckAllBuffers());
}

// ===================== GetTypeName =====================

TEST(MicrophoneTest, GetTypeNameIsDottedXnaName)
{
    Microphone mic = MakeMic();
    EXPECT_EQ(mic.GetTypeName(), "Microsoft.Xna.Framework.Audio.Microphone");
}
