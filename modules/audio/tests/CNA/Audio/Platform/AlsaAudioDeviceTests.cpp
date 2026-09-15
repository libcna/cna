// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0151: the ALSA playback device, against ALSA's own `null` and `file`
// devices -- real libasound, real PCM configuration, real writes, and never a sound. `file`
// records exactly what the device handed ALSA, so what the callback produced can be compared with
// what came out, byte for byte.

#include <gtest/gtest.h>

#if defined(CNA_AUDIO_PLATFORM_ALSA)

#include "Platform/Alsa/AlsaAudioDevice.hpp"

#include "System/Environment.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace CNA::Audio::Platform;
using CNA::Audio::Platform::Alsa::AlsaAudioDevice;

/// Writes an ever-increasing 16-bit counter, and remembers everything it wrote.
class CountingCallback final : public IAudioBufferCallback
{
public:
    void FillBuffer(const std::span<std::byte> output, const std::size_t sampleCount) noexcept override
    {
        const std::lock_guard lock(mutex_);
        const auto* first = reinterpret_cast<const std::int16_t*>(output.data());
        auto* samples = reinterpret_cast<std::int16_t*>(output.data());
        for (std::size_t index = 0; index < sampleCount; ++index)
        {
            samples[index] = static_cast<std::int16_t>(next_++);
        }
        written_.insert(written_.end(), first, first + sampleCount);
        calls_.fetch_add(1, std::memory_order_release);
    }

    [[nodiscard]] std::vector<std::int16_t> Written() const
    {
        const std::lock_guard lock(mutex_);
        return written_;
    }

    [[nodiscard]] int Calls() const { return calls_.load(std::memory_order_acquire); }

private:
    mutable std::mutex mutex_;
    std::vector<std::int16_t> written_;
    std::uint16_t next_ = 1;
    std::atomic<int> calls_{0};
};

void SkipWithoutAlsa()
{
    if (!Alsa::IsAlsaAvailable())
    {
        GTEST_SKIP() << "libasound.so.2 is not installed";
    }
}

TEST(AlsaAudioDevice, OpensTheNullDeviceAndStopIsABarrier)
{
    SkipWithoutAlsa();
    AlsaAudioDevice device("null");
    auto callback = std::make_shared<CountingCallback>();
    const AudioFormat format = device.Open({44100, 2, AudioSampleFormat::Signed16}, callback);
    EXPECT_TRUE(IsValid(format));
    EXPECT_EQ(format.channels, 2);
    EXPECT_EQ(format.sampleRate, 44100u);
    EXPECT_EQ(format.sampleFormat, AudioSampleFormat::Signed16);
    EXPECT_GT(device.GetPeriodFrames(), 0u);
    EXPECT_TRUE(device.IsOpen());
    EXPECT_FALSE(device.IsRunning());
    EXPECT_EQ(callback->Calls(), 0);  // Opened paused.

    device.Start();
    device.Start();  // Idempotent.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (callback->Calls() < 3 && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_GE(callback->Calls(), 3);
    EXPECT_TRUE(device.IsRunning());

    device.Stop();
    const int afterStop = callback->Calls();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_EQ(callback->Calls(), afterStop) << "a callback ran after Stop() returned";
    EXPECT_FALSE(device.IsRunning());

    device.Start();  // And it starts again after a stop.
    while (callback->Calls() == afterStop && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_GT(callback->Calls(), afterStop);

    device.Close();
    device.Close();  // Idempotent.
    EXPECT_FALSE(device.IsOpen());
    EXPECT_FALSE(IsValid(device.GetFormat()));
}

TEST(AlsaAudioDevice, KeepsRealTimeOnADeviceWithNoClock)
{
    // ALSA's null device takes any amount at once. Unpaced, a 0.1 s sound would be over the
    // moment it started; paced, the callback is asked for about real time's worth.
    SkipWithoutAlsa();
    AlsaAudioDevice device("null");
    auto callback = std::make_shared<CountingCallback>();
    const AudioFormat format = device.Open({44100, 2, AudioSampleFormat::Signed16}, callback);
    const auto start = std::chrono::steady_clock::now();
    device.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    device.Stop();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double produced = static_cast<double>(callback->Written().size()) / format.channels / format.sampleRate;
    EXPECT_GT(produced, seconds * 0.5) << produced << " s of audio in " << seconds << " s";
    EXPECT_LT(produced, seconds * 2.0 + 0.1) << produced << " s of audio in " << seconds << " s";
}

TEST(AlsaAudioDevice, TheFileDeviceRecordsExactlyWhatTheCallbackProduced)
{
    SkipWithoutAlsa();
    const std::filesystem::path recording =
        std::filesystem::temp_directory_path() /
        ("cna-alsa-test-" + std::to_string(::getpid()) + ".raw");
    std::error_code error;
    std::filesystem::remove(recording, error);

    std::vector<std::int16_t> written;
    {
        AlsaAudioDevice device("file:FILE=" + recording.string() + ",FORMAT=raw");
        auto callback = std::make_shared<CountingCallback>();
        const AudioFormat format = device.Open({44100, 2, AudioSampleFormat::Signed16}, callback);
        ASSERT_EQ(format.sampleFormat, AudioSampleFormat::Signed16);
        device.Start();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (callback->Calls() < 5 && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        device.Close();
        written = callback->Written();
    }

    std::ifstream file(recording, std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::filesystem::remove(recording, error);
    ASSERT_GE(written.size(), 5u * 2u);
    ASSERT_FALSE(bytes.empty()) << "ALSA's file device recorded nothing";
    std::vector<std::int16_t> recorded(bytes.size() / sizeof(std::int16_t));
    std::memcpy(recorded.data(), bytes.data(), recorded.size() * sizeof(std::int16_t));

    // What ALSA was handed is a prefix of what the callback produced -- the last period may still
    // have been waiting when the device closed -- and it is that prefix exactly, in order.
    ASSERT_LE(recorded.size(), written.size());
    EXPECT_GE(recorded.size(), written.size() / 2);
    EXPECT_TRUE(std::equal(recorded.begin(), recorded.end(), written.begin()));
}

TEST(AlsaAudioDevice, AnUnknownDeviceFailsToOpenAndSaysWhich)
{
    SkipWithoutAlsa();
    AlsaAudioDevice device("cna_no_such_device");
    try
    {
        (void) device.Open({44100, 2, AudioSampleFormat::Signed16}, std::make_shared<CountingCallback>());
        FAIL() << "an unknown ALSA device opened";
    }
    catch (const std::runtime_error& failure)
    {
        EXPECT_NE(std::string(failure.what()).find("cna_no_such_device"), std::string::npos)
            << failure.what();
    }
    EXPECT_FALSE(device.IsOpen());
}

TEST(AlsaAudioDevice, InvalidRequestsAreRefused)
{
    SkipWithoutAlsa();
    AlsaAudioDevice device("null");
    EXPECT_THROW((void) device.Open({0, 2, AudioSampleFormat::Signed16}, std::make_shared<CountingCallback>()),
                 std::invalid_argument);
    EXPECT_THROW((void) device.Open({44100, 2, AudioSampleFormat::Signed16}, nullptr), std::invalid_argument);
    EXPECT_THROW(device.Start(), std::logic_error);
    (void) device.Open({44100, 2, AudioSampleFormat::Signed16}, std::make_shared<CountingCallback>());
    EXPECT_THROW((void) device.Open({44100, 2, AudioSampleFormat::Signed16}, std::make_shared<CountingCallback>()),
                 std::logic_error);
}

TEST(AlsaAudioDevice, TheDefaultDeviceNameComesFromTheEnvironment)
{
    using System::Environment;
    const std::optional<std::string> saved = Environment::GetEnvironmentVariable("CNA_AUDIO_DEVICE");
    Environment::SetEnvironmentVariable("CNA_AUDIO_DEVICE", std::string("null"));
    EXPECT_EQ(AlsaAudioDevice().GetDeviceName(), "null");
    Environment::SetEnvironmentVariable("CNA_AUDIO_DEVICE", std::nullopt);
    EXPECT_EQ(AlsaAudioDevice().GetDeviceName(), "default");
    Environment::SetEnvironmentVariable("CNA_AUDIO_DEVICE", std::string());
    EXPECT_EQ(AlsaAudioDevice().GetDeviceName(), "default");  // Set but empty is not a name.
    Environment::SetEnvironmentVariable("CNA_AUDIO_DEVICE", saved);
}

} // namespace

#endif // CNA_AUDIO_PLATFORM_ALSA
