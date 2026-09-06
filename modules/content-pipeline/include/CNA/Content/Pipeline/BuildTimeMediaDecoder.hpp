// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/VideoContentPipeline.hpp"

namespace CNA::Content::Pipeline
{
    /**
     * @brief Build-time reading of the three compressed source formats XNA reads through Windows
     *        Media -- MP3, WMA and WMV.
     *
     * This is the build side of the boundary `modules/content-pipeline` exists to keep: a game
     * that loads content at run time never links a demuxer or a decoder, because only
     * `cna_content_compiler` links this module. The runtime's own FFmpeg backend
     * (`modules/video-ffmpeg`) is a separate, optional thing and is not what these importers use.
     *
     * Availability is a three-state CMake switch, `CNA_ENABLE_MEDIA_PIPELINE`, matching the shape
     * `CNA_ENABLE_FONT_PIPELINE` already has. Where the decoder is absent, `IsAvailable()` answers
     * false and each entry point refuses with a sentence naming what is missing, rather than the
     * importer pretending the source was corrupt.
     */
    namespace BuildTimeMedia
    {
        /**
         * @brief Which compressed format a caller will accept from a source.
         *
         * XNA's audio importers are format-specific, not extension-specific: a WAV renamed `.mp3`
         * is refused by `Mp3Importer`, and an MP3 renamed `.wav` by `WavImporter` (measured,
         * tests/reference/xna40/media cases mp3/actually_wav.mp3 and wav/actually_mp3.wav). A
         * demuxer that reads anything would accept both, so the caller says what it wants.
         */
        enum class AudioSourceFormat
        {
            /** @brief Any stream the decoder can read. */
            Any,
            /** @brief MPEG audio only, of any layer or version. */
            Mpeg,
            /** @brief Windows Media audio in an ASF container only. */
            WindowsMedia,
        };

        /** @brief Audio decoded to the linear PCM the pipeline works in. */
        struct DecodedAudio
        {
            /** @brief Channels in the decoded samples; the source's own count. */
            int channels = 0;
            /** @brief Sample rate of the decoded samples. */
            int sampleRate = 0;
            /** @brief Bits per decoded sample; always 16. */
            int bitsPerSample = 16;
            /** @brief Interleaved little-endian signed 16-bit samples. */
            std::vector<std::uint8_t> pcm;
            /** @brief The stream's own length, in 100-nanosecond ticks, before truncation. */
            std::int64_t durationTicks = 0;
        };

        /** @brief What a video source declares about itself. */
        struct ProbedVideo
        {
            /** @brief Frame width in pixels. */
            int width = 0;
            /** @brief Frame height in pixels. */
            int height = 0;
            /** @brief Frames per second, as the stream's own rate. */
            float framesPerSecond = 0.0f;
            /** @brief The container's declared bit rate, or the video stream's when it has none. */
            int bitsPerSecond = 0;
            /** @brief The stream's own length, in 100-nanosecond ticks. */
            std::int64_t durationTicks = 0;
            /** @brief Whether the file carries an audio stream at all. */
            bool hasAudio = false;
        };

        /**
         * @brief Tells whether this build can read compressed media at all.
         *
         * @return true when the decoder was compiled in.
         */
        [[nodiscard]] bool IsAvailable() noexcept;

        /**
         * @brief Names what is missing, for a refusal a user can act on.
         *
         * @return A sentence naming the absent component, or an empty string when it is present.
         */
        [[nodiscard]] std::string UnavailableReason();

        /**
         * @brief Decodes one audio file to 16-bit PCM.
         *
         * The rate is the one XNA reports for the route: 44100 Hz whatever the source carries,
         * with the channel count preserved (measured, tests/reference/xna40/media, cases mp3/*;
         * `docs/xna-content-pipeline-media.md` section 2). Pass 0 to keep the source's own rate.
         *
         * @param filename Path to the source.
         * @param forcedSampleRate The rate to resample to, or 0 to keep the source's.
         * @param required The format the caller will accept; anything else is refused even when
         *        the decoder could read it.
         * @return The decoded samples and what they are.
         * @throws std::runtime_error when the file cannot be opened, is not of the required
         *         format, has no audio stream, or cannot be decoded; the message names which.
         */
        [[nodiscard]] DecodedAudio DecodeAudio(const std::string& filename, int forcedSampleRate,
                                               AudioSourceFormat required = AudioSourceFormat::Any);

        /**
         * @brief Reads a video file's declared shape without decoding a frame.
         *
         * @param filename Path to the source.
         * @return What the container and its video stream declare.
         * @throws std::runtime_error when the file cannot be opened or has no video stream.
         */
        [[nodiscard]] ProbedVideo ProbeVideo(const std::string& filename);

        /**
         * @brief The canonical video route's frame-metadata probe, backed by this decoder.
         *
         * `cna_content` cannot read a media file and must not learn how -- a game that loads a
         * compiled video needs no decoder -- so the canonical `VideoImporter` takes its probe from
         * whoever registers it. This is that probe. It answers nothing when this build has no
         * decoder or when the file is not one it can read, which leaves the processor requiring
         * the metadata as parameters exactly as it did before (plans/plan_xnapipeline_parity.md
         * `XNAPP-021`).
         *
         * @return A probe suitable for RegisterVideoContentPipeline().
         */
        [[nodiscard]] VideoMetadataProbe MakeVideoMetadataProbe();

        /**
         * @brief Encodes PCM samples to Windows Media audio in an ASF container.
         *
         * `SongProcessor` needs a song to be Windows Media audio; Microsoft's own encoder exists
         * only on the platform that owns it, and could not even be measured here (XNA's never
         * returns under the oracle's Wine prefix). What this produces instead is a real WMA that a
         * Windows Media runtime accepts, which is the semantic the format's consumers depend on.
         *
         * @param pcm Interleaved little-endian signed 16-bit samples.
         * @param channels Channels in @p pcm.
         * @param sampleRate Sample rate of @p pcm.
         * @param bitsPerSecond Target bit rate.
         * @param filename Where to write the file.
         * @throws std::runtime_error when the encoder is absent or the file cannot be written.
         */
        void EncodeWindowsMediaAudio(const std::vector<std::uint8_t>& pcm, int channels, int sampleRate,
                                     int bitsPerSecond, const std::string& filename);

        /**
         * @brief The identity of the decoder this build carries, for a build fingerprint.
         *
         * A processor's output depends on which decoder produced its samples, so the identity has
         * to enter the incremental fingerprint the way a compiler's version does.
         *
         * @return A stable one-line identity, or "none" where no decoder is compiled in.
         */
        [[nodiscard]] std::string Identity();
    }
}
