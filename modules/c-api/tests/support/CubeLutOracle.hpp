// SPDX-License-Identifier: MS-PL
#pragma once

// plans/plan_binding.md CBIND-090. The independent oracle for cna_cube_lut_parse.
//
// CBIND-040B's rule: an oracle that mirrors the implementation agrees with its mistakes, so this
// one is deliberately a *different algorithm* running in the opposite direction. The parser
// **scans** text and produces a table; this **emits** text from a table. They share no code, no
// data structure and no control flow, and the only thing they agree on is the .cube format
// itself. A table emitted here, parsed back through the ABI, and compared entry by entry across
// the whole three-dimensional grid is therefore a claim about the parser rather than a restatement
// of it.
//
// The entry values are a closed-form function of the index, so the expected colour at (r,g,b) is
// computable without storing the table the emitter used -- which is what keeps the comparison
// independent of the emitter's own bookkeeping.

#include <cstdio>
#include <string>

namespace CNA::C::Test {

/** @brief The red channel this oracle puts at index (@p r, @p g, @p b) of a table of @p size. */
[[nodiscard]] inline float CubeLutExpectedRed(const int r, const int g, const int b,
                                              const int size)
{
    (void)g;
    (void)b;
    return static_cast<float>(r) / static_cast<float>(size - 1);
}

/** @brief The green channel this oracle puts at (@p r, @p g, @p b). */
[[nodiscard]] inline float CubeLutExpectedGreen(const int r, const int g, const int b,
                                                const int size)
{
    (void)r;
    (void)b;
    return static_cast<float>(g) / static_cast<float>(size - 1);
}

/** @brief The blue channel this oracle puts at (@p r, @p g, @p b). */
[[nodiscard]] inline float CubeLutExpectedBlue(const int r, const int g, const int b,
                                               const int size)
{
    (void)r;
    (void)g;
    return static_cast<float>(b) / static_cast<float>(size - 1);
}

/**
 * @brief Emits a well-formed `.cube` table of @p size slices.
 *
 * The `.cube` format stores entries with **red varying fastest**, so the emission order is
 * blue-outer, green-middle, red-inner. Getting that order wrong is the single most likely mistake
 * on either side, which is why the oracle writes it explicitly rather than reusing whatever the
 * parser believes: if the two disagree about the order, the comparison fails at the first entry
 * where two channels differ.
 */
[[nodiscard]] inline std::string EmitCubeLut(const int size, const std::string& title = "oracle")
{
    std::string text;
    text += "TITLE \"" + title + "\"\n";
    text += "LUT_3D_SIZE " + std::to_string(size) + "\n";
    text += "DOMAIN_MIN 0.0 0.0 0.0\n";
    text += "DOMAIN_MAX 1.0 1.0 1.0\n";
    char line[128];
    for (int b = 0; b < size; ++b) {
        for (int g = 0; g < size; ++g) {
            for (int r = 0; r < size; ++r) {
                std::snprintf(line, sizeof line, "%.6f %.6f %.6f\n",
                              static_cast<double>(CubeLutExpectedRed(r, g, b, size)),
                              static_cast<double>(CubeLutExpectedGreen(r, g, b, size)),
                              static_cast<double>(CubeLutExpectedBlue(r, g, b, size)));
                text += line;
            }
        }
    }
    return text;
}

} // namespace CNA::C::Test
