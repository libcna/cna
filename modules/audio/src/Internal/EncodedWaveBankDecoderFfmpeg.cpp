// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/EncodedWaveBankDecoder.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace CNA::Internal::Audio
{
    namespace
    {
        using Bytes = std::vector<std::uint8_t>;

        void U16(Bytes& bytes, std::uint16_t value)
        {
            bytes.push_back(static_cast<std::uint8_t>(value));
            bytes.push_back(static_cast<std::uint8_t>(value >> 8));
        }
        void U32(Bytes& bytes, std::uint32_t value)
        {
            U16(bytes, static_cast<std::uint16_t>(value));
            U16(bytes, static_cast<std::uint16_t>(value >> 16));
        }
        void Tag(Bytes& bytes, const char* value) { bytes.insert(bytes.end(), value, value + 4); }
        void Chunk(Bytes& bytes, const char* tag, const Bytes& contents)
        {
            Tag(bytes, tag); U32(bytes, static_cast<std::uint32_t>(contents.size()));
            bytes.insert(bytes.end(), contents.begin(), contents.end());
            if (contents.size() & 1) bytes.push_back(0);
        }

        // FACT.h:438-470. The original packed byte is not PCM block alignment.
        constexpr std::uint16_t blockAlignments[] = {
            929, 1487, 1280, 2230, 8917, 8192, 4459, 5945, 2304,
            1536, 1485, 1008, 2731, 4096, 6827, 5462, 1280
        };
        constexpr std::uint32_t byteRates[] = {12000, 24000, 4000, 6000, 8000, 20000, 2500};

        Bytes Descriptor(const XwbEntry& entry, std::size_t encodedBytes, bool xma)
        {
            Bytes format, seeks;
            const auto packed = (entry.miniFormat >> 23) & 255;
            if (xma)
            {
                // Modern XACT XMA2 descriptor, FACT.c:1765-1798 and DirectXTK
                // WaveBankReader::GetFormat:834-895. Dispatch remains gated below
                // until a lawful native fixture establishes priming and metadata.
                if (entry.seekTable.empty() || entry.seekTable.size() > 65535 || encodedBytes % 2048 != 0)
                    throw std::runtime_error("XMA2 block/seek metadata is invalid");
                U16(format, 0x166); U16(format, entry.channels); U32(format, entry.sampleRate);
                U32(format, entry.sampleRate * entry.channels * 2); U16(format, entry.channels * 2);
                U16(format, 16); U16(format, 34); U16(format, (entry.channels + 1) / 2);
                U32(format, entry.channels == 1 ? 4 : entry.channels == 2 ? 3 : 0x3f);
                U32(format, entry.seekTable.back()); U32(format, 65536);
                U32(format, 0); U32(format, entry.durationSamples ? entry.durationSamples : entry.seekTable.back());
                U32(format, 0); U32(format, 0); format.push_back(0); format.push_back(4);
                U16(format, static_cast<std::uint16_t>(entry.seekTable.size()));
            }
            else
            {
                if ((packed & 31) >= std::size(blockAlignments) || (packed >> 5) >= std::size(byteRates))
                    throw std::runtime_error("WMA packed block alignment or byte-rate index is invalid");
                // WMA Pro is selected by bit31, not inferred from channel count.
                U16(format, (entry.miniFormat & 0x80000000u) ? 0x162 : 0x161);
                U16(format, entry.channels); U32(format, entry.sampleRate);
                U32(format, byteRates[packed >> 5]); U16(format, blockAlignments[packed & 31]);
                U16(format, 16); U16(format, 0);
                const std::uint64_t packets = (encodedBytes + blockAlignments[packed & 31] - 1) /
                    blockAlignments[packed & 31];
                if (!entry.seekTable.empty() && entry.seekTable.size() != packets)
                    throw std::runtime_error("WMA packet count does not match its dpds table");
                for (auto value : entry.seekTable)
                {
                    if (value % (2 * entry.channels) != 0)
                        throw std::runtime_error("WMA dpds value is not a decoded whole-frame byte count");
                    U32(seeks, value);
                }
            }
            Bytes body;
            Tag(body, xma ? "WAVE" : "XWMA"); Chunk(body, "fmt ", format);
            if (!xma && !seeks.empty()) Chunk(body, "dpds", seeks);
            Tag(body, "data"); U32(body, static_cast<std::uint32_t>(encodedBytes));
            Bytes header;
            Tag(header, "RIFF"); U32(header, static_cast<std::uint32_t>(body.size() + encodedBytes));
            header.insert(header.end(), body.begin(), body.end());
            return header;
        }

        // AVIO sees only one synthetic descriptor and the exact entry span. No
        // pathname/URL callback or byte outside the entry is accessible to FFmpeg.
        struct EntryInput
        {
            Bytes header;
            std::span<const std::uint8_t> data;
            std::int64_t position = 0;
            std::int64_t Size() const { return static_cast<std::int64_t>(header.size() + data.size()); }
            static int Read(void* opaque, std::uint8_t* output, int requested)
            {
                auto& input = *static_cast<EntryInput*>(opaque);
                if (requested <= 0) return AVERROR(EINVAL);
                if (input.position == input.Size()) return AVERROR_EOF;
                const int count = static_cast<int>(std::min<std::int64_t>(requested, input.Size() - input.position));
                int written = 0;
                if (input.position < static_cast<std::int64_t>(input.header.size()))
                {
                    written = std::min<int>(count, static_cast<int>(input.header.size() - input.position));
                    std::memcpy(output, input.header.data() + input.position, written);
                    input.position += written;
                }
                if (written < count)
                {
                    std::memcpy(output + written, input.data.data() + input.position - input.header.size(), count - written);
                    input.position += count - written;
                }
                return count;
            }
            static std::int64_t Seek(void* opaque, std::int64_t offset, int whence)
            {
                auto& input = *static_cast<EntryInput*>(opaque);
                if (whence & AVSEEK_SIZE) return input.Size();
                whence &= ~AVSEEK_FORCE;
                const auto base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? input.position :
                    whence == SEEK_END ? input.Size() : -1;
                if (base < 0 || offset < -base || offset > input.Size() - base) return AVERROR(EINVAL);
                input.position = base + offset;
                return input.position;
            }
        };

        struct Resources
        {
            AVFormatContext* format = nullptr;
            AVIOContext* io = nullptr;
            AVCodecContext* codec = nullptr;
            AVFrame* frame = nullptr;
            AVPacket* packet = nullptr;
            SwrContext* convert = nullptr;
            ~Resources()
            {
                av_packet_free(&packet); av_frame_free(&frame); swr_free(&convert);
                avcodec_free_context(&codec); avformat_close_input(&format);
                if (io) av_freep(&io->buffer);
                avio_context_free(&io);
            }
        };

        void Require(int result, const char* action)
        {
            if (result >= 0) return;
            std::array<char, AV_ERROR_MAX_STRING_SIZE> error{};
            av_strerror(result, error.data(), error.size());
            throw std::runtime_error(std::string(action) + ": " + error.data());
        }

        std::vector<std::int16_t> Decode(const XwbEntry& entry, std::span<const std::uint8_t> encoded,
                                          AVCodecID expected, std::uint64_t& actualFrames)
        {
            const bool xma = expected == AV_CODEC_ID_XMA2;
            EntryInput input{Descriptor(entry, encoded.size(), xma), encoded};
            Resources resources;
            resources.format = avformat_alloc_context();
            if (!resources.format) throw std::bad_alloc();
            auto* buffer = static_cast<std::uint8_t*>(av_malloc(4096));
            if (!buffer) throw std::bad_alloc();
            resources.io = avio_alloc_context(buffer, 4096, 0, &input, EntryInput::Read, nullptr, EntryInput::Seek);
            if (!resources.io) { av_free(buffer); throw std::bad_alloc(); }
            resources.format->pb = resources.io;
            resources.format->flags |= AVFMT_FLAG_CUSTOM_IO;
            const auto* demuxer = av_find_input_format(xma ? "wav" : "xwma");
            if (!demuxer) throw std::runtime_error("required FFmpeg entry demuxer is unavailable");
            Require(avformat_open_input(&resources.format, nullptr, demuxer, nullptr), "open encoded entry");
            if (resources.format->nb_streams != 1) throw std::runtime_error("encoded entry must have exactly one stream");
            const auto* parameters = resources.format->streams[0]->codecpar;
            if (parameters->codec_id != expected) throw std::runtime_error("encoded WaveBank codec dispatch mismatch");
            const auto* decoder = avcodec_find_decoder(expected);
            if (!decoder) throw std::runtime_error("requested FFmpeg codec decoder is unavailable");
            resources.codec = avcodec_alloc_context3(decoder);
            if (!resources.codec) throw std::bad_alloc();
            Require(avcodec_parameters_to_context(resources.codec, parameters), "copy entry codec parameters");
            resources.codec->err_recognition = AV_EF_EXPLODE | AV_EF_CAREFUL;
            resources.codec->thread_count = 1;
            Require(avcodec_open2(resources.codec, decoder, nullptr), "open entry codec");
            resources.frame = av_frame_alloc(); resources.packet = av_packet_alloc();
            if (!resources.frame || !resources.packet) throw std::bad_alloc();

            std::vector<std::int16_t> pcm;
            std::vector<std::int16_t> converted;
            auto convertFrame = [&](const AVFrame* frame)
            {
                if (frame->sample_rate != static_cast<int>(entry.sampleRate) ||
                    frame->ch_layout.nb_channels != entry.channels || (frame->flags & AV_FRAME_FLAG_CORRUPT))
                    throw std::runtime_error("decoded frame format differs from WaveBank metadata or is corrupt");
                if (!resources.convert)
                {
                    AVChannelLayout layout{};
                    if (frame->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
                        av_channel_layout_default(&layout, entry.channels);
                    else Require(av_channel_layout_copy(&layout, &frame->ch_layout), "copy entry channel layout");
                    const int result = swr_alloc_set_opts2(&resources.convert, &layout, AV_SAMPLE_FMT_S16,
                        entry.sampleRate, &layout, static_cast<AVSampleFormat>(frame->format), entry.sampleRate, 0, nullptr);
                    av_channel_layout_uninit(&layout);
                    Require(result, "allocate PCM conversion");
                    Require(av_opt_set_int(resources.convert, "dither_method", SWR_DITHER_NONE, 0), "disable PCM dither");
                    Require(swr_init(resources.convert), "initialize PCM conversion");
                }
                const int capacity = swr_get_out_samples(resources.convert, frame->nb_samples);
                Require(capacity, "size PCM conversion");
                if (static_cast<std::uint64_t>(capacity) * entry.channels * 2 > EncodedWaveBankByteLimit)
                    throw std::runtime_error("decoded frame exceeds the 256 MiB WaveBank budget");
                converted.resize(static_cast<std::size_t>(capacity) * entry.channels);
                std::uint8_t* output = reinterpret_cast<std::uint8_t*>(converted.data());
                std::array<const std::uint8_t*, AV_NUM_DATA_POINTERS> source{};
                const auto planes = av_sample_fmt_is_planar(static_cast<AVSampleFormat>(frame->format))
                    ? static_cast<std::size_t>(entry.channels) : 1u;
                for (std::size_t i = 0; i < planes; ++i) source[i] = frame->extended_data[i];
                const int count = swr_convert(resources.convert, &output, capacity, source.data(), frame->nb_samples);
                Require(count, "convert entry PCM");
                actualFrames += static_cast<std::uint64_t>(count);
                if (actualFrames * entry.channels * 2 > EncodedWaveBankByteLimit)
                    throw std::runtime_error("decoded entry exceeds the 256 MiB WaveBank budget");
                pcm.insert(pcm.end(), converted.begin(), converted.begin() + static_cast<std::size_t>(count) * entry.channels);
            };
            auto receive = [&]()
            {
                for (;;)
                {
                    const int result = avcodec_receive_frame(resources.codec, resources.frame);
                    if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return result;
                    Require(result, "decode entry frame");
                    convertFrame(resources.frame); av_frame_unref(resources.frame);
                }
            };
            int packetResult;
            while ((packetResult = av_read_frame(resources.format, resources.packet)) >= 0)
            {
                int result = avcodec_send_packet(resources.codec, resources.packet);
                if (result == AVERROR(EAGAIN)) { receive(); result = avcodec_send_packet(resources.codec, resources.packet); }
                Require(result, "submit encoded packet"); receive(); av_packet_unref(resources.packet);
            }
            if (packetResult != AVERROR_EOF) Require(packetResult, "read encoded packet");
            Require(avcodec_send_packet(resources.codec, nullptr), "drain encoded entry"); receive();
            if (resources.convert && swr_get_delay(resources.convert, entry.sampleRate) != 0)
                throw std::runtime_error("unexpected delayed frames in native-rate PCM conversion");
            return pcm;
        }
    }

    std::unique_ptr<EncodedWaveBankDecoder> OpenEncodedWaveBankDecoder(
        const XwbData& bank, std::size_t entryIndex, std::span<const std::uint8_t> encodedEntry)
    {
        if (entryIndex >= bank.entries.size()) throw std::runtime_error("WaveBank entry index is invalid");
        const auto& entry = bank.entries[entryIndex];
        if (entry.format != XwbFormat::WMA && entry.format != XwbFormat::XMA)
            throw std::runtime_error("WaveBank adapter requires WMA/xWMA or XMA/XMA2");
        const bool xma = entry.format == XwbFormat::XMA;
        const char* name = xma ? "XMA/XMA2" : entry.miniFormat & 0x80000000u ? "WMA Pro" : "WMA/WMAv2";
        try
        {
            // Owner Phase-2 decision: the shared FFmpeg pipeline supports XMA
            // packets, but no XMA entry is enabled before lawful native-fixture
            // qualification. In particular, do not guess legacy XMA1 metadata.
            constexpr bool xmaQualified = false;
            if (xma && !xmaQualified)
                throw std::runtime_error("XMA/XMA2 decoding is unqualified: a lawful native fixture is required");
            // Phase-3 owner fallback: the genuine SDK Pro fixture resamples
            // 44100 -> 48000Hz. Its authored Duration/dpds specify37713 frames,
            // while independent FFmpeg returns35665 (2048 fewer). FACT.c:1879
            // forwards entry->Duration; do not silently shorten, pad or shift
            // that native timeline. See docs/wavebank-codecs.md. A shorter
            // metadata derivative must not bypass this qualification gate.
            constexpr bool wmaProQualified = false;
            if (!xma && (entry.miniFormat & 0x80000000u) && !wmaProQualified)
                throw std::runtime_error("WMA Pro decoding is unqualified: native duration/priming policy is unresolved");
            if (xma && (bank.version < 44 || bank.version > 46 || bank.headerVersion != 44 ||
                        (entry.miniFormat & 0x80000000u) == 0))
                throw std::runtime_error("legacy XMA/XMA1 metadata is unqualified; codec inference is not permitted");
            if ((entry.miniFormat & 3) != static_cast<unsigned>(entry.format) ||
                entry.channels == 0 || (entry.channels != 1 && entry.channels != 2 && entry.channels != 6) ||
                entry.channels != ((entry.miniFormat >> 2) & 7) ||
                entry.sampleRate != ((entry.miniFormat >> 5) & 0x3ffff) ||
                entry.sampleRate < 8000 || entry.sampleRate > 48000)
                throw std::runtime_error("invalid native format metadata");
            if (encodedEntry.empty() || encodedEntry.size() != entry.dataLength ||
                encodedEntry.size() > EncodedWaveBankByteLimit)
                throw std::runtime_error("encoded PlayRegion is truncated, empty or exceeds 256 MiB");
            if (static_cast<std::uint64_t>(entry.durationSamples) * entry.channels * 2 > EncodedWaveBankByteLimit)
                throw std::runtime_error("authored duration exceeds the 256 MiB decode budget");
            if (!std::is_sorted(entry.seekTable.begin(), entry.seekTable.end()))
                throw std::runtime_error("seek-table cumulative values decrease");
            const auto codec = xma ? AV_CODEC_ID_XMA2 : entry.miniFormat & 0x80000000u ? AV_CODEC_ID_WMAPRO : AV_CODEC_ID_WMAV2;
            std::uint64_t actualFrames = 0;
            auto pcm = Decode(entry, encodedEntry, codec, actualFrames);
            std::uint64_t playable = entry.durationSamples;
            if (!playable && !entry.seekTable.empty())
                playable = xma ? entry.seekTable.back() : entry.seekTable.back() / (entry.channels * 2);
            if (!playable) playable = actualFrames;
            if (playable == 0 || playable > actualFrames)
                throw std::runtime_error("decoder ended before the declared playable frames");
            if (static_cast<std::uint64_t>(entry.loopStartSample) + entry.loopTotalSamples > playable)
                throw std::runtime_error("loop region exceeds decoded frames");
            pcm.resize(static_cast<std::size_t>(playable) * entry.channels);
            return std::unique_ptr<EncodedWaveBankDecoder>(new EncodedWaveBankDecoder(
                xma ? EncodedWaveBankCodec::Xma2 : codec == AV_CODEC_ID_WMAPRO ? EncodedWaveBankCodec::WmaPro : EncodedWaveBankCodec::Wma2,
                entry.sampleRate, entry.channels, std::move(pcm)));
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(std::string(name) + ": " + error.what());
        }
    }
}
