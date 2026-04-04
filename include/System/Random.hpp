//
// Created by robertvokac on 5/28/25.
//

#pragma once
#include <random>

#include "CNA/CnaHelper.hpp"

namespace System {
    using CNA::intcs;

    /**
     * Provides methods to generate pseudo-random numbers using a Mersenne Twister
     * engine. The class includes methods for producing random integers within specified
     * ranges or using default bounds.
     */
    class Random {
    private:
        std::mt19937 generator;

        /**
         * Constructs a new Random object, initializing the random number generator
         * using a Mersenne Twister engine seeded with a random device.
         */
    public:
        /// Constructs a Random object, seeded using a random device.
        Random();

        /// Non-copyable to avoid unintended sharing of internal generator state
        Random(const Random&) = delete;
        Random& operator=(const Random&) = delete;

        /// Movable if needed
        Random(Random&&) noexcept = default;
        Random& operator=(Random&&) noexcept = default;

        /// Destructor
        ~Random() = default;

        /**
         * Generates a random integer in the range [0, maxValue).
         *
         * @param maxValue The exclusive upper limit of the random number to generate.
         *                 Must be greater than 0.
         * @return A random integer in the range [0, maxValue).
         */
        intcs Next(const intcs& maxValue);

        /**
         * Generates a random integer within the specified range [minValue, maxValue).
         *
         * @param minValue The inclusive lower limit of the range.
         * @param maxValue The exclusive upper limit of the range. Must be greater than minValue.
         * @return A random integer in the range [minValue, maxValue).
         */
        intcs Next(const CNA::intcs& minValue, const intcs& maxValue);

        /**
         * Generates a random integer in the range [0, CSINT_MAX).
         *
         * @return A random integer in the range [0, CSINT_MAX).
         */
        intcs Next();
    };
} // System

