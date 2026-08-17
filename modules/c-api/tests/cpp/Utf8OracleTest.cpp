// SPDX-License-Identifier: MS-PL

/* plan_binding.md CBIND-040B: the byte-facing surfaces, fuzzed against an independent oracle.
 *
 * Every `CNA_StringView` this ABI accepts passes through `ValidateStringView`, which is the one
 * place a C caller's arbitrary bytes meet a decision. A random fuzzer would sample that decision;
 * where the input space is small enough, this test *proves* it instead -- every byte sequence of
 * length one, two and three, which is 16,843,008 cases and the entire space in which a UTF-8
 * scanner's interesting mistakes live: truncation, overlong forms, surrogates, out-of-range lead
 * bytes and stray continuations.
 *
 * The oracle is deliberately a different algorithm, not a copy. The implementation pattern-matches
 * on byte ranges; the reference here decodes the code point and then applies the Unicode rules to
 * the value. Two implementations that agree by construction would prove nothing.
 *
 * Beyond three bytes the space stops being enumerable, so the same oracle is driven by a seeded
 * generator whose sequence is fixed: a failure here is reproducible, not a lucky catch.
 */

#include "CnaCApiDetail.hpp"

#include "CApiFuzzOracle.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

using CNA::C::Detail::CheckedElementByteCount;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::ValidateBuffer;
using CNA::C::Detail::ValidateStringView;
using CNA::C::Tests::MultiplyWide;
using CNA::C::Tests::ReferenceIsWellFormedUtf8;

std::vector<unsigned char> failingCase;

[[nodiscard]] bool Agrees(
    const unsigned char* const bytes,
    const std::size_t length,
    const bool rejectEmbeddedNul)
{
    const CNA_StringView view{
        reinterpret_cast<const char*>(bytes), static_cast<std::uint64_t>(length)};
    const CNA_Result measured = ValidateStringView(view, rejectEmbeddedNul);
    const bool expected = ReferenceIsWellFormedUtf8(bytes, length, rejectEmbeddedNul);
    if ((measured == CNA_RESULT_SUCCESS) != expected) {
        failingCase.assign(bytes, bytes + length);
        return false;
    }
    if (measured != CNA_RESULT_SUCCESS && measured != CNA_RESULT_ENCODING) {
        failingCase.assign(bytes, bytes + length);
        return false;
    }
    return true;
}

void ReportFailure(const char* const stage, const bool rejectEmbeddedNul)
{
    std::fprintf(stderr, "%s disagreed (rejectEmbeddedNul=%d) on:", stage, rejectEmbeddedNul ? 1 : 0);
    for (const unsigned char byte : failingCase) {
        std::fprintf(stderr, " %02X", static_cast<unsigned>(byte));
    }
    std::fprintf(stderr, "\n");
}

/* A fixed generator, so a failure is reproducible. The bias matters: uniform random bytes almost
   never form a valid multi-byte sequence, and a fuzzer that only ever produces invalid input proves
   only that rejection works. */
class Generator final {
public:
    [[nodiscard]] std::uint64_t Next() noexcept
    {
        state_ ^= state_ << 13U;
        state_ ^= state_ >> 7U;
        state_ ^= state_ << 17U;
        return state_;
    }

    [[nodiscard]] unsigned char NextByte() noexcept
    {
        const std::uint64_t value = Next();
        switch (value % 4U) {
        case 0U:
            // ASCII, including NUL.
            return static_cast<unsigned char>((value >> 8U) & 0x7FU);
        case 1U:
            // A plausible lead byte.
            return static_cast<unsigned char>(0xC0U + ((value >> 8U) % 0x40U));
        case 2U:
            // A plausible continuation byte.
            return static_cast<unsigned char>(0x80U + ((value >> 8U) % 0x40U));
        default:
            return static_cast<unsigned char>((value >> 8U) & 0xFFU);
        }
    }

private:
    std::uint64_t state_ = UINT64_C(0x243F6A8885A308D3);
};

} // namespace

int main()
{
    unsigned char buffer[16] = {0};

    // Length one, two and three: the whole space, both NUL policies.
    for (unsigned int first = 0U; first <= 0xFFU; ++first) {
        buffer[0] = static_cast<unsigned char>(first);
        if (!Agrees(buffer, 1U, false) || !Agrees(buffer, 1U, true)) {
            ReportFailure("The one-byte sweep", true);
            return 1;
        }
        for (unsigned int second = 0U; second <= 0xFFU; ++second) {
            buffer[1] = static_cast<unsigned char>(second);
            if (!Agrees(buffer, 2U, false) || !Agrees(buffer, 2U, true)) {
                ReportFailure("The two-byte sweep", true);
                return 2;
            }
            for (unsigned int third = 0U; third <= 0xFFU; ++third) {
                buffer[2] = static_cast<unsigned char>(third);
                if (!Agrees(buffer, 3U, false)) {
                    ReportFailure("The three-byte sweep", false);
                    return 3;
                }
            }
        }
    }

    // Four bytes is 4.3 billion sequences, so the sweep becomes structured instead of exhaustive:
    // every lead byte crossed with the values where a UTF-8 decision changes.
    {
        static const unsigned char boundaries[] = {
            0x00U, 0x01U, 0x7FU, 0x80U, 0x8FU, 0x90U, 0x9FU, 0xA0U,
            0xBFU, 0xC0U, 0xC1U, 0xC2U, 0xEDU, 0xF0U, 0xF4U, 0xF5U, 0xFFU
        };
        for (unsigned int lead = 0U; lead <= 0xFFU; ++lead) {
            buffer[0] = static_cast<unsigned char>(lead);
            for (const unsigned char second : boundaries) {
                buffer[1] = second;
                for (const unsigned char third : boundaries) {
                    buffer[2] = third;
                    for (const unsigned char fourth : boundaries) {
                        buffer[3] = fourth;
                        if (!Agrees(buffer, 4U, false) || !Agrees(buffer, 4U, true)) {
                            ReportFailure("The four-byte boundary sweep", true);
                            return 4;
                        }
                    }
                }
            }
        }
    }

    // Longer strings, where sequences meet each other: a valid one followed by a truncated one, a
    // continuation stranded after a complete character, and so on.
    {
        Generator generator;
        for (unsigned int iteration = 0U; iteration < 2000000U; ++iteration) {
            const std::size_t length =
                static_cast<std::size_t>(generator.Next() % sizeof(buffer)) + 1U;
            for (std::size_t index = 0U; index < length; ++index) {
                buffer[index] = generator.NextByte();
            }
            const bool rejectEmbeddedNul = (generator.Next() & 1U) != 0U;
            if (!Agrees(buffer, length, rejectEmbeddedNul)) {
                ReportFailure("The seeded sweep", rejectEmbeddedNul);
                return 5;
            }
            // Whatever the verdict, a copy of an accepted view must reproduce the bytes exactly --
            // including any embedded NUL, which is a byte and not a terminator.
            const CNA_StringView view{
                reinterpret_cast<const char*>(buffer), static_cast<std::uint64_t>(length)};
            std::string copied = "sentinel";
            const CNA_Result copyResult = CopyStringView(view, rejectEmbeddedNul, &copied);
            const CNA_Result validateResult = ValidateStringView(view, rejectEmbeddedNul);
            if (copyResult != validateResult) {
                return 6;
            }
            if (copyResult == CNA_RESULT_SUCCESS &&
                (copied.size() != length ||
                 std::memcmp(copied.data(), buffer, length) != 0)) {
                return 7;
            }
            if (copyResult != CNA_RESULT_SUCCESS && copied != "sentinel") {
                return 8;
            }
        }
    }

    // The pointer contract is separate from the encoding one: no bytes means no bytes, and a null
    // pointer with a length is a caller error rather than bad text.
    {
        std::string copied;
        if (ValidateStringView(CNA_StringView{nullptr, UINT64_C(0)}, true) != CNA_RESULT_SUCCESS ||
            ValidateStringView(CNA_StringView{nullptr, UINT64_C(1)}, true) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            ValidateStringView(CNA_StringView{nullptr, UINT64_MAX}, false) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            CopyStringView(CNA_StringView{nullptr, UINT64_C(0)}, true, &copied) !=
                CNA_RESULT_SUCCESS ||
            !copied.empty() ||
            CopyStringView(CNA_StringView{"x", UINT64_C(1)}, true, nullptr) !=
                CNA_RESULT_INVALID_ARGUMENT) {
            return 9;
        }
        if (ValidateBuffer(nullptr, UINT64_C(0)) != CNA_RESULT_SUCCESS ||
            ValidateBuffer(nullptr, UINT64_C(1)) != CNA_RESULT_INVALID_ARGUMENT ||
            ValidateBuffer(buffer, UINT64_C(0)) != CNA_RESULT_SUCCESS) {
            return 10;
        }
    }

    // The other place caller-supplied numbers are trusted: element count times element size. The
    // oracle is 128-bit arithmetic, which cannot wrap where the 64-bit product would.
    {
        static const std::uint64_t interesting[] = {
            UINT64_C(0),
            UINT64_C(1),
            UINT64_C(2),
            UINT64_C(3),
            UINT64_C(255),
            UINT64_C(65535),
            UINT64_C(0x7FFFFFFF),
            UINT64_C(0x80000000),
            UINT64_C(0xFFFFFFFF),
            UINT64_C(0x100000000),
            UINT64_MAX / UINT64_C(2),
            UINT64_MAX - UINT64_C(1),
            UINT64_MAX
        };
        for (const std::uint64_t count : interesting) {
            for (const std::uint64_t size : interesting) {
                std::size_t measured = 0U;
                const CNA_Result result =
                    CheckedElementByteCount(buffer, count, size, &measured);
                if (size == UINT64_C(0)) {
                    if (result != CNA_RESULT_INVALID_ARGUMENT) {
                        return 11;
                    }
                    continue;
                }
                std::uint64_t high = 0U;
                std::uint64_t low = 0U;
                MultiplyWide(count, size, &high, &low);
                const bool fits =
                    high == 0U && low <= std::numeric_limits<std::size_t>::max();
                if (fits) {
                    if (result != CNA_RESULT_SUCCESS ||
                        measured != static_cast<std::size_t>(low)) {
                        return 12;
                    }
                } else if (result != CNA_RESULT_OVERFLOW) {
                    return 13;
                }
            }
        }
        std::size_t measured = 0U;
        if (CheckedElementByteCount(buffer, UINT64_C(1), UINT64_C(1), nullptr) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            CheckedElementByteCount(nullptr, UINT64_C(1), UINT64_C(1), &measured) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            CheckedElementByteCount(nullptr, UINT64_C(0), UINT64_C(1), &measured) !=
                CNA_RESULT_SUCCESS ||
            measured != 0U) {
            return 14;
        }
    }

    return 0;
}
