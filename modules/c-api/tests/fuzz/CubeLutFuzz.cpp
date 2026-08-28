// SPDX-License-Identifier: MS-PL
/**
 * @file
 * @brief libFuzzer entry point for the `.cube` LUT parser.
 *
 * plans/plan_binding.md `CBIND-090`. `CApi_CubeLutOracle` enumerates: every accepted table size,
 * every entry of every table against a closed form, and one case per refusal the parser can
 * produce. What enumeration cannot reach is text nobody chose -- a header truncated mid-number, a
 * size line with a hundred digits, an entry count that overflows, bytes that are not text at all.
 * That is this target's job.
 *
 * **It judges the answer, not the absence of a crash.** A parser that accepted anything would be
 * crash-free and useless. So every accepted table is re-read through the public routes and must
 * be self-consistent: the reported size within the documented bounds, and the corner entries
 * readable while one index past the edge is refused. A parse that succeeds and then cannot answer
 * for its own table is a finding, and aborting is the only way to make it visible -- libFuzzer
 * treats a return as "this input was fine".
 *
 * Not part of the ctest suite, deliberately: it needs Clang and does not terminate.
 * `docs/c-api/FUZZING.md` carries the command line. It is compiled by the normal build as an
 * object library nobody links, so it cannot rot silently.
 */

#include "CNA/C/engine_layer.h"

#include <cstdint>
#include <cstdlib>
#include <cstdio>

namespace {

void Require(const bool condition, const char* const what)
{
    if (!condition) {
        std::fprintf(stderr, "cube-lut fuzz: %s\n", what);
        std::abort();
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    CNA_StringView text;
    text.data = reinterpret_cast<const char*>(data);
    text.byte_length = static_cast<uint64_t>(size);

    CNA_CubeLutHandle lut = CNA_INVALID_HANDLE;
    const CNA_Result parsed = cna_cube_lut_parse(text, &lut);

    if (parsed != CNA_RESULT_SUCCESS) {
        // Every refusal must leave the caller's handle untouched. A route that refused and still
        // wrote something would hand back a handle nobody can safely release.
        Require(lut == CNA_INVALID_HANDLE, "a refused parse wrote a handle");
        return 0;
    }

    // Accepted. Now hold it to its own claims.
    int32_t size_ = -1;
    Require(cna_cube_lut_get_size(lut, &size_) == CNA_RESULT_SUCCESS,
            "an accepted table cannot report its size");
    Require(size_ >= CNA_CUBE_LUT_MIN_SIZE_EXT && size_ <= CNA_CUBE_LUT_MAX_SIZE_EXT,
            "an accepted table reports a size outside the documented bounds");

    CNA_Vector3 corner;
    Require(cna_cube_lut_get_entry(lut, 0, 0, 0, &corner) == CNA_RESULT_SUCCESS,
            "an accepted table cannot read its first entry");
    Require(cna_cube_lut_get_entry(lut, size_ - 1, size_ - 1, size_ - 1, &corner) ==
                CNA_RESULT_SUCCESS,
            "an accepted table cannot read its last entry");
    Require(cna_cube_lut_get_entry(lut, size_, 0, 0, &corner) == CNA_RESULT_INVALID_ARGUMENT,
            "an index past the edge was not refused");

    CNA_Bool unit = UINT8_C(9);
    Require(cna_cube_lut_is_unit_domain(lut, &unit) == CNA_RESULT_SUCCESS &&
                (unit == CNA_TRUE || unit == CNA_FALSE),
            "the unit-domain flag is neither true nor false");

    uint64_t bytes = UINT64_C(0);
    const CNA_Result title = cna_cube_lut_copy_title(lut, nullptr, UINT64_C(0), &bytes);
    Require(title == CNA_RESULT_SUCCESS || title == CNA_RESULT_BUFFER_TOO_SMALL,
            "the title query answered with neither success nor buffer-too-small");

    Require(cna_cube_lut_destroy(lut) == CNA_RESULT_SUCCESS,
            "an accepted table cannot be released");
    return 0;
}
