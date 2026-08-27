// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <limits>
#include <string>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace CNA::Content::Cnb
{
    /**
     * @brief Adds two file-declared `std::uint64_t` values, throwing instead of wrapping around.
     *
     * Every `offset + size` computation in a `.cnb` reader combines two values the file itself
     * declares. Unsigned wrap-around is well-defined in C++ but produces a *small* result from two
     * huge inputs, which then passes a naive `<= fileSize` bound check -- the classic way a
     * bounds-checked parser still reads out of range. Checking before adding removes that class
     * of bug entirely.
     *
     * @param a       First addend.
     * @param b       Second addend.
     * @param context Text placed verbatim at the front of the exception message, naming what was
     *                being computed (e.g. `"chunk 3 of 'model.cnb'"`).
     * @return The exact sum.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the sum is not
     *         representable in `std::uint64_t`.
     */
    [[nodiscard]] inline std::uint64_t CheckedAdd(std::uint64_t a, std::uint64_t b,
                                                  const std::string& context)
    {
        if (a > std::numeric_limits<std::uint64_t>::max() - b)
        {
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                context + ": declared offset/size values overflow a 64-bit byte count.");
        }
        return a + b;
    }

    /**
     * @brief Multiplies two file-declared `std::uint64_t` values, throwing instead of wrapping.
     *
     * The counterpart to CheckedAdd() for `elementCount * elementSize` computations. The check is
     * performed by division *before* the multiplication, so the overflow never actually occurs.
     *
     * @param a       First factor.
     * @param b       Second factor.
     * @param context Text placed verbatim at the front of the exception message.
     * @return The exact product.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the product is not
     *         representable in `std::uint64_t`.
     */
    [[nodiscard]] inline std::uint64_t CheckedMultiply(std::uint64_t a, std::uint64_t b,
                                                        const std::string& context)
    {
        if (a != 0u && b > std::numeric_limits<std::uint64_t>::max() / a)
        {
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                context + ": declared element count and element size overflow a 64-bit byte "
                          "count.");
        }
        return a * b;
    }
}
