// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Audio/XactTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace CNA::Internal::Audio
{
    /** @brief Native encoded WaveBank codec; independent of decoded PCM depth. */
    enum class EncodedWaveBankCodec { Wma2, WmaPro, Xma1, Xma2 };

    /** @brief Maximum encoded input or native decoded PCM bytes accepted per entry. */
    inline constexpr std::size_t EncodedWaveBankByteLimit = 256u * 1024u * 1024u;

    /** @brief Owns immutable native-channel PCM and an entry-local decoded-frame cursor. */
    class EncodedWaveBankDecoder
    {
    public:
        /** @brief Gets the selected native codec. @return Native codec. */
        [[nodiscard]] EncodedWaveBankCodec Codec() const noexcept;
        /** @brief Gets the native sample rate. @return Frames per second. */
        [[nodiscard]] std::uint32_t SampleRate() const noexcept;
        /** @brief Gets the native channel count. @return Channels per frame. */
        [[nodiscard]] std::uint8_t Channels() const noexcept;
        /** @brief Gets the playable decoded length. @return Sample frames. */
        [[nodiscard]] std::uint64_t FrameCount() const noexcept;
        /**
         * @brief Reads whole interleaved frames, writing only the returned frames.
         * @param output Destination containing a whole number of native-channel frames.
         * @return Frames written, or zero at EOF.
         */
        std::size_t ReadFrames(std::span<std::int16_t> output);
        /**
         * @brief Seeks to an exact decoded frame; the length itself denotes EOF.
         * @param frame Frame in the inclusive range zero through FrameCount().
         * @throws std::out_of_range for an invalid frame, leaving the position unchanged.
         */
        void SeekFrame(std::uint64_t frame);
        /** @brief Releases the decoded data. */
        ~EncodedWaveBankDecoder();

    private:
        friend std::unique_ptr<EncodedWaveBankDecoder> OpenEncodedWaveBankDecoder(
            const XwbData&, std::size_t, std::span<const std::uint8_t>);
        EncodedWaveBankDecoder(EncodedWaveBankCodec codec, std::uint32_t rate,
                              std::uint8_t channels, std::vector<std::int16_t> pcm);
        EncodedWaveBankCodec codec_;
        std::uint32_t rate_;
        std::uint8_t channels_;
        std::vector<std::int16_t> pcm_;
        std::uint64_t position_ = 0;
    };

    /**
     * @brief Decodes a bounded entry, refusing unqualified WMA Pro/XMA by name.
     * @param bank Metadata owning the entry and its seek table.
     * @param entryIndex Entry index in the bank.
     * @param encodedEntry Exactly the entry's declared PlayRegion bytes.
     * @return A native-channel decoder cursor, initially at frame zero.
     * @throws std::runtime_error when unavailable, malformed or unqualified.
     */
    [[nodiscard]] std::unique_ptr<EncodedWaveBankDecoder> OpenEncodedWaveBankDecoder(
        const XwbData& bank, std::size_t entryIndex, std::span<const std::uint8_t> encodedEntry);

    /**
     * @brief Applies XAudio2's default 5.1-to-stereo matrix to interleaved S16 PCM.
     * @param pcm Source frames in FL, FR, FC, LFE, BL, BR order.
     * @return Interleaved stereo S16LE bytes with the same frame count.
     */
    [[nodiscard]] std::vector<std::uint8_t> DownmixEncodedWaveBank51(
        std::span<const std::int16_t> pcm);
}
