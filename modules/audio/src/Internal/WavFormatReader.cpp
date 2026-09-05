// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/WavFormatReader.hpp"

#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Audio
{
    namespace
    {
        [[nodiscard]] std::uint16_t ReadUInt16(const std::span<const std::uint8_t> bytes, const std::size_t at)
        {
            return static_cast<std::uint16_t>(bytes[at] | (static_cast<std::uint16_t>(bytes[at + 1]) << 8));
        }

        [[nodiscard]] std::uint32_t ReadUInt32(const std::span<const std::uint8_t> bytes, const std::size_t at)
        {
            return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
                   (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
                   (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
        }

        [[nodiscard]] bool Tag(const std::span<const std::uint8_t> bytes, const std::size_t at, const char* name)
        {
            return at + 4 <= bytes.size() && std::memcmp(bytes.data() + at, name, 4) == 0;
        }
    }

    WavFormatAndData ReadWavFormatAndData(const std::span<const std::uint8_t> wav, const std::string& origin)
    {
        const auto refuse = [&origin](const char* reason)
        { throw std::runtime_error("'" + origin + "': " + reason); };
        if (wav.size() < 12u || !Tag(wav, 0, "RIFF") || !Tag(wav, 8, "WAVE"))
        {
            refuse("not a RIFF/WAVE file.");
        }
        WavFormatAndData result;
        bool sawFormat = false;
        bool sawData = false;
        std::size_t at = 12u;
        while (at + 8u <= wav.size())
        {
            const std::uint32_t size = ReadUInt32(wav, at + 4u);
            const std::size_t body = at + 8u;
            if (body + size > wav.size())
            {
                // A truncated final chunk is taken as far as it goes, which is what a lenient
                // reader must do for files whose RIFF size is one byte short.
                if (body >= wav.size())
                {
                    break;
                }
            }
            const std::size_t available = std::min<std::size_t>(size, wav.size() - body);
            if (Tag(wav, at, "fmt "))
            {
                if (available < 16u)
                {
                    refuse("the format chunk is too short.");
                }
                result.formatTag = ReadUInt16(wav, body);
                result.channels = ReadUInt16(wav, body + 2u);
                result.sampleRate = ReadUInt32(wav, body + 4u);
                result.averageBytesPerSecond = ReadUInt32(wav, body + 8u);
                result.blockAlign = ReadUInt16(wav, body + 12u);
                result.bitsPerSample = ReadUInt16(wav, body + 14u);
                if (available >= 18u)
                {
                    const std::size_t extensionSize =
                        std::min<std::size_t>(ReadUInt16(wav, body + 16u), available - 18u);
                    result.extension.assign(wav.begin() + static_cast<std::ptrdiff_t>(body + 18u),
                                            wav.begin() + static_cast<std::ptrdiff_t>(body + 18u + extensionSize));
                }
                sawFormat = true;
            }
            else if (Tag(wav, at, "data"))
            {
                result.data.assign(wav.begin() + static_cast<std::ptrdiff_t>(body),
                                   wav.begin() + static_cast<std::ptrdiff_t>(body + available));
                sawData = true;
            }
            else if (Tag(wav, at, "smpl") && available >= 36u + 24u)
            {
                // One loop is read, the first: start and end are inclusive frame indices.
                const std::uint32_t loops = ReadUInt32(wav, body + 28u);
                if (loops > 0u)
                {
                    const std::uint32_t start = ReadUInt32(wav, body + 36u + 8u);
                    const std::uint32_t end = ReadUInt32(wav, body + 36u + 12u);
                    if (end >= start)
                    {
                        result.loopStart = start;
                        result.loopLength = end - start + 1u;
                    }
                }
            }
            at = body + size + (size % 2u);
        }
        if (!sawFormat)
        {
            refuse("no format chunk.");
        }
        if (!sawData || result.data.empty())
        {
            refuse("no audio data.");
        }
        if (result.channels == 0u || result.sampleRate == 0u || result.blockAlign == 0u)
        {
            refuse("the format names no channels, no rate or no block size.");
        }
        return result;
    }
}
