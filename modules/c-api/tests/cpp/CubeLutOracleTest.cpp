// SPDX-License-Identifier: MS-PL
//
// plans/plan_binding.md CBIND-090. The .cube parser against an independent oracle.
//
// `CubeLut` is the engine layer's only byte-facing surface: everything else in Phase B9 takes
// numbers a caller chose, and this takes text a caller may have downloaded. The release gate's
// rule for parser-like surfaces (CBIND-040B) is therefore an independent oracle **and** a fuzz
// target, and the oracle must be a different algorithm rather than a mirror of the implementation.
//
// The oracle here **emits**; the parser **scans**. They run in opposite directions and share no
// code. The expected entry at any index is a closed-form function of the index, so the comparison
// does not depend on the emitter's bookkeeping either -- if the emitter and the parser disagreed
// about the .cube ordering convention, the very first entry with two different channels would say
// so.
//
// This judges the answer, not the absence of a crash: every entry of every table is compared.

#include "CNA/C/engine_layer.h"

#include "CubeLutOracle.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Check(const bool condition, const std::string& what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

CNA_StringView View(const std::string& text)
{
    CNA_StringView view;
    view.data = text.data();
    view.byte_length = static_cast<uint64_t>(text.size());
    return view;
}

// Every table the parser accepts is compared entry by entry against the closed form. The sizes
// span the whole accepted range: the two bounds themselves, and a few in between where an
// off-by-one in the row/slice arithmetic would show but the bounds would not.
void SweepWellFormedTables()
{
    const int sizes[] = {CNA_CUBE_LUT_MIN_SIZE_EXT, 3, 4, 5, 8, 17, 32,
                         CNA_CUBE_LUT_MAX_SIZE_EXT};
    for (const int size : sizes) {
        const std::string text = CNA::C::Test::EmitCubeLut(size);
        CNA_CubeLutHandle lut = CNA_INVALID_HANDLE;
        const CNA_Result parsed = cna_cube_lut_parse(View(text), &lut);
        if (parsed == CNA_RESULT_NOT_SUPPORTED) {
            return; // layer absent: the refusal arm is covered by EngineLayerSmoke.c
        }
        Check(parsed == CNA_RESULT_SUCCESS,
              "a well-formed table of size " + std::to_string(size) + " parses");
        if (parsed != CNA_RESULT_SUCCESS) {
            continue;
        }

        int reported = -1;
        Check(cna_cube_lut_get_size(lut, &reported) == CNA_RESULT_SUCCESS && reported == size,
              "the parsed size matches the declared one for " + std::to_string(size));

        CNA_Bool unit = UINT8_C(9);
        Check(cna_cube_lut_is_unit_domain(lut, &unit) == CNA_RESULT_SUCCESS && unit == CNA_TRUE,
              "the emitted unit domain is recognised as one");

        // The domain corners the oracle emitted must come back as it wrote them. `isUnitDomain`
        // alone would pass even if both corners were wrong in the same direction.
        CNA_Vector3 low;
        CNA_Vector3 high;
        Check(cna_cube_lut_get_domain_min(lut, &low) == CNA_RESULT_SUCCESS && low.x == 0.0F &&
                  low.y == 0.0F && low.z == 0.0F,
              "the emitted domain minimum round-trips");
        Check(cna_cube_lut_get_domain_max(lut, &high) == CNA_RESULT_SUCCESS && high.x == 1.0F &&
                  high.y == 1.0F && high.z == 1.0F,
              "the emitted domain maximum round-trips");

        for (int b = 0; b < size; ++b) {
            for (int g = 0; g < size; ++g) {
                for (int r = 0; r < size; ++r) {
                    CNA_Vector3 entry;
                    if (cna_cube_lut_get_entry(lut, r, g, b, &entry) != CNA_RESULT_SUCCESS) {
                        Check(false, "entry read failed");
                        b = g = r = size;
                        break;
                    }
                    const float wantR = CNA::C::Test::CubeLutExpectedRed(r, g, b, size);
                    const float wantG = CNA::C::Test::CubeLutExpectedGreen(r, g, b, size);
                    const float wantB = CNA::C::Test::CubeLutExpectedBlue(r, g, b, size);
                    // The emitter writes six decimals, so the tolerance is about the text format
                    // rather than about floating point generally.
                    const bool agree = std::fabs(entry.x - wantR) < 1e-5F &&
                        std::fabs(entry.y - wantG) < 1e-5F &&
                        std::fabs(entry.z - wantB) < 1e-5F;
                    if (!agree) {
                        std::fprintf(stderr,
                                     "FAIL: size %d entry (%d,%d,%d) is (%.6f,%.6f,%.6f), "
                                     "the oracle says (%.6f,%.6f,%.6f)\n",
                                     size, r, g, b, (double)entry.x, (double)entry.y,
                                     (double)entry.z, (double)wantR, (double)wantG, (double)wantB);
                        ++failures;
                        b = g = r = size;
                        break;
                    }
                }
            }
        }
        Check(cna_cube_lut_destroy(lut) == CNA_RESULT_SUCCESS, "the table releases");
    }
}

// One case per refusal the parser can produce. Each is a well-formed table with exactly one thing
// wrong, so a refusal cannot be credited to the wrong cause.
void SweepMalformedTables()
{
    const int size = 3;
    const std::string good = CNA::C::Test::EmitCubeLut(size);
    struct Case {
        const char* what;
        std::string text;
    };
    std::vector<Case> cases;
    cases.push_back({"empty text", ""});
    cases.push_back({"no size declared", "TITLE \"x\"\n0.0 0.0 0.0\n"});
    cases.push_back({"size keyword with no value", "LUT_3D_SIZE\n"});
    cases.push_back({"size below the minimum", CNA::C::Test::EmitCubeLut(3)});
    cases.back().text.replace(cases.back().text.find("LUT_3D_SIZE 3"), 13, "LUT_3D_SIZE 1");
    cases.push_back({"size above the maximum", good});
    cases.back().text.replace(cases.back().text.find("LUT_3D_SIZE 3"), 13, "LUT_3D_SIZE 999");
    cases.push_back({"too few entries", "LUT_3D_SIZE 3\n0.0 0.0 0.0\n"});
    cases.push_back({"an entry line with two numbers", good});
    cases.back().text.replace(cases.back().text.rfind("1.000000 1.000000 1.000000"), 25,
                              "1.000000 1.000000");
    cases.push_back({"an entry line that is not numeric", good});
    cases.back().text.replace(cases.back().text.rfind("1.000000 1.000000 1.000000"), 25,
                              "red green blue");
    cases.push_back({"a malformed domain line", good});
    cases.back().text.replace(cases.back().text.find("DOMAIN_MAX 1.0 1.0 1.0"), 22,
                              "DOMAIN_MAX 1.0");
    cases.push_back({"truncated mid-entry", good.substr(0, good.size() / 2)});

    for (const Case& item : cases) {
        CNA_CubeLutHandle lut = (CNA_CubeLutHandle)UINT64_C(0x5A5A5A5A);
        const CNA_Result result = cna_cube_lut_parse(View(item.text), &lut);
        if (result == CNA_RESULT_NOT_SUPPORTED) {
            return;
        }
        Check(result == CNA_RESULT_INVALID_ARGUMENT,
              std::string("refused: ") + item.what);
        Check(lut == CNA_INVALID_HANDLE,
              std::string("no handle leaks on refusal: ") + item.what);
    }
}

} // namespace

int main()
{
    SweepWellFormedTables();
    SweepMalformedTables();
    if (failures != 0) {
        std::fprintf(stderr, "%d oracle disagreement(s)\n", failures);
        return 1;
    }
    return 0;
}
