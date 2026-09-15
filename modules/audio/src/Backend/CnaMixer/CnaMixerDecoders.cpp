// SPDX-License-Identifier: MS-PL
#include "Backend/CnaMixer/CnaMixer.hpp"
#include "Backend/CnaMixer/StbVorbisApi.hpp"

#include "CNA/Internal/Audio/WavDecoder.hpp"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Audio
{
    /** @brief One track's Ogg Vorbis decoder over shared file bytes. */
    class VorbisCursor
    {
    public:
        VorbisCursor(std::shared_ptr<const MixerAudioData> data, stb_vorbis* vorbis, const int channels)
            : data_(std::move(data)), vorbis_(vorbis), channels_(channels)
        {
            buffer_.resize(static_cast<std::size_t>(kBufferFrames) * static_cast<std::size_t>(channels));
        }

        ~VorbisCursor()
        {
            stb_vorbis_close(vorbis_);
        }

        VorbisCursor(const VorbisCursor&) = delete;
        VorbisCursor& operator=(const VorbisCursor&) = delete;

        bool Read(std::array<float, 2>& frame) noexcept
        {
            if (index_ == count_)
            {
                const int frames = stb_vorbis_get_samples_float_interleaved(
                    vorbis_, channels_, buffer_.data(), kBufferFrames * channels_);
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
        static constexpr int kBufferFrames = 1024;

        std::shared_ptr<const MixerAudioData> data_;
        stb_vorbis* vorbis_;
        int channels_;
        std::vector<float> buffer_;
        int index_ = 0;
        int count_ = 0;
    };

    void VorbisCursorDeleter::operator()(VorbisCursor* cursor) const noexcept
    {
        delete cursor;
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
                                           "(it plays WAV and Ogg Vorbis)";
                    return nullptr;
                }
                std::string reason;
                auto data = DecodeVorbis(bytes, predecode, reason);
                if (!data)
                {
                    error = "'" + origin + "': " + reason;
                }
                return data;
            }
            const char* format = nullptr;
            if (StartsWith(bytes, "ID3") ||
                (bytes.size() >= 2 && std::to_integer<unsigned>(bytes[0]) == 0xFF &&
                 (std::to_integer<unsigned>(bytes[1]) & 0xE0) == 0xE0))
            {
                format = "MP3";
            }
            else if (StartsWith(bytes, "fLaC"))
            {
                format = "FLAC";
            }
            error = "'" + origin + "' is " + (format != nullptr ? std::string(format)
                                                                  : std::string("not a known audio format")) +
                    ": CNA's mixer plays WAV (PCM, float, MS-ADPCM, IMA-ADPCM) and Ogg Vorbis";
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

    VorbisCursorPtr OpenVorbisCursor(const std::shared_ptr<const MixerAudioData>& data)
    {
        if (!data || data->encoding != MixerAudioData::Encoding::Vorbis || data->encoded.empty())
        {
            return nullptr;
        }
        int code = 0;
        stb_vorbis* vorbis = stb_vorbis_open_memory(
            data->encoded.data(), static_cast<int>(data->encoded.size()), &code, nullptr);
        if (vorbis == nullptr)
        {
            return nullptr;
        }
        const int channels = stb_vorbis_get_info(vorbis).channels;
        return VorbisCursorPtr(new VorbisCursor(data, vorbis, channels));
    }

    bool ReadVorbisFrame(VorbisCursor& cursor, std::array<float, 2>& frame) noexcept
    {
        return cursor.Read(frame);
    }

    bool SeekVorbis(VorbisCursor& cursor, const std::uint64_t frame) noexcept
    {
        return cursor.Seek(frame);
    }
}
