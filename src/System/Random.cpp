//
// Created by robertvokac on 5/28/25.
//

#include "System/Random.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

namespace System {

    Random::Random()
        : generator(std::random_device{}()) {
    }

    intcs Random::Next()
    {
        return Next(0, CNA::INTCS_MAX);
    }

    intcs Random::Next(intcs maxValue)
    {
        if (maxValue < 0)
        {
            throw ArgumentOutOfRangeException("maxValue must be >= 0");
        }

        if (maxValue == 0)
        {
            return 0;
        }

        return Next(0, maxValue);
    }

    intcs Random::Next(intcs minValue, intcs maxValue)
    {
        if (minValue > maxValue)
        {
            throw ArgumentOutOfRangeException("minValue must be <= maxValue");
        }

        if (minValue == maxValue)
        {
            return minValue;
        }

        std::uniform_int_distribution<intcs> distribution(minValue, maxValue - 1);
        return distribution(generator);
    }

} // namespace System