//
// Created by robertvokac on 5/28/25.
//

#include "System/Random.h"

#include "System/ArgumentOutOfRangeException.h"

namespace System {
    Random::Random() : generator(std::random_device{}()) {
    }

    intcs Random::Next(const intcs& maxValue) {
        return Next(0, maxValue);
    }

    intcs Random::Next(const intcs& minValue, const intcs& maxValue) {
        if (minValue >= maxValue)
            throw ArgumentOutOfRangeException("minValue must be < maxValue");
        std::uniform_int_distribution<int> distribution(minValue, maxValue - 1);
        return distribution(generator);
    }

    intcs Random::Next() {
        return Next(0, INTCS_MAX);
    }
} // System
