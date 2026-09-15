// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0151: CNA's own mixer, driven directly -- no device, no thread, no timing.
//
// The mixer stands in for SDL3_mixer behind MixerEngine.hpp, and the XNA layer above it was
// written against SDL3_mixer's behaviour. So each rule SDL3_mixer has, and the XNA layer relies on,
// is pinned here with sample-exact data: gain before the mix callback and master gain after it,
// mono doubled into both channels, loops as extra passes, a loop region that plays its intro once,
// streams that wait when starved, and track destruction from a track's own stopped callback.

#include <gtest/gtest.h>

#if defined(CNA_AUDIO_PLATFORM_ALSA)

#include "Backend/CnaMixer/CnaMixer.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using namespace CNA::Internal::Audio;

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

std::vector<std::byte> ReadFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<std::byte> result(bytes.size());
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}

/// Float PCM, so values survive exactly.
MixerAudio FloatAudio(const std::vector<float>& samples, const int channels, const int rate)
{
    std::string error;
    auto data = WrapMixerRawAudio(std::as_bytes(std::span<const float>(samples)),
                                  {rate, channels, MixerSampleFormat::Float32}, error);
    EXPECT_TRUE(data) << error;
    MixerAudio audio;
    audio.data = data;
    audio.format = {rate, channels, MixerSampleFormat::Float32};
    audio.durationFrames = data ? data->frames : 0;
    return audio;
}

std::vector<float> Render(CnaMixer& mixer, const int frames)
{
    std::vector<float> out(static_cast<std::size_t>(frames) * 2, -99.0f);
    mixer.Render(out.data(), frames);
    return out;
}

std::vector<float> Left(const std::vector<float>& stereo)
{
    std::vector<float> left;
    for (std::size_t index = 0; index < stereo.size(); index += 2)
    {
        left.push_back(stereo[index]);
    }
    return left;
}

MixerTrack* PlayOnce(CnaMixer& mixer, const MixerAudio& audio, MixerPlayOptions options = {})
{
    MixerTrack* track = mixer.CreateTrack();
    EXPECT_TRUE(mixer.BindAudio(track, &audio));
    EXPECT_TRUE(mixer.Play(track, options));
    return track;
}

struct StopCounter
{
    int stops = 0;
    static void Callback(void* userdata, MixerTrack*)
    {
        ++static_cast<StopCounter*>(userdata)->stops;
    }
};

// --- the signal path ----------------------------------------------------------------------------

TEST(CnaMixer, SilenceWhenNothingPlays)
{
    CnaMixer mixer(48000);
    for (const float sample : Render(mixer, 64))
    {
        EXPECT_EQ(sample, 0.0f);
    }
}

TEST(CnaMixer, AMonoSourceIsDoubledIntoBothChannelsAndEndsOnItsLastFrame)
{
    CnaMixer mixer(48000);
    const MixerAudio audio = FloatAudio({0.5f, -0.25f, 0.125f}, 1, 48000);
    MixerTrack* track = PlayOnce(mixer, audio);
    StopCounter stops;
    track->stoppedCallback = StopCounter::Callback;
    track->stoppedUserdata = &stops;

    const std::vector<float> out = Render(mixer, 5);
    const std::vector<float> expected{0.5f, 0.5f, -0.25f, -0.25f, 0.125f, 0.125f, 0.0f, 0.0f, 0.0f, 0.0f};
    EXPECT_EQ(out, expected);
    EXPECT_EQ(track->state, MixerTrack::State::Stopped);
    EXPECT_EQ(stops.stops, 1);
}

TEST(CnaMixer, TrackGainAppliesBeforeTheMixCallbackAndMasterGainAfter)
{
    // SDL3_mixer's order, which SoundEffectInstance's filter and pan depend on: the callback sees
    // the track at its own volume, and the master volume is applied to what the callback left.
    CnaMixer mixer(48000);
    const MixerAudio audio = FloatAudio({0.5f, -0.5f, 0.5f, -0.5f}, 2, 48000);
    MixerTrack* track = PlayOnce(mixer, audio);
    track->gain = 0.5f;
    mixer.SetMasterGain(0.5f);

    struct Seen
    {
        std::vector<float> samples;
        int channels = 0;
        static void Callback(void* userdata, MixerTrack*, const int channels, float* pcm, const int samples)
        {
            auto* seen = static_cast<Seen*>(userdata);
            seen->channels = channels;
            seen->samples.assign(pcm, pcm + samples);
            for (int index = 1; index < samples; index += 2)
            {
                pcm[index] = 0.0f;  // The callback silences the right channel.
            }
        }
    } seen;
    track->mixCallback = Seen::Callback;
    track->mixUserdata = &seen;

    const std::vector<float> out = Render(mixer, 2);
    EXPECT_EQ(seen.channels, 2);
    EXPECT_EQ(seen.samples, (std::vector<float>{0.25f, -0.25f, 0.25f, -0.25f}));
    EXPECT_EQ(out, (std::vector<float>{0.125f, 0.0f, 0.125f, 0.0f}));
}

TEST(CnaMixer, TracksAddUpAndThePostMixCallbackSeesTheFinishedMix)
{
    CnaMixer mixer(48000);
    const MixerAudio first = FloatAudio({0.25f, 0.25f}, 1, 48000);
    const MixerAudio second = FloatAudio({0.5f, 0.5f}, 1, 48000);
    PlayOnce(mixer, first);
    PlayOnce(mixer, second);
    mixer.SetMasterGain(2.0f);

    struct PostMix
    {
        std::vector<float> seen;
        static void Callback(void* userdata, const int channels, float* pcm, const int samples)
        {
            EXPECT_EQ(channels, 2);
            static_cast<PostMix*>(userdata)->seen.assign(pcm, pcm + samples);
        }
    } post;
    mixer.SetPostMixCallback(PostMix::Callback, &post);

    const std::vector<float> out = Render(mixer, 2);
    EXPECT_EQ(out, (std::vector<float>{1.5f, 1.5f, 1.5f, 1.5f}));
    EXPECT_EQ(post.seen, out);
}

// --- rates -------------------------------------------------------------------------------------

TEST(CnaMixer, ASlowerSourceIsInterpolatedLinearly)
{
    // Each source frame plays for its own interval at the mixer's rate: two frames at half the
    // rate are four output frames, the second interval holding the last value.
    CnaMixer mixer(48000);
    const MixerAudio audio = FloatAudio({0.0f, 1.0f}, 1, 24000);
    PlayOnce(mixer, audio);
    EXPECT_EQ(Left(Render(mixer, 6)), (std::vector<float>{0.0f, 0.5f, 1.0f, 1.0f, 0.0f, 0.0f}));
}

TEST(CnaMixer, TheFrequencyRatioIsPlaybackSpeed)
{
    CnaMixer mixer(48000);
    std::vector<float> ramp;
    for (int index = 0; index < 16; ++index)
    {
        ramp.push_back(static_cast<float>(index) / 16.0f);
    }
    const MixerAudio audio = FloatAudio(ramp, 1, 48000);
    MixerTrack* track = PlayOnce(mixer, audio);
    track->ratio = 2.0f;  // An octave up: every other source frame.
    const std::vector<float> left = Left(Render(mixer, 10));
    EXPECT_EQ((std::vector<float>(left.begin(), left.begin() + 8)),
              (std::vector<float>{0.0f, 2 / 16.0f, 4 / 16.0f, 6 / 16.0f, 8 / 16.0f, 10 / 16.0f,
                                  12 / 16.0f, 14 / 16.0f}));
    EXPECT_EQ(track->state, MixerTrack::State::Stopped);
}

// --- loops -------------------------------------------------------------------------------------

TEST(CnaMixer, LoopsAreExtraPasses)
{
    CnaMixer mixer(48000);
    const MixerAudio audio = FloatAudio({0.1f, 0.2f, 0.3f}, 1, 48000);
    MixerPlayOptions options;
    options.loopCount = 2;
    MixerTrack* track = PlayOnce(mixer, audio, options);
    const std::vector<float> left = Left(Render(mixer, 11));
    EXPECT_EQ(left, (std::vector<float>{0.1f, 0.2f, 0.3f, 0.1f, 0.2f, 0.3f, 0.1f, 0.2f, 0.3f, 0.0f, 0.0f}));
    EXPECT_EQ(track->state, MixerTrack::State::Stopped);
}

TEST(CnaMixer, ALoopRegionPlaysTheIntroOnceThenRepeatsOnlyTheRegion)
{
    // SoundEffectInstance's LoopStart/LoopLength: a loop-start frame and a max frame that ends
    // every pass, the first pass included.
    CnaMixer mixer(48000);
    std::vector<float> frames;
    for (int index = 0; index < 10; ++index)
    {
        frames.push_back(static_cast<float>(index));
    }
    const MixerAudio audio = FloatAudio(frames, 1, 48000);
    MixerPlayOptions options;
    options.loopCount = 2;
    options.hasLoopStartFrame = true;
    options.loopStartFrame = 4;
    options.hasMaxFrame = true;
    options.maxFrame = 7;
    PlayOnce(mixer, audio, options);
    const std::vector<float> left = Left(Render(mixer, 15));
    EXPECT_EQ(left, (std::vector<float>{0, 1, 2, 3, 4, 5, 6, 4, 5, 6, 4, 5, 6, 0, 0}));
}

TEST(CnaMixer, AnEndlessLoopPlaysUntilStoppedAndStopFiresOnce)
{
    CnaMixer mixer(48000);
    const MixerAudio audio = FloatAudio({0.5f, 0.25f}, 1, 48000);
    MixerPlayOptions options;
    options.loopCount = -1;
    MixerTrack* track = PlayOnce(mixer, audio, options);
    StopCounter stops;
    track->stoppedCallback = StopCounter::Callback;
    track->stoppedUserdata = &stops;

    (void) Render(mixer, 5000);
    EXPECT_EQ(track->state, MixerTrack::State::Playing);
    EXPECT_EQ(stops.stops, 0);

    mixer.Stop(track);
    EXPECT_EQ(track->state, MixerTrack::State::Stopped);
    EXPECT_EQ(stops.stops, 1);
    mixer.Stop(track);  // Stopping a stopped track is not another stop.
    EXPECT_EQ(stops.stops, 1);
    for (const float sample : Render(mixer, 8))
    {
        EXPECT_EQ(sample, 0.0f);
    }
}

// --- state -------------------------------------------------------------------------------------

TEST(CnaMixer, PauseHoldsThePositionAndResumeContinuesFromIt)
{
    CnaMixer mixer(48000);
    const MixerAudio audio = FloatAudio({0.1f, 0.2f, 0.3f, 0.4f}, 1, 48000);
    MixerTrack* track = PlayOnce(mixer, audio);
    EXPECT_EQ(Left(Render(mixer, 2)), (std::vector<float>{0.1f, 0.2f}));
    mixer.Pause(track);
    EXPECT_EQ(track->state, MixerTrack::State::Paused);
    EXPECT_EQ(Left(Render(mixer, 2)), (std::vector<float>{0.0f, 0.0f}));
    mixer.Resume(track);
    EXPECT_EQ(Left(Render(mixer, 3)), (std::vector<float>{0.3f, 0.4f, 0.0f}));
}

TEST(CnaMixer, PlayingAgainStartsOver)
{
    CnaMixer mixer(48000);
    const MixerAudio audio = FloatAudio({0.1f, 0.2f, 0.3f}, 1, 48000);
    MixerTrack* track = PlayOnce(mixer, audio);
    (void) Render(mixer, 2);
    EXPECT_TRUE(mixer.Play(track, {}));
    EXPECT_EQ(Left(Render(mixer, 3)), (std::vector<float>{0.1f, 0.2f, 0.3f}));
}

TEST(CnaMixer, AnEmptySoundPlaysAndEndsAtOnce)
{
    // A zero-length SoundEffect is legal XNA. Its data pointer may be null; wrapping it must not
    // hand that to memcpy (UndefinedBehaviorSanitizer caught exactly that), and playing it is an
    // immediate, silent end -- not a stuck track.
    CnaMixer mixer(48000);
    std::string error;
    auto data = WrapMixerRawAudio({}, {48000, 1, MixerSampleFormat::Signed16}, error);
    ASSERT_TRUE(data) << error;
    EXPECT_EQ(data->frames, 0);
    MixerAudio audio;
    audio.data = data;
    MixerTrack* track = PlayOnce(mixer, audio);
    StopCounter stops;
    track->stoppedCallback = StopCounter::Callback;
    track->stoppedUserdata = &stops;
    for (const float sample : Render(mixer, 8))
    {
        EXPECT_EQ(sample, 0.0f);
    }
    EXPECT_EQ(track->state, MixerTrack::State::Stopped);
    EXPECT_EQ(stops.stops, 1);

    // And looping nothing forever still ends: there is nothing to loop.
    MixerPlayOptions forever;
    forever.loopCount = -1;
    EXPECT_TRUE(mixer.Play(track, forever));
    (void) Render(mixer, 8);
    EXPECT_EQ(track->state, MixerTrack::State::Stopped);
}

TEST(CnaMixer, NothingBoundCannotPlay)
{
    CnaMixer mixer(48000);
    MixerTrack* track = mixer.CreateTrack();
    EXPECT_FALSE(mixer.Play(track, {}));
    EXPECT_EQ(track->state, MixerTrack::State::Stopped);
}

// --- lifetime ------------------------------------------------------------------------------------

TEST(CnaMixer, ATrackDestroyedFromItsOwnStoppedCallbackLeavesTheMixAndIsFreedLater)
{
    // SoundEffect::Play()'s fire-and-forget tracks destroy themselves from their stopped
    // callback, on the audio thread. The track must leave the mix at once and not be freed while
    // the mixer still holds it; the facade frees it at the next safe point.
    CnaMixer mixer(48000);
    const MixerAudio audio = FloatAudio({0.5f}, 1, 48000);
    MixerTrack* track = PlayOnce(mixer, audio);
    struct Destroyer
    {
        CnaMixer* mixer = nullptr;
        static void Callback(void* userdata, MixerTrack* stopped)
        {
            static_cast<Destroyer*>(userdata)->mixer->DestroyTrack(stopped);
        }
    } destroyer{&mixer};
    track->stoppedCallback = Destroyer::Callback;
    track->stoppedUserdata = &destroyer;

    (void) Render(mixer, 4);
    EXPECT_TRUE(mixer.GetTracks().empty());
    EXPECT_TRUE(track->destroyDeferred);  // Still allocated: the deferred list holds it.
    mixer.DestroyTrack(track);             // A second request is a no-op, not a double free.
    mixer.DrainDeferred();                 // Freed here; AddressSanitizer checks the rest.
}

TEST(CnaMixer, AStoppedCallbackCanRestartItsTrack)
{
    CnaMixer mixer(48000);
    const MixerAudio audio = FloatAudio({0.5f, 0.25f}, 1, 48000);
    MixerTrack* track = PlayOnce(mixer, audio);
    struct Restarter
    {
        CnaMixer* mixer = nullptr;
        int restarts = 0;
        static void Callback(void* userdata, MixerTrack* stopped)
        {
            auto* self = static_cast<Restarter*>(userdata);
            if (self->restarts++ == 0)
            {
                EXPECT_TRUE(self->mixer->Play(stopped, {}));
            }
        }
    } restarter{&mixer};
    track->stoppedCallback = Restarter::Callback;
    track->stoppedUserdata = &restarter;

    (void) Render(mixer, 3);
    EXPECT_EQ(track->state, MixerTrack::State::Playing);
    (void) Render(mixer, 1024);
    EXPECT_EQ(track->state, MixerTrack::State::Stopped);
    EXPECT_EQ(restarter.restarts, 2);
}

// --- streams -----------------------------------------------------------------------------------

TEST(CnaMixer, AStreamPlaysWhatIsQueuedAndWaitsWhenItRunsOut)
{
    // DynamicSoundEffectInstance: the track never halts, and new data picks up where the old
    // left off.
    CnaMixer mixer(48000);
    MixerStream stream;
    stream.format = {48000, 1, MixerSampleFormat::Signed16};
    const std::vector<std::int16_t> first{8192, 16384, -8192, 4096};
    const auto* bytes = reinterpret_cast<const std::byte*>(first.data());
    stream.queue.assign(bytes, bytes + first.size() * sizeof(std::int16_t));

    MixerTrack* track = mixer.CreateTrack();
    mixer.BindStream(track, &stream);
    MixerPlayOptions options;
    options.haltWhenExhausted = false;
    ASSERT_TRUE(mixer.Play(track, options));

    // Three frames come out; the fourth waits for a successor to interpolate towards.
    EXPECT_EQ(Left(Render(mixer, 6)), (std::vector<float>{0.25f, 0.5f, -0.25f, 0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(stream.Queued(), 0u);
    EXPECT_EQ(track->state, MixerTrack::State::Playing);

    const std::vector<std::int16_t> second{-16384, 0};
    const auto* more = reinterpret_cast<const std::byte*>(second.data());
    stream.queue.insert(stream.queue.end(), more, more + second.size() * sizeof(std::int16_t));
    EXPECT_EQ(Left(Render(mixer, 3)), (std::vector<float>{0.125f, -0.5f, 0.0f}));
    EXPECT_EQ(track->state, MixerTrack::State::Playing);
}

TEST(CnaMixer, AHaltingStreamStopsWhenItRunsOut)
{
    CnaMixer mixer(48000);
    MixerStream stream;
    stream.format = {48000, 2, MixerSampleFormat::Float32};
    const std::vector<float> frames{0.5f, -0.5f, 0.25f, -0.25f};
    const auto* bytes = reinterpret_cast<const std::byte*>(frames.data());
    stream.queue.assign(bytes, bytes + frames.size() * sizeof(float));
    MixerTrack* track = mixer.CreateTrack();
    mixer.BindStream(track, &stream);
    ASSERT_TRUE(mixer.Play(track, {}));
    EXPECT_EQ(Render(mixer, 3), (std::vector<float>{0.5f, -0.5f, 0.25f, -0.25f, 0.0f, 0.0f}));
    EXPECT_EQ(track->state, MixerTrack::State::Stopped);
}

TEST(CnaMixer, ADetachedStreamLeavesItsTracksSilent)
{
    CnaMixer mixer(48000);
    auto stream = std::make_unique<MixerStream>();
    stream->format = {48000, 1, MixerSampleFormat::Float32};
    const std::vector<float> frames(64, 0.5f);
    const auto* bytes = reinterpret_cast<const std::byte*>(frames.data());
    stream->queue.assign(bytes, bytes + frames.size() * sizeof(float));
    MixerTrack* track = mixer.CreateTrack();
    mixer.BindStream(track, stream.get());
    MixerPlayOptions options;
    options.haltWhenExhausted = false;
    ASSERT_TRUE(mixer.Play(track, options));
    (void) Render(mixer, 4);
    mixer.DetachStream(stream.get());
    stream.reset();  // Freed: the track must not read it again.
    (void) Render(mixer, 16);
    EXPECT_EQ(track->stream, nullptr);
}

// --- decoding ------------------------------------------------------------------------------------

TEST(CnaMixer, WavAndOggVorbisDecodeAndEverythingElseIsRefusedByName)
{
    std::string error;
    const std::vector<std::byte> wav = ReadFile(Locate("tests/assets/xna40/media/tone_mono_44100.wav"));
    ASSERT_FALSE(wav.empty());
    const auto tone = DecodeMixerAudio(wav, "tone", true, error);
    ASSERT_TRUE(tone) << error;
    EXPECT_EQ(tone->encoding, MixerAudioData::Encoding::Pcm16);
    EXPECT_EQ(tone->sampleRate, 44100);
    EXPECT_EQ(tone->channels, 1);
    EXPECT_EQ(tone->frames, 22050);  // 0.5 s.

    const std::vector<std::byte> stereo8 =
        ReadFile(Locate("tests/assets/xna40/media/pcm8_stereo_22050.wav"));
    const auto eightBit = DecodeMixerAudio(stereo8, "pcm8", true, error);
    ASSERT_TRUE(eightBit) << error;
    EXPECT_EQ(eightBit->channels, 2);
    EXPECT_EQ(eightBit->sampleRate, 22050);

    const std::vector<std::byte> ogg =
        ReadFile(Locate("tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg"));
    ASSERT_FALSE(ogg.empty());
    const auto streamed = DecodeMixerAudio(ogg, "sunrise", false, error);
    ASSERT_TRUE(streamed) << error;
    EXPECT_EQ(streamed->encoding, MixerAudioData::Encoding::Vorbis);
    EXPECT_EQ(streamed->sampleRate, 44100);
    EXPECT_EQ(streamed->channels, 1);
    EXPECT_EQ(streamed->frames, 88200);  // 2.0 s.
    const auto predecoded = DecodeMixerAudio(ogg, "sunrise", true, error);
    ASSERT_TRUE(predecoded) << error;
    EXPECT_EQ(predecoded->encoding, MixerAudioData::Encoding::Pcm16);
    EXPECT_EQ(predecoded->frames, 88200);

    const std::vector<std::byte> mp3 = ReadFile(Locate("tests/assets/xna40/media/mp3_mono_44100_128k.mp3"));
    EXPECT_FALSE(DecodeMixerAudio(mp3, "song.mp3", true, error));
    EXPECT_NE(error.find("MP3"), std::string::npos) << error;
    const std::vector<std::byte> disguised = ReadFile(Locate("tests/assets/xna40/media/actually_mp3.wav"));
    EXPECT_FALSE(DecodeMixerAudio(disguised, "actually_mp3.wav", true, error));
    const std::vector<std::byte> garbage{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    EXPECT_FALSE(DecodeMixerAudio(garbage, "garbage", true, error));
    EXPECT_NE(error.find("not a known audio format"), std::string::npos) << error;
}

TEST(CnaMixer, AStreamedVorbisTrackPlaysEveryFrameAndMatchesTheDecodedOne)
{
    std::string error;
    const std::vector<std::byte> ogg =
        ReadFile(Locate("tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg"));
    auto streamedData = DecodeMixerAudio(ogg, "sunrise", false, error);
    auto decodedData = DecodeMixerAudio(ogg, "sunrise", true, error);
    ASSERT_TRUE(streamedData && decodedData) << error;
    MixerAudio streamed;
    streamed.data = streamedData;
    MixerAudio decoded;
    decoded.data = decodedData;

    struct Played
    {
        std::vector<float> left;
        static void Callback(void* userdata, MixerTrack*, const int channels, float* pcm, const int samples)
        {
            auto* played = static_cast<Played*>(userdata);
            for (int index = 0; index < samples; index += channels)
            {
                played->left.push_back(pcm[index]);
            }
        }
    };
    const auto playAll = [](const MixerAudio& audio) {
        CnaMixer mixer(44100);
        MixerTrack* track = mixer.CreateTrack();
        EXPECT_TRUE(mixer.BindAudio(track, &audio));
        EXPECT_TRUE(mixer.Play(track, {}));
        Played played;
        track->mixCallback = Played::Callback;
        track->mixUserdata = &played;
        for (int block = 0; block < 64 && track->state == MixerTrack::State::Playing; ++block)
        {
            (void) Render(mixer, 4096);
        }
        EXPECT_EQ(track->state, MixerTrack::State::Stopped);
        return played.left;
    };
    // At the file's own rate, one output frame per source frame: all of them, and no more.
    const std::vector<float> fromStream = playAll(streamed);
    const std::vector<float> fromMemory = playAll(decoded);
    EXPECT_EQ(fromStream.size(), 88200u);
    ASSERT_EQ(fromStream.size(), fromMemory.size());
    double worst = 0.0;
    for (std::size_t index = 0; index < fromStream.size(); ++index)
    {
        worst = std::max(worst, std::fabs(static_cast<double>(fromStream[index] - fromMemory[index])));
    }
    EXPECT_LE(worst, 2.0 / 32768.0);  // Float decoding against its own 16-bit rounding.
}

TEST(CnaMixer, AVorbisLoopSeeksBackToTheStart)
{
    std::string error;
    const std::vector<std::byte> ogg =
        ReadFile(Locate("tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg"));
    auto data = DecodeMixerAudio(ogg, "sunrise", false, error);
    ASSERT_TRUE(data) << error;
    MixerAudio audio;
    audio.data = data;
    CnaMixer mixer(44100);
    MixerTrack* track = mixer.CreateTrack();
    ASSERT_TRUE(mixer.BindAudio(track, &audio));
    MixerPlayOptions options;
    options.loopCount = 1;
    ASSERT_TRUE(mixer.Play(track, options));
    const std::vector<float> firstPass = Left(Render(mixer, 88200));
    EXPECT_EQ(track->state, MixerTrack::State::Playing);
    const std::vector<float> secondPass = Left(Render(mixer, 88200));
    EXPECT_EQ(firstPass, secondPass);
    (void) Render(mixer, 16);
    EXPECT_EQ(track->state, MixerTrack::State::Stopped);
}

} // namespace

#endif // CNA_AUDIO_PLATFORM_ALSA
