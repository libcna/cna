// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0162: capture through ALSA, against ALSA's own `null` device and its `file`
// device reading a known tone -- real libasound, real capture streams, and never a microphone.
// Every session here opens a PCM the test names; the one test that asks what the machine has only
// enumerates.

#include <gtest/gtest.h>

#if defined(CNA_AUDIO_PLATFORM_ALSA)

#include "Platform/Alsa/AlsaAudioRecordingDevice.hpp"

#include "System/Environment.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace CNA::Audio::Platform;
using CNA::Audio::Platform::Alsa::AlsaAudioRecordingDevice;
using CNA::Audio::Platform::Alsa::AlsaAudioRecordingDeviceProvider;

/// Sets one environment variable for a scope, or removes it.
class ScopedVariable
{
public:
    ScopedVariable(std::string name, std::optional<std::string> value)
        : name_(std::move(name)), saved_(System::Environment::GetEnvironmentVariable(name_))
    {
        System::Environment::SetEnvironmentVariable(name_, value);
    }
    ~ScopedVariable() { System::Environment::SetEnvironmentVariable(name_, saved_); }
    ScopedVariable(const ScopedVariable&) = delete;
    ScopedVariable& operator=(const ScopedVariable&) = delete;

private:
    std::string name_;
    std::optional<std::string> saved_;
};

constexpr AudioFormat kMono16{44100, 1, AudioSampleFormat::Signed16};

/// Takes what the session queued until @p bytes are in hand or @p budget runs out.
std::vector<std::byte> Collect(IAudioRecordingDevice& device, const std::size_t bytes,
                               const std::chrono::milliseconds budget)
{
    std::vector<std::byte> collected;
    std::vector<std::byte> block(4096);
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (collected.size() < bytes && std::chrono::steady_clock::now() < deadline)
    {
        const AudioRecordingIoResult result = device.Read(block);
        if (result.status == AudioRecordingIoStatus::Success)
        {
            collected.insert(collected.end(), block.begin(),
                             block.begin() + static_cast<std::ptrdiff_t>(result.byteCount));
            continue;
        }
        EXPECT_EQ(result.status, AudioRecordingIoStatus::WouldBlock);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return collected;
}

TEST(AlsaAudioRecordingDevice, TheConfiguredDeviceIsTheOneDefaultEntry)
{
    const ScopedVariable configured("CNA_AUDIO_RECORDING_DEVICE", std::string("null"));
    AlsaAudioRecordingDeviceProvider provider;
    const std::vector<AudioRecordingDeviceInfo> devices = provider.GetDevices();
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].name, "null");
    EXPECT_TRUE(devices[0].isDefault);
    std::unique_ptr<IAudioRecordingDevice> device = provider.CreateDevice(devices[0].id);
    ASSERT_NE(device, nullptr);
    EXPECT_EQ(device->GetInfo(), devices[0]);
    EXPECT_FALSE(device->IsOpen()) << "a created session starts closed";
    EXPECT_EQ(provider.CreateDevice(devices[0].id + 1), nullptr);
}

TEST(AlsaAudioRecordingDevice, TheNullDeviceIsCapturedInRealTime)
{
    AlsaAudioRecordingDevice device({1, "null", true}, "null");
    const AudioFormat format = device.Open(kMono16);
    EXPECT_EQ(format.channels, 1);
    EXPECT_EQ(format.sampleFormat, AudioSampleFormat::Signed16);
    EXPECT_EQ(format.sampleRate, 44100u);
    EXPECT_TRUE(device.IsOpen());
    EXPECT_TRUE(device.IsConnected());

    // No clock in the device, so the session keeps one: 300 ms bring about 300 ms of silence.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const AudioRecordingIoResult available = device.GetAvailableBytes();
    ASSERT_EQ(available.status, AudioRecordingIoStatus::Success);
    const double seconds = static_cast<double>(available.byteCount) / 2.0 / 44100.0;
    EXPECT_GE(seconds, 0.2);
    EXPECT_LE(seconds, 0.6) << "a device with no clock must not deliver everything at once";

    std::vector<std::byte> block(available.byteCount);
    const AudioRecordingIoResult read = device.Read(block);
    ASSERT_EQ(read.status, AudioRecordingIoStatus::Success);
    EXPECT_EQ(read.byteCount % 2, 0u) << "whole frames only";
    EXPECT_TRUE(std::all_of(block.begin(), block.begin() + static_cast<std::ptrdiff_t>(read.byteCount),
                            [](const std::byte value) { return value == std::byte{0}; }));

    device.Close();
    EXPECT_FALSE(device.IsOpen());
    EXPECT_FALSE(IsValid(device.GetFormat()));
    EXPECT_EQ(device.GetAvailableBytes().status, AudioRecordingIoStatus::WouldBlock);
    device.Close();  // twice is fine
}

TEST(AlsaAudioRecordingDevice, AFileDeviceIsCapturedByteForByte)
{
    // ALSA's `file` device reads a capture stream from a file -- through a configuration of the
    // test's own, since the standard `file:` shorthand takes no input file. The system
    // configuration comes first, so everything else keeps its meaning.
    const std::filesystem::path system = "/usr/share/alsa/alsa.conf";
    if (!std::filesystem::exists(system))
    {
        GTEST_SKIP() << "no ALSA system configuration at " << system;
    }
    const std::filesystem::path directory = std::filesystem::temp_directory_path();
    const std::filesystem::path tone = directory / ("cna-capture-tone-" + std::to_string(::getpid()) + ".raw");
    const std::filesystem::path configuration =
        directory / ("cna-capture-" + std::to_string(::getpid()) + ".conf");
    std::vector<std::byte> expected;
    {
        std::ofstream out(tone, std::ios::binary);
        for (int index = 0; index < 44100; ++index)
        {
            const auto sample = static_cast<std::int16_t>(
                std::lround(8000.0 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * index / 44100.0)));
            char bytes[2];
            std::memcpy(bytes, &sample, 2);
            out.write(bytes, 2);
            expected.push_back(static_cast<std::byte>(bytes[0]));
            expected.push_back(static_cast<std::byte>(bytes[1]));
        }
    }
    {
        std::ofstream out(configuration);
        out << "pcm.cna_capture_test { type file slave.pcm \"null\" file \"/dev/null\" infile \""
            << tone.string() << "\" format \"raw\" }\n";
    }
    {
        const ScopedVariable path("ALSA_CONFIG_PATH", system.string() + ":" + configuration.string());
        const ScopedVariable configured("CNA_AUDIO_RECORDING_DEVICE", std::string("cna_capture_test"));
        AlsaAudioRecordingDeviceProvider provider;
        const std::vector<AudioRecordingDeviceInfo> devices = provider.GetDevices();
        ASSERT_EQ(devices.size(), 1u);
        std::unique_ptr<IAudioRecordingDevice> device = provider.CreateDevice(devices[0].id);
        ASSERT_NE(device, nullptr);
        const AudioFormat format = device->Open(kMono16);
        ASSERT_EQ(format.sampleRate, 44100u);
        ASSERT_EQ(format.channels, 1);
        ASSERT_EQ(format.sampleFormat, AudioSampleFormat::Signed16);

        // Half a second of the tone, which takes about half a second to capture.
        const auto start = std::chrono::steady_clock::now();
        const std::vector<std::byte> captured = Collect(*device, 44100, std::chrono::milliseconds(3000));
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        device->Close();
        ASSERT_GE(captured.size(), 44100u);
        EXPECT_TRUE(std::equal(captured.begin(), captured.begin() + 44100, expected.begin()))
            << "the captured bytes are not the file's";
        EXPECT_GE(elapsed, 0.4) << "captured faster than real time";
    }
    std::filesystem::remove(tone);
    std::filesystem::remove(configuration);
}

TEST(AlsaAudioRecordingDevice, TheContractsEdgesHold)
{
    AlsaAudioRecordingDevice device({1, "null", true}, "null");
    EXPECT_THROW((void) device.Open(AudioFormat{}), std::invalid_argument);
    EXPECT_FALSE(device.IsOpen()) << "a failed open leaves the session closed";
    (void) device.Open(kMono16);
    EXPECT_THROW((void) device.Open(kMono16), std::logic_error);

    std::vector<std::byte> empty;
    EXPECT_EQ(device.Read(empty).status, AudioRecordingIoStatus::WouldBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // Smaller than a frame: nothing is taken, nothing is written.
    std::vector<std::byte> tiny(1, std::byte{0x5A});
    const AudioRecordingIoResult result = device.Read(tiny);
    EXPECT_EQ(result.status, AudioRecordingIoStatus::WouldBlock);
    EXPECT_EQ(result.byteCount, 0u);
    EXPECT_EQ(tiny[0], std::byte{0x5A});
    device.Close();
}

TEST(AlsaAudioRecordingDevice, AnUnknownDeviceFailsToOpenAndSaysWhich)
{
    AlsaAudioRecordingDevice device({1, "cna_no_such_capture_device", true}, "cna_no_such_capture_device");
    try
    {
        (void) device.Open(kMono16);
        FAIL() << "an unknown PCM opened";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string(error.what()).find("cna_no_such_capture_device"), std::string::npos)
            << error.what();
    }
    EXPECT_FALSE(device.IsOpen());
}

TEST(AlsaAudioRecordingDevice, TheMachinesDevicesAreListedAsTheContractOrdersThem)
{
    // Enumeration only: ALSA's configuration and each card's control interface are asked what
    // there is. Nothing is opened for capture.
    const ScopedVariable unset("CNA_AUDIO_RECORDING_DEVICE", std::nullopt);
    AlsaAudioRecordingDeviceProvider provider;
    const std::vector<AudioRecordingDeviceInfo> devices = provider.GetDevices();
    std::size_t defaults = 0;
    for (std::size_t index = 0; index < devices.size(); ++index)
    {
        EXPECT_FALSE(devices[index].name.empty());
        EXPECT_NE(devices[index].name, "Default Device") << "no invented entry";
        if (devices[index].isDefault)
        {
            ++defaults;
            EXPECT_EQ(index, 0u) << "the default is first";
        }
        if (index > 0 && !devices[index - 1].isDefault)
        {
            EXPECT_LT(devices[index - 1].id, devices[index].id) << "then ascending ids";
        }
    }
    EXPECT_LE(defaults, 1u);
    std::printf("[          ] %zu recording device(s):", devices.size());
    for (const AudioRecordingDeviceInfo& device : devices)
    {
        std::printf(" [%s%s]", device.name.c_str(), device.isDefault ? ", default" : "");
    }
    std::printf("\n");
}

} // namespace

#endif // CNA_AUDIO_PLATFORM_ALSA
