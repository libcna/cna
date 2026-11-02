// SPDX-License-Identifier: MS-PL

#include "CNA/Audio/Platform/IAudioDevice.hpp"
#include "Platform/Null/NullAudioDevice.hpp"

#if defined(CNA_AUDIO_PLATFORM_SDL3)
#include "Platform/Sdl3/Sdl3AudioDevice.hpp"

#include "System/Environment.hpp"
#endif
#if defined(CNA_AUDIO_PLATFORM_ALSA)
#include "Platform/Alsa/AlsaAudioDevice.hpp"
#endif

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <semaphore>
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
#if defined(CNA_AUDIO_PLATFORM_ALSA)
    Alsa,
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
#if defined(CNA_AUDIO_PLATFORM_ALSA)
        case AudioDeviceImplementation::Alsa:
            // ALSA's own silent device: the conformance contract, with no sound and no card.
            return std::make_unique<CNA::Audio::Platform::Alsa::AlsaAudioDevice>("null");
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
#if defined(CNA_AUDIO_PLATFORM_ALSA)
    if (CNA::Audio::Platform::Alsa::IsAlsaAvailable())
    {
        result.push_back({AudioDeviceImplementation::Alsa, "ALSA"});
    }
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

// Only the silent conformance devices use this gate. Holding one callback open deliberately
// forces the lifecycle barrier to meet an in-flight buffer instead of relying on a busy loop.
class GatedBufferCallback final : public IAudioBufferCallback
{
public:
    void FillBuffer(const std::span<std::byte> output, const std::size_t) noexcept override
    {
        if (!entered.exchange(true))
        {
            active.store(true);
            inside.release();
            if (!release.try_acquire_for(std::chrono::seconds(5))) timedOut.store(true);
            active.store(false);
        }
        if (barrierReturned.load()) lateCalls.fetch_add(1);
        calls.fetch_add(1);
        std::fill(output.begin(), output.end(), std::byte{});
    }

    std::binary_semaphore inside{0};
    std::binary_semaphore release{0};
    std::atomic<bool> entered{false}, active{false}, timedOut{false}, barrierReturned{false};
    std::atomic<unsigned> calls{0}, lateCalls{0};
};

void ExerciseInFlightBarrier(const AudioDeviceCase deviceCase, const bool close)
{
    constexpr AudioFormat requested{44100, 2, AudioSampleFormat::Signed16};
    auto callback = std::make_shared<GatedBufferCallback>();
    auto device = CreateDevice(deviceCase.implementation);
    ASSERT_EQ(device->Open(requested, callback), requested);
    device->Start();
    ASSERT_TRUE(callback->inside.try_acquire_for(std::chrono::seconds(2)));

    std::promise<void> started, finished;
    auto startedFuture = started.get_future();
    auto finishedFuture = finished.get_future();
    std::atomic<bool> returnedWhileActive{false};
    std::thread lifecycle([&]
    {
        started.set_value();
        if (close) device->Close();
        else device->Stop();
        returnedWhileActive.store(callback->active.load());
        callback->barrierReturned.store(true);
        finished.set_value();
    });
    // A failed join must fail the process, never detach a worker retaining test-stack data.
    const auto requireReady = [](auto& future)
    {
        if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        {
            std::fputs("audio lifecycle barrier exceeded five seconds\n", stderr);
            std::abort();
        }
    };
    requireReady(startedFuture);
    EXPECT_EQ(finishedFuture.wait_for(std::chrono::milliseconds(50)),
              std::future_status::timeout) << "barrier returned before releasing the callback";
    callback->release.release();
    requireReady(finishedFuture);
    lifecycle.join();
    EXPECT_FALSE(returnedWhileActive.load());
    EXPECT_FALSE(callback->timedOut.load());
    EXPECT_FALSE(callback->active.load());
    EXPECT_FALSE(device->IsRunning());

    const auto calls = callback->calls.load();
    // Keep the retired callback alive across a new device generation. That generation must
    // invoke only its newly published callback, even if a native demand was pending at Close.
    device->Close();
    EXPECT_EQ(callback.use_count(), 1) << "Close must release the device's callback ownership";
    auto replacement = std::make_shared<ConformanceBufferCallback>();
    ASSERT_EQ(device->Open(requested, replacement), requested);
    device->Start();
    EXPECT_TRUE(WaitForCalls(*replacement, 2));
    device->Close();
    EXPECT_EQ(callback->calls.load(), calls);
    EXPECT_EQ(callback->lateCalls.load(), 0u);
    std::weak_ptr<GatedBufferCallback> retired = callback;
    callback.reset();
    EXPECT_TRUE(retired.expired());
}

TEST_P(AudioDeviceConformanceTests, StopWaitsForInFlightCallbackBeforeReturning)
{
    ExerciseInFlightBarrier(GetParam(), false);
}

TEST_P(AudioDeviceConformanceTests, CloseWaitsForInFlightCallbackBeforeReleasingOwnership)
{
    ExerciseInFlightBarrier(GetParam(), true);
}

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
