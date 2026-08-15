// SPDX-License-Identifier: MS-PL

#include "CNA/Audio/Platform/IAudioRecordingDevice.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using CNA::Audio::Platform::AudioFormat;
using CNA::Audio::Platform::AudioRecordingDeviceId;
using CNA::Audio::Platform::AudioRecordingDeviceInfo;
using CNA::Audio::Platform::AudioRecordingIoResult;
using CNA::Audio::Platform::AudioRecordingIoStatus;
using CNA::Audio::Platform::AudioSampleFormat;
using CNA::Audio::Platform::IAudioRecordingDevice;
using CNA::Audio::Platform::IAudioRecordingDeviceProvider;
using CNA::Audio::Platform::IsValid;

class ContractRecordingDevice final : public IAudioRecordingDevice
{
public:
    ContractRecordingDevice(AudioRecordingDeviceInfo info, AudioFormat negotiated,
                            std::vector<std::byte> captured)
        : info_(std::move(info)), negotiated_(negotiated), captured_(std::move(captured))
    {
    }

    [[nodiscard]] const AudioRecordingDeviceInfo& GetInfo() const noexcept override
    {
        return info_;
    }

    [[nodiscard]] bool IsConnected() const noexcept override
    {
        return connected_;
    }

    [[nodiscard]] AudioFormat Open(const AudioFormat& requested) override
    {
        if (open_)
        {
            throw std::logic_error("recording session is already open");
        }
        if (!connected_)
        {
            throw std::runtime_error("recording device is disconnected");
        }
        if (!IsValid(requested) || !IsValid(negotiated_))
        {
            throw std::invalid_argument("invalid recording format");
        }
        open_ = true;
        readOffset_ = 0;
        return negotiated_;
    }

    void Close() noexcept override
    {
        open_ = false;
        readOffset_ = 0;
    }

    [[nodiscard]] bool IsOpen() const noexcept override
    {
        return open_;
    }

    [[nodiscard]] AudioFormat GetFormat() const noexcept override
    {
        return open_ ? negotiated_ : AudioFormat{};
    }

    [[nodiscard]] AudioRecordingIoResult GetAvailableBytes() const noexcept override
    {
        if (!connected_)
        {
            return {AudioRecordingIoStatus::DeviceLost, 0};
        }
        if (!open_)
        {
            return {AudioRecordingIoStatus::Error, 0};
        }
        const std::size_t available = captured_.size() - readOffset_;
        return available > 0
            ? AudioRecordingIoResult{AudioRecordingIoStatus::Success, available}
            : AudioRecordingIoResult{AudioRecordingIoStatus::WouldBlock, 0};
    }

    [[nodiscard]] AudioRecordingIoResult Read(
        const std::span<std::byte> destination) noexcept override
    {
        if (destination.empty())
        {
            return {AudioRecordingIoStatus::WouldBlock, 0};
        }
        const AudioRecordingIoResult availability = GetAvailableBytes();
        if (availability.status != AudioRecordingIoStatus::Success)
        {
            return availability;
        }

        const std::size_t count = std::min(destination.size(), availability.byteCount);
        std::copy_n(captured_.begin() + static_cast<std::ptrdiff_t>(readOffset_), count,
                    destination.begin());
        readOffset_ += count;
        return {AudioRecordingIoStatus::Success, count};
    }

    void Disconnect() noexcept
    {
        connected_ = false;
    }

private:
    AudioRecordingDeviceInfo info_;
    AudioFormat negotiated_{};
    std::vector<std::byte> captured_;
    std::size_t readOffset_ = 0;
    bool connected_ = true;
    bool open_ = false;
};

class ContractRecordingProvider final : public IAudioRecordingDeviceProvider
{
public:
    explicit ContractRecordingProvider(std::vector<AudioRecordingDeviceInfo> devices)
        : devices_(std::move(devices))
    {
    }

    [[nodiscard]] std::vector<AudioRecordingDeviceInfo> GetDevices() const override
    {
        return devices_;
    }

    [[nodiscard]] std::unique_ptr<IAudioRecordingDevice> CreateDevice(
        const AudioRecordingDeviceId id) override
    {
        const auto found = std::find_if(devices_.begin(), devices_.end(),
                                        [id](const auto& info) { return info.id == id; });
        if (found == devices_.end())
        {
            return nullptr;
        }
        return std::make_unique<ContractRecordingDevice>(
            *found, AudioFormat{44100, 1, AudioSampleFormat::Signed16},
            std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}});
    }

private:
    std::vector<AudioRecordingDeviceInfo> devices_;
};

TEST(AudioRecordingDeviceContractTests, DeviceMetadataKeepsDefaultRouteDistinct)
{
    const AudioRecordingDeviceInfo defaultRoute{10, "Default input", true};
    const AudioRecordingDeviceInfo physical{11, "USB microphone", false};

    EXPECT_NE(defaultRoute, physical);
    EXPECT_TRUE(defaultRoute.isDefault);
    EXPECT_FALSE(physical.isDefault);
}

TEST(AudioRecordingDeviceContractTests, NullProviderIsTheUnsupportedCapabilityBoundary)
{
    IAudioRecordingDeviceProvider* unsupported = nullptr;
    EXPECT_EQ(unsupported, nullptr);

    ContractRecordingProvider supportedWithoutHardware({});
    EXPECT_TRUE(supportedWithoutHardware.GetDevices().empty());
}

TEST(AudioRecordingDeviceContractTests, ProviderEnumeratesAndCreatesIndependentClosedSession)
{
    const std::vector<AudioRecordingDeviceInfo> expected{
        {1, "Default input", true},
        {7, "Built-in microphone", false},
    };
    ContractRecordingProvider provider(expected);

    EXPECT_EQ(provider.GetDevices(), expected);
    std::unique_ptr<IAudioRecordingDevice> device = provider.CreateDevice(7);
    ASSERT_NE(device, nullptr);
    EXPECT_EQ(device->GetInfo(), expected[1]);
    EXPECT_TRUE(device->IsConnected());
    EXPECT_FALSE(device->IsOpen());
    EXPECT_FALSE(IsValid(device->GetFormat()));
    EXPECT_EQ(provider.CreateDevice(999), nullptr);
}

TEST(AudioRecordingDeviceContractTests, OpenReportsNegotiatedReadFormatAndCloseIsIdempotent)
{
    constexpr AudioFormat requested{48000, 2, AudioSampleFormat::Float32};
    constexpr AudioFormat negotiated{44100, 1, AudioSampleFormat::Signed16};
    ContractRecordingDevice device({3, "Capture", false}, negotiated, {});

    EXPECT_EQ(device.Open(requested), negotiated);
    EXPECT_TRUE(device.IsOpen());
    EXPECT_EQ(device.GetFormat(), negotiated);
    EXPECT_THROW(static_cast<void>(device.Open(requested)), std::logic_error);

    device.Close();
    device.Close();
    EXPECT_FALSE(device.IsOpen());
    EXPECT_FALSE(IsValid(device.GetFormat()));
}

TEST(AudioRecordingDeviceContractTests, AvailabilityDistinguishesNoDataClosedAndLostDevice)
{
    constexpr AudioFormat format{44100, 1, AudioSampleFormat::Signed16};
    ContractRecordingDevice device({3, "Capture", false}, format, {});

    EXPECT_EQ(device.GetAvailableBytes(),
              (AudioRecordingIoResult{AudioRecordingIoStatus::Error, 0}));
    ASSERT_EQ(device.Open(format), format);
    EXPECT_EQ(device.GetAvailableBytes(),
              (AudioRecordingIoResult{AudioRecordingIoStatus::WouldBlock, 0}));

    device.Disconnect();
    EXPECT_EQ(device.GetAvailableBytes(),
              (AudioRecordingIoResult{AudioRecordingIoStatus::DeviceLost, 0}));
}

TEST(AudioRecordingDeviceContractTests, ReadIsNonBlockingBoundedAndPreservesUnreadDestination)
{
    constexpr AudioFormat format{44100, 1, AudioSampleFormat::Signed16};
    ContractRecordingDevice device(
        {3, "Capture", false}, format,
        {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
    ASSERT_EQ(device.Open(format), format);

    std::vector<std::byte> destination(6, std::byte{0x7f});
    EXPECT_EQ(device.Read({}),
              (AudioRecordingIoResult{AudioRecordingIoStatus::WouldBlock, 0}));
    EXPECT_EQ(device.Read(std::span(destination).first(2)),
              (AudioRecordingIoResult{AudioRecordingIoStatus::Success, 2}));
    EXPECT_EQ(destination[0], std::byte{1});
    EXPECT_EQ(destination[1], std::byte{2});
    EXPECT_EQ(destination[2], std::byte{0x7f});
    EXPECT_EQ(device.GetAvailableBytes(),
              (AudioRecordingIoResult{AudioRecordingIoStatus::Success, 2}));

    EXPECT_EQ(device.Read(destination),
              (AudioRecordingIoResult{AudioRecordingIoStatus::Success, 2}));
    EXPECT_EQ(destination[0], std::byte{3});
    EXPECT_EQ(destination[1], std::byte{4});
    EXPECT_EQ(destination[2], std::byte{0x7f});
    EXPECT_EQ(device.Read(destination),
              (AudioRecordingIoResult{AudioRecordingIoStatus::WouldBlock, 0}));
}

TEST(AudioRecordingDeviceContractTests, InvalidOrDisconnectedOpenIsRefusedAndLeavesClosed)
{
    constexpr AudioFormat format{44100, 1, AudioSampleFormat::Signed16};
    ContractRecordingDevice device({3, "Capture", false}, format, {});

    EXPECT_THROW(static_cast<void>(device.Open(AudioFormat{})), std::invalid_argument);
    EXPECT_FALSE(device.IsOpen());
    device.Disconnect();
    EXPECT_THROW(static_cast<void>(device.Open(format)), std::runtime_error);
    EXPECT_FALSE(device.IsOpen());
}

} // namespace
