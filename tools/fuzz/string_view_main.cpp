// SPDX-License-Identifier: MS-PL
//
// MTH-SAFE-007: the C API string-view/buffer-arithmetic fuzz target
// (modules/c-api/tests/fuzz/StringViewFuzz.cpp) as a standalone seeded executable, so the long
// campaign runs the same entry point and the same independent oracle without Clang or libFuzzer.
// A disagreement aborts inside the entry point, exactly as it does under libFuzzer.
//
// Byte zero selects the NUL policy, the rest is the text, and bytes 1-16 of an input at least 17
// bytes long double as the element count and element size; mutations therefore mix UTF-8 boundary
// bytes with 64-bit edge values. Inputs are capped at 64 bytes, the documented libFuzzer max_len.
#include "seeded_harness.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);
extern "C" void CnaStringViewFuzzCounters(std::uint64_t* values) noexcept;

namespace
{
    constexpr std::size_t MaximumLength = 64U;

    constexpr std::array<std::uint8_t, 18> BoundaryBytes{
        0x00U, 0x01U, 0x7FU, 0x80U, 0x8FU, 0x90U, 0x9FU, 0xA0U, 0xBFU,
        0xC0U, 0xC1U, 0xC2U, 0xDFU, 0xE0U, 0xEDU, 0xF0U, 0xF4U, 0xF5U};

    constexpr std::array<std::uint64_t, 9> EdgeValues{
        0U, 1U, 2U, 3U, 0xFFFFFFFFULL, 0x100000000ULL, 0x5555555555555556ULL,
        0x8000000000000000ULL, 0xFFFFFFFFFFFFFFFFULL};

    CNA::Tools::Fuzz::Bytes Mutate(CNA::Tools::Fuzz::Bytes bytes, std::uint64_t value)
    {
        const auto next = [&value] { return CNA::Tools::Fuzz::NextMixed(value); };
        const auto edits = 1U + next() % 4U;
        for (std::uint64_t edit = 0U; edit < edits; ++edit)
        {
            const auto choice = next() % 7U;
            const auto at = bytes.empty() ? std::size_t{0} : static_cast<std::size_t>(next() % bytes.size());
            switch (choice)
            {
            case 0:
                if (!bytes.empty()) { bytes[at] ^= static_cast<std::uint8_t>(1U << (next() % 8U)); }
                break;
            case 1:
                if (!bytes.empty()) { bytes[at] = BoundaryBytes[next() % BoundaryBytes.size()]; }
                break;
            case 2:
                bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                             BoundaryBytes[next() % BoundaryBytes.size()]);
                break;
            case 3:
                if (!bytes.empty()) { bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(at)); }
                break;
            case 4:
                bytes.resize(static_cast<std::size_t>(next() % (bytes.size() + 1U)));
                break;
            case 5:
            {
                // Element count (offset 1) or element size (offset 9) set to an edge value.
                if (bytes.size() < 17U) { bytes.resize(17U, 0x41U); }
                const std::uint64_t edge = EdgeValues[next() % EdgeValues.size()];
                std::memcpy(bytes.data() + (next() % 2U == 0U ? 1U : 9U), &edge, sizeof edge);
                break;
            }
            default:
                if (!bytes.empty()) { bytes[0] ^= 1U; }
                break;
            }
            if (bytes.size() > MaximumLength) { bytes.resize(MaximumLength); }
        }
        return bytes;
    }
}

int main(int argc, char** argv)
{
    const CNA::Tools::Fuzz::SeededHarness harness{
        "cna_c_api_string_view_fuzzer",
        [] { return std::string("C API string view, buffer and element-size validation"); },
        Mutate,
        [](const CNA::Tools::Fuzz::Bytes& bytes) { LLVMFuzzerTestOneInput(bytes.data(), bytes.size()); },
        [] {
            std::array<std::uint64_t, 5> values{};
            CnaStringViewFuzzCounters(values.data());
            return CNA::Tools::Fuzz::Counters{{"operations", values[0]}, {"accepted_text", values[1]},
                {"rejected_text", values[2]}, {"arithmetic_checks", values[3]}, {"overflow_verdicts", values[4]}};
        }};
    const int handled = CNA::Tools::Fuzz::SeededMain(argc, argv, harness);
    if (handled >= 0) { return handled; }
    std::fprintf(stderr,
                 "usage: %s --seeded <first-seed> <count> <corpus>...\n"
                 "       %s --emit <seed> <output-file> <corpus>...\n"
                 "       %s --replay-one <input-file>\n",
                 argv[0], argv[0], argv[0]);
    return 2;
}
