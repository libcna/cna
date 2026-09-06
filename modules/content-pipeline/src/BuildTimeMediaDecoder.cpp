// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Pipeline/BuildTimeMediaDecoder.hpp"

#include <stdexcept>

#ifdef CNA_HAVE_BUILD_MEDIA
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
#include <algorithm>
#include <cstring>
#include <memory>
#endif

namespace CNA::Content::Pipeline::BuildTimeMedia
{
#ifndef CNA_HAVE_BUILD_MEDIA
    bool IsAvailable() noexcept { return false; }

    std::string UnavailableReason()
    {
        return "this build has no media decoder, so an MP3, WMA or WMV source cannot be read. "
               "Configure with -DCNA_ENABLE_MEDIA_PIPELINE=ON against libavcodec, libavformat, "
               "libavutil and libswresample.";
    }

    std::string Identity() { return "none"; }

    DecodedAudio DecodeAudio(const std::string&, int, AudioSourceFormat)
    {
        throw std::runtime_error(UnavailableReason());
    }

    ProbedVideo ProbeVideo(const std::string&) { throw std::runtime_error(UnavailableReason()); }

    void EncodeWindowsMediaAudio(const std::vector<std::uint8_t>&, int, int, int, const std::string&)
    {
        throw std::runtime_error(UnavailableReason());
    }
#else
    namespace
    {
        /** @brief One hour of stereo 48 kHz PCM: the ceiling a build-time source may reach. */
        constexpr std::size_t MaximumDecodedBytes = 48000u * 2u * 2u * 3600u;

        struct FormatContextCloser
        {
            void operator()(AVFormatContext* context) const noexcept
            {
                if (context != nullptr)
                {
                    avformat_close_input(&context);
                }
            }
        };
        struct CodecContextCloser
        {
            void operator()(AVCodecContext* context) const noexcept { avcodec_free_context(&context); }
        };
        struct FrameCloser
        {
            void operator()(AVFrame* frame) const noexcept { av_frame_free(&frame); }
        };
        struct PacketCloser
        {
            void operator()(AVPacket* packet) const noexcept { av_packet_free(&packet); }
        };
        struct ResamplerCloser
        {
            void operator()(SwrContext* context) const noexcept { swr_free(&context); }
        };

        using FormatContext = std::unique_ptr<AVFormatContext, FormatContextCloser>;
        using CodecContext = std::unique_ptr<AVCodecContext, CodecContextCloser>;
        using Frame = std::unique_ptr<AVFrame, FrameCloser>;
        using Packet = std::unique_ptr<AVPacket, PacketCloser>;
        using Resampler = std::unique_ptr<SwrContext, ResamplerCloser>;

        [[nodiscard]] FormatContext Open(const std::string& filename)
        {
            // The library's own diagnostics are not the pipeline's: a source the importer is
            // about to refuse should not also print a demuxer's opinion into a build log.
            av_log_set_level(AV_LOG_QUIET);
            AVFormatContext* raw = nullptr;
            if (avformat_open_input(&raw, filename.c_str(), nullptr, nullptr) < 0)
            {
                throw std::runtime_error("the file could not be opened as media");
            }
            FormatContext context(raw);
            if (avformat_find_stream_info(context.get(), nullptr) < 0)
            {
                throw std::runtime_error("the file carries no readable stream information");
            }
            return context;
        }

        /** @brief A container duration in 100-nanosecond ticks, or 0 when it declares none. */
        [[nodiscard]] std::int64_t Ticks(std::int64_t duration, AVRational base)
        {
            if (duration == AV_NOPTS_VALUE || duration <= 0)
            {
                return 0;
            }
            // 100-nanosecond ticks: seconds * 10^7, computed in the stream's own time base.
            return av_rescale_q(duration, base, AVRational{1, 10000000});
        }
    }

    bool IsAvailable() noexcept { return true; }

    std::string UnavailableReason() { return std::string(); }

    std::string Identity()
    {
        return std::string("libavformat ") + AV_STRINGIFY(LIBAVFORMAT_VERSION) + " libavcodec " +
               AV_STRINGIFY(LIBAVCODEC_VERSION) + " libswresample " + AV_STRINGIFY(LIBSWRESAMPLE_VERSION);
    }

    DecodedAudio DecodeAudio(const std::string& filename, const int forcedSampleRate,
                             const AudioSourceFormat required)
    {
        FormatContext format = Open(filename);
        const AVCodec* codec = nullptr;
        const int index = av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (index < 0 || codec == nullptr)
        {
            throw std::runtime_error("the file carries no audio stream");
        }
        AVStream* stream = format->streams[index];
        // The importer that asked names the format it will accept, because XNA's are
        // format-specific rather than extension-specific: a WAV renamed .mp3 is refused. A
        // demuxer will happily read whatever it recognizes, so the refusal has to be here.
        if (required != AudioSourceFormat::Any)
        {
            const AVCodecID codecId = stream->codecpar->codec_id;
            const bool matches =
                required == AudioSourceFormat::Mpeg
                    ? (codecId == AV_CODEC_ID_MP3 || codecId == AV_CODEC_ID_MP2 ||
                       codecId == AV_CODEC_ID_MP1 || codecId == AV_CODEC_ID_MP3ADU ||
                       codecId == AV_CODEC_ID_MP3ON4)
                    : (codecId == AV_CODEC_ID_WMAV1 || codecId == AV_CODEC_ID_WMAV2 ||
                       codecId == AV_CODEC_ID_WMAPRO || codecId == AV_CODEC_ID_WMALOSSLESS ||
                       codecId == AV_CODEC_ID_WMAVOICE);
            if (!matches)
            {
                throw std::runtime_error("the file's audio is not of the format this importer reads");
            }
        }

        CodecContext decoder(avcodec_alloc_context3(codec));
        if (decoder == nullptr || avcodec_parameters_to_context(decoder.get(), stream->codecpar) < 0)
        {
            throw std::runtime_error("the audio stream could not be decoded");
        }
        // XNA does not honour an MP3's gapless-playback information: it reports the whole decoded
        // stream, encoder delay and padding included, so a half-second tone is 548 ms and a file
        // carrying a Xing/LAME header is 574 ms rather than the 500 ms of actual content
        // (measured, tests/reference/xna40/media cases mp3/mp3_mono_44100_tagged.mp3 and
        // mp3/mp3_mono_44100_vbr.mp3). SKIP_MANUAL makes the decoder report the skip as side data
        // instead of applying it, which is what leaves those frames in.
        decoder->flags2 |= AV_CODEC_FLAG2_SKIP_MANUAL;
        if (avcodec_open2(decoder.get(), codec, nullptr) < 0)
        {
            throw std::runtime_error("the audio stream could not be decoded");
        }

        DecodedAudio decoded;
        decoded.channels = decoder->ch_layout.nb_channels;
        decoded.sampleRate = forcedSampleRate > 0 ? forcedSampleRate : decoder->sample_rate;
        if (decoded.channels <= 0 || decoded.sampleRate <= 0)
        {
            throw std::runtime_error("the audio stream declares no channels or no sample rate");
        }
        decoded.durationTicks = Ticks(stream->duration, stream->time_base);
        if (decoded.durationTicks == 0)
        {
            decoded.durationTicks = Ticks(format->duration, AVRational{1, AV_TIME_BASE});
        }

        AVChannelLayout outLayout{};
        av_channel_layout_default(&outLayout, decoded.channels);
        SwrContext* rawResampler = nullptr;
        if (swr_alloc_set_opts2(&rawResampler, &outLayout, AV_SAMPLE_FMT_S16, decoded.sampleRate,
                                &decoder->ch_layout, decoder->sample_fmt, decoder->sample_rate, 0,
                                nullptr) < 0 ||
            swr_init(rawResampler) < 0)
        {
            swr_free(&rawResampler);
            av_channel_layout_uninit(&outLayout);
            throw std::runtime_error("the audio stream could not be converted to signed 16-bit PCM");
        }
        Resampler resampler(rawResampler);

        Packet packet(av_packet_alloc());
        Frame frame(av_frame_alloc());
        std::vector<std::uint8_t> converted;
        const auto append = [&decoded, &converted, &resampler](const AVFrame* input, int samples)
        {
            const int capacity = samples > 0 ? samples : swr_get_out_samples(resampler.get(), 0);
            if (capacity <= 0)
            {
                return;
            }
            converted.assign(static_cast<std::size_t>(capacity) * decoded.channels * 2u, 0u);
            std::uint8_t* output = converted.data();
            const int written = swr_convert(resampler.get(), &output, capacity,
                                            input == nullptr ? nullptr : input->extended_data,
                                            input == nullptr ? 0 : input->nb_samples);
            if (written <= 0)
            {
                return;
            }
            const std::size_t bytes = static_cast<std::size_t>(written) * decoded.channels * 2u;
            if (decoded.pcm.size() + bytes > MaximumDecodedBytes)
            {
                throw std::runtime_error("the audio stream decodes to more than an hour of samples");
            }
            decoded.pcm.insert(decoded.pcm.end(), converted.begin(), converted.begin() + bytes);
        };

        while (av_read_frame(format.get(), packet.get()) >= 0)
        {
            if (packet->stream_index == index && avcodec_send_packet(decoder.get(), packet.get()) >= 0)
            {
                while (avcodec_receive_frame(decoder.get(), frame.get()) >= 0)
                {
                    append(frame.get(), swr_get_out_samples(resampler.get(), frame->nb_samples));
                    av_frame_unref(frame.get());
                }
            }
            av_packet_unref(packet.get());
        }
        // Flush the decoder, then the resampler: a source whose tail sits in either is otherwise
        // silently short, which is exactly the kind of defect a duration test does not catch.
        if (avcodec_send_packet(decoder.get(), nullptr) >= 0)
        {
            while (avcodec_receive_frame(decoder.get(), frame.get()) >= 0)
            {
                append(frame.get(), swr_get_out_samples(resampler.get(), frame->nb_samples));
                av_frame_unref(frame.get());
            }
        }
        append(nullptr, 0);
        av_channel_layout_uninit(&outLayout);

        if (decoded.pcm.empty())
        {
            throw std::runtime_error("the audio stream decoded to no samples at all");
        }
        return decoded;
    }

    ProbedVideo ProbeVideo(const std::string& filename)
    {
        FormatContext format = Open(filename);
        const int index = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (index < 0)
        {
            throw std::runtime_error("the file carries no video stream");
        }
        const AVStream* stream = format->streams[index];

        ProbedVideo probed;
        probed.width = stream->codecpar->width;
        probed.height = stream->codecpar->height;
        if (probed.width <= 0 || probed.height <= 0)
        {
            throw std::runtime_error("the video stream declares no frame size");
        }
        const AVRational rate = stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0
                                    ? stream->avg_frame_rate
                                    : stream->r_frame_rate;
        probed.framesPerSecond = rate.den > 0 ? static_cast<float>(av_q2d(rate)) : 0.0f;
        probed.bitsPerSecond = static_cast<int>(format->bit_rate > 0 ? format->bit_rate
                                                                    : stream->codecpar->bit_rate);
        probed.durationTicks = Ticks(stream->duration, stream->time_base);
        if (probed.durationTicks == 0)
        {
            probed.durationTicks = Ticks(format->duration, AVRational{1, AV_TIME_BASE});
        }
        probed.hasAudio = av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) >= 0;
        return probed;
    }

    void EncodeWindowsMediaAudio(const std::vector<std::uint8_t>& pcm, const int channels,
                                 const int sampleRate, const int bitsPerSecond,
                                 const std::string& filename)
    {
        if (channels <= 0 || sampleRate <= 0 || pcm.empty())
        {
            throw std::runtime_error("the samples to encode are empty or declare no shape");
        }
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_WMAV2);
        if (codec == nullptr)
        {
            throw std::runtime_error("this build's libavcodec carries no Windows Media audio encoder");
        }
        AVFormatContext* rawFormat = nullptr;
        if (avformat_alloc_output_context2(&rawFormat, nullptr, "asf", filename.c_str()) < 0 ||
            rawFormat == nullptr)
        {
            throw std::runtime_error("an ASF container could not be created");
        }
        FormatContext format(rawFormat);
        AVStream* stream = avformat_new_stream(format.get(), nullptr);
        CodecContext encoder(avcodec_alloc_context3(codec));
        if (stream == nullptr || encoder == nullptr)
        {
            throw std::runtime_error("the Windows Media audio stream could not be created");
        }
        // The Windows Media encoder takes planar float and nothing else, so the caller's
        // interleaved PCM16 is converted on the way in rather than being handed over as it is --
        // which is what avcodec_open2 refuses, and the refusal is indistinguishable from "no
        // encoder" unless it is dealt with here.
        encoder->sample_fmt = AV_SAMPLE_FMT_FLTP;
        for (const AVSampleFormat* supported = codec->sample_fmts;
             supported != nullptr && *supported != AV_SAMPLE_FMT_NONE; ++supported)
        {
            if (*supported == AV_SAMPLE_FMT_S16)
            {
                encoder->sample_fmt = AV_SAMPLE_FMT_S16;
                break;
            }
        }
        encoder->sample_rate = sampleRate;
        encoder->bit_rate = bitsPerSecond > 0 ? bitsPerSecond : 128000;
        av_channel_layout_default(&encoder->ch_layout, channels);
        if ((format->oformat->flags & AVFMT_GLOBALHEADER) != 0)
        {
            encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        if (avcodec_open2(encoder.get(), codec, nullptr) < 0 ||
            avcodec_parameters_from_context(stream->codecpar, encoder.get()) < 0)
        {
            throw std::runtime_error("the Windows Media audio encoder could not be opened");
        }
        stream->time_base = AVRational{1, sampleRate};
        if (avio_open(&format->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0 ||
            avformat_write_header(format.get(), nullptr) < 0)
        {
            throw std::runtime_error("the song file could not be written");
        }

        const int frameSamples = encoder->frame_size > 0 ? encoder->frame_size : 1024;
        const std::size_t frameBytes = static_cast<std::size_t>(frameSamples) * channels * 2u;
        Frame frame(av_frame_alloc());
        Packet packet(av_packet_alloc());
        frame->format = encoder->sample_fmt;
        frame->nb_samples = frameSamples;
        frame->sample_rate = sampleRate;
        av_channel_layout_copy(&frame->ch_layout, &encoder->ch_layout);
        if (av_frame_get_buffer(frame.get(), 0) < 0)
        {
            throw std::runtime_error("the encoder's frame buffer could not be allocated");
        }

        Resampler toEncoderFormat;
        if (encoder->sample_fmt != AV_SAMPLE_FMT_S16)
        {
            SwrContext* raw = nullptr;
            if (swr_alloc_set_opts2(&raw, &encoder->ch_layout, encoder->sample_fmt, sampleRate,
                                    &encoder->ch_layout, AV_SAMPLE_FMT_S16, sampleRate, 0,
                                    nullptr) < 0 ||
                swr_init(raw) < 0)
            {
                swr_free(&raw);
                throw std::runtime_error("the samples could not be converted for the encoder");
            }
            toEncoderFormat.reset(raw);
        }

        const auto drain = [&format, &encoder, &packet, stream]()
        {
            while (avcodec_receive_packet(encoder.get(), packet.get()) >= 0)
            {
                av_packet_rescale_ts(packet.get(), encoder->time_base, stream->time_base);
                packet->stream_index = stream->index;
                av_interleaved_write_frame(format.get(), packet.get());
                av_packet_unref(packet.get());
            }
        };

        std::int64_t position = 0;
        for (std::size_t at = 0; at < pcm.size(); at += frameBytes)
        {
            if (av_frame_make_writable(frame.get()) < 0)
            {
                throw std::runtime_error("the encoder's frame buffer could not be written");
            }
            const std::size_t take = std::min(frameBytes, pcm.size() - at);
            // A short tail is padded with silence rather than truncated, so the encoded song is
            // as long as the source rather than a frame shorter.
            std::vector<std::uint8_t> block(frameBytes, 0u);
            std::memcpy(block.data(), pcm.data() + at, take);
            if (toEncoderFormat == nullptr)
            {
                std::memcpy(frame->data[0], block.data(), frameBytes);
            }
            else
            {
                const std::uint8_t* input = block.data();
                if (swr_convert(toEncoderFormat.get(), frame->data, frameSamples, &input,
                                frameSamples) < 0)
                {
                    throw std::runtime_error("the samples could not be converted for the encoder");
                }
            }
            frame->pts = position;
            position += frameSamples;
            if (avcodec_send_frame(encoder.get(), frame.get()) < 0)
            {
                throw std::runtime_error("the samples could not be encoded");
            }
            drain();
        }
        avcodec_send_frame(encoder.get(), nullptr);
        drain();
        av_write_trailer(format.get());
        avio_closep(&format->pb);
    }
#endif
}
