// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/Microphone.hpp"
#include "Microsoft/Xna/Framework/Audio/MicrophoneState.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/Environment.hpp"
#include "System/TimeSpan.hpp"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "System/Environment.hpp"

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

        // MC-6: CheckBuffer() is private (FNA has it as `internal`); this wrapper is the
        // sanctioned bridge for tests that exercise it directly, without making it public API.
        static void CheckBuffer(Microphone& mic)
        {
            mic.CheckBuffer();
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
        System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
        return true;
    }();
}

// ===================== Static discovery through the selected recording capability =====================

TEST(MicrophoneTest, AllReflectsTheSelectedRecordingCapability)
{
#if defined(CNA_AUDIO_PLATFORM_SDL3)
    // The SDL "dummy" driver (forced above) always exposes exactly one recording device, so
    // real enumeration never sees zero microphones here, unlike a genuine headless machine.
    EXPECT_FALSE(Microphone::getAllProperty().empty());
#elif defined(CNA_AUDIO_PLATFORM_SDL2) || defined(CNA_AUDIO_PLATFORM_NULL)
    // SDL2 and NULL currently advertise no recording provider and must never fall back to SDL3.
    EXPECT_TRUE(Microphone::getAllProperty().empty());
#else
#error "CNA audio platform selection did not define an implementation"
#endif
}

TEST(MicrophoneTest, DefaultDeviceEntryIsNamedDefaultDevice)
{
    const auto& all = Microphone::getAllProperty();
#if defined(CNA_AUDIO_PLATFORM_SDL2) || defined(CNA_AUDIO_PLATFORM_NULL)
    GTEST_SKIP() << "selected audio backend has no recording capability";
#endif
    ASSERT_FALSE(all.empty());
    EXPECT_EQ(all[0]->Name, "Default Device");
}

TEST(MicrophoneTest, DefaultPropertyIsFirstEntryOrNullWhenRecordingIsUnsupported)
{
    const auto& all = Microphone::getAllProperty();
    if (all.empty())
    {
        EXPECT_EQ(Microphone::getDefaultProperty(), nullptr);
    }
    else
    {
        EXPECT_EQ(Microphone::getDefaultProperty(), all[0]);
    }
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
    // Never Start()-ed, so no capture stream is open: nothing to read, returns 0.
    Microphone mic = MakeMic();
    std::vector<SharpRuntime::bytecs> buffer(10);
    EXPECT_EQ(mic.GetData(buffer), 0);
}

// P10-MIC-004: the single-arg overload delegates to GetData(buffer, 0, buffer.size()) -- an empty
// buffer makes count == 0, which the 3-arg overload's own validation rejects (matches FNA
// exactly: Microphone.cs's GetData(byte[]) computes `GetData(buffer, 0, buffer.Length)`, and
// `count <= 0` throws there too). A genuinely distinct call path from the 3-arg
// GetDataZeroOrNegativeCountThrows test above, which never goes through the single-arg overload.
TEST(MicrophoneTest, GetDataSingleArgOverloadWithEmptyBufferThrows)
{
    Microphone mic = MakeMic();
    std::vector<SharpRuntime::bytecs> buffer; // empty -- delegates to count == 0
    EXPECT_THROW(mic.GetData(buffer), System::ArgumentException);
}

TEST(MicrophoneTest, GetDataValidRangeReturnsZero)
{
    Microphone mic = MakeMic();
    std::vector<SharpRuntime::bytecs> buffer(100);
    EXPECT_EQ(mic.GetData(buffer, 10, 50), 0);
}

TEST(MicrophoneTest, GetDataLeavesBufferUntouchedWhenNoDataAvailable)
{
    // Matches FNA (MC-3): GetData never zero-fills the requested range on a no-op read -- the
    // caller's existing buffer contents must survive untouched.
    Microphone mic = MakeMic();
    std::vector<SharpRuntime::bytecs> buffer(10, static_cast<SharpRuntime::bytecs>(0xAB));
    EXPECT_EQ(mic.GetData(buffer, 0, 10), 0);
    for (auto b : buffer)
    {
        EXPECT_EQ(b, static_cast<SharpRuntime::bytecs>(0xAB));
    }
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

TEST(MicrophoneTest, GetDataNegativeCountThrows)
{
    // GetDataZeroOrNegativeCountThrows (above) only exercises count == 0 despite its name;
    // the validation is `count <= 0`, so a genuinely negative count needs its own test (MC-5).
    Microphone mic = MakeMic();
    std::vector<SharpRuntime::bytecs> buffer(10);
    EXPECT_THROW(mic.GetData(buffer, 0, -5), System::ArgumentException);
}

TEST(MicrophoneTest, GetDataCountBeyondBufferThrows)
{
    Microphone mic = MakeMic();
    std::vector<SharpRuntime::bytecs> buffer(10);
    EXPECT_THROW(mic.GetData(buffer, 0, 11), System::ArgumentException);
}

// P9-AUDIT-002: offset+count must be checked without computing the (possibly overflowing) sum
// directly -- the same int32-overflow class P9-VALIDATION-003 already fixed in SoundEffect's
// buffer/range ctor and DynamicSoundEffectInstance::SubmitBuffer/SubmitFloatBufferEXT, just
// missed here since this file wasn't in that task's named scope. A small, in-bounds offset plus
// a huge count overflows int32 when naively summed, wrapping to a small/negative value that
// silently passes a naive "offset + count > buffer.size()" check while still handing an
// enormous count to SDL_GetAudioStreamData(buffer.data() + offset, count) -- a real
// out-of-bounds write, not just a rejected call.
TEST(MicrophoneTest, GetDataRejectsOffsetCountIntegerOverflow)
{
    Microphone mic = MakeMic();
    std::vector<SharpRuntime::bytecs> buffer(10);
    constexpr SharpRuntime::intcs hugeCount = 2147483647; // INT32_MAX; offset(10)+count overflows int32
    EXPECT_THROW(mic.GetData(buffer, 10, hugeCount), System::ArgumentException);
}

// ===================== CheckBuffer / CheckAllBuffers =====================

TEST(MicrophoneTest, CheckBufferDoesNotThrowWithNoSubscribers)
{
    Microphone mic = MakeMic();
    EXPECT_NO_THROW(MicrophoneTestAccess::CheckBuffer(mic));
}

// MC-7: existing coverage only had "no subscriber -> doesn't throw" (short-circuits on
// BufferReady.Empty(), never actually exercises the `>` comparison) and "real capture, given
// enough time -> eventually fires" (positive path only, needs the dummy driver and up to 2s of
// polling). Neither deterministically proves BufferReady stays silent when queued duration is
// below BufferDuration -- this isolated (never Start()ed) instance always has GetQueuedBytes()==0
// (its capture session is null), so the comparison genuinely runs and must evaluate false.
TEST(MicrophoneTest, CheckBufferDoesNotRaiseWhenQueuedDurationIsBelowBufferDuration)
{
    Microphone mic = MakeMic();
    int fired = 0;
    Microphone* sender = nullptr;
    mic.BufferReady += [&fired, &sender](System::Object* s, const System::EventArgs&)
    {
        ++fired;
        sender = static_cast<Microphone*>(s);
    };

    MicrophoneTestAccess::CheckBuffer(mic);

    EXPECT_EQ(fired, 0);
    EXPECT_EQ(sender, nullptr);
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
    // to every test in this binary. BufferReady is also cleared so a test-local lambda (which
    // captures locals that go out of scope at the end of that test) can never be invoked again
    // by a later test sharing the same singleton.
    void TearDown() override
    {
        if (mic_ != nullptr)
        {
            mic_->Stop();
            mic_->BufferReady.Clear();
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

// P10-MIC-004: GetData() after a real Start()-then-Stop() cycle must behave exactly like "never
// started" (return 0, leave the caller's buffer untouched, MC-3) -- Stop() closes the capture
// session, a genuinely different history than
// GetDataLeavesBufferUntouchedWhenNoDataAvailable (which never opens a stream at all), even
// though both currently land on the same null-stream code path afterward.
TEST_F(MicrophoneCaptureTest, GetDataAfterStopReturnsZeroAndLeavesBufferUntouched)
{
    REQUIRE_MIC();
    mic_->Start();
    mic_->Stop();

    std::vector<SharpRuntime::bytecs> buffer(10, static_cast<SharpRuntime::bytecs>(0xAB));
    EXPECT_EQ(mic_->GetData(buffer, 0, 10), 0);
    for (auto b : buffer)
    {
        EXPECT_EQ(b, static_cast<SharpRuntime::bytecs>(0xAB));
    }
}

// MC-4: GetQueuedBytes/CheckBuffer were only just wired up to real SDL data (T-4A); before
// that, GetQueuedBytes always returned 0, so BufferReady could never fire and no test could
// have caught it. Use the smallest allowed BufferDuration (100ms) and poll CheckBuffer() while
// the real capture device accumulates data, verifying the handler actually fires.
TEST_F(MicrophoneCaptureTest, BufferReadyFiresWhenQueuedDataExceedsBufferDuration)
{
    REQUIRE_MIC();
    mic_->setBufferDurationProperty(System::TimeSpan::FromMilliseconds(100));

    int fired = 0;
    mic_->BufferReady += [&fired](System::Object*, const System::EventArgs&) { ++fired; };

    mic_->Start();

    for (int attempt = 0; attempt < 40 && fired == 0; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        MicrophoneTestAccess::CheckBuffer(*mic_);
    }

    EXPECT_GT(fired, 0);
}

// ===================== GetTypeName =====================

TEST(MicrophoneTest, GetTypeNameIsDottedXnaName)
{
    Microphone mic = MakeMic();
    EXPECT_EQ(mic.GetTypeName(), "Microsoft.Xna.Framework.Audio.Microphone");
}
