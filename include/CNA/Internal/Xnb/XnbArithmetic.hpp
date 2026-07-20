// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace CNA::Internal::Xnb
{
    /**
     * @brief Multiplies a sequence of non-negative @c int64_t factors, throwing
     *        `ContentLoadException` instead of committing signed-integer-overflow undefined
     *        behavior if the running product would ever exceed what @c int64_t can represent.
     *
     * Every XNB texture reader (`Texture2DReader`, `Texture3DReader`, `TextureCubeReader`)
     * computes an expected decoded byte size or pixel/voxel count as a product of file-declared,
     * attacker-controlled dimensions. Widening each factor to @c int64_t before multiplying is not
     * sufficient on its own: two dimensions near `INT32_MAX` multiply to a value still
     * representable in @c int64_t, but a further `* 4` (or a third/fourth dimension factor) can
     * overflow @c int64_t itself -- confirmed by UBSan (`REMED-CONTENT-009`) inside
     * `Texture2DContentTypeReader.cpp`'s own `REMED-CONTENT-001` fix. This helper checks each
     * multiplication via division *before* performing it (`product > INT64_MAX / factor`), so an
     * overflowing input is rejected as a clean exception rather than the overflow ever occurring.
     *
     * @param factors The non-negative dimension factors to multiply, in any order.
     * @param readerName Used verbatim in the thrown exception's message, matching this file
     *                    family's existing convention (e.g. `"Texture2DReader"`).
     * @return The exact product, guaranteed representable in @c int64_t.
     */
    inline int64_t CheckedMultiplyOrThrow(std::initializer_list<int64_t> factors,
                                           const std::string& readerName)
    {
        int64_t product = 1;
        for (const int64_t factor : factors)
        {
            if (factor < 0)
            {
                throw Microsoft::Xna::Framework::Content::ContentLoadException(
                    readerName + ": negative dimension in decoded-size calculation.");
            }
            if (factor != 0 && product > std::numeric_limits<int64_t>::max() / factor)
            {
                throw Microsoft::Xna::Framework::Content::ContentLoadException(
                    readerName + ": declared dimensions overflow the maximum representable "
                    "decoded byte size.");
            }
            product *= factor;
        }
        return product;
    }
}
