// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0151: the XNA audio classes over CNA's own mixer and the ALSA device --
// SoundEffect, SoundEffectInstance and DynamicSoundEffectInstance through the real facade, on
// ALSA's `null` device, which keeps real time and makes no sound.
//
// The SDL3_mixer suites (SoundEffectTests, SoundEffectInstanceTests, ...) read SDL3_mixer's own
// track handles and so cannot run here; these assert the same behaviour through the public API
// alone. Under NULL audio -- the only SDL-free option before -- every one of them fails: Play()
// returns false, an instance is Stopped the moment it starts, and a dynamic instance stops asking
// for buffers after its third.

#include <gtest/gtest.h>

#if defined(CNA_AUDIO_PLATFORM_ALSA)

#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"
#include "System/Environment.hpp"
#include "System/NotSupportedException.hpp"

#include "Platform/Alsa/AlsaAudioDevice.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

// Every test binary of an ALSA build plays to ALSA's silent `null` device unless told otherwise:
// the mixer opens its device once per process, the first time anything makes a sound, so this has
// to be settled before any test runs. ctest sets the same value; this covers a binary started by
// hand. An explicit CNA_AUDIO_DEVICE wins. Since plans/plan_x11.md X11-0162 the same goes for
// capture: no test records from the machine's microphone -- a room is not test data.
const bool kSilentByDefault = [] {
    if (!System::Environment::GetEnvironmentVariable("CNA_AUDIO_DEVICE").has_value())
    {
        System::Environment::SetEnvironmentVariable("CNA_AUDIO_DEVICE", std::string("null"));
    }
    if (!System::Environment::GetEnvironmentVariable("CNA_AUDIO_RECORDING_DEVICE").has_value())
    {
        System::Environment::SetEnvironmentVariable("CNA_AUDIO_RECORDING_DEVICE", std::string("null"));
    }
    return true;
}();

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Audio;

std::filesystem::path Locate(const std::filesystem::path& relative)
{
    for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty(); dir = dir.parent_path())
    {
        if (std::filesystem::exists(dir / relative))
        {
            return dir / relative;
        }
        if (dir == dir.root_path())
        {
            break;
        }
    }
    return relative;
}

class CnaMixerXna : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(kSilentByDefault);
        const char* device = std::getenv("CNA_AUDIO_DEVICE");
        if (device == nullptr || std::string(device) != "null")
        {
            GTEST_SKIP() << "CNA_AUDIO_DEVICE is '" << (device != nullptr ? device : "")
                         << "', not null: these tests make sound only on ALSA's silent device";
        }
        if (!CNA::Audio::Platform::Alsa::IsAlsaAvailable())
        {
            GTEST_SKIP() << "libasound.so.2 is not installed";
        }
    }

    /// A 16-bit mono tone.
    static std::vector<SharpRuntime::bytecs> Tone(const double seconds, const int rate = 44100)
    {
        const auto frames = static_cast<std::size_t>(seconds * rate);
        std::vector<SharpRuntime::bytecs> bytes(frames * 2);
        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            const auto sample = static_cast<std::int16_t>(
                8000.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * static_cast<double>(frame) / rate));
            bytes[frame * 2] = static_cast<SharpRuntime::bytecs>(sample & 0xFF);
            bytes[frame * 2 + 1] = static_cast<SharpRuntime::bytecs>((sample >> 8) & 0xFF);
        }
        return bytes;
    }

    static bool WaitFor(const std::function<bool()>& done, const std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!done())
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                return false;
            }
            FrameworkDispatcher::Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return true;
    }
};

TEST_F(CnaMixerXna, AFireAndForgetPlayIsAccepted)
{
    SoundEffect effect(Tone(0.05), 44100, AudioChannels::Mono);
    EXPECT_TRUE(effect.Play());
    EXPECT_TRUE(effect.Play(0.5f, 0.5f, -0.5f));
    // The tracks destroy themselves when they finish; the next Play() frees them. Nothing to see,
    // except that none of it crashes or leaks (AddressSanitizer).
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_TRUE(effect.Play());
}

TEST_F(CnaMixerXna, AnInstancePlaysForItsDurationAndThenStops)
{
    SoundEffect effect(Tone(0.1), 44100, AudioChannels::Mono);
    EXPECT_NEAR(effect.getDurationProperty().getTotalSecondsProperty(), 0.1, 0.001);
    SoundEffectInstance instance = effect.CreateInstance();
    instance.Play();
    EXPECT_EQ(instance.getStateProperty(), SoundState::Playing);
    EXPECT_TRUE(WaitFor([&] { return instance.getStateProperty() == SoundState::Stopped; },
                        std::chrono::milliseconds(2000)));
}

TEST_F(CnaMixerXna, PauseResumeAndStop)
{
    SoundEffect effect(Tone(2.0), 44100, AudioChannels::Mono);
    SoundEffectInstance instance = effect.CreateInstance();
    instance.Play();
    EXPECT_EQ(instance.getStateProperty(), SoundState::Playing);
    instance.Pause();
    EXPECT_EQ(instance.getStateProperty(), SoundState::Paused);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(instance.getStateProperty(), SoundState::Paused);
    instance.Resume();
    EXPECT_EQ(instance.getStateProperty(), SoundState::Playing);
    instance.Stop();
    EXPECT_EQ(instance.getStateProperty(), SoundState::Stopped);
    instance.Play();  // And plays again from the start.
    EXPECT_EQ(instance.getStateProperty(), SoundState::Playing);
    instance.Stop();
}

TEST_F(CnaMixerXna, ALoopedInstanceOutlastsItsSound)
{
    SoundEffect effect(Tone(0.05), 44100, AudioChannels::Mono);
    SoundEffectInstance instance = effect.CreateInstance();
    const bool looped = true;
    instance.setIsLoopedProperty(looped);
    instance.Play();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(instance.getStateProperty(), SoundState::Playing);
    instance.Stop();
    EXPECT_EQ(instance.getStateProperty(), SoundState::Stopped);
}

TEST_F(CnaMixerXna, ADynamicInstanceConsumesItsBuffersAndKeepsAskingForMore)
{
    // Under NULL audio nothing drains the queue, so BufferNeeded stops after the third buffer and
    // a streaming game stalls. With a mixer the buffers play out and the instance asks again.
    DynamicSoundEffectInstance instance(44100, AudioChannels::Mono);
    int needed = 0;
    instance.BufferNeeded += [&needed](System::Object*, const System::EventArgs&) { ++needed; };
    const std::vector<SharpRuntime::bytecs> buffer = Tone(0.05);
    instance.SubmitBuffer(buffer);
    instance.SubmitBuffer(buffer);
    instance.SubmitBuffer(buffer);
    instance.Play();
    EXPECT_EQ(instance.getStateProperty(), SoundState::Playing);
    EXPECT_TRUE(WaitFor([&] { return instance.getPendingBufferCountProperty() == 0; },
                        std::chrono::milliseconds(3000)))
        << instance.getPendingBufferCountProperty() << " buffers still pending";
    EXPECT_GE(needed, 1);

    // It keeps going as long as it is fed.
    const int before = needed;
    instance.SubmitBuffer(buffer);
    EXPECT_TRUE(WaitFor([&] { return needed > before; }, std::chrono::milliseconds(3000)));
    EXPECT_EQ(instance.getStateProperty(), SoundState::Playing);
    instance.Stop();
}

TEST_F(CnaMixerXna, WavOggMp3AndFlacFilesLoadAndOtherFormatsAreRefusedByName)
{
    const SoundEffect wav(Locate("tests/assets/xna40/media/tone_mono_44100.wav").string());
    EXPECT_NEAR(wav.getDurationProperty().getTotalSecondsProperty(), 0.5, 0.001);

    const SoundEffect ogg(Locate("tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg").string());
    EXPECT_NEAR(ogg.getDurationProperty().getTotalSecondsProperty(), 2.0, 0.001);

    // plans/plan_x11.md X11-0161: 21 MPEG frames of 1152, and one lossless second.
    const SoundEffect mp3(Locate("tests/assets/xna40/media/mp3_mono_44100_128k.mp3").string());
    EXPECT_NEAR(mp3.getDurationProperty().getTotalSecondsProperty(), 24192.0 / 44100.0, 0.001);
    const SoundEffect flac(
        Locate("tests/assets/media/music/Artist Three/Album Flac/01 - Flac Song.flac").string());
    EXPECT_NEAR(flac.getDurationProperty().getTotalSecondsProperty(), 1.0, 0.001);

    try
    {
        const SoundEffect opus(
            Locate("tests/assets/media/music/Artist Four/Album Opus/01 - Opus Song.opus").string());
        FAIL() << "an Opus file loaded";
    }
    catch (const System::NotSupportedException& refusal)
    {
        EXPECT_NE(std::string(refusal.what()).find("Opus"), std::string::npos) << refusal.what();
    }
}

// The whole chain, end to end: a SoundEffect played through the public API, mixed by CNA's
// mixer, written by the ALSA device thread through libasound -- and recorded by ALSA's `file`
// device, so what came out can be measured. Runs only under
// CNA_AUDIO_DEVICE=file:FILE=<path>,FORMAT=raw (ctest's CnaAudioAlsaRecordingTest); anywhere else
// the process's mixer plays to another device and there is nothing to read back.
TEST(CnaMixerXnaRecording, APlayedSoundEffectReachesTheAlsaDeviceAtItsPitchAndLevel)
{
    const char* device = std::getenv("CNA_AUDIO_DEVICE");
    const std::string prefix = "file:FILE=";
    if (device == nullptr || std::string(device).rfind(prefix, 0) != 0)
    {
        GTEST_SKIP() << "needs CNA_AUDIO_DEVICE=file:FILE=<path>,FORMAT=raw";
    }
    const std::string spec(device);
    const std::filesystem::path recording =
        spec.substr(prefix.size(), spec.find(',', prefix.size()) - prefix.size());

    // A 0.3 s, 440 Hz tone at 8000/32768 of full scale, mono, at the mixer's own rate.
    std::vector<SharpRuntime::bytecs> tone(static_cast<std::size_t>(0.3 * 44100) * 2);
    for (std::size_t frame = 0; frame < tone.size() / 2; ++frame)
    {
        const auto sample = static_cast<std::int16_t>(
            8000.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * static_cast<double>(frame) / 44100.0));
        tone[frame * 2] = static_cast<SharpRuntime::bytecs>(sample & 0xFF);
        tone[frame * 2 + 1] = static_cast<SharpRuntime::bytecs>((sample >> 8) & 0xFF);
    }
    SoundEffect effect(tone, 44100, AudioChannels::Mono);  // Opens the device: recording starts.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_TRUE(std::filesystem::exists(recording)) << recording;
    const auto before = std::filesystem::file_size(recording);
    ASSERT_TRUE(effect.Play());
    std::this_thread::sleep_for(std::chrono::milliseconds(700));

    std::ifstream file(recording, std::ios::binary);
    file.seekg(static_cast<std::streamoff>(before));
    const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::error_code error;
    std::filesystem::remove(recording, error);

    // The mixer asks for float stereo and ALSA's file device accepts it, so that is the recording.
    std::vector<float> samples(bytes.size() / sizeof(float));
    std::memcpy(samples.data(), bytes.data(), samples.size() * sizeof(float));
    ASSERT_GT(samples.size(), static_cast<std::size_t>(0.3 * 44100 * 2)) << "too little was recorded";

    float peak = 0.0f;
    std::size_t first = samples.size();
    std::size_t last = 0;
    for (std::size_t index = 0; index < samples.size(); index += 2)
    {
        EXPECT_EQ(samples[index], samples[index + 1]) << "mono must reach both channels alike";
        const float magnitude = std::fabs(samples[index]);
        peak = std::max(peak, magnitude);
        if (magnitude > 0.01f)
        {
            first = std::min(first, index / 2);
            last = index / 2;
        }
    }
    EXPECT_NEAR(peak, 8000.0f / 32768.0f, 0.01f);
    ASSERT_LT(first, last);
    const double seconds = static_cast<double>(last - first) / 44100.0;
    EXPECT_NEAR(seconds, 0.3, 0.02) << "the tone lasted " << seconds << " s";

    int crossings = 0;
    for (std::size_t frame = first + 1; frame <= last; ++frame)
    {
        if ((samples[frame * 2] >= 0.0f) != (samples[(frame - 1) * 2] >= 0.0f))
        {
            ++crossings;
        }
    }
    EXPECT_NEAR(crossings / 2.0 / seconds, 440.0, 10.0) << "the tone's pitch changed";
}

} // namespace

#endif // CNA_AUDIO_PLATFORM_ALSA
