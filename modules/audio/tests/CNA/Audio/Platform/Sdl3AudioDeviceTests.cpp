// SPDX-License-Identifier: MS-PL

#include "Platform/Sdl3/Sdl3AudioDevice.hpp"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace {

using CNA::Audio::Platform::AudioFormat;
using CNA::Audio::Platform::AudioSampleFormat;
using CNA::Audio::Platform::BytesPerSample;
using CNA::Audio::Platform::IAudioBufferCallback;
using CNA::Audio::Platform::IsValid;
using CNA::Audio::Platform::Sdl3::Sdl3AudioDevice;

class AtomicBufferCallback final : public IAudioBufferCallback
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
        std::fill(output.begin(), output.end(), std::byte{0x19});
    }

    std::size_t channels = 2;
    AudioSampleFormat sampleFormat = AudioSampleFormat::Signed16;
    std::atomic<std::size_t> calls{0};
    std::atomic<std::size_t> samples{0};
    std::atomic<bool> invalidGeometry{false};
};

class Sdl3AudioDeviceTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // A dedicated CTest already supplies SDL_AUDIODRIVER=dummy. Direct CnaTests runs need
        // the same deterministic edge when no earlier test owns the process-global subsystem.
        if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0)
        {
            const char* previous = SDL_getenv("SDL_AUDIODRIVER");
            if (previous)
            {
                previousDriver_ = previous;
            }
            SDL_SetEnvironmentVariable(
                SDL_GetEnvironment(), "SDL_AUDIODRIVER", "dummy", true);
            changedDriver_ = true;
        }
    }

    void TearDown() override
    {
        if (!changedDriver_)
        {
            return;
        }
        if (previousDriver_)
        {
            SDL_SetEnvironmentVariable(
                SDL_GetEnvironment(), "SDL_AUDIODRIVER", previousDriver_->c_str(), true);
        }
        else
        {
            SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "SDL_AUDIODRIVER");
        }
    }

    static bool WaitForCalls(const AtomicBufferCallback& callback,
                             const std::size_t minimum)
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

private:
    std::optional<std::string> previousDriver_;
    bool changedDriver_ = false;
};

TEST_F(Sdl3AudioDeviceTests, OpenNegotiatesRequestedApplicationFormatAndRemainsPaused)
{
    constexpr AudioFormat requested{48000, 2, AudioSampleFormat::Float32};
    auto callback = std::make_shared<AtomicBufferCallback>();
    callback->sampleFormat = requested.sampleFormat;
    callback->channels = requested.channels;
    Sdl3AudioDevice device;

    EXPECT_EQ(device.Open(requested, callback), requested);
    EXPECT_TRUE(device.IsOpen());
    EXPECT_FALSE(device.IsRunning());
    EXPECT_EQ(device.GetFormat(), requested);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_EQ(callback->calls.load(std::memory_order_acquire), 0u);
    EXPECT_FALSE(device.HasStreamError());
}

TEST_F(Sdl3AudioDeviceTests, StartFeedsCompleteFramesAndStopIsACallbackBarrier)
{
    constexpr AudioFormat requested{44100, 2, AudioSampleFormat::Signed16};
    auto callback = std::make_shared<AtomicBufferCallback>();
    callback->sampleFormat = requested.sampleFormat;
    callback->channels = requested.channels;
    Sdl3AudioDevice device;
    ASSERT_EQ(device.Open(requested, callback), requested);

    device.Start();
    EXPECT_TRUE(device.IsRunning());
    ASSERT_TRUE(WaitForCalls(*callback, 2));

    device.Stop();
    EXPECT_TRUE(device.IsOpen());
    EXPECT_FALSE(device.IsRunning());
    const std::size_t stoppedCalls = callback->calls.load(std::memory_order_acquire);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    EXPECT_EQ(callback->calls.load(std::memory_order_acquire), stoppedCalls);
    EXPECT_GT(callback->samples.load(std::memory_order_acquire), 0u);
    EXPECT_FALSE(callback->invalidGeometry.load(std::memory_order_acquire));
    EXPECT_FALSE(device.HasStreamError());

    device.Start();
    EXPECT_TRUE(WaitForCalls(*callback, stoppedCalls + 1));
}

TEST_F(Sdl3AudioDeviceTests, InvalidAndDuplicateOpenFailWithoutDamagingLifecycle)
{
    constexpr AudioFormat requested{44100, 1, AudioSampleFormat::Signed16};
    auto callback = std::make_shared<AtomicBufferCallback>();
    callback->channels = requested.channels;
    Sdl3AudioDevice device;

    EXPECT_THROW((void)device.Open(AudioFormat{}, callback), std::invalid_argument);
    EXPECT_THROW((void)device.Open(requested, nullptr), std::invalid_argument);
    EXPECT_FALSE(device.IsOpen());
    EXPECT_THROW(device.Start(), std::logic_error);

    ASSERT_EQ(device.Open(requested, callback), requested);
    EXPECT_THROW((void)device.Open(requested, callback), std::logic_error);
    device.Close();
    device.Close();
    EXPECT_FALSE(device.IsOpen());
    EXPECT_FALSE(device.IsRunning());
    EXPECT_FALSE(IsValid(device.GetFormat()));
}

TEST_F(Sdl3AudioDeviceTests, CloseWaitsForCallbacksAndReleasesCallbackOwnership)
{
    constexpr AudioFormat requested{48000, 2, AudioSampleFormat::Float32};
    auto callback = std::make_shared<AtomicBufferCallback>();
    callback->sampleFormat = requested.sampleFormat;
    std::weak_ptr<AtomicBufferCallback> lifetime = callback;
    Sdl3AudioDevice device;
    ASSERT_EQ(device.Open(requested, callback), requested);
    callback.reset();

    device.Start();
    ASSERT_FALSE(lifetime.expired());
    ASSERT_TRUE(WaitForCalls(*lifetime.lock(), 1));
    device.Close();

    EXPECT_TRUE(lifetime.expired());
    EXPECT_FALSE(device.IsOpen());
}

} // namespace
