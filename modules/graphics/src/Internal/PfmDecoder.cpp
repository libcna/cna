// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Graphics/PfmDecoder.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Graphics
{
    namespace
    {
        /** @brief Reads one whitespace-delimited header token. */
        [[nodiscard]] std::string NextToken(const std::span<const std::uint8_t> bytes, std::size_t& at)
        {
            while (at < bytes.size() && (bytes[at] == ' ' || bytes[at] == '\n' || bytes[at] == '\r' ||
                                         bytes[at] == '\t'))
            {
                ++at;
            }
            const std::size_t start = at;
            while (at < bytes.size() && bytes[at] != ' ' && bytes[at] != '\n' && bytes[at] != '\r' &&
                   bytes[at] != '\t')
            {
                ++at;
            }
            return std::string(reinterpret_cast<const char*>(bytes.data() + start), at - start);
        }
    }

    bool IsPfm(const std::span<const std::uint8_t> bytes) noexcept
    {
        return bytes.size() >= 2u && bytes[0] == 'P' && (bytes[1] == 'F' || bytes[1] == 'f');
    }

    DecodedPfm DecodePfm(const std::span<const std::uint8_t> bytes, const std::string& origin)
    {
        const auto refuse = [&origin](const char* reason)
        { throw std::runtime_error("'" + origin + "': " + reason); };
        if (!IsPfm(bytes))
        {
            refuse("not a portable float map.");
        }
        std::size_t at = 0u;
        const std::string signature = NextToken(bytes, at);
        const bool colour = signature == "PF";
        const std::string widthToken = NextToken(bytes, at);
        const std::string heightToken = NextToken(bytes, at);
        const std::string scaleToken = NextToken(bytes, at);
        if (at < bytes.size())
        {
            // Exactly one whitespace character separates the header from the payload.
            ++at;
        }
        DecodedPfm decoded;
        try
        {
            decoded.width = static_cast<std::uint32_t>(std::stoul(widthToken));
            decoded.height = static_cast<std::uint32_t>(std::stoul(heightToken));
        }
        catch (const std::exception&)
        {
            refuse("the header does not name a size.");
        }
        double scale = -1.0;
        try
        {
            scale = std::stod(scaleToken);
        }
        catch (const std::exception&)
        {
            refuse("the header does not name a scale.");
        }
        const std::size_t channels = colour ? 3u : 1u;
        const std::size_t needed =
            static_cast<std::size_t>(decoded.width) * decoded.height * channels * sizeof(float);
        if (decoded.width == 0u || decoded.height == 0u || bytes.size() - at < needed)
        {
            refuse("the payload is shorter than the size names.");
        }
        decoded.pixels.assign(static_cast<std::size_t>(decoded.width) * decoded.height * 4u, 0.0f);
        for (std::uint32_t row = 0u; row < decoded.height; ++row)
        {
            // A negative scale means the rows are stored bottom to top, which is the usual form.
            const std::uint32_t source = scale < 0.0 ? decoded.height - 1u - row : row;
            for (std::uint32_t column = 0u; column < decoded.width; ++column)
            {
                const std::size_t from =
                    at + ((static_cast<std::size_t>(source) * decoded.width + column) * channels) * sizeof(float);
                const std::size_t to = (static_cast<std::size_t>(row) * decoded.width + column) * 4u;
                float values[3] = {0.0f, 0.0f, 0.0f};
                for (std::size_t channel = 0; channel < channels; ++channel)
                {
                    std::memcpy(&values[channel], bytes.data() + from + channel * sizeof(float), sizeof(float));
                }
                decoded.pixels[to] = values[0];
                decoded.pixels[to + 1u] = colour ? values[1] : values[0];
                decoded.pixels[to + 2u] = colour ? values[2] : values[0];
                decoded.pixels[to + 3u] = 1.0f;
            }
        }
        return decoded;
    }
}
