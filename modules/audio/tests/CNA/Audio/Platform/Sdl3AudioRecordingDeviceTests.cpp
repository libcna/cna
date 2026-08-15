// SPDX-License-Identifier: MS-PL

#include "Platform/Sdl3/Sdl3AudioRecordingDevice.hpp"

#include "System/Environment.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using CNA::Audio::Platform::AudioFormat;
using CNA::Audio::Platform::AudioRecordingIoStatus;
using CNA::Audio::Platform::AudioSampleFormat;
using CNA::Audio::Platform::IsValid;
using CNA::Audio::Platform::Sdl3::Sdl3AudioRecordingDeviceProvider;

class Sdl3AudioRecordingDeviceTests : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    }
};

TEST_F(Sdl3AudioRecordingDeviceTests, ProviderEnumeratesDefaultFirstAndSortedPhysicalDevices)
{
    Sdl3AudioRecordingDeviceProvider provider;
    const auto devices = provider.GetDevices();
    ASSERT_GE(devices.size(), 2u);
    EXPECT_TRUE(devices.front().isDefault);
    EXPECT_EQ(devices.front().name, "Default Device");
    EXPECT_EQ(std::count_if(devices.begin(), devices.end(), [](const auto& info)
    {
        return info.isDefault;
    }), 1);
    EXPECT_TRUE(std::is_sorted(devices.begin() + 1, devices.end(), [](const auto& left,
                                                                     const auto& right)
    {
        return left.id < right.id;
    }));
    EXPECT_EQ(provider.CreateDevice(0), nullptr);
}

TEST_F(Sdl3AudioRecordingDeviceTests, OpenStartsCaptureAndCloseIsIdempotent)
{
    Sdl3AudioRecordingDeviceProvider provider;
    const auto devices = provider.GetDevices();
    ASSERT_FALSE(devices.empty());
    auto device = provider.CreateDevice(devices.front().id);
    ASSERT_NE(device, nullptr);

    constexpr AudioFormat requested{44100, 1, AudioSampleFormat::Signed16};
    EXPECT_TRUE(device->IsConnected());
    EXPECT_FALSE(device->IsOpen());
    EXPECT_FALSE(IsValid(device->GetFormat()));
    EXPECT_EQ(device->Open(requested), requested);
    EXPECT_TRUE(device->IsOpen());
    EXPECT_EQ(device->GetFormat(), requested);

    device->Close();
    device->Close();
    EXPECT_FALSE(device->IsOpen());
    EXPECT_FALSE(IsValid(device->GetFormat()));
}

TEST_F(Sdl3AudioRecordingDeviceTests, CaptureReadIsNonBlockingBoundedAndPreservesSuffix)
{
    Sdl3AudioRecordingDeviceProvider provider;
    const auto devices = provider.GetDevices();
    ASSERT_FALSE(devices.empty());
    auto device = provider.CreateDevice(devices.front().id);
    ASSERT_NE(device, nullptr);
    ASSERT_EQ(device->Open({44100, 1, AudioSampleFormat::Signed16}),
              (AudioFormat{44100, 1, AudioSampleFormat::Signed16}));

    EXPECT_EQ(device->Read({}).status, AudioRecordingIoStatus::WouldBlock);
    std::vector<std::byte> destination(4100, std::byte{0x7f});
    std::size_t read = 0;
    for (int attempt = 0; attempt < 20 && read == 0; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        const auto available = device->GetAvailableBytes();
        if (available.status == AudioRecordingIoStatus::Success)
        {
            const auto result = device->Read(std::span(destination).first(4096));
            ASSERT_EQ(result.status, AudioRecordingIoStatus::Success);
            read = result.byteCount;
        }
        else
        {
            ASSERT_EQ(available.status, AudioRecordingIoStatus::WouldBlock);
        }
    }

    ASSERT_GT(read, 0u);
    EXPECT_LE(read, 4096u);
    EXPECT_EQ(destination[4096], std::byte{0x7f});
    EXPECT_EQ(destination[4097], std::byte{0x7f});
    EXPECT_EQ(destination[4098], std::byte{0x7f});
    EXPECT_EQ(destination[4099], std::byte{0x7f});
}

TEST_F(Sdl3AudioRecordingDeviceTests, InvalidAndDuplicateOpenAreRefusedWithoutBreakingClose)
{
    Sdl3AudioRecordingDeviceProvider provider;
    const auto devices = provider.GetDevices();
    ASSERT_FALSE(devices.empty());
    auto device = provider.CreateDevice(devices.front().id);
    ASSERT_NE(device, nullptr);

    EXPECT_THROW(static_cast<void>(device->Open(AudioFormat{})), std::invalid_argument);
    EXPECT_FALSE(device->IsOpen());
    EXPECT_NO_THROW(static_cast<void>(
        device->Open({44100, 1, AudioSampleFormat::Signed16})));
    EXPECT_THROW(static_cast<void>(
        device->Open({44100, 1, AudioSampleFormat::Signed16})), std::logic_error);
    EXPECT_NO_THROW(device->Close());
}

} // namespace
