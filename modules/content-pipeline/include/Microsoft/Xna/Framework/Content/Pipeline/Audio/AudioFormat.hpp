// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Audio
{
    /**
     * @brief The encoding of an `AudioContent`: the fields of a `WAVEFORMATEX`, read only.
     */
    class AudioFormat final : public System::Object
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Audio.AudioFormat";

        /**
         * @brief Builds a format from its `WAVEFORMATEX` fields.
         *
         * XNA has no public constructor here: a format is only ever answered by an `AudioContent`.
         *
         * @param format The format tag, 1 for PCM.
         * @param channelCount The channel count.
         * @param sampleRate The sample rate in hertz.
         * @param averageBytesPerSecond The average byte rate.
         * @param blockAlign The block alignment in bytes.
         * @param bitsPerSample The bits per sample.
         * @param extension The bytes following the sixteen-byte base fields, without `cbSize`.
         */
        CNAEXT AudioFormat(SharpRuntime::intcs format, SharpRuntime::intcs channelCount,
                           SharpRuntime::intcs sampleRate, SharpRuntime::intcs averageBytesPerSecond,
                           SharpRuntime::intcs blockAlign, SharpRuntime::intcs bitsPerSample,
                           std::vector<SharpRuntime::bytecs> extension = {});

        /**
         * @brief Gets the average byte rate of the format.
         *
         * @return The bytes one second occupies.
         */
        [[nodiscard]] SharpRuntime::intcs getAverageBytesPerSecondProperty() const noexcept;

        /**
         * @brief Gets the bit depth of the format.
         *
         * @return The bits per sample.
         */
        [[nodiscard]] SharpRuntime::intcs getBitsPerSampleProperty() const noexcept;

        /**
         * @brief Gets the block alignment of the format.
         *
         * @return The bytes one block occupies.
         */
        [[nodiscard]] SharpRuntime::intcs getBlockAlignProperty() const noexcept;

        /**
         * @brief Gets the number of channels.
         *
         * @return The channel count.
         */
        [[nodiscard]] SharpRuntime::intcs getChannelCountProperty() const noexcept;

        /**
         * @brief Gets the format tag: 1 for PCM, 2 for Microsoft ADPCM.
         *
         * @return The format tag.
         */
        [[nodiscard]] SharpRuntime::intcs getFormatProperty() const noexcept;

        /**
         * @brief Gets the format as a `WAVEFORMATEX` byte block.
         *
         * @return The eighteen base bytes, followed by the extension when the format has one.
         */
        [[nodiscard]] const std::vector<SharpRuntime::bytecs>& getNativeWaveFormatProperty() const noexcept;

        /**
         * @brief Gets the sample rate.
         *
         * @return The samples per second.
         */
        [[nodiscard]] SharpRuntime::intcs getSampleRateProperty() const noexcept;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        SharpRuntime::intcs format_ = 1;
        SharpRuntime::intcs channelCount_ = 0;
        SharpRuntime::intcs sampleRate_ = 0;
        SharpRuntime::intcs averageBytesPerSecond_ = 0;
        SharpRuntime::intcs blockAlign_ = 0;
        SharpRuntime::intcs bitsPerSample_ = 0;
        std::vector<SharpRuntime::bytecs> native_;
    };
}
