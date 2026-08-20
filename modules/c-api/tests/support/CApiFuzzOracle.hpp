// SPDX-License-Identifier: MS-PL

/* plans/plan_binding.md CBIND-040B: the independent oracles the byte-facing tests judge against.
 *
 * Shared by the exhaustive sweep in `tests/cpp/Utf8OracleTest.cpp` and the libFuzzer target in
 * `tests/fuzz/StringViewFuzz.cpp`, so both judge the implementation by the same standard and a
 * fuzzer finding is reproducible as a sweep case.
 *
 * Each function here answers the same question the implementation does, by a **different** method.
 * That is the whole point: an oracle that mirrors the implementation's structure agrees with its
 * mistakes.
 */

#ifndef CNA_C_API_TESTS_SUPPORT_FUZZ_ORACLE_HPP
#define CNA_C_API_TESTS_SUPPORT_FUZZ_ORACLE_HPP

#include <cstddef>
#include <cstdint>

namespace CNA::C::Tests {

/**
 * @brief Decides UTF-8 well-formedness by decoding, where the implementation decides by byte range.
 *
 * @param bytes Bytes to judge; may be null only when @p length is zero.
 * @param length Number of bytes.
 * @param rejectEmbeddedNul Whether a zero byte is text or an error.
 * @return True when every sequence is a shortest-form, non-surrogate code point in range.
 */
[[nodiscard]] inline bool ReferenceIsWellFormedUtf8(
    const unsigned char* const bytes,
    const std::size_t length,
    const bool rejectEmbeddedNul) noexcept
{
    std::size_t index = 0U;
    while (index < length) {
        const unsigned char lead = bytes[index];
        std::size_t sequenceLength = 0U;
        std::uint32_t codePoint = 0U;

        if (lead <= 0x7FU) {
            if (rejectEmbeddedNul && lead == 0U) {
                return false;
            }
            ++index;
            continue;
        }
        if (lead >= 0xC2U && lead <= 0xDFU) {
            sequenceLength = 2U;
            codePoint = static_cast<std::uint32_t>(lead & 0x1FU);
        } else if (lead >= 0xE0U && lead <= 0xEFU) {
            sequenceLength = 3U;
            codePoint = static_cast<std::uint32_t>(lead & 0x0FU);
        } else if (lead >= 0xF0U && lead <= 0xF4U) {
            sequenceLength = 4U;
            codePoint = static_cast<std::uint32_t>(lead & 0x07U);
        } else {
            // 0x80..0xC1 and 0xF5..0xFF can never lead: a stray continuation, an overlong two-byte
            // form, or a value above the Unicode range.
            return false;
        }

        if (length - index < sequenceLength) {
            return false;
        }
        for (std::size_t offset = 1U; offset < sequenceLength; ++offset) {
            const unsigned char continuation = bytes[index + offset];
            if ((continuation & 0xC0U) != 0x80U) {
                return false;
            }
            codePoint = (codePoint << 6U) | static_cast<std::uint32_t>(continuation & 0x3FU);
        }

        // The decoded value carries every remaining rule: shortest form, no surrogate, in range.
        if (sequenceLength == 2U && (codePoint < 0x80U || codePoint > 0x7FFU)) {
            return false;
        }
        if (sequenceLength == 3U &&
            (codePoint < 0x800U || codePoint > 0xFFFFU ||
             (codePoint >= 0xD800U && codePoint <= 0xDFFFU))) {
            return false;
        }
        if (sequenceLength == 4U && (codePoint < 0x10000U || codePoint > 0x10FFFFU)) {
            return false;
        }
        index += sequenceLength;
    }
    return true;
}

/**
 * @brief Forms the full 64x64 product in 32-bit limbs, where the implementation asks a division.
 *
 * @param left First factor.
 * @param right Second factor.
 * @param outHigh Receives the high 64 bits of the product.
 * @param outLow Receives the low 64 bits of the product.
 *
 * `__int128` would be shorter and is what one reaches for first, but the C API's warning wall
 * builds with `-pedantic`, under which it is not standard C++.
 */
inline void MultiplyWide(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t* const outHigh,
    std::uint64_t* const outLow) noexcept
{
    const std::uint64_t leftLow = left & UINT64_C(0xFFFFFFFF);
    const std::uint64_t leftHigh = left >> 32U;
    const std::uint64_t rightLow = right & UINT64_C(0xFFFFFFFF);
    const std::uint64_t rightHigh = right >> 32U;

    const std::uint64_t lowLow = leftLow * rightLow;
    const std::uint64_t lowHigh = leftLow * rightHigh;
    const std::uint64_t highLow = leftHigh * rightLow;
    const std::uint64_t highHigh = leftHigh * rightHigh;

    const std::uint64_t middle =
        (lowLow >> 32U) + (lowHigh & UINT64_C(0xFFFFFFFF)) + (highLow & UINT64_C(0xFFFFFFFF));
    *outLow = (middle << 32U) | (lowLow & UINT64_C(0xFFFFFFFF));
    *outHigh = highHigh + (lowHigh >> 32U) + (highLow >> 32U) + (middle >> 32U);
}

} // namespace CNA::C::Tests

#endif
