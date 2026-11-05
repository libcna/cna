// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/Microphone.hpp"
#include "Microsoft/Xna/Framework/Audio/MicrophoneState.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/detail/FrameworkCompatibility.hpp"
#include <typeinfo>
#include "System/Environment.hpp"
#include "System/TimeSpan.hpp"

#include "CNA/Audio/Platform/IAudioRecordingDevice.hpp"
#include "Microsoft/Xna/Framework/Audio/NoMicrophoneConnectedException.hpp"
#include "System/InvalidOperationException.hpp"
#include <stdexcept>
#include <new>
#include <limits>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <algorithm>
#include <cstddef>

using Microsoft::Xna::Framework::Audio::AudioChannels;
using Microsoft::Xna::Framework::Audio::Microphone;
using Microsoft::Xna::Framework::Audio::MicrophoneState;
using Microsoft::Xna::Framework::Audio::SoundEffect;

namespace Microsoft::Xna::Framework::Audio
{
    // Test-only accessor for Microphone's private constructor (see Microphone.hpp). Lets tests
    // build an isolated instance with a caller-chosen provider, independent of whatever
    // getAllProperty() enumerates on the current machine/driver.
    struct MicrophoneTestAccess
    {
        static Microphone Make(SharpRuntime::uintcs id, std::string name,
                               CNA::Audio::Platform::IAudioRecordingDeviceProvider* provider = nullptr)
        {
            return Microphone(id, std::move(name), provider);
        }

        struct ScopedList
        {
            explicit ScopedList(std::vector<Microphone*> devices)
                : entries(std::move(devices)), previous(Microphone::micList)
            {
                Microphone::micList = &entries;
            }
            ~ScopedList() { Microphone::micList = previous; }
            ScopedList(const ScopedList&) = delete;
            ScopedList& operator=(const ScopedList&) = delete;
            std::vector<Microphone*> entries;
            std::vector<Microphone*>* previous;
        };

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

    namespace Capture = CNA::Audio::Platform;

    struct RecordingScript
    {
        Capture::AudioFormat format{44100, 1, Capture::AudioSampleFormat::Signed16};
        bool connected = true;
        bool missing = false;
        bool failOpen = false;
        bool failAllocation = false;
        bool startsOpen = true;
        bool disconnectOnOpen = false;
        std::size_t queuedBytes = 0;
        mutable int availabilityQueries = 0;
        int reads = 0;
        std::size_t requestedBytes = 0;
        Capture::AudioRecordingDeviceId requestedId = 0;
        Capture::AudioFormat requestedFormat{};
        std::vector<std::byte> payload;
        int created = 0;
        int opened = 0;
        int closed = 0;
        int destroyed = 0;
    };

    class ScriptedRecordingDevice final : public Capture::IAudioRecordingDevice
    {
    public:
        explicit ScriptedRecordingDevice(RecordingScript& script) : script_(script) {}
        ~ScriptedRecordingDevice() override
        {
            if (open_) Close();
            ++script_.destroyed;
        }
        const Capture::AudioRecordingDeviceInfo& GetInfo() const noexcept override { return info_; }
        bool IsConnected() const noexcept override { return script_.connected; }
        Capture::AudioFormat Open(const Capture::AudioFormat& requested) override
        {
            script_.requestedFormat = requested;
            ++script_.opened;
            if (script_.failAllocation) throw std::bad_alloc();
            if (script_.failOpen) throw std::runtime_error("scripted capture open failure");
            open_ = script_.startsOpen;
            if (script_.disconnectOnOpen) script_.connected = false;
            return script_.format;
        }
        void Close() noexcept override { ++script_.closed; open_ = false; }
        bool IsOpen() const noexcept override { return open_; }
        Capture::AudioFormat GetFormat() const noexcept override
        {
            return open_ ? script_.format : Capture::AudioFormat{};
        }
        Capture::AudioRecordingIoResult GetAvailableBytes() const noexcept override
        {
            ++script_.availabilityQueries;
            if (script_.queuedBytes == 0) return {};
            return {Capture::AudioRecordingIoStatus::Success, script_.queuedBytes};
        }
        Capture::AudioRecordingIoResult Read(std::span<std::byte> destination) noexcept override
        {
            ++script_.reads;
            script_.requestedBytes = destination.size();
            const auto bytes = std::min(destination.size(), script_.payload.size());
            std::copy_n(script_.payload.begin(), bytes, destination.begin());
            script_.payload.erase(script_.payload.begin(), script_.payload.begin() + bytes);
            return bytes == 0 ? Capture::AudioRecordingIoResult{}
                : Capture::AudioRecordingIoResult{Capture::AudioRecordingIoStatus::Success, bytes};
        }
    private:
        RecordingScript& script_;
        Capture::AudioRecordingDeviceInfo info_{1, "Scripted capture", true};
        bool open_ = false;
    };

    class ScriptedRecordingProvider final : public Capture::IAudioRecordingDeviceProvider
    {
    public:
        RecordingScript script;
        std::vector<Capture::AudioRecordingDeviceInfo> GetDevices() const override
        {
            return {{1, "Scripted capture", true}};
        }
        std::unique_ptr<Capture::IAudioRecordingDevice> CreateDevice(
            Capture::AudioRecordingDeviceId id) override
        {
            script.requestedId = id;
            ++script.created;
            if (script.missing || !script.connected) return nullptr;
            return std::make_unique<ScriptedRecordingDevice>(script);
        }
    };

    Microphone MakeMic(const std::string& name = "Test Mic")
    {
        static ScriptedRecordingProvider provider;
        return MicrophoneTestAccess::Make(1, name, &provider);
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
#elif defined(CNA_AUDIO_PLATFORM_ALSA)
    // X11-0162: ALSA captures. The test binaries point it at ALSA's `null`
    // device (CNA_AUDIO_RECORDING_DEVICE), never at the machine's microphone.
    EXPECT_FALSE(Microphone::getAllProperty().empty());
#elif defined(CNA_AUDIO_PLATFORM_NULL)
    // NULL advertises no recording provider and must never fall back to SDL3.
    EXPECT_TRUE(Microphone::getAllProperty().empty());
#else
#error "CNA audio platform selection did not define an implementation"
#endif
}

// XNA's Microphone.All is the machine's device list and Microphone.Default is a real device
// reporting the driver's own name. CNA used to prepend a synthetic entry literally named
// "Default Device", reproducing FNA; SAMPLE-098 caught the difference on screen against the real
// XNA runtime, and CLAUDE.md makes XNA the tie-break. No entry may carry that invented name.
TEST(MicrophoneTest, AllContainsOnlyRealDevicesWithNoInventedDefaultEntry)
{
#if defined(CNA_AUDIO_PLATFORM_NULL)
    GTEST_SKIP() << "selected audio backend has no recording capability";
#endif
    const auto& all = Microphone::getAllProperty();
    ASSERT_FALSE(all.empty());
    for (const Microphone* microphone : all)
    {
        EXPECT_NE(microphone->Name, "Default Device");
    }
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

TEST(MicrophoneTest, FailedStartPropagatesTheOpenErrorAndCanBeRetried)
{
    ScriptedRecordingProvider provider;
    provider.script.failOpen = true;
    auto mic = MicrophoneTestAccess::Make(1, "Failure", &provider);
    try
    {
        mic.Start();
        FAIL() << "Start must propagate the provider's open failure";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_STREQ(error.what(), "scripted capture open failure");
    }
    EXPECT_EQ(mic.getStateProperty(), MicrophoneState::Stopped);
    EXPECT_EQ(mic.getSampleRateProperty(), 44100);
    EXPECT_EQ(provider.script.destroyed, 1);
    provider.script.failOpen = false;
    provider.script.format.sampleRate = 48000;
    EXPECT_NO_THROW(mic.Start());
    EXPECT_EQ(mic.getStateProperty(), MicrophoneState::Started);
    EXPECT_EQ(mic.getSampleRateProperty(), 48000);
    EXPECT_EQ(mic.GetSampleSizeInBytes(System::TimeSpan::FromSeconds(1)), 96000);
    EXPECT_NO_THROW(mic.Start());
    EXPECT_EQ(provider.script.opened, 2);
    EXPECT_EQ(provider.script.created, 2);
    mic.Stop();
    EXPECT_EQ(provider.script.closed, 1);
}

TEST(MicrophoneTest, StartPropagatesAllocationFailureWithoutPublishingStarted)
{
    ScriptedRecordingProvider provider;
    provider.script.failAllocation = true;
    auto mic = MicrophoneTestAccess::Make(1, "Allocation failure", &provider);
    EXPECT_THROW(mic.Start(), std::bad_alloc);
    EXPECT_EQ(mic.getStateProperty(), MicrophoneState::Stopped);
    EXPECT_EQ(provider.script.destroyed, 1);
}

TEST(MicrophoneTest, StartRefusesAbsentRecordingCapabilityOrDisconnectedDevice)
{
    using Microsoft::Xna::Framework::Audio::NoMicrophoneConnectedException;
    // Refresh Start IL_0017-IL_001c propagates the native error; Helpers.GetExceptionFromResult
    // IL_0229 constructs NoMicrophoneConnectedException(), whose .ctor IL_0000-IL_0006
    // delegates to System.Exception's runtime-type fallback message.
    auto expectDisconnected = [](Microphone& mic)
    {
        try { mic.Start(); FAIL() << "missing device accepted"; }
        catch (const NoMicrophoneConnectedException& error)
        {
            EXPECT_EQ(error.getMessageProperty(),
                "Exception of type 'Microsoft.Xna.Framework.Audio.NoMicrophoneConnectedException' was thrown.");
            EXPECT_EQ(error.getInnerExceptionProperty(), nullptr);
        }
    };
    auto unsupported = MicrophoneTestAccess::Make(1, "Unsupported");
    expectDisconnected(unsupported);
    EXPECT_EQ(unsupported.getStateProperty(), MicrophoneState::Stopped);
    ScriptedRecordingProvider provider;
    provider.script.missing = true;
    auto missing = MicrophoneTestAccess::Make(1, "Missing", &provider);
    expectDisconnected(missing);
    EXPECT_EQ(missing.getStateProperty(), MicrophoneState::Stopped);

    provider.script.missing = false;
    provider.script.disconnectOnOpen = true;
    auto lostDuringOpen = MicrophoneTestAccess::Make(1, "Lost during open", &provider);
    expectDisconnected(lostDuringOpen);
    EXPECT_EQ(lostDuringOpen.getStateProperty(), MicrophoneState::Stopped);
    EXPECT_EQ(provider.script.closed, 1);
    EXPECT_EQ(provider.script.destroyed, 1);
}

TEST(MicrophoneTest, StartRefusesInvalidNegotiationAndClosesTheOpenedDevice)
{
    const std::vector<Capture::AudioFormat> invalid = {
        {0, 1, Capture::AudioSampleFormat::Signed16},
        {44100, 2, Capture::AudioSampleFormat::Signed16},
        {44100, 1, Capture::AudioSampleFormat::Float32},
        {std::numeric_limits<std::uint32_t>::max(), 1, Capture::AudioSampleFormat::Signed16}};
    for (const auto& format : invalid)
    {
        ScriptedRecordingProvider provider;
        provider.script.format = format;
        auto mic = MicrophoneTestAccess::Make(1, "Invalid format", &provider);
        EXPECT_THROW(mic.Start(), System::InvalidOperationException);
        EXPECT_EQ(mic.getStateProperty(), MicrophoneState::Stopped);
        EXPECT_EQ(mic.getSampleRateProperty(), 44100);
        EXPECT_EQ(provider.script.closed, 1);
        EXPECT_EQ(provider.script.destroyed, 1);
    }
    ScriptedRecordingProvider provider;
    provider.script.startsOpen = false;
    auto closed = MicrophoneTestAccess::Make(1, "Not started", &provider);
    EXPECT_THROW(closed.Start(), System::InvalidOperationException);
    EXPECT_EQ(closed.getStateProperty(), MicrophoneState::Stopped);
}

TEST(MicrophoneTest, RepeatedStartAfterDeviceLossDoesNotReportStarted)
{
    using Microsoft::Xna::Framework::Audio::NoMicrophoneConnectedException;
    ScriptedRecordingProvider provider;
    auto mic = MicrophoneTestAccess::Make(1, "Disconnected", &provider);
    mic.Start();
    provider.script.connected = false;
    EXPECT_THROW(mic.Start(), NoMicrophoneConnectedException);
    EXPECT_EQ(mic.getStateProperty(), MicrophoneState::Stopped);
    EXPECT_EQ(provider.script.closed, 1);
    provider.script.connected = true;
    EXPECT_NO_THROW(mic.Start());
    EXPECT_EQ(mic.getStateProperty(), MicrophoneState::Started);
}

TEST(MicrophoneTest, IsHeadsetMatchesRefreshConstructor)
{
    Microphone mic = MakeMic();
    // Refresh Microphone::.ctor IL_004a-IL_004c; get_IsHeadset IL_0000-IL_0006.
    EXPECT_TRUE(mic.getIsHeadsetProperty());
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
    Microphone mic = MakeMic();
    EXPECT_THROW(mic.setBufferDurationProperty(System::TimeSpan::FromMilliseconds(105)),
                 System::ArgumentOutOfRangeException);
}

// BINDFIX-032. The setter reads TotalMilliseconds, as XNA's IL does, not the sub-second
// `Milliseconds` component FNA reads. These four cases are exactly what that changes, and the
// comment that used to sit above the test before this one asserted the opposite.
TEST(MicrophoneTest, BufferDurationUsesTotalMillisecondsAsXnaDoes)
{
    Microphone mic = MakeMic();

    // The whole point: 1000 ms is the value the property REPORTS on an unconfigured microphone,
    // so `mic.BufferDuration = mic.BufferDuration` has to work. Reading the sub-second component
    // makes 1000 ms read as 0 and throw.
    EXPECT_NO_THROW(mic.setBufferDurationProperty(mic.getBufferDurationProperty()));
    EXPECT_NO_THROW(mic.setBufferDurationProperty(System::TimeSpan::FromMilliseconds(1000)));
    EXPECT_DOUBLE_EQ(mic.getBufferDurationProperty().getTotalMillisecondsProperty(), 1000.0);

    // And the upper branch is reachable now. Under the sub-second reading these three were
    // accepted, because their `Milliseconds` components are 100, 500 and 500.
    EXPECT_THROW(mic.setBufferDurationProperty(System::TimeSpan::FromMilliseconds(1100)),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(mic.setBufferDurationProperty(System::TimeSpan::FromMilliseconds(1500)),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(mic.setBufferDurationProperty(System::TimeSpan::FromMilliseconds(2500)),
                 System::ArgumentOutOfRangeException);

    // The refusals left the property untouched.
    EXPECT_DOUBLE_EQ(mic.getBufferDurationProperty().getTotalMillisecondsProperty(), 1000.0);
}

// XNA Refresh Microphone.set_BufferDuration IL_0011-IL_0060 compares the double
// TotalMilliseconds, including fractional milliseconds; no truncation precedes validation.
TEST(MicrophoneTest, BufferDurationChecksEveryStepAndRefusesFractionalTicksWithXnaDiagnostic)
{
    Microphone mic = MakeMic();
    for (int milliseconds = 100; milliseconds <= 1000; milliseconds += 10)
    {
        SCOPED_TRACE(milliseconds);
        const auto ticks = static_cast<SharpRuntime::longcs>(milliseconds) * 10000;
        ASSERT_NO_THROW(mic.setBufferDurationProperty(System::TimeSpan::FromTicks(ticks)));
        EXPECT_EQ(mic.getBufferDurationProperty().getTicksProperty(), ticks);
    }
    // A rejection must preserve a non-default prior setting (250 ms = 2,500,000 ticks).
    mic.setBufferDurationProperty(System::TimeSpan::FromTicks(2500000));
    for (const auto ticks : { -1LL, 0LL, 999999LL, 1000001LL, 1099999LL,
                             1100001LL, 9999999LL, 10000001LL, 11000000LL })
    {
        SCOPED_TRACE(ticks);
        try
        {
            mic.setBufferDurationProperty(System::TimeSpan::FromTicks(ticks));
            FAIL() << "invalid duration accepted";
        }
        catch (const System::ArgumentOutOfRangeException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentOutOfRangeException));
            EXPECT_EQ(error.getParamNameProperty(), "value");
            // SharpRuntime supports both diagnostic profiles; pin the XNA resource text
            // exactly and the explicit parameter suffix used by the selected host profile.
            const std::string suffix = System::detail::UseNetFrameworkArgumentExceptionMessages()
                ? "\nParameter name: value" : " (Parameter 'value')";
            EXPECT_EQ(error.getMessageProperty(),
                "Microphone buffer duration must be between 100ms and 1sec and  10ms aligned." + suffix);
        }
        EXPECT_EQ(mic.getBufferDurationProperty().getTicksProperty(), 2500000);
    }
}

// ===================== GetSampleDuration / GetSampleSizeInBytes =====================

TEST(MicrophoneTest, GetSampleDurationRoundTripsWithGetSampleSizeInBytes)
{
    Microphone mic = MakeMic();
    const auto bytes = mic.GetSampleSizeInBytes(System::TimeSpan::FromSeconds(1.0));
    // XNA's (int)(ms * (double)(44100f / 1000f)): 44099 whole frames of 16-bit mono.
    EXPECT_EQ(bytes, 88198);
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
    // Refresh AudioFormat.DurationFromSize IL_0000-IL_001a: 100/2 = 50 frames;
    // float(50)*1000/44100 = 1.133...ms; .NET FromMilliseconds rounds to 1ms.
    const auto expected = SoundEffect::GetSampleDuration(
        100, mic.getSampleRateProperty(), AudioChannels::Mono);
    EXPECT_TRUE(mic.GetSampleDuration(100) == expected);
    EXPECT_EQ(mic.GetSampleDuration(100).getTicksProperty(), 10000);
    EXPECT_EQ(mic.GetSampleDuration(100).getMillisecondsProperty(), 1);
}

TEST(MicrophoneTest, GetSampleSizeInBytesDelegatesToSoundEffectWithMonoAndSampleRate)
{
    Microphone mic = MakeMic();
    const auto duration = System::TimeSpan::FromMilliseconds(250);
    const auto expected = SoundEffect::GetSampleSizeInBytes(
        duration, mic.getSampleRateProperty(), AudioChannels::Mono);
    EXPECT_EQ(mic.GetSampleSizeInBytes(duration), expected);
    // Refresh SizeFromDuration IL_000d-IL_0028: float(44100)/1000 =
    // 44.09999847412109375; trunc(250 * that) = 11024 frames; *2 = 22048 bytes.
    EXPECT_EQ(mic.GetSampleSizeInBytes(duration), 22048);
}

// Independent literal expectations keep shared CNA helper regressions from hiding behind
// Microphone == SoundEffect delegation assertions above. All comparisons are integer ticks/bytes.
TEST(MicrophoneTest, SampleConversionsPinWholeFramesHalfMillisecondRoundingAndOverflow)
{
    Microphone mic = MakeMic();
    // Refresh AudioFormat.DurationFromSize IL_0000-IL_001a divides by blockAlign=2
    // as an integer before float milliseconds and .NET nearest-ms rounding.
    for (const auto [bytes, ticks] : std::vector<std::pair<int, SharpRuntime::longcs>>{
             {0, 0}, {1, 0}, {43, 0}, {44, 0}, {45, 0}, {46, 10000},
             {47, 10000}, {100, 10000}, {88200, 10000000}})
        EXPECT_EQ(mic.GetSampleDuration(bytes).getTicksProperty(), ticks) << bytes;
    // float(44100)/1000f=44.09999847412109375; truncate frames, then multiply by 2.
    for (const auto [ticks, bytes] : std::vector<std::pair<SharpRuntime::longcs, int>>{
             {0, 0}, {1, 0}, {10000, 88}, {2500000, 22048}, {10000000, 88198}})
        EXPECT_EQ(mic.GetSampleSizeInBytes(System::TimeSpan::FromTicks(ticks)), bytes) << ticks;
    try { (void)mic.GetSampleDuration(-1); FAIL(); }
    catch (const System::ArgumentException& error)
    {
        EXPECT_EQ(typeid(error), typeid(System::ArgumentException));
        EXPECT_EQ(error.getParamNameProperty(), "");
        EXPECT_EQ(error.getMessageProperty(), "Buffer size cannot be negative.");
    }
    ScriptedRecordingProvider provider;
    provider.script.format.sampleRate = 48000;
    auto exactRate = MicrophoneTestAccess::Make(1, "48 kHz", &provider);
    exactRate.Start();
    // 48 bytes = 24 frames; 24*1000/48000 = exactly 0.5ms -> 1ms, not truncation.
    EXPECT_EQ(exactRate.GetSampleDuration(47).getTicksProperty(), 0);
    EXPECT_EQ(exactRate.GetSampleDuration(48).getTicksProperty(), 10000);
    EXPECT_EQ(exactRate.GetSampleDuration(49).getTicksProperty(), 10000);
    // 22,369,621ms*48 = 1,073,741,808 frames; *2=2,147,483,616 fits int32.
    EXPECT_EQ(exactRate.GetSampleSizeInBytes(System::TimeSpan::FromTicks(223696210000LL)), 2147483616);
    // One more millisecond keeps the frame count in int32 but overflows checked byte multiplication.
    // 44,739,243ms*48 = 2,147,483,664 also overflows the frame conversion itself.
    // Refresh Microphone.GetSampleSizeInBytes IL_0061-IL_006c translates both to duration refusals.
    for (const auto ticks : {-1LL, 223696220000LL, 447392430000LL, 21474836470000LL, 21474836480000LL})
    {
        try { (void)exactRate.GetSampleSizeInBytes(System::TimeSpan::FromTicks(ticks)); FAIL() << ticks; }
        catch (const System::ArgumentOutOfRangeException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentOutOfRangeException));
            EXPECT_EQ(error.getParamNameProperty(), "duration");
        }
    }
}

// ===================== GetData =====================

TEST(MicrophoneTest, GetDataSingleArgOverloadDelegatesAndReturnsZero)
{
    // Never Start()-ed, so no capture stream is open: nothing to read, returns 0. The buffer is
    // long enough to pass XNA's one-millisecond rule (see GetDataRefusesWhatXnaRefuses).
    Microphone mic = MakeMic();
    std::vector<SharpRuntime::bytecs> buffer(4096);
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
    std::vector<SharpRuntime::bytecs> buffer(100, static_cast<SharpRuntime::bytecs>(0xAB));
    EXPECT_EQ(mic.GetData(buffer, 0, 100), 0);
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

// XNA Microphone.GetData(byte[], int, int) (IL_0011-IL_00aa) refuses with ArgumentException, in
// order: an empty buffer or one ending in half a 16-bit sample; an offset outside the buffer
// (offset == Length included) or not on a sample; a count that is not positive, does not fit or
// is not whole samples; and a count whose DurationFromSize is TimeSpan.Zero. Hand-derived at the
// default 44100 Hz: 44 bytes are 22 frames, 0.4989 ms -> 0 ms (refused); 46 bytes are 23 frames,
// 0.5215 ms -> 1 ms (accepted). GetData(byte[]) checks the buffer itself first (IL_0000-IL_0022).
TEST(MicrophoneTest, GetDataRefusesWhatXnaRefuses)
{
    Microphone mic = MakeMic();
    ASSERT_EQ(mic.getSampleRateProperty(), 44100);
    // Refresh GetData IL_0029-IL_0033 / IL_004c-IL_0056 / IL_00a0-IL_00aa:
    // plain ArgumentException(resource message), with no parameter name. The former test
    // incorrectly asserted CNA's invented "buffer"/"offset"/"count" parameter names.
    const std::string bufferMessage =
        "Ensure that the buffer length is non-zero and meets the block alignment requirements for the audio format.";
    const std::string offsetMessage =
        "Offset must be within the buffer boundaries and meet the block alignment requirements for the audio format.";
    const std::string countMessage =
        "Ensure that count is valid and meets the block alignment requirements for the audio format. Offset and count must define a valid region within the buffer boundaries.";
    using Bytes = std::vector<SharpRuntime::bytecs>;
    auto refusalOf = [&](Bytes buffer, int offset, int count) -> std::string {
        try { (void)mic.GetData(buffer, offset, count); }
        catch (const System::ArgumentException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentException));
            EXPECT_EQ(error.getParamNameProperty(), "");
            return error.getMessageProperty();
        }
        return "<no exception>";
    };
    EXPECT_EQ(refusalOf(Bytes(), -1, -1), bufferMessage);
    EXPECT_EQ(refusalOf(Bytes(101), 0, 100), bufferMessage);
    EXPECT_EQ(refusalOf(Bytes(100), -2, 50), offsetMessage);
    EXPECT_EQ(refusalOf(Bytes(100), 1, 50), offsetMessage);
    EXPECT_EQ(refusalOf(Bytes(100), 100, 2), offsetMessage);
    EXPECT_EQ(refusalOf(Bytes(100), 102, 2), offsetMessage);
    EXPECT_EQ(refusalOf(Bytes(100), 0, 0), countMessage);
    EXPECT_EQ(refusalOf(Bytes(100), 0, -2), countMessage);
    EXPECT_EQ(refusalOf(Bytes(100), 0, 51), countMessage);
    EXPECT_EQ(refusalOf(Bytes(100), 50, 52), countMessage);
    // Valid aligned offset, positive aligned count, but checked offset+count overflows int32.
    EXPECT_EQ(refusalOf(Bytes(100), 2, 2147483646), countMessage);
    EXPECT_EQ(refusalOf(Bytes(100), 0, 44), countMessage);
    EXPECT_EQ(refusalOf(Bytes(100), 0, 46), "<no exception>");
    EXPECT_EQ(refusalOf(Bytes(100), 54, 46), "<no exception>");
    EXPECT_EQ(refusalOf(Bytes(3), 1, 1), bufferMessage);
    EXPECT_EQ(refusalOf(Bytes(100), 3, 1), offsetMessage);
    for (const auto length : {0, 101, 44})
    {
        Bytes buffer(length);
        try { (void)mic.GetData(buffer); FAIL() << "invalid whole buffer accepted"; }
        catch (const System::ArgumentException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentException));
            EXPECT_EQ(error.getParamNameProperty(), "");
            EXPECT_EQ(error.getMessageProperty(), length == 44 ? countMessage : bufferMessage);
        }
    }

    Bytes odd(4097);
    EXPECT_THROW((void)mic.GetData(odd), System::ArgumentException);
    Bytes tooShort(44);
    EXPECT_THROW((void)mic.GetData(tooShort), System::ArgumentException);
    Bytes justLongEnough(46);
    EXPECT_EQ(mic.GetData(justLongEnough), 0);
}

// XNA Microphone.GetSampleDuration (IL_0011-IL_0032) refuses only a negative size -- the rate is
// the microphone's own format, never checked against SoundEffect's 8000..48000 range -- and
// GetSampleSizeInBytes (IL_0011-IL_006c) refuses a duration outside 0..int.MaxValue ms and turns
// an OverflowException into ArgumentOutOfRangeException("duration"). A device that opens at
// 96 kHz must still convert; delegating to SoundEffect's validated statics would refuse it.
TEST(MicrophoneTest, SampleConversionsRefuseWhatXnaRefusesAndAcceptTheDeviceRate)
{
    Microphone mic = MakeMic();
    try { (void)mic.GetSampleDuration(-2); FAIL(); }
    catch (const System::ArgumentOutOfRangeException&) { FAIL() << "a size refusal is a plain ArgumentException"; }
    catch (const System::ArgumentException&) {}
    auto paramOf = [&](System::TimeSpan duration) -> std::string {
        try { (void)mic.GetSampleSizeInBytes(duration); }
        catch (const System::ArgumentOutOfRangeException& error) { return error.getParamNameProperty(); }
        return "<no exception>";
    };
    EXPECT_EQ(paramOf(System::TimeSpan::FromTicks(-1)), "duration");
    EXPECT_EQ(paramOf(System::TimeSpan::FromMilliseconds(2147483648.0)), "duration");
    // 24 h at 44100 Hz mono: 86,400,000 ms * 44.0999985 = 3.81e9 frames, past int32.
    EXPECT_EQ(paramOf(System::TimeSpan::FromHours(24.0)), "duration");

    ScriptedRecordingProvider provider;
    provider.script.format.sampleRate = 96000;
    auto fast = MicrophoneTestAccess::Make(1, "96 kHz", &provider);
    fast.Start();
    ASSERT_EQ(fast.getSampleRateProperty(), 96000);
    // 1 s at 96 kHz mono = 96000 frames = 192000 bytes, and back again.
    EXPECT_EQ(fast.GetSampleSizeInBytes(System::TimeSpan::FromSeconds(1.0)), 192000);
    EXPECT_EQ(fast.GetSampleDuration(192000).getTicksProperty(), 1000 * System::TimeSpan::TicksPerMillisecond);
    fast.Stop();
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

// TQ-061: all bytes and queue lengths below are supplied by this canned provider.
TEST(MicrophoneTest, ScriptedReadReturnsExactBytesInsideRequestedRangeAndPreservesTheRest)
{
    ScriptedRecordingProvider provider;
    auto mic = MicrophoneTestAccess::Make(73, "Range capture", &provider);
    mic.Start();
    EXPECT_EQ(provider.script.requestedId, 73U);
    EXPECT_EQ(provider.script.requestedFormat.sampleRate, 44100U);
    EXPECT_EQ(provider.script.requestedFormat.channels, 1U);
    EXPECT_EQ(provider.script.requestedFormat.sampleFormat, Capture::AudioSampleFormat::Signed16);
    // Refresh UnsafeGetData IL_001c-IL_002c forwards base+offset/count; IL_003b returns
    // the native transferred count. These literal bytes are canned input, not CNA output.
    provider.script.payload = {std::byte{0x12}, std::byte{0x34}, std::byte{0x56}, std::byte{0x78},
                               std::byte{0x9a}, std::byte{0xbc}, std::byte{0xde}, std::byte{0xf0}};
    std::vector<SharpRuntime::bytecs> buffer(100, 0xa5);
    EXPECT_EQ(mic.GetData(buffer, 10, 50), 8);
    EXPECT_EQ(provider.script.requestedBytes, 50U);
    const std::vector<SharpRuntime::bytecs> expectedBytes{0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};
    for (std::size_t i = 0; i < buffer.size(); ++i)
        EXPECT_EQ(buffer[i], i >= 10 && i < 18 ? expectedBytes[i - 10] : 0xa5) << i;
    const auto beforeEmptyRead = buffer;
    EXPECT_EQ(mic.GetData(buffer, 10, 50), 0);
    EXPECT_EQ(buffer, beforeEmptyRead);
    provider.script.payload = {std::byte{0x01}, std::byte{0x23}, std::byte{0x45}, std::byte{0x67}};
    // GetData(byte[]) IL_0023-IL_002e supplies offset=0 and count=Length.
    EXPECT_EQ(mic.GetData(buffer), 4);
    EXPECT_EQ(provider.script.requestedBytes, 100U);
    EXPECT_EQ(buffer[0], 0x01);
    EXPECT_EQ(buffer[1], 0x23);
    EXPECT_EQ(buffer[2], 0x45);
    EXPECT_EQ(buffer[3], 0x67);
    for (std::size_t i = 4; i < buffer.size(); ++i) EXPECT_EQ(buffer[i], beforeEmptyRead[i]);
    EXPECT_EQ(provider.script.reads, 3);
}

TEST(MicrophoneTest, ScriptedStopIsIdempotentAndLeavesTheNegotiatedRateAndUnreadBuffer)
{
    ScriptedRecordingProvider provider;
    provider.script.format.sampleRate = 48000;
    {
        auto mic = MicrophoneTestAccess::Make(1, "Lifecycle", &provider);
        for (int cycle = 1; cycle <= 2; ++cycle)
        {
            mic.Start();
            mic.Start();
            EXPECT_EQ(mic.getStateProperty(), MicrophoneState::Started);
            EXPECT_EQ(provider.script.created, cycle);
            EXPECT_EQ(provider.script.opened, cycle);
            mic.Stop();
            mic.Stop();
            EXPECT_EQ(mic.getStateProperty(), MicrophoneState::Stopped);
            EXPECT_EQ(provider.script.closed, cycle);
            EXPECT_EQ(provider.script.destroyed, cycle);
        }
        // Refresh GetData IL_00ab-IL_00b5 returns 0 when stopped. SampleRate is stored
        // in format (get_SampleRate IL_0011-IL_001c), so stopping does not reset it.
        provider.script.payload = {std::byte{0x12}, std::byte{0x34}};
        std::vector<SharpRuntime::bytecs> buffer(100, 0xa5);
        EXPECT_EQ(mic.GetData(buffer), 0);
        EXPECT_EQ(buffer, std::vector<SharpRuntime::bytecs>(100, 0xa5));
        EXPECT_EQ(provider.script.reads, 0);
        EXPECT_EQ(mic.getSampleRateProperty(), 48000);
        EXPECT_EQ(mic.Name, "Lifecycle");
    }
    EXPECT_EQ(provider.script.closed, 2);
    EXPECT_EQ(provider.script.destroyed, 2);
}

TEST(MicrophoneTest, ScriptedBufferReadyPinsThresholdRoundingSenderAndDrain)
{
    ScriptedRecordingProvider provider;
    provider.script.format.sampleRate = 48000;
    auto mic = MicrophoneTestAccess::Make(1, "Threshold", &provider);
    mic.Start();
    mic.setBufferDurationProperty(System::TimeSpan::FromMilliseconds(100));
    provider.script.queuedBytes = 9648;
    MicrophoneTestAccess::CheckBuffer(mic);
    EXPECT_EQ(provider.script.availabilityQueries, 0); // FNA Microphone.cs:212 short-circuit.
    int fired = 0;
    mic.BufferReady += [&](System::Object* sender, const System::EventArgs& args)
    {
        EXPECT_EQ(sender, static_cast<System::Object*>(&mic));
        EXPECT_EQ(&args, &System::EventArgs::Empty);
        ++fired;
    };
    // FNA Microphone.cs:210-216 supplies the portable strict '>' queue rule. Refresh
    // AudioFormat.DurationFromSize IL_0000-IL_001a supplies rounding; XNA OnBufferReady
    // IL_001b-IL_001e supplies sender=this, Collection.OnBufferReady IL_0024 supplies Empty.
    // 48kHz mono: 9598/2/48=99.979ms, 9600/2/48=100ms, 9646/2/48=100.479ms
    // all round to 100ms; 9648/2/48=100.5ms rounds away from zero to 101ms.
    for (const auto queued : {9598U, 9600U, 9646U})
    {
        provider.script.queuedBytes = queued;
        MicrophoneTestAccess::CheckBuffer(mic);
        EXPECT_EQ(fired, 0) << queued;
    }
    provider.script.queuedBytes = 9648;
    MicrophoneTestAccess::CheckBuffer(mic);
    EXPECT_EQ(fired, 1);
    MicrophoneTestAccess::CheckBuffer(mic);
    EXPECT_EQ(fired, 2); // Checking does not consume captured bytes (FNA fallback contract).
    provider.script.queuedBytes = 0;
    MicrophoneTestAccess::CheckBuffer(mic);
    EXPECT_EQ(fired, 2);
    mic.Stop();
    provider.script.queuedBytes = 9648;
    MicrophoneTestAccess::CheckBuffer(mic);
    EXPECT_EQ(fired, 2);
}

TEST(MicrophoneTest, ScopedDeviceListProvesEmptyDefaultIdentityAndCheckAllDispatch)
{
    ScriptedRecordingProvider firstProvider, secondProvider;
    auto first = MicrophoneTestAccess::Make(1, "First scripted device", &firstProvider);
    auto second = MicrophoneTestAccess::Make(2, "Second scripted device", &secondProvider);
    {
        MicrophoneTestAccess::ScopedList list({});
        EXPECT_TRUE(Microphone::getAllProperty().empty());
        EXPECT_EQ(Microphone::getDefaultProperty(), nullptr);
        Microphone::CheckAllBuffers();
    }
    {
        // Private seam changes only the enumerated list; every session is still canned.
        MicrophoneTestAccess::ScopedList list({&first, &second});
        const auto& all = Microphone::getAllProperty();
        ASSERT_EQ(all.size(), 2U);
        EXPECT_EQ(all[0], &first);
        EXPECT_EQ(all[1], &second);
        EXPECT_EQ(all[0]->Name, "First scripted device");
        EXPECT_EQ(all[1]->Name, "Second scripted device");
        // The platform contract orders the default first; this tests CNA's projection of it.
        EXPECT_EQ(Microphone::getDefaultProperty(), &first);
        first.Start();
        second.Start();
        first.setBufferDurationProperty(System::TimeSpan::FromMilliseconds(100));
        second.setBufferDurationProperty(System::TimeSpan::FromMilliseconds(100));
        firstProvider.script.queuedBytes = 88200; // 44100 mono PCM16 frames = 1 second.
        secondProvider.script.queuedBytes = 0;
        int firstEvents = 0, secondEvents = 0;
        first.BufferReady += [&](System::Object* sender, const System::EventArgs&)
        { EXPECT_EQ(sender, static_cast<System::Object*>(&first)); ++firstEvents; };
        second.BufferReady += [&](System::Object*, const System::EventArgs&) { ++secondEvents; };
        Microphone::CheckAllBuffers();
        EXPECT_EQ(firstEvents, 1);
        EXPECT_EQ(secondEvents, 0);
        firstProvider.script.queuedBytes = 0;
        secondProvider.script.queuedBytes = 88200;
        Microphone::CheckAllBuffers();
        EXPECT_EQ(firstEvents, 1);
        EXPECT_EQ(secondEvents, 1);
        first.BufferReady.Clear();
        second.BufferReady.Clear();
    }
}

// ===================== Real capture (SDL dummy driver opens a genuinely working device) =====================

// Unlike MakeMic() (an isolated instance with a scripted provider), the "Default
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

    std::vector<SharpRuntime::bytecs> buffer(100, static_cast<SharpRuntime::bytecs>(0xAB));
    EXPECT_EQ(mic_->GetData(buffer, 0, 100), 0);
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
