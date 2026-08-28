// SPDX-License-Identifier: MS-PL
#pragma once

// plans/plan_cnb.md CNBF-120: strict numeric parsing for CNA's content-tool command lines.
//
// Header-only and shared by the tools rather than repeated in each of them, because the previous
// per-tool `std::stoul`/`std::stof` calls were the same four defects written twice:
//
//   * `std::stoul("12abc")` is 12. Suffix junk was silently discarded, so a typo in a build script
//     compiled successfully with a number nobody wrote.
//   * `std::stoul("-1")` is 18446744073709551615, and the narrowing cast that followed made it
//     0xFFFFFFFF. A negative value for an unsigned option therefore became the largest one.
//   * A value above the destination's range was truncated by the same cast rather than refused.
//   * `std::stof("nan")` and `std::stof("inf")` both succeed, and a frame rate of NaN then reached
//     an encoder whose finiteness check is the only thing standing between it and a player.
//
// Every function here consumes the WHOLE token, checks the range before narrowing, and throws
// std::runtime_error with a message naming the option -- which each tool already prints as
// "error: <what>" and exits 1 for.

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>

namespace CNA::Tools
{
    /**
     * @brief Parses @p text as an unsigned integer in `[0, maxInclusive]`, consuming all of it.
     *
     * @param option Option name for the diagnostic, e.g. `"--duration-ms"`.
     * @param text   The whole argument token.
     * @param maxInclusive Largest accepted value.
     * @return The parsed value.
     * @throws std::runtime_error if @p text is empty, is not entirely a decimal integer, carries a
     *         sign, or exceeds @p maxInclusive.
     */
    [[nodiscard]] inline std::uint64_t ParseUnsignedArg(const char* option, const std::string& text,
                                                        std::uint64_t maxInclusive)
    {
        if (text.empty()) { throw std::runtime_error(std::string(option) + " needs a number"); }
        // A leading '+' or '-' is refused before strtoull sees it: strtoull ACCEPTS "-1" and
        // returns its two's-complement wraparound, which is how a negative became the largest
        // possible value.
        for (const char c : text)
        {
            if (c < '0' || c > '9')
            {
                throw std::runtime_error(std::string(option) + " expects a whole number, not '" +
                                          text + "'");
            }
        }
        errno = 0;
        char* end = nullptr;
        const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
        if (end != text.c_str() + text.size())
        {
            throw std::runtime_error(std::string(option) + " expects a whole number, not '" +
                                      text + "'");
        }
        if (errno == ERANGE || static_cast<std::uint64_t>(value) > maxInclusive)
        {
            throw std::runtime_error(std::string(option) + " must be 0-" +
                                      std::to_string(maxInclusive) + ", not '" + text + "'");
        }
        return static_cast<std::uint64_t>(value);
    }

    /**
     * @brief Parses @p text as a finite `float` in `(minExclusive, maxInclusive]`, consuming all
     *        of it.
     *
     * @param option       Option name for the diagnostic.
     * @param text         The whole argument token.
     * @param minExclusive Value the result must be strictly greater than.
     * @param maxInclusive Largest accepted value.
     * @return The parsed value.
     * @throws std::runtime_error if @p text is empty, is not entirely a number, names a NaN or an
     *         infinity (which `std::strtof` accepts by their literal spellings), or falls outside
     *         the range.
     */
    [[nodiscard]] inline float ParseFiniteFloatArg(const char* option, const std::string& text,
                                                   float minExclusive, float maxInclusive)
    {
        if (text.empty()) { throw std::runtime_error(std::string(option) + " needs a number"); }
        errno = 0;
        char* end = nullptr;
        const float value = std::strtof(text.c_str(), &end);
        if (end != text.c_str() + text.size())
        {
            throw std::runtime_error(std::string(option) + " expects a number, not '" + text + "'");
        }
        if (!std::isfinite(value))
        {
            throw std::runtime_error(std::string(option) +
                                      " must be a finite number, not '" + text + "'");
        }
        if (!(value > minExclusive) || !(value <= maxInclusive))
        {
            throw std::runtime_error(std::string(option) + " must be greater than " +
                                      std::to_string(minExclusive) + " and at most " +
                                      std::to_string(maxInclusive) + ", not '" + text + "'");
        }
        return value;
    }
}
