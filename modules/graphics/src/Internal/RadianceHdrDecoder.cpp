// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Graphics/RadianceHdrDecoder.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Graphics
{
    namespace
    {
        constexpr std::uint32_t MaximumDimension = 65536u;
        constexpr std::size_t MaximumPixels = 268435456u;   // 256 Mpixel, the same ceiling the PFM reader uses

        /** @brief One header line, without its terminator; `at` moves past it. */
        [[nodiscard]] std::string ReadLine(std::span<const std::uint8_t> bytes, std::size_t& at,
                                           const std::string& origin)
        {
            const std::size_t start = at;
            while (at < bytes.size() && bytes[at] != '\n')
            {
                ++at;
            }
            if (at >= bytes.size())
            {
                throw std::runtime_error("The Radiance picture \"" + origin + "\" ends inside its header.");
            }
            const std::size_t end = at;
            ++at;
            return std::string(reinterpret_cast<const char*>(bytes.data()) + start, end - start);
        }

        /** @brief The value a stored mantissa and shared exponent stand for. */
        [[nodiscard]] float Value(std::uint8_t mantissa, std::uint8_t exponent) noexcept
        {
            if (exponent == 0u)
            {
                return 0.0f;
            }
            return static_cast<float>((static_cast<double>(mantissa) + 0.5) / 256.0 *
                                      std::ldexp(1.0, static_cast<int>(exponent) - 128));
        }
    }

    bool IsRadianceHdr(std::span<const std::uint8_t> bytes) noexcept
    {
        static constexpr char Radiance[] = "#?RADIANCE";
        static constexpr char Rgbe[] = "#?RGBE";
        if (bytes.size() >= sizeof(Radiance) - 1u &&
            std::memcmp(bytes.data(), Radiance, sizeof(Radiance) - 1u) == 0)
        {
            return true;
        }
        return bytes.size() >= sizeof(Rgbe) - 1u &&
               std::memcmp(bytes.data(), Rgbe, sizeof(Rgbe) - 1u) == 0;
    }

    DecodedRadianceHdr DecodeRadianceHdr(std::span<const std::uint8_t> bytes, const std::string& origin)
    {
        if (!IsRadianceHdr(bytes))
        {
            throw std::runtime_error("The file \"" + origin + "\" is not a Radiance picture.");
        }
        std::size_t at = 0u;
        // The header runs to the first empty line; the resolution follows it.
        (void)ReadLine(bytes, at, origin);
        for (;;)
        {
            const std::string line = ReadLine(bytes, at, origin);
            if (line.empty() || line == "\r")
            {
                break;
            }
        }
        const std::string resolution = ReadLine(bytes, at, origin);
        // Only the -Y rows +X columns orientation is written in practice, and it is the one XNA
        // answers for; any other is refused rather than silently transposed.
        unsigned height = 0u;
        unsigned width = 0u;
        if (std::sscanf(resolution.c_str(), "-Y %u +X %u", &height, &width) != 2)
        {
            throw std::runtime_error("The Radiance picture \"" + origin + "\" declares \"" + resolution +
                                     "\", which is not a -Y rows +X columns resolution.");
        }
        if (width == 0u || height == 0u || width > MaximumDimension || height > MaximumDimension ||
            static_cast<std::size_t>(width) * height > MaximumPixels)
        {
            throw std::runtime_error("The Radiance picture \"" + origin + "\" declares a size of " +
                                     std::to_string(width) + " by " + std::to_string(height) +
                                     ", which is not a size this reader accepts.");
        }

        DecodedRadianceHdr decoded;
        decoded.width = width;
        decoded.height = height;
        decoded.pixels.assign(static_cast<std::size_t>(width) * height * 4u, 0.0f);

        std::vector<std::uint8_t> scanline(static_cast<std::size_t>(width) * 4u);
        for (std::uint32_t row = 0u; row < height; ++row)
        {
            const bool runLength = width >= 8u && width < 32768u && at + 4u <= bytes.size() &&
                                   bytes[at] == 2u && bytes[at + 1u] == 2u &&
                                   ((static_cast<std::uint32_t>(bytes[at + 2u]) << 8) | bytes[at + 3u]) == width;
            if (runLength)
            {
                at += 4u;
                // Run-length rows store the four channels one after another, each as a sequence of
                // runs: a count above 128 repeats the next byte, and one at or below 128 copies
                // that many bytes.
                for (std::uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    std::uint32_t column = 0u;
                    while (column < width)
                    {
                        if (at >= bytes.size())
                        {
                            throw std::runtime_error("The Radiance picture \"" + origin +
                                                     "\" ends inside a run-length scanline.");
                        }
                        const std::uint32_t count = bytes[at++];
                        if (count > 128u)
                        {
                            const std::uint32_t run = count - 128u;
                            if (at >= bytes.size() || column + run > width)
                            {
                                throw std::runtime_error("The Radiance picture \"" + origin +
                                                         "\" has a run that leaves its scanline.");
                            }
                            const std::uint8_t value = bytes[at++];
                            for (std::uint32_t i = 0u; i < run; ++i)
                            {
                                scanline[static_cast<std::size_t>(column++) * 4u + channel] = value;
                            }
                        }
                        else
                        {
                            if (count == 0u || at + count > bytes.size() || column + count > width)
                            {
                                throw std::runtime_error("The Radiance picture \"" + origin +
                                                         "\" has a literal run that leaves its scanline.");
                            }
                            for (std::uint32_t i = 0u; i < count; ++i)
                            {
                                scanline[static_cast<std::size_t>(column++) * 4u + channel] = bytes[at++];
                            }
                        }
                    }
                }
            }
            else
            {
                if (at + static_cast<std::size_t>(width) * 4u > bytes.size())
                {
                    throw std::runtime_error("The Radiance picture \"" + origin +
                                             "\" ends inside a flat scanline.");
                }
                std::memcpy(scanline.data(), bytes.data() + at, static_cast<std::size_t>(width) * 4u);
                at += static_cast<std::size_t>(width) * 4u;
            }
            for (std::uint32_t column = 0u; column < width; ++column)
            {
                const std::uint8_t* stored = scanline.data() + static_cast<std::size_t>(column) * 4u;
                const std::size_t out = (static_cast<std::size_t>(row) * width + column) * 4u;
                decoded.pixels[out] = Value(stored[0], stored[3]);
                decoded.pixels[out + 1u] = Value(stored[1], stored[3]);
                decoded.pixels[out + 2u] = Value(stored[2], stored[3]);
                decoded.pixels[out + 3u] = 1.0f;
            }
        }
        return decoded;
    }
}
