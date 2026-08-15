// SPDX-License-Identifier: MS-PL

#include "Platform/Sdl2/Sdl2AudioDevice.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <thread>

namespace {

using CNA::Audio::Platform::AudioFormat;
using CNA::Audio::Platform::AudioSampleFormat;
using CNA::Audio::Platform::IAudioBufferCallback;
using CNA::Audio::Platform::Sdl2::Sdl2AudioDevice;

class CountingCallback final : public IAudioBufferCallback
{
public:
    void FillBuffer(const std::span<std::byte> output, std::size_t) noexcept override
    {
        calls.fetch_add(1, std::memory_order_release);
        std::fill(output.begin(), output.end(), std::byte{});
    }

    std::atomic<int> calls{0};
};

TEST(Sdl2AudioDeviceTests, OpenIsPausedAndStopIsACallbackBarrier)
{
    // CnaAudioPlatformTests supplies SDL_AUDIODRIVER=dummy, so this verifies SDL2's real
    // callback/lifecycle path without requiring a host sound device.
    Sdl2AudioDevice device;
    auto callback = std::make_shared<CountingCallback>();
    const AudioFormat requested{44100, 2, AudioSampleFormat::Signed16};

    EXPECT_EQ(device.Open(requested, callback), requested);
    EXPECT_TRUE(device.IsOpen());
    EXPECT_FALSE(device.IsRunning());
    EXPECT_EQ(callback->calls.load(std::memory_order_acquire), 0);

    device.Start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (callback->calls.load(std::memory_order_acquire) == 0
           && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_GT(callback->calls.load(std::memory_order_acquire), 0);

    device.Stop();
    const int callsAfterStop = callback->calls.load(std::memory_order_acquire);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_EQ(callback->calls.load(std::memory_order_acquire), callsAfterStop);
    EXPECT_FALSE(device.IsRunning());

    device.Close();
    EXPECT_FALSE(device.IsOpen());
}

} // namespace
