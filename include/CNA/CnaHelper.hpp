//
// Created by robertvokac on 5/30/25.
//
#pragma once

#include <cstdint>
#include <limits>

/**
 * @def INTCS_MAX
 * @brief Maximum value of @c CNA::intcs.
 */
#define INTCS_MAX std::numeric_limits<CNA::intcs>::max()

/**
 * @def INTCS_MIN
 * @brief Minimum value of @c CNA::intcs.
 */
#define INTCS_MIN std::numeric_limits<CNA::intcs>::min()

/**
 * @def UINTCS_MAX
 * @brief Maximum value of @c CNA::uintcs.
 */
#define UINTCS_MAX std::numeric_limits<CNA::uintcs>::max()

/**
 * @def UINTCS_MIN
 * @brief Minimum value of @c CNA::uintcs.
 */
#define UINTCS_MIN std::numeric_limits<CNA::uintcs>::min()

/**
 * @def LONGCS_MAX
 * @brief Maximum value of @c CNA::longcs.
 */
#define LONGCS_MAX std::numeric_limits<CNA::longcs>::max()

/**
 * @def LONGCS_MIN
 * @brief Minimum value of @c CNA::longcs.
 */
#define LONGCS_MIN std::numeric_limits<CNA::longcs>::min()

/**
 * @def BYTE_MAX
 * @brief Maximum value of @c CNA::byte.
 */
#define BYTE_MAX std::numeric_limits<CNA::byte>::max()

/**
 * @def BYTE_MIN
 * @brief Minimum value of @c CNA::byte.
 */
#define BYTE_MIN std::numeric_limits<CNA::byte>::min()

namespace CNA {

    /**
     * @brief 32-bit signed integer type compatible with C# @c int.
     */
    typedef int32_t intcs;

    /**
     * @brief 32-bit unsigned integer type compatible with C# unsigned integer usage.
     */
    typedef uint32_t uintcs;

    /**
     * @brief 64-bit signed integer type compatible with C# @c long.
     */
    typedef int64_t longcs;

    /**
     * @brief 8-bit unsigned byte type compatible with C# @c byte.
     */
    typedef unsigned char byte;

} // CNA