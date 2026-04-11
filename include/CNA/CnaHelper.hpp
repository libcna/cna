//
// Created by robertvokac on 5/30/25.
//

#pragma once

#include <cstdint>
#include <limits>

namespace CNA
{
    /**
     * @brief 32-bit signed integer type compatible with C# @c int.
     */
    using intcs = int32_t;

    /**
     * @brief 32-bit unsigned integer type compatible with C# unsigned integer usage.
     */
    using uintcs = uint32_t;

    /**
     * @brief 64-bit signed integer type compatible with C# @c long.
     */
    using longcs = int64_t;

    /**
     * @brief 8-bit unsigned byte type compatible with C# @c byte.
     */
    using bytecs = uint8_t;

    /**
     * @brief Maximum value of @c CNA::intcs.
     */
    inline constexpr intcs INTCS_MAX = std::numeric_limits<intcs>::max();

    /**
     * @brief Minimum value of @c CNA::intcs.
     */
    inline constexpr intcs INTCS_MIN = std::numeric_limits<intcs>::min();

    /**
     * @brief Maximum value of @c CNA::uintcs.
     */
    inline constexpr uintcs UINTCS_MAX = std::numeric_limits<uintcs>::max();

    /**
     * @brief Minimum value of @c CNA::uintcs.
     */
    inline constexpr uintcs UINTCS_MIN = std::numeric_limits<uintcs>::min();

    /**
     * @brief Maximum value of @c CNA::longcs.
     */
    inline constexpr longcs LONGCS_MAX = std::numeric_limits<longcs>::max();

    /**
     * @brief Minimum value of @c CNA::longcs.
     */
    inline constexpr longcs LONGCS_MIN = std::numeric_limits<longcs>::min();

    /**
     * @brief Maximum value of @c CNA::byte.
     */
    inline constexpr bytecs BYTE_MAX = std::numeric_limits<bytecs>::max();

    /**
     * @brief Minimum value of @c CNA::byte.
     */
    inline constexpr bytecs BYTE_MIN = std::numeric_limits<bytecs>::min();
} // namespace CNA
