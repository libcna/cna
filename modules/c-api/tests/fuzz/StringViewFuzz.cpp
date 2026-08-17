// SPDX-License-Identifier: MS-PL

/* plan_binding.md CBIND-040B: a libFuzzer entry point for the string-view surface.
 *
 * The exhaustive sweep in `tests/cpp/Utf8OracleTest.cpp` settles every byte sequence up to three
 * bytes and samples the rest from a fixed seed. This target covers what neither can: inputs a
 * coverage-guided fuzzer discovers by watching which branches it reaches, at lengths and shapes
 * nobody enumerated. It is a **differential** target, not a crash-only one -- it judges the answer
 * against the same independent oracle the sweep uses, so a wrong verdict fails as loudly as a
 * memory error.
 *
 * Not part of the ctest suite, and deliberately so: it needs Clang and never terminates on its own,
 * which is the opposite of what a gate must be. `docs/c-api/FUZZING.md` has the build and run
 * commands. The sweep is what runs in CI; this is what one runs when hunting.
 */

#include "CnaCApiDetail.hpp"

#include "CApiFuzzOracle.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

/* A fuzzer that reports a disagreement by returning would report nothing at all: libFuzzer treats a
   return as "this input was fine". Aborting is the only way to make the finding visible, and it is
   what leaves the reproducer file behind. */
void Require(const bool condition) noexcept
{
    if (!condition) {
        std::abort();
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* const data, const std::size_t size)
{
    using CNA::C::Detail::CheckedElementByteCount;
    using CNA::C::Detail::CopyStringView;
    using CNA::C::Detail::ValidateBuffer;
    using CNA::C::Detail::ValidateStringView;
    using CNA::C::Tests::MultiplyWide;
    using CNA::C::Tests::ReferenceIsWellFormedUtf8;

    // The first byte chooses the NUL policy, so one corpus covers both; the rest is the text.
    const bool rejectEmbeddedNul = size != 0U && (data[0] & 1U) != 0U;
    const std::uint8_t* const bytes = size == 0U ? nullptr : data + 1U;
    const std::size_t length = size == 0U ? 0U : size - 1U;

    const CNA_StringView view{
        reinterpret_cast<const char*>(length == 0U ? nullptr : bytes),
        static_cast<std::uint64_t>(length)};

    const CNA_Result validated = ValidateStringView(view, rejectEmbeddedNul);
    const bool expected =
        length == 0U ||
        ReferenceIsWellFormedUtf8(
            reinterpret_cast<const unsigned char*>(bytes), length, rejectEmbeddedNul);
    Require((validated == CNA_RESULT_SUCCESS) == expected);
    Require(validated == CNA_RESULT_SUCCESS || validated == CNA_RESULT_ENCODING);

    // A copy must agree with the validation that precedes it, and must reproduce the bytes exactly
    // -- an embedded NUL included, because these are counted bytes and not a C string.
    std::string copied = "\xff sentinel";
    const CNA_Result copyResult = CopyStringView(view, rejectEmbeddedNul, &copied);
    Require(copyResult == validated);
    if (copyResult == CNA_RESULT_SUCCESS) {
        Require(copied.size() == length);
        Require(length == 0U || std::memcmp(copied.data(), bytes, length) == 0);
    } else {
        Require(copied == "\xff sentinel");
    }

    // The same input drives the buffer arithmetic, so the fuzzer's mutations reach both surfaces.
    if (size >= 17U) {
        std::uint64_t count = 0U;
        std::uint64_t elementSize = 0U;
        std::memcpy(&count, data + 1U, sizeof(count));
        std::memcpy(&elementSize, data + 9U, sizeof(elementSize));

        std::size_t byteCount = 0U;
        const CNA_Result result = CheckedElementByteCount(data, count, elementSize, &byteCount);
        if (elementSize == 0U) {
            Require(result == CNA_RESULT_INVALID_ARGUMENT);
        } else {
            std::uint64_t high = 0U;
            std::uint64_t low = 0U;
            MultiplyWide(count, elementSize, &high, &low);
            if (high == 0U && low <= SIZE_MAX) {
                Require(result == CNA_RESULT_SUCCESS);
                Require(byteCount == static_cast<std::size_t>(low));
            } else {
                Require(result == CNA_RESULT_OVERFLOW);
            }
        }
        Require(ValidateBuffer(nullptr, count) ==
                (count == 0U ? CNA_RESULT_SUCCESS : CNA_RESULT_INVALID_ARGUMENT));
    }

    return 0;
}
