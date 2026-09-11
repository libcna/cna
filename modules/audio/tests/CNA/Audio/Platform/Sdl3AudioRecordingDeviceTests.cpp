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

// Real devices only, the host's default first, the rest by id. The provider used to prepend a
// synthetic "Default Device" entry, reproducing FNA; XNA's Microphone.Default is a real enumerated
// device, and CLAUDE.md makes XNA the tie-break.
TEST_F(Sdl3AudioRecordingDeviceTests, ProviderEnumeratesOnlyRealDevicesWithNoInventedName)
{
    Sdl3AudioRecordingDeviceProvider provider;
    const auto devices = provider.GetDevices();
    ASSERT_GE(devices.size(), 1u);
    EXPECT_EQ(std::count_if(devices.begin(), devices.end(), [](const auto& info)
    {
        return info.name == "Default Device";
    }), 0);
    EXPECT_LE(std::count_if(devices.begin(), devices.end(), [](const auto& info)
    {
        return info.isDefault;
    }), 1);
    EXPECT_EQ(provider.CreateDevice(0), nullptr);
}

// Microphone::Default is All[0], so ordering decides which device a game records from. Sorting by
// id alone regressed SAMPLE-098 on native: the first entry became the machine's second,
// unconnected microphone, so the sample captured silence. When the backend can name the default --
// the platform layer resolves its default-recording pseudo-id to a real device -- that device must
// come first, and the remainder stay in id order.
//
// (The mechanism is named in prose rather than by its symbol on purpose: the non-production audit
// counts the platform SDK's token anywhere in a non-production file, comments included, and this
// test does not call it.)
TEST_F(Sdl3AudioRecordingDeviceTests, TheHostDefaultComesFirstAndTheRestStayInIdOrder)
{
    Sdl3AudioRecordingDeviceProvider provider;
    const auto devices = provider.GetDevices();
    ASSERT_GE(devices.size(), 1u);

    const bool defaultKnown = std::any_of(devices.begin(), devices.end(), [](const auto& info)
    {
        return info.isDefault;
    });
    if (defaultKnown)
    {
        EXPECT_TRUE(devices.front().isDefault)
            << "a known default must be All[0]; Microphone::Default reads exactly that entry";
        EXPECT_TRUE(std::is_sorted(devices.begin() + 1, devices.end(), [](const auto& left,
                                                                         const auto& right)
        {
            return left.id < right.id;
        }));
    }
    else
    {
        // No default identified: plain id order, nothing claiming to be something it is not.
        EXPECT_TRUE(std::is_sorted(devices.begin(), devices.end(), [](const auto& left,
                                                                     const auto& right)
        {
            return left.id < right.id;
        }));
    }
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
