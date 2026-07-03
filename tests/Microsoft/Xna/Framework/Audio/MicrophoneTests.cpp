// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/Microphone.hpp"
#include "Microsoft/Xna/Framework/Audio/MicrophoneState.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/TimeSpan.hpp"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using Microsoft::Xna::Framework::Audio::AudioChannels;
using Microsoft::Xna::Framework::Audio::Microphone;
using Microsoft::Xna::Framework::Audio::MicrophoneState;
using Microsoft::Xna::Framework::Audio::SoundEffect;

namespace Microsoft::Xna::Framework::Audio
{
    // Test-only accessor for Microphone's private constructor (see Microphone.hpp). Lets tests
    // build an isolated instance with a caller-chosen (possibly invalid) handle, independent of
    // whatever getAllProperty() enumerates on the current machine/driver.
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

TEST(MicrophoneTest, GetSampleDurationDelegatesToSoundEffectWithMonoAndSampleRate)
{
    Microphone mic = MakeMic();
    // 100 bytes / (mono * 2 bytes/sample) = 50 samples; 50 / (44100/1000.0) = 1.133...ms,
    // which SoundEffect::GetSampleDuration truncates to a whole millisecond (1ms). The old
    // formula computed fractional seconds directly and would not match this truncation --
    // this pins the delegation FNA's Microphone.GetSampleDuration performs (MC-1).
    const auto expected = SoundEffect::GetSampleDuration(
        100, mic.getSampleRateProperty(), AudioChannels::Mono);
    EXPECT_TRUE(mic.GetSampleDuration(100) == expected);
    EXPECT_EQ(mic.GetSampleDuration(100).getMillisecondsProperty(), 1);
}

TEST(MicrophoneTest, GetSampleSizeInBytesDelegatesToSoundEffectWithMonoAndSampleRate)
{
    Microphone mic = MakeMic();
    const auto duration = System::TimeSpan::FromMilliseconds(250);
    const auto expected = SoundEffect::GetSampleSizeInBytes(
        duration, mic.getSampleRateProperty(), AudioChannels::Mono);
    EXPECT_EQ(mic.GetSampleSizeInBytes(duration), expected);
}

// ===================== GetData =====================

TEST(MicrophoneTest, GetDataSingleArgOverloadDelegatesAndReturnsZero)
{
    // Never Start()-ed, so no capture stream is open: falls through to the zero-fill stub.
    Microphone mic = MakeMic();
    std::vector<SharpRuntime::bytecs> buffer(10);
    EXPECT_EQ(mic.GetData(buffer), 0);
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

// ===================== Real capture (SDL dummy driver opens a genuinely working device) =====================

// Unlike MakeMic() (an isolated instance with an arbitrary, invalid handle), the "Default
// Device" entry from getAllProperty() opens for real even under the SDL dummy driver -- its
// RecordDevice callback continuously produces silence, so Start()/GetData() behave exactly like
// a real capture device. That makes these tests deterministic in headless CI with no GTEST_SKIP
// needed for the dummy-driver case; the skip only guards the case where no device at all is
// available (e.g. a build without SOUND_ENABLED).
class MicrophoneCaptureTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        mic_ = Microphone::getDefaultProperty();
    }

    // Always stop, even on assertion failure mid-test, so the next test starts from a clean
    // (Stopped, no open stream) state -- getDefaultProperty() returns the same cached singleton
    // to every test in this binary.
    void TearDown() override
    {
        if (mic_ != nullptr)
        {
            mic_->Stop();
        }
    }

    Microphone* mic_ = nullptr;
};

#define REQUIRE_MIC() do { if (mic_ == nullptr) GTEST_SKIP() << "no microphone device available"; } while (0)

TEST_F(MicrophoneCaptureTest, StartTransitionsToStartedOnRealDevice)
{
    REQUIRE_MIC();
    mic_->Start();
    EXPECT_EQ(mic_->getStateProperty(), MicrophoneState::Started);
}

TEST_F(MicrophoneCaptureTest, StopTransitionsBackToStoppedOnRealDevice)
{
    REQUIRE_MIC();
    mic_->Start();
    mic_->Stop();
    EXPECT_EQ(mic_->getStateProperty(), MicrophoneState::Stopped);
}

TEST_F(MicrophoneCaptureTest, RepeatedStartStopCyclesDoNotCrash)
{
    REQUIRE_MIC();
    EXPECT_NO_THROW(mic_->Start());
    EXPECT_NO_THROW(mic_->Stop());
    EXPECT_NO_THROW(mic_->Start());
    EXPECT_NO_THROW(mic_->Stop());
}

TEST_F(MicrophoneCaptureTest, GetDataReturnsNonZeroBytesAfterCapture)
{
    REQUIRE_MIC();
    mic_->Start();

    std::vector<SharpRuntime::bytecs> buffer(4096);
    SharpRuntime::intcs read = 0;
    for (int attempt = 0; attempt < 20 && read <= 0; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        read = mic_->GetData(buffer);
    }

    EXPECT_GT(read, 0);
}

// ===================== GetTypeName =====================

TEST(MicrophoneTest, GetTypeNameIsDottedXnaName)
{
    Microphone mic = MakeMic();
    EXPECT_EQ(mic.GetTypeName(), "Microsoft.Xna.Framework.Audio.Microphone");
}
