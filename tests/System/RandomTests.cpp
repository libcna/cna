#include <iostream>
#include <bits/ostream.tcc>

#include "gtest/gtest.h"
#include "System/Random.hpp"

using CNA::intcs;
TEST(RandomTests, NextWithMaxValue) {
    System::Random rng;
    const intcs max = 100;
    bool maxFound = false;
    bool maxMinusOneFound = false;
    for (int i = 0; i < 10000; ++i) {
        intcs value = rng.Next(max);
        //std::cout << value << std::endl;
        ASSERT_GE(value, 0);
        ASSERT_LT(value, max);
        maxFound = maxFound || value == max;
        maxMinusOneFound = maxMinusOneFound || value == (max - 1);
    }

    if (maxFound) { FAIL() << "Max value " << max << " found!";}
    if (!maxMinusOneFound) { FAIL() << "Max value minus one " << (max - 1) << " not found!";}
}

TEST(RandomTests, NextWithMinAndMaxValue) {
    System::Random rng;
    const intcs min = 10;
    const intcs max = 20;
    bool minFound = false;
    bool maxFound = false;
    bool maxMinusOneFound = false;
    for (int i = 0; i < 1000; ++i) {
        //std::cout << "Run " << i << std::endl;
        intcs value = rng.Next(min, max);
        //std::cout << value << std::endl;
        ASSERT_GE(value, min);
        ASSERT_LT(value, max);
        maxFound = maxFound || value == max;
        maxMinusOneFound = maxMinusOneFound || value == (max - 1);
        minFound = minFound || value == min;
    }
    if (maxFound) { FAIL() << "Max value " << max << " found!";}
    if (!maxMinusOneFound) { FAIL() << "Max value minus one " << (max - 1) << " not found!";}
    if (!minFound) { FAIL() << "Min value " << min << " not found!";}
}

TEST(RandomTests, NextWithoutArguments) {
    System::Random rng;
    for (int i = 0; i < 1000; ++i) {
        intcs value = rng.Next();
        ASSERT_GE(value, 0);
        ASSERT_LT(value, CNA::INTCS_MAX);
    }
}
