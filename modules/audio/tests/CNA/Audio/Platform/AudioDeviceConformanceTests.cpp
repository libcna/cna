// SPDX-License-Identifier: MS-PL

#include "CNA/Audio/Platform/IAudioDevice.hpp"
#include "Platform/Null/NullAudioDevice.hpp"

#if defined(CNA_AUDIO_PLATFORM_SDL3)
#include "Platform/Sdl3/Sdl3AudioDevice.hpp"

#include "System/Environment.hpp"
#endif

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using CNA::Audio::Platform::AudioFormat;
using CNA::Audio::Platform::AudioSampleFormat;
using CNA::Audio::Platform::BytesPerSample;
using CNA::Audio::Platform::IAudioBufferCallback;
using CNA::Audio::Platform::IAudioDevice;
using CNA::Audio::Platform::IsValid;
using CNA::Audio::Platform::Null::NullAudioDevice;

enum class AudioDeviceImplementation
{
    Null,
#if defined(CNA_AUDIO_PLATFORM_SDL3)
    Sdl3,
#endif
};

struct AudioDeviceCase
{
    AudioDeviceImplementation implementation;
    const char* name;
};

std::unique_ptr<IAudioDevice> CreateDevice(const AudioDeviceImplementation implementation)
{
    switch (implementation)
    {
        case AudioDeviceImplementation::Null:
            return std::make_unique<NullAudioDevice>();
#if defined(CNA_AUDIO_PLATFORM_SDL3)
        case AudioDeviceImplementation::Sdl3:
            return std::make_unique<CNA::Audio::Platform::Sdl3::Sdl3AudioDevice>();
#endif
    }
    throw std::logic_error("unknown audio device conformance implementation");
}

std::vector<AudioDeviceCase> GetAudioDeviceCases()
{
    std::vector<AudioDeviceCase> result{{AudioDeviceImplementation::Null, "NULL"}};
#if defined(CNA_AUDIO_PLATFORM_SDL3)
    result.push_back({AudioDeviceImplementation::Sdl3, "SDL3"});
#endif
    return result;
}

class ConformanceBufferCallback final : public IAudioBufferCallback
{
public:
    void FillBuffer(const std::span<std::byte> output,
                    const std::size_t sampleCount) noexcept override
    {
        calls.fetch_add(1, std::memory_order_relaxed);
        samples.fetch_add(sampleCount, std::memory_order_relaxed);
        if (sampleCount % channels != 0
            || output.size() != sampleCount * BytesPerSample(sampleFormat))
        {
            invalidGeometry.store(true, std::memory_order_release);
        }
        std::fill(output.begin(), output.end(), std::byte{0x31});
    }

    std::size_t channels = 2;
    AudioSampleFormat sampleFormat = AudioSampleFormat::Signed16;
    std::atomic<std::size_t> calls{0};
    std::atomic<std::size_t> samples{0};
    std::atomic<bool> invalidGeometry{false};
};

bool WaitForCalls(const ConformanceBufferCallback& callback, const std::size_t minimum)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (callback.calls.load(std::memory_order_acquire) >= minimum)
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return callback.calls.load(std::memory_order_acquire) >= minimum;
}

class AudioDeviceConformanceTests : public ::testing::TestWithParam<AudioDeviceCase>
{
protected:
    static void SetUpTestSuite()
    {
#if defined(CNA_AUDIO_PLATFORM_SDL3)
        System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
#endif
    }
};

TEST_P(AudioDeviceConformanceTests, OpenNegotiatesAndRetainsCallbackWithoutStarting)
{
    constexpr AudioFormat requested{48000, 2, AudioSampleFormat::Float32};
    auto callback = std::make_shared<ConformanceBufferCallback>();
    callback->sampleFormat = requested.sampleFormat;
    std::weak_ptr<ConformanceBufferCallback> lifetime = callback;
    auto device = CreateDevice(GetParam().implementation);

    EXPECT_EQ(device->Open(requested, callback), requested);
    EXPECT_TRUE(device->IsOpen());
    EXPECT_FALSE(device->IsRunning());
    EXPECT_EQ(device->GetFormat(), requested);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_EQ(callback->calls.load(std::memory_order_acquire), 0u);

    callback.reset();
    EXPECT_FALSE(lifetime.expired());
    device->Close();
    EXPECT_TRUE(lifetime.expired());
    EXPECT_FALSE(device->IsOpen());
    EXPECT_FALSE(IsValid(device->GetFormat()));
}

TEST_P(AudioDeviceConformanceTests, StartStopAndCloseFormCallbackBarriers)
{
    constexpr AudioFormat requested{44100, 2, AudioSampleFormat::Signed16};
    auto callback = std::make_shared<ConformanceBufferCallback>();
    auto device = CreateDevice(GetParam().implementation);
    ASSERT_EQ(device->Open(requested, callback), requested);

    device->Start();
    device->Start();
    ASSERT_TRUE(WaitForCalls(*callback, 2));
    EXPECT_TRUE(device->IsRunning());

    device->Stop();
    device->Stop();
    const std::size_t stoppedCalls = callback->calls.load(std::memory_order_acquire);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    EXPECT_EQ(callback->calls.load(std::memory_order_acquire), stoppedCalls);
    EXPECT_GT(callback->samples.load(std::memory_order_acquire), 0u);
    EXPECT_FALSE(callback->invalidGeometry.load(std::memory_order_acquire));
    EXPECT_TRUE(device->IsOpen());
    EXPECT_FALSE(device->IsRunning());

    device->Start();
    ASSERT_TRUE(WaitForCalls(*callback, stoppedCalls + 1));
    device->Close();
    const std::size_t closedCalls = callback->calls.load(std::memory_order_acquire);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    EXPECT_EQ(callback->calls.load(std::memory_order_acquire), closedCalls);
}

TEST_P(AudioDeviceConformanceTests, InvalidDuplicateAndClosedOperationsAreRefused)
{
    constexpr AudioFormat requested{44100, 1, AudioSampleFormat::Signed16};
    auto callback = std::make_shared<ConformanceBufferCallback>();
    callback->channels = requested.channels;
    auto device = CreateDevice(GetParam().implementation);

    EXPECT_THROW(static_cast<void>(device->Open(AudioFormat{}, callback)),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(device->Open(requested, nullptr)),
                 std::invalid_argument);
    EXPECT_THROW(device->Start(), std::logic_error);
    EXPECT_FALSE(device->IsOpen());

    ASSERT_EQ(device->Open(requested, callback), requested);
    EXPECT_THROW(static_cast<void>(device->Open(requested, callback)), std::logic_error);
    device->Close();
    device->Close();
    EXPECT_THROW(device->Start(), std::logic_error);
    EXPECT_FALSE(device->IsOpen());
    EXPECT_FALSE(device->IsRunning());
}

INSTANTIATE_TEST_SUITE_P(
    AvailableAudioDevices,
    AudioDeviceConformanceTests,
    ::testing::ValuesIn(GetAudioDeviceCases()),
    [](const ::testing::TestParamInfo<AudioDeviceCase>& info)
    {
        return std::string(info.param.name);
    });

TEST(NullAudioDeviceTests, WorkerIsPacedInsteadOfSpinningAsFastAsPossible)
{
    constexpr AudioFormat requested{44100, 2, AudioSampleFormat::Signed16};
    auto callback = std::make_shared<ConformanceBufferCallback>();
    NullAudioDevice device;
    ASSERT_EQ(device.Open(requested, callback), requested);

    device.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    device.Stop();

    const std::size_t calls = callback->calls.load(std::memory_order_acquire);
    EXPECT_GT(calls, 0u);
    EXPECT_LT(calls, 50u) << "NULL playback must model device pacing, not busy-loop";
}

} // namespace
