// SPDX-License-Identifier: MS-PL

#include "CNA/Audio/Platform/IAudioDevice.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using CNA::Audio::Platform::AudioFormat;
using CNA::Audio::Platform::AudioSampleFormat;
using CNA::Audio::Platform::BytesPerSample;
using CNA::Audio::Platform::IAudioBufferCallback;
using CNA::Audio::Platform::IAudioDevice;
using CNA::Audio::Platform::IsValid;

class RecordingBufferCallback final : public IAudioBufferCallback
{
public:
    void FillBuffer(const std::span<std::byte> output,
                    const std::size_t sampleCount) noexcept override
    {
        ++callCount;
        lastSampleCount = sampleCount;
        lastOutputSize = output.size();
        std::fill(output.begin(), output.end(), std::byte{0x2a});
    }

    std::size_t callCount = 0;
    std::size_t lastSampleCount = 0;
    std::size_t lastOutputSize = 0;
};

// PLAT-99 adds the production NullAudioDevice and the shared conformance suite. This small
// contract double exists only to freeze PLAT-91's lifecycle, negotiation, and buffer granularity
// before either implementation is written.
class ContractAudioDevice final : public IAudioDevice
{
public:
    explicit ContractAudioDevice(AudioFormat negotiated)
        : negotiated_(negotiated)
    {
    }

    AudioFormat Open(const AudioFormat& requested,
                     std::shared_ptr<IAudioBufferCallback> callback) override
    {
        if (open_)
        {
            throw std::logic_error("audio device is already open");
        }
        if (!IsValid(requested) || !callback || !IsValid(negotiated_))
        {
            throw std::invalid_argument("invalid audio open request");
        }

        requested_ = requested;
        callback_ = std::move(callback);
        open_ = true;
        return negotiated_;
    }

    void Start() override
    {
        if (!open_)
        {
            throw std::logic_error("audio device is closed");
        }
        running_ = true;
    }

    void Stop() noexcept override
    {
        running_ = false;
    }

    void Close() noexcept override
    {
        Stop();
        open_ = false;
        callback_.reset();
    }

    [[nodiscard]] bool IsOpen() const noexcept override
    {
        return open_;
    }

    [[nodiscard]] bool IsRunning() const noexcept override
    {
        return running_;
    }

    [[nodiscard]] AudioFormat GetFormat() const noexcept override
    {
        return open_ ? negotiated_ : AudioFormat{};
    }

    void RequestBuffer(const std::size_t sampleCount)
    {
        if (!running_)
        {
            throw std::logic_error("audio device is not running");
        }
        if (sampleCount % negotiated_.channels != 0)
        {
            throw std::invalid_argument("sample count does not contain complete frames");
        }

        buffer_.assign(sampleCount * BytesPerSample(negotiated_.sampleFormat), std::byte{});
        callback_->FillBuffer(buffer_, sampleCount);
    }

    [[nodiscard]] const AudioFormat& RequestedFormat() const noexcept
    {
        return requested_;
    }

    [[nodiscard]] const std::vector<std::byte>& Buffer() const noexcept
    {
        return buffer_;
    }

private:
    AudioFormat negotiated_{};
    AudioFormat requested_{};
    std::shared_ptr<IAudioBufferCallback> callback_;
    std::vector<std::byte> buffer_;
    bool open_ = false;
    bool running_ = false;
};

TEST(AudioDeviceContractTests, SampleFormatsHaveStableScalarWidths)
{
    EXPECT_EQ(BytesPerSample(AudioSampleFormat::Unknown), 0u);
    EXPECT_EQ(BytesPerSample(AudioSampleFormat::Signed16), 2u);
    EXPECT_EQ(BytesPerSample(AudioSampleFormat::Float32), 4u);
}

TEST(AudioDeviceContractTests, FormatRequiresRateChannelsAndKnownRepresentation)
{
    EXPECT_TRUE(IsValid(AudioFormat{44100, 2, AudioSampleFormat::Signed16}));
    EXPECT_TRUE(IsValid(AudioFormat{48000, 1, AudioSampleFormat::Float32}));
    EXPECT_FALSE(IsValid(AudioFormat{0, 2, AudioSampleFormat::Signed16}));
    EXPECT_FALSE(IsValid(AudioFormat{44100, 0, AudioSampleFormat::Signed16}));
    EXPECT_FALSE(IsValid(AudioFormat{44100, 2, AudioSampleFormat::Unknown}));
}

TEST(AudioDeviceContractTests, OpenReportsNegotiatedFormatAndRetainsCallback)
{
    constexpr AudioFormat requested{44100, 2, AudioSampleFormat::Signed16};
    constexpr AudioFormat negotiated{48000, 2, AudioSampleFormat::Float32};
    ContractAudioDevice device(negotiated);
    auto callback = std::make_shared<RecordingBufferCallback>();
    std::weak_ptr<RecordingBufferCallback> lifetime = callback;

    EXPECT_EQ(device.Open(requested, callback), negotiated);
    EXPECT_TRUE(device.IsOpen());
    EXPECT_FALSE(device.IsRunning());
    EXPECT_EQ(device.GetFormat(), negotiated);
    EXPECT_EQ(device.RequestedFormat(), requested);

    callback.reset();
    EXPECT_FALSE(lifetime.expired());
    device.Close();
    EXPECT_TRUE(lifetime.expired());
    EXPECT_FALSE(device.IsOpen());
    EXPECT_FALSE(IsValid(device.GetFormat()));
}

TEST(AudioDeviceContractTests, OneDispatchFillsOneWholeInterleavedBuffer)
{
    constexpr AudioFormat format{48000, 2, AudioSampleFormat::Float32};
    ContractAudioDevice device(format);
    auto callback = std::make_shared<RecordingBufferCallback>();
    ASSERT_EQ(device.Open(format, callback), format);
    device.Start();

    constexpr std::size_t stereoFrames = 256;
    constexpr std::size_t scalarSamples = stereoFrames * format.channels;
    device.RequestBuffer(scalarSamples);

    EXPECT_EQ(callback->callCount, 1u);
    EXPECT_EQ(callback->lastSampleCount, scalarSamples);
    EXPECT_EQ(callback->lastOutputSize, scalarSamples * sizeof(float));
    EXPECT_EQ(device.Buffer().size(), scalarSamples * sizeof(float));
    EXPECT_TRUE(std::all_of(device.Buffer().begin(), device.Buffer().end(),
                            [](const std::byte value) { return value == std::byte{0x2a}; }));
}

TEST(AudioDeviceContractTests, InvalidOpenAndIncompleteFrameAreRefused)
{
    constexpr AudioFormat valid{44100, 2, AudioSampleFormat::Signed16};
    ContractAudioDevice device(valid);
    auto callback = std::make_shared<RecordingBufferCallback>();

    EXPECT_THROW(device.Open(AudioFormat{}, callback), std::invalid_argument);
    EXPECT_THROW(device.Open(valid, nullptr), std::invalid_argument);
    EXPECT_FALSE(device.IsOpen());

    ASSERT_EQ(device.Open(valid, callback), valid);
    EXPECT_THROW(device.Open(valid, callback), std::logic_error);
    EXPECT_THROW(device.RequestBuffer(4), std::logic_error);
    device.Start();
    EXPECT_THROW(device.RequestBuffer(3), std::invalid_argument);
    EXPECT_EQ(callback->callCount, 0u);
}

TEST(AudioDeviceContractTests, CloseIsIdempotentAndStopsFutureCallbacks)
{
    constexpr AudioFormat format{44100, 1, AudioSampleFormat::Signed16};
    ContractAudioDevice device(format);
    auto callback = std::make_shared<RecordingBufferCallback>();
    ASSERT_EQ(device.Open(format, callback), format);
    device.Start();
    device.RequestBuffer(32);

    device.Stop();
    device.Stop();
    EXPECT_TRUE(device.IsOpen());
    EXPECT_FALSE(device.IsRunning());
    EXPECT_THROW(device.RequestBuffer(32), std::logic_error);

    device.Start();
    device.RequestBuffer(32);

    device.Close();
    device.Close();

    EXPECT_FALSE(device.IsOpen());
    EXPECT_THROW(device.RequestBuffer(32), std::logic_error);
    EXPECT_EQ(callback->callCount, 2u);
}

} // namespace
