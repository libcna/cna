// SPDX-License-Identifier: MS-PL

#include <cmath>
#include <fstream>
#include <gtest/gtest.h>
#include "CNA/Internal/Media/VideoDecoder.hpp"

using CNA::Internal::Media::VideoDecoder;

namespace
{
    constexpr const char* kChroma420 = "tests/assets/media/video/chroma_420.mkv";
    constexpr const char* kChroma422 = "tests/assets/media/video/chroma_422.mkv";
    constexpr const char* kChroma444 = "tests/assets/media/video/chroma_444.mkv";
    constexpr const char* kBitDepth10 = "tests/assets/media/video/bitdepth_10.mkv";
    constexpr const char* kBitDepth12 = "tests/assets/media/video/bitdepth_12.mkv";
    constexpr const char* kAudioTail = "tests/assets/media/video/audio_tail.mkv";
    constexpr const char* kAv1WithAudio = "tests/assets/media/video/av1_with_audio.mkv";
    constexpr const char* kMultiTrackAudio = "tests/assets/media/video/multi_track_audio.mkv";

    // SMPTE bar reference colors sampled at fixture-authoring time (manifest.json), y=0.3*height,
    // 7 bars left to right. Generous tolerance for chroma-subsampling/bit-depth-downshift rounding.
    struct Rgb { int r, g, b; };
    constexpr Rgb kBars[7] = {
        {190, 190, 190}, // White
        {190, 190, 0},   // Yellow
        {0, 190, 190},   // Cyan
        {0, 190, 0},     // Green
        {190, 0, 190},   // Magenta
        {190, 0, 0},     // Red
        {0, 0, 190},     // Blue
    };
    constexpr int kTolerance = 20;

    void ExpectBarsMatch(const std::vector<uint8_t>& rgba, int width, int height)
    {
        int y = static_cast<int>(height * 0.3);
        for (int i = 0; i < 7; ++i)
        {
            int x = static_cast<int>((i + 0.5) * width / 7);
            std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4;
            ASSERT_LT(idx + 3, rgba.size());
            EXPECT_NEAR(rgba[idx + 0], kBars[i].r, kTolerance) << "bar " << i << " red";
            EXPECT_NEAR(rgba[idx + 1], kBars[i].g, kTolerance) << "bar " << i << " green";
            EXPECT_NEAR(rgba[idx + 2], kBars[i].b, kTolerance) << "bar " << i << " blue";
            EXPECT_EQ(rgba[idx + 3], 255) << "bar " << i << " alpha";
        }
    }
}

TEST(VideoDecoderTest, OpenFailsGracefullyForMissingFile)
{
    VideoDecoder decoder;
    EXPECT_FALSE(decoder.Open("tests/assets/media/video/does-not-exist.mkv"));
    EXPECT_FALSE(decoder.IsOpen());
}

TEST(VideoDecoderTest, OpenReportsCorrectDimensionsAndFps)
{
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(kChroma420));
    EXPECT_EQ(decoder.GetWidth(), 160);
    EXPECT_EQ(decoder.GetHeight(), 90);
    EXPECT_NEAR(decoder.GetFPS(), 25.0f, 0.5f);
}

// plans/plan_media.md MEDIA-35: 4:2:0 baseline + 4:2:2/4:4:4 chroma subsampling now decode to correct
// colors instead of the magenta fallback.
TEST(VideoDecoderTest, Decodes420ChromaToCorrectColors)
{
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(kChroma420));
    std::vector<uint8_t> rgba;
    double pts = 0.0;
    ASSERT_TRUE(decoder.NextFrame(rgba, pts));
    ExpectBarsMatch(rgba, decoder.GetWidth(), decoder.GetHeight());
}

TEST(VideoDecoderTest, Decodes422ChromaToCorrectColors)
{
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(kChroma422));
    std::vector<uint8_t> rgba;
    double pts = 0.0;
    ASSERT_TRUE(decoder.NextFrame(rgba, pts));
    ExpectBarsMatch(rgba, decoder.GetWidth(), decoder.GetHeight());
}

TEST(VideoDecoderTest, Decodes444ChromaToCorrectColors)
{
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(kChroma444));
    std::vector<uint8_t> rgba;
    double pts = 0.0;
    ASSERT_TRUE(decoder.NextFrame(rgba, pts));
    ExpectBarsMatch(rgba, decoder.GetWidth(), decoder.GetHeight());
}

// plans/plan_media.md MEDIA-36: 10-/12-bit content now decodes to correct, non-clipped, non-magenta
// color instead of falling through to the unsupported-format fallback.
TEST(VideoDecoderTest, Decodes10BitToCorrectColors)
{
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(kBitDepth10));
    std::vector<uint8_t> rgba;
    double pts = 0.0;
    ASSERT_TRUE(decoder.NextFrame(rgba, pts));
    ExpectBarsMatch(rgba, decoder.GetWidth(), decoder.GetHeight());
}

TEST(VideoDecoderTest, Decodes12BitToCorrectColors)
{
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(kBitDepth12));
    std::vector<uint8_t> rgba;
    double pts = 0.0;
    ASSERT_TRUE(decoder.NextFrame(rgba, pts));
    ExpectBarsMatch(rgba, decoder.GetWidth(), decoder.GetHeight());
}

// plans/plan_media.md MEDIA-37: AV1 content keeps its audio track (a deliberate improvement over FNA's
// own AV1-is-video-only limitation) -- exercised generically here since these fixtures aren't AV1
// specifically, but HasAudio()/DrainAudio() must work uniformly regardless of video codec.
TEST(VideoDecoderTest, AudioTailFixtureHasAudio)
{
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(kAudioTail));
    EXPECT_TRUE(decoder.HasAudio());
    EXPECT_GT(decoder.GetSampleRate(), 0);
    EXPECT_GT(decoder.GetChannels(), 0);
}

// plans/plan_media.md MEDIA-93: DrainAudio() itself, not exercised by any other test above (only
// HasAudio()/GetSampleRate()/GetChannels() presence were checked) -- confirms real decoded audio
// samples are actually produced and returned, not just that the stream metadata is present.
TEST(VideoDecoderTest, DrainAudioProducesRealSamplesAfterDecodingFrames)
{
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(kAudioTail));
    ASSERT_TRUE(decoder.HasAudio());

    std::vector<uint8_t> rgba;
    double pts = 0.0;
    for (int i = 0; i < 5; ++i)
    {
        if (!decoder.NextFrame(rgba, pts)) break;
    }

    std::vector<float> samples;
    decoder.DrainAudio(samples);
    EXPECT_GT(samples.size(), 0u);

    // A second drain with no further decoding must not return stale/duplicate data --
    // DrainAudio()'s own documented "appended to, not replaced" contract still means the
    // *source* buffer (pendingAudio_) is cleared after each drain.
    std::vector<float> secondDrain;
    decoder.DrainAudio(secondDrain);
    EXPECT_TRUE(secondDrain.empty());
}

// plans/plan_media.md MEDIA-89/MEDIA-37: real AV1-coded content (not just the generic ffv1 fixtures)
// keeps its audio track through the same unified decode path -- a deliberate improvement beyond
// FNA's own dav1dfile-based AV1 handling, which is video-only.
TEST(VideoDecoderTest, Av1ContentDecodesVideoAndKeepsItsAudioTrack)
{
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(kAv1WithAudio));
    EXPECT_EQ(decoder.GetWidth(), 160);
    EXPECT_EQ(decoder.GetHeight(), 90);
    ASSERT_TRUE(decoder.HasAudio());
    EXPECT_GT(decoder.GetSampleRate(), 0);
    EXPECT_GT(decoder.GetChannels(), 0);

    std::vector<uint8_t> rgba;
    double pts = 0.0;
    ASSERT_TRUE(decoder.NextFrame(rgba, pts));
    EXPECT_EQ(rgba.size(), static_cast<std::size_t>(160 * 90 * 4));

    // Audio packets interleaved in the container may not all land before the very first video
    // packet -- decode a few more frames (matching DrainAudioProducesRealSamplesAfterDecodingFrames'
    // own established pattern) before expecting DrainAudio() to have real samples.
    for (int i = 0; i < 4; ++i)
    {
        if (!decoder.NextFrame(rgba, pts)) break;
    }

    std::vector<float> samples;
    decoder.DrainAudio(samples);
    EXPECT_GT(samples.size(), 0u);
}

// plans/plan_media.md MEDIA-90/MEDIA-95: SetAudioStream() by index, against a real 2-track fixture --
// the two tracks deliberately use different sample rates (48000 vs 44100) so GetSampleRate()
// actually changing after the switch proves the track really changed, not just that the call
// didn't crash.
TEST(VideoDecoderTest, SetAudioStreamSwitchesToTheRequestedTrack)
{
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(kMultiTrackAudio));
    ASSERT_TRUE(decoder.HasAudio());
    EXPECT_EQ(decoder.GetSampleRate(), 48000); // track 0, the default

    decoder.SetAudioStream(1);
    EXPECT_EQ(decoder.GetSampleRate(), 44100); // track 1

    decoder.SetAudioStream(0);
    EXPECT_EQ(decoder.GetSampleRate(), 48000); // back to track 0
}

// plans/plan_media.md MEDIA-95: SetVideoStream() -- the multi-track fixture has only one video stream
// (index 0), so this confirms re-selecting the same, already-active video stream by index is a
// safe, correctness-preserving no-op rather than confirming an actual switch (no second video
// stream fixture exists to test that with -- see plans/plan_media.md's own honest-gap note).
TEST(VideoDecoderTest, SetVideoStreamReselectingTheSameStreamPreservesDimensions)
{
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(kMultiTrackAudio));
    ASSERT_EQ(decoder.GetWidth(), 160);
    ASSERT_EQ(decoder.GetHeight(), 90);

    decoder.SetVideoStream(0);

    EXPECT_EQ(decoder.GetWidth(), 160);
    EXPECT_EQ(decoder.GetHeight(), 90);

    std::vector<uint8_t> rgba;
    double pts = 0.0;
    EXPECT_TRUE(decoder.NextFrame(rgba, pts));
}

// plans/plan_media.md MEDIA-38: fault injection -- Open() must not crash on a truncated/corrupted file,
// even though the failure mode here is "Open returns false", not an exception (a genuinely
// malformed container is typically rejected during avformat_open_input/find_stream_info, before
// any of the hardened alloc sites are even reached).
TEST(VideoDecoderTest, OpenDoesNotCrashOnTruncatedFile)
{
    std::ifstream src(kChroma420, std::ios::binary);
    ASSERT_TRUE(src.is_open());
    std::vector<char> bytes((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
    ASSERT_GT(bytes.size(), 100u);

    const std::string truncatedPath = "tests/assets/media/video/.truncated_test_fixture.mkv";
    {
        std::ofstream out(truncatedPath, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size() / 20));
    }

    VideoDecoder decoder;
    // A truncated container is typically rejected outright by avformat_open_input/
    // find_stream_info; the important guarantee is "does not crash", not a specific bool result.
    EXPECT_NO_THROW({ (void)decoder.Open(truncatedPath); });

    std::remove(truncatedPath.c_str());
}

// plans/plan_media.md MEDIA-40 (corrected -- found by external code review): the previous version of
// this test truncated the trailing bytes off a real Matroska file and asserted success on EITHER
// a thrown exception OR a clean EOF, making it a tautology that could never fail and provided zero
// real coverage. Empirically, trailing-byte truncation of a Matroska/EBML file makes libavformat's
// matroska demuxer log a "File ended prematurely" warning but still return AVERROR_EOF, not a
// distinct I/O error code -- so NextFrame() correctly (and unavoidably, at the FFmpeg API level)
// treats it as clean EOF, not an error. This test now asserts that real, verified behavior
// directly instead of accepting either outcome.
TEST(VideoDecoderTest, TruncatedFileEndsAtCleanEOFRatherThanThrowing)
{
    std::ifstream src(kChroma420, std::ios::binary);
    ASSERT_TRUE(src.is_open());
    std::vector<char> bytes((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
    ASSERT_GT(bytes.size(), 1000u);

    const std::string truncatedPath = "tests/assets/media/video/.midstream_truncated_fixture.mkv";
    {
        std::ofstream out(truncatedPath, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size() - 200));
    }

    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(truncatedPath));

    std::vector<uint8_t> rgba;
    double pts = 0.0;
    EXPECT_NO_THROW(
        {
            while (decoder.NextFrame(rgba, pts))
            {
            }
        });

    std::remove(truncatedPath.c_str());
}

// plans/plan_media.md MEDIA-40: the real "genuine mid-stream error, not EOF" case. Empirically verified
// (via direct ffmpeg CLI experimentation across a dozen+ corruption strategies) that this repo's
// ffv1/Matroska fixtures cannot exercise this path: Matroska's demuxer is deliberately
// streaming-tolerant and treats a truncated/malformed tail as clean EOF (see the test above) no
// matter where the cut lands, and this build's ffv1 decoder logs but never hard-fails on a
// corrupted slice's CRC mismatch even with every AV_EF_* strictness flag set. H264 is much less
// lenient about corrupted macroblock data -- corrupting real H264 Annex-B bytes inside
// corrupt_test_h264.mp4 (added specifically for this test) reliably makes avcodec_send_packet()
// return a genuine AVERROR_INVALIDDATA under AV_EF_EXPLODE, which NextFrame() must surface as a
// thrown exception rather than silently treating it as "video finished normally".
TEST(VideoDecoderTest, CorruptedMidStreamDataThrowsRatherThanSilentlyEndingCleanly)
{
    std::ifstream src("tests/assets/media/video/corrupt_test_h264.mp4", std::ios::binary);
    ASSERT_TRUE(src.is_open());
    std::vector<char> bytes((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
    ASSERT_GT(bytes.size(), 100u);

    const std::string corruptedPath = "tests/assets/media/video/.midstream_corrupted_fixture.mp4";
    {
        // Flip a run of bytes roughly a third of the way through the compressed video data --
        // verified via direct ffmpeg CLI testing to reliably corrupt real macroblock data (not
        // just container metadata), producing "Error submitting packet to decoder: Invalid data
        // found when processing input" under strict error detection.
        std::size_t start = bytes.size() * 3 / 10;
        std::size_t length = std::min<std::size_t>(16, bytes.size() - start);
        for (std::size_t i = start; i < start + length; ++i)
        {
            bytes[i] = static_cast<char>(static_cast<unsigned char>(bytes[i]) ^ 0xFF);
        }
        std::ofstream out(corruptedPath, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(corruptedPath));

    std::vector<uint8_t> rgba;
    double pts = 0.0;
    EXPECT_THROW(
        {
            while (decoder.NextFrame(rgba, pts))
            {
            }
        },
        std::runtime_error);

    std::remove(corruptedPath.c_str());
}

TEST(VideoDecoderTest, SeekToStartResetsToBeginning)
{
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(kChroma420));
    std::vector<uint8_t> firstFrame, laterFrame;
    double pts = 0.0;
    ASSERT_TRUE(decoder.NextFrame(firstFrame, pts));
    for (int i = 0; i < 5; ++i)
    {
        decoder.NextFrame(laterFrame, pts);
    }
    decoder.SeekToStart();
    std::vector<uint8_t> afterSeek;
    ASSERT_TRUE(decoder.NextFrame(afterSeek, pts));
    ExpectBarsMatch(afterSeek, decoder.GetWidth(), decoder.GetHeight());
}

TEST(VideoDecoderTest, CloseResetsState)
{
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(kChroma420));
    decoder.Close();
    EXPECT_FALSE(decoder.IsOpen());
    EXPECT_EQ(decoder.GetWidth(), 0);
    EXPECT_EQ(decoder.GetHeight(), 0);
}

// plans/plan_media.md MEDIA-146 (found by external code review): NextFrame()'s EAGAIN packet-retention
// flag used to be a function-local variable, so a packet retained after avcodec_send_packet()
// returned EAGAIN was silently lost the moment the very next avcodec_receive_frame() call (almost
// always the next thing that happens, since EAGAIN on send means "drain output first") returned a
// buffered frame and the function returned to the caller -- across that call boundary the flag
// reset to false and the retained packet was overwritten by the next av_read_frame() without ever
// being resent, dropping that video frame. ffprobe independently confirms chroma_420.mkv has
// exactly 50 real video frames (`nb_read_frames=50`); decoding the entire file end-to-end and
// counting exactly 50, with strictly increasing timestamps and no gap, is a real, deterministic
// regression guard against any frame silently going missing, not just "decoding didn't throw."
TEST(VideoDecoderTest, DecodesTheFullFileWithoutSilentlyDroppingAnyFrame)
{
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(kChroma420));

    int frameCount = 0;
    double lastPts = -1.0;
    std::vector<uint8_t> frame;
    double pts = 0.0;
    while (decoder.NextFrame(frame, pts))
    {
        EXPECT_GT(pts, lastPts) << "frame " << frameCount << " PTS did not strictly increase";
        lastPts = pts;
        ++frameCount;
    }

    EXPECT_EQ(frameCount, 50);
}

// plans/plan_media.md MEDIA-155 (found by external code review): Close() reset every other decode-state
// member but left pendingAudio_ untouched. A caller that reuses the same VideoDecoder instance
// (Close() then Open() again, rather than allocating a fresh instance every time -- VideoPlayer
// itself always allocates fresh, but the class's own contract should not depend on that) would have
// its first DrainAudio() call after the second Open() return stale samples decoded from the FIRST
// file, never actually cleared.
TEST(VideoDecoderTest, CloseClearsAnyUndrainedPendingAudioFromThePreviousFile)
{
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(kAudioTail));
    ASSERT_TRUE(decoder.HasAudio());

    std::vector<uint8_t> rgba;
    double pts = 0.0;
    for (int i = 0; i < 5; ++i)
    {
        if (!decoder.NextFrame(rgba, pts)) break;
    }
    // Deliberately do NOT drain here -- pendingAudio_ should now hold real, undrained samples.

    decoder.Close();
    ASSERT_TRUE(decoder.Open(kChroma420)); // a different file, reusing the same instance

    std::vector<float> samples;
    decoder.DrainAudio(samples);
    EXPECT_TRUE(samples.empty()) << "DrainAudio() returned " << samples.size()
                                  << " stale samples left over from the previous file";
}

// plans/plan_media.md MEDIA-159 (found by external code review): SeekToStart() flushed both codec
// contexts and reset the EAGAIN pending-packet state (MEDIA-155), but never cleared pendingAudio_
// itself. A direct SeekToStart() call (not routed through VideoPlayer, whose own GetTexture() loop
// happens to drain right before it calls SeekToStart() on loop -- masking this for that one caller)
// with real, undrained samples already sitting in pendingAudio_ would splice those stale pre-seek
// samples onto whatever decodes after the seek on the next DrainAudio() call.
TEST(VideoDecoderTest, SeekToStartClearsAnyUndrainedPendingAudioFromBeforeTheSeek)
{
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(kAudioTail));
    ASSERT_TRUE(decoder.HasAudio());

    std::vector<uint8_t> rgba;
    double pts = 0.0;
    for (int i = 0; i < 5; ++i)
    {
        if (!decoder.NextFrame(rgba, pts)) break;
    }
    // Deliberately do NOT drain here -- pendingAudio_ should now hold real, undrained samples.

    decoder.SeekToStart();

    std::vector<float> samples;
    decoder.DrainAudio(samples);
    EXPECT_TRUE(samples.empty()) << "DrainAudio() returned " << samples.size()
                                  << " stale samples left over from before SeekToStart()";
}

// plans/plan_media.md MEDIA-171 (found by external code review): MEDIA-167 replaced SeekToStart()'s
// manual resampler-delay drain with a swr_free() + CreateResampler() recreation, and claimed
// regression coverage from two existing tests -- but NEITHER actually covered it.
// LoopedVideoKeepsPlayingPastItsDuration uses chroma_420.mkv, which has no audio stream at all
// (ffprobe: one ffv1 video stream, nothing else), so it never enters SeekToStart()'s
// `if (audioCtx_ && swrCtx_)` block. SeekToStartClearsAnyUndrainedPendingAudioFromBeforeTheSeek
// (directly above) only asserts the pending buffer is EMPTY after the seek -- which would pass even
// MORE readily if the recreation left swrCtx_ null, since no audio would ever decode again.
//
// This is the missing direct proof: after a seek, the decoder must still have a WORKING resampler
// (HasAudio() true) and must genuinely produce real audio samples again from further decoding.
// Fails loudly if swr_free() were ever left without a successful CreateResampler() replacing it.
TEST(VideoDecoderTest, SeekToStartRebuildsAWorkingResamplerSoAudioStillDecodesAfterTheSeek)
{
    VideoDecoder decoder;
    ASSERT_TRUE(decoder.Open(kAudioTail));
    ASSERT_TRUE(decoder.HasAudio());

    std::vector<uint8_t> rgba;
    double pts = 0.0;
    for (int i = 0; i < 5; ++i)
    {
        if (!decoder.NextFrame(rgba, pts)) break;
    }
    std::vector<float> samplesBefore;
    decoder.DrainAudio(samplesBefore);
    ASSERT_GT(samplesBefore.size(), 0u) << "fixture should produce audio before the seek at all";

    decoder.SeekToStart();

    // The resampler was freed and rebuilt by the seek -- it must be a real, working one, not null.
    EXPECT_TRUE(decoder.HasAudio())
        << "SeekToStart() left the decoder without a working resampler";

    // ...and decoding after the seek must genuinely produce audio again through that new resampler.
    for (int i = 0; i < 5; ++i)
    {
        if (!decoder.NextFrame(rgba, pts)) break;
    }
    std::vector<float> samplesAfter;
    decoder.DrainAudio(samplesAfter);
    EXPECT_GT(samplesAfter.size(), 0u)
        << "no audio samples decoded after SeekToStart() -- the rebuilt resampler is not working";
}
