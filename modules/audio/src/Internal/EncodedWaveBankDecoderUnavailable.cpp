// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/EncodedWaveBankDecoder.hpp"

#include <stdexcept>
#include <string>

namespace CNA::Internal::Audio
{
    std::unique_ptr<EncodedWaveBankDecoder> OpenEncodedWaveBankDecoder(
        const XwbData& bank, std::size_t entryIndex, std::span<const std::uint8_t>)
    {
        if (entryIndex >= bank.entries.size()) throw std::runtime_error("WaveBank entry index is invalid");
        const auto& entry = bank.entries[entryIndex];
        const auto tag = entry.format;
        const char* name = tag == XwbFormat::WMA
            ? (entry.miniFormat & 0x80000000u ? "WMA Pro" : "WMA/WMAv2")
            : tag == XwbFormat::XMA ? "XMA/XMA2" : "unknown";
        throw std::runtime_error(std::string(name) + " requires the unavailable FFmpeg WaveBank adapter");
    }
}
