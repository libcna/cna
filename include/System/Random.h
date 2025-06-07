//
// Created by robertvokac on 5/28/25.
//

#ifndef RANDOM_H
#define RANDOM_H
#include <random>

#include "CNA/CnaHelper.h"

namespace System {
    using CNA::csint;

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
        Random();

        /**
         * Generates a random integer in the range [0, maxValue).
         *
         * @param maxValue The exclusive upper limit of the random number to generate.
         *                 Must be greater than 0.
         * @return A random integer in the range [0, maxValue).
         */
        csint Next(const csint& maxValue);

        /**
         * Generates a random integer within the specified range [minValue, maxValue).
         *
         * @param minValue The inclusive lower limit of the range.
         * @param maxValue The exclusive upper limit of the range. Must be greater than minValue.
         * @return A random integer in the range [minValue, maxValue).
         */
        csint Next(const CNA::csint& minValue, const csint& maxValue);

        /**
         * Generates a random integer in the range [0, CSINT_MAX).
         *
         * @return A random integer in the range [0, CSINT_MAX).
         */
        csint Next();
    };
} // System

#endif //RANDOM_H
