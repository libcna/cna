// SPDX-License-Identifier: MS-PL
#include "Backend/CnaMixer/CnaMixer.hpp"
#include "Backend/CnaMixer/DrLibsApi.hpp"
#include "Backend/CnaMixer/StbVorbisApi.hpp"

#include "CNA/Internal/Audio/WavDecoder.hpp"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Audio
{
    /**
     * @brief One track's decoder over shared encoded file bytes: Vorbis, MP3 or FLAC.
     *
     * Decodes a block at a time into floats and hands them out a frame at a time; the file's
     * bytes are the MixerAudioData's, shared by every track playing it.
     */
    class EncodedCursor
    {
    public:
        explicit EncodedCursor(std::shared_ptr<const MixerAudioData> data)
            : data_(std::move(data)), channels_(data_->channels)
        {
            buffer_.resize(static_cast<std::size_t>(kBufferFrames) * static_cast<std::size_t>(channels_));
        }

        virtual ~EncodedCursor() = default;

        EncodedCursor(const EncodedCursor&) = delete;
        EncodedCursor& operator=(const EncodedCursor&) = delete;

        bool Read(std::array<float, 2>& frame) noexcept
        {
            if (index_ == count_)
            {
                const int frames = Decode(buffer_.data(), kBufferFrames);
                if (frames <= 0)
                {
                    return false;
                }
                count_ = frames;
                index_ = 0;
            }
            const float* source = buffer_.data() + static_cast<std::ptrdiff_t>(index_) * channels_;
            frame = {source[0], channels_ > 1 ? source[1] : source[0]};
            ++index_;
            return true;
        }

        bool Seek(const std::uint64_t frame) noexcept
        {
            index_ = 0;
            count_ = 0;
            return SeekTo(frame);
        }

    protected:
        /// Decodes up to @p frames interleaved frames; the count decoded, 0 at the end.
        virtual int Decode(float* destination, int frames) noexcept = 0;
        virtual bool SeekTo(std::uint64_t frame) noexcept = 0;

        std::shared_ptr<const MixerAudioData> data_;
        int channels_;

    private:
        static constexpr int kBufferFrames = 1024;

        std::vector<float> buffer_;
        int index_ = 0;
        int count_ = 0;
    };

    void EncodedCursorDeleter::operator()(EncodedCursor* cursor) const noexcept
    {
        delete cursor;
    }

    namespace
    {
        class VorbisCursor final : public EncodedCursor
        {
        public:
            VorbisCursor(std::shared_ptr<const MixerAudioData> data, stb_vorbis* vorbis)
                : EncodedCursor(std::move(data)), vorbis_(vorbis)
            {
            }

            ~VorbisCursor() override { stb_vorbis_close(vorbis_); }

        protected:
            int Decode(float* destination, const int frames) noexcept override
            {
                return stb_vorbis_get_samples_float_interleaved(vorbis_, channels_, destination,
                                                                frames * channels_);
            }

            bool SeekTo(const std::uint64_t frame) noexcept override
            {
                if (frame == 0)
                {
                    return stb_vorbis_seek_start(vorbis_) != 0;
                }
                if (frame > 0xFFFFFFFFull)
                {
                    return false;
                }
                return stb_vorbis_seek(vorbis_, static_cast<unsigned int>(frame)) != 0;
            }

        private:
            stb_vorbis* vorbis_;
        };

        class Mp3Cursor final : public EncodedCursor
        {
        public:
            explicit Mp3Cursor(std::shared_ptr<const MixerAudioData> data) : EncodedCursor(std::move(data)) {}

            ~Mp3Cursor() override
            {
                if (open_) { drmp3_uninit(&mp3_); }
            }

            bool Open() noexcept
            {
                open_ = drmp3_init_memory(&mp3_, data_->encoded.data(), data_->encoded.size(), nullptr) != 0;
                return open_ && static_cast<int>(mp3_.channels) == channels_;
            }

        protected:
            int Decode(float* destination, const int frames) noexcept override
            {
                return static_cast<int>(
                    drmp3_read_pcm_frames_f32(&mp3_, static_cast<drmp3_uint64>(frames), destination));
            }

            bool SeekTo(const std::uint64_t frame) noexcept override
            {
                return drmp3_seek_to_pcm_frame(&mp3_, frame) != 0;
            }

        private:
            drmp3 mp3_{};
            bool open_ = false;
        };

        class FlacCursor final : public EncodedCursor
        {
        public:
            explicit FlacCursor(std::shared_ptr<const MixerAudioData> data) : EncodedCursor(std::move(data)) {}

            ~FlacCursor() override
            {
                if (flac_ != nullptr) { drflac_close(flac_); }
            }

            bool Open() noexcept
            {
                flac_ = drflac_open_memory(data_->encoded.data(), data_->encoded.size(), nullptr);
                return flac_ != nullptr && static_cast<int>(flac_->channels) == channels_;
            }

        protected:
            int Decode(float* destination, const int frames) noexcept override
            {
                return static_cast<int>(
                    drflac_read_pcm_frames_f32(flac_, static_cast<drflac_uint64>(frames), destination));
            }

            bool SeekTo(const std::uint64_t frame) noexcept override
            {
                return drflac_seek_to_pcm_frame(flac_, frame) != 0;
            }

        private:
            drflac* flac_ = nullptr;
        };
    }

    namespace
    {
        /// memcpy, except that an empty copy touches nothing: an empty vector's data() may be null,
        /// and memcpy with a null pointer is undefined even for zero bytes (glibc declares both
        /// arguments nonnull). A zero-length SoundEffect is legal and reaches here.
        void CopyBytes(void* destination, const void* source, const std::size_t bytes) noexcept
        {
            if (bytes != 0)
            {
                std::memcpy(destination, source, bytes);
            }
        }

        bool StartsWith(const std::span<const std::byte> bytes, const char* magic)
        {
            const std::size_t length = std::strlen(magic);
            return bytes.size() >= length && std::memcmp(bytes.data(), magic, length) == 0;
        }

        bool ContainsWithin(const std::span<const std::byte> bytes, const char* needle,
                            const std::size_t limit)
        {
            const std::size_t length = std::strlen(needle);
            const std::size_t end = std::min(bytes.size(), limit);
            for (std::size_t offset = 0; offset + length <= end; ++offset)
            {
                if (std::memcmp(bytes.data() + offset, needle, length) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        std::string DescribeVorbisError(const int error)
        {
            switch (error)
            {
                case VORBIS_outofmem: return "out of memory";
                case VORBIS_unexpected_eof: return "the file ends early";
                case VORBIS_invalid_setup:
                case VORBIS_invalid_stream:
                case VORBIS_invalid_first_page:
                case VORBIS_invalid_stream_structure_version: return "the stream is damaged";
                case VORBIS_missing_capture_pattern: return "not an Ogg stream";
                default: return "stb_vorbis error " + std::to_string(error);
            }
        }

        std::shared_ptr<MixerAudioData> DecodeWav(const std::span<const std::byte> bytes,
                                                  const std::string& origin)
        {
            const DecodedWavPcm16 decoded = DecodeWavToPcm16(
                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                              bytes.size()),
                origin);
            auto data = std::make_shared<MixerAudioData>();
            data->encoding = MixerAudioData::Encoding::Pcm16;
            data->sampleRate = static_cast<int>(decoded.sampleRate);
            data->channels = decoded.channels;
            data->pcm16.resize(decoded.samples.size() / sizeof(std::int16_t));
            CopyBytes(data->pcm16.data(), decoded.samples.data(),
                      data->pcm16.size() * sizeof(std::int16_t));
            data->frames = data->channels > 0
                               ? static_cast<std::int64_t>(data->pcm16.size()) / data->channels
                               : 0;
            return data;
        }

        std::shared_ptr<MixerAudioData> DecodeVorbis(const std::span<const std::byte> bytes,
                                                     const bool predecode, std::string& error)
        {
            if (bytes.size() > static_cast<std::size_t>(0x7FFFFFFF))
            {
                error = "the Ogg Vorbis file is larger than 2 GiB";
                return nullptr;
            }
            const auto* raw = reinterpret_cast<const unsigned char*>(bytes.data());
            const int length = static_cast<int>(bytes.size());

            int code = 0;
            stb_vorbis* probe = stb_vorbis_open_memory(raw, length, &code, nullptr);
            if (probe == nullptr)
            {
                error = "not a decodable Ogg Vorbis stream: " + DescribeVorbisError(code);
                return nullptr;
            }
            const stb_vorbis_info info = stb_vorbis_get_info(probe);
            const unsigned int frames = stb_vorbis_stream_length_in_samples(probe);
            stb_vorbis_close(probe);
            if (info.channels <= 0 || info.sample_rate == 0)
            {
                error = "the Ogg Vorbis stream declares no channels or no rate";
                return nullptr;
            }

            auto data = std::make_shared<MixerAudioData>();
            data->sampleRate = static_cast<int>(info.sample_rate);
            data->channels = info.channels;
            if (predecode)
            {
                int channels = 0;
                int rate = 0;
                short* samples = nullptr;
                const int decoded = stb_vorbis_decode_memory(raw, length, &channels, &rate, &samples);
                if (decoded < 0 || samples == nullptr)
                {
                    std::free(samples);
                    error = "the Ogg Vorbis stream could not be decoded";
                    return nullptr;
                }
                data->encoding = MixerAudioData::Encoding::Pcm16;
                data->channels = channels;
                data->sampleRate = rate;
                data->pcm16.assign(samples, samples + static_cast<std::ptrdiff_t>(decoded) * channels);
                std::free(samples);
                data->frames = decoded;
                return data;
            }
            data->encoding = MixerAudioData::Encoding::Vorbis;
            data->encoded.assign(raw, raw + length);
            // stb_vorbis answers 0 when the length cannot be determined; the stream's own end
            // is then its end.
            data->frames = frames > 0 ? static_cast<std::int64_t>(frames) : -1;
            return data;
        }

        std::shared_ptr<MixerAudioData> DecodeMp3(const std::span<const std::byte> bytes,
                                                  const bool predecode, std::string& error)
        {
            drmp3 mp3{};
            if (drmp3_init_memory(&mp3, bytes.data(), bytes.size(), nullptr) == 0)
            {
                error = "not a decodable MP3 stream";
                return nullptr;
            }
            auto data = std::make_shared<MixerAudioData>();
            data->channels = static_cast<int>(mp3.channels);
            data->sampleRate = static_cast<int>(mp3.sampleRate);
            // Counting walks the frame headers without synthesising them -- quick, and it is what
            // gives a song its duration.
            const drmp3_uint64 frames = drmp3_get_pcm_frame_count(&mp3);
            if (data->channels <= 0 || data->sampleRate <= 0 || frames == 0)
            {
                drmp3_uninit(&mp3);
                error = "the MP3 stream holds no audio";
                return nullptr;
            }
            if (predecode)
            {
                if (drmp3_seek_to_pcm_frame(&mp3, 0) == 0)
                {
                    drmp3_uninit(&mp3);
                    error = "the MP3 stream could not be rewound";
                    return nullptr;
                }
                data->encoding = MixerAudioData::Encoding::Pcm16;
                data->pcm16.resize(static_cast<std::size_t>(frames) * static_cast<std::size_t>(data->channels));
                const drmp3_uint64 decoded = drmp3_read_pcm_frames_s16(&mp3, frames, data->pcm16.data());
                drmp3_uninit(&mp3);
                data->pcm16.resize(static_cast<std::size_t>(decoded) * static_cast<std::size_t>(data->channels));
                data->frames = static_cast<std::int64_t>(decoded);
                return data;
            }
            drmp3_uninit(&mp3);
            const auto* raw = reinterpret_cast<const unsigned char*>(bytes.data());
            data->encoding = MixerAudioData::Encoding::Mp3;
            data->encoded.assign(raw, raw + bytes.size());
            data->frames = static_cast<std::int64_t>(frames);
            return data;
        }

        std::shared_ptr<MixerAudioData> DecodeFlac(const std::span<const std::byte> bytes,
                                                   const bool predecode, std::string& error)
        {
            drflac* flac = drflac_open_memory(bytes.data(), bytes.size(), nullptr);
            if (flac == nullptr)
            {
                error = "not a decodable FLAC stream";
                return nullptr;
            }
            auto data = std::make_shared<MixerAudioData>();
            data->channels = static_cast<int>(flac->channels);
            data->sampleRate = static_cast<int>(flac->sampleRate);
            const drflac_uint64 frames = flac->totalPCMFrameCount;
            if (data->channels <= 0 || data->sampleRate <= 0)
            {
                drflac_close(flac);
                error = "the FLAC stream declares no channels or no rate";
                return nullptr;
            }
            if (predecode)
            {
                // A stream that does not state its length (0) is read to its end in blocks.
                data->encoding = MixerAudioData::Encoding::Pcm16;
                const std::size_t channels = static_cast<std::size_t>(data->channels);
                constexpr drflac_uint64 kBlock = 4096;
                std::vector<std::int16_t> block(static_cast<std::size_t>(kBlock) * channels);
                drflac_uint64 total = 0;
                while (true)
                {
                    const drflac_uint64 decoded = drflac_read_pcm_frames_s16(flac, kBlock, block.data());
                    if (decoded == 0) { break; }
                    data->pcm16.insert(data->pcm16.end(), block.begin(),
                                       block.begin() + static_cast<std::ptrdiff_t>(decoded * channels));
                    total += decoded;
                }
                drflac_close(flac);
                if (total == 0)
                {
                    error = "the FLAC stream holds no audio";
                    return nullptr;
                }
                data->frames = static_cast<std::int64_t>(total);
                return data;
            }
            drflac_close(flac);
            const auto* raw = reinterpret_cast<const unsigned char*>(bytes.data());
            data->encoding = MixerAudioData::Encoding::Flac;
            data->encoded.assign(raw, raw + bytes.size());
            data->frames = frames > 0 ? static_cast<std::int64_t>(frames) : -1;
            return data;
        }

        bool LooksLikeMp3(const std::span<const std::byte> bytes)
        {
            // An ID3v2 tag, or an MPEG audio frame's sync word.
            return StartsWith(bytes, "ID3") ||
                   (bytes.size() >= 2 && std::to_integer<unsigned>(bytes[0]) == 0xFF &&
                    (std::to_integer<unsigned>(bytes[1]) & 0xE0) == 0xE0);
        }
    }

    std::shared_ptr<MixerAudioData> DecodeMixerAudio(const std::span<const std::byte> bytes,
                                                     const std::string& origin, const bool predecode,
                                                     std::string& error)
    {
        try
        {
            if (StartsWith(bytes, "RIFF") && bytes.size() >= 12 &&
                std::memcmp(bytes.data() + 8, "WAVE", 4) == 0)
            {
                return DecodeWav(bytes, origin);
            }
            if (StartsWith(bytes, "OggS"))
            {
                if (ContainsWithin(bytes, "OpusHead", 256))
                {
                    error = "'" + origin + "' is Ogg Opus, which CNA's mixer does not decode "
                                           "(it plays WAV, Ogg Vorbis, MP3 and FLAC)";
                    return nullptr;
                }
                std::string reason;
                // FLAC in an Ogg container announces itself in its first packet.
                auto data = ContainsWithin(bytes, "\x7F" "FLAC", 256)
                                ? DecodeFlac(bytes, predecode, reason)
                                : DecodeVorbis(bytes, predecode, reason);
                if (!data)
                {
                    error = "'" + origin + "': " + reason;
                }
                return data;
            }
            if (StartsWith(bytes, "fLaC"))
            {
                std::string reason;
                auto data = DecodeFlac(bytes, predecode, reason);
                if (!data)
                {
                    error = "'" + origin + "': " + reason;
                }
                return data;
            }
            if (LooksLikeMp3(bytes))
            {
                std::string reason;
                auto data = DecodeMp3(bytes, predecode, reason);
                if (!data)
                {
                    error = "'" + origin + "': " + reason;
                }
                return data;
            }
            error = "'" + origin + "' is not a known audio format: CNA's mixer plays WAV (PCM, "
                                   "float, MS-ADPCM, IMA-ADPCM), Ogg Vorbis, MP3 and FLAC";
            return nullptr;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return nullptr;
        }
    }

    std::shared_ptr<MixerAudioData> WrapMixerRawAudio(const std::span<const std::byte> pcm,
                                                      const MixerFormat& format, std::string& error)
    {
        if (format.sampleRate <= 0 || format.channels <= 0 || format.channels > 8)
        {
            error = "raw audio needs a positive rate and 1 to 8 channels";
            return nullptr;
        }
        auto data = std::make_shared<MixerAudioData>();
        data->sampleRate = format.sampleRate;
        data->channels = format.channels;
        if (format.sampleFormat == MixerSampleFormat::Float32)
        {
            data->encoding = MixerAudioData::Encoding::Float32;
            data->pcmFloat.resize(pcm.size() / sizeof(float));
            CopyBytes(data->pcmFloat.data(), pcm.data(), data->pcmFloat.size() * sizeof(float));
            data->frames = static_cast<std::int64_t>(data->pcmFloat.size()) / format.channels;
        }
        else
        {
            data->encoding = MixerAudioData::Encoding::Pcm16;
            data->pcm16.resize(pcm.size() / sizeof(std::int16_t));
            CopyBytes(data->pcm16.data(), pcm.data(), data->pcm16.size() * sizeof(std::int16_t));
            data->frames = static_cast<std::int64_t>(data->pcm16.size()) / format.channels;
        }
        return data;
    }

    EncodedCursorPtr OpenEncodedCursor(const std::shared_ptr<const MixerAudioData>& data)
    {
        if (!data || !data->IsEncoded() || data->encoded.empty() || data->channels <= 0)
        {
            return nullptr;
        }
        switch (data->encoding)
        {
            case MixerAudioData::Encoding::Vorbis:
            {
                int code = 0;
                stb_vorbis* vorbis = stb_vorbis_open_memory(
                    data->encoded.data(), static_cast<int>(data->encoded.size()), &code, nullptr);
                if (vorbis == nullptr)
                {
                    return nullptr;
                }
                if (stb_vorbis_get_info(vorbis).channels != data->channels)
                {
                    stb_vorbis_close(vorbis);
                    return nullptr;
                }
                return EncodedCursorPtr(new VorbisCursor(data, vorbis));
            }
            case MixerAudioData::Encoding::Mp3:
            {
                auto cursor = std::make_unique<Mp3Cursor>(data);
                return cursor->Open() ? EncodedCursorPtr(cursor.release()) : nullptr;
            }
            case MixerAudioData::Encoding::Flac:
            {
                auto cursor = std::make_unique<FlacCursor>(data);
                return cursor->Open() ? EncodedCursorPtr(cursor.release()) : nullptr;
            }
            default:
                return nullptr;
        }
    }

    bool ReadEncodedFrame(EncodedCursor& cursor, std::array<float, 2>& frame) noexcept
    {
        return cursor.Read(frame);
    }

    bool SeekEncoded(EncodedCursor& cursor, const std::uint64_t frame) noexcept
    {
        return cursor.Seek(frame);
    }
}
