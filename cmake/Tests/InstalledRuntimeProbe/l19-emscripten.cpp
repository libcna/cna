// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include <emscripten.h>
#include <cstdio>
#include <stdexcept>

int main()
{
    using Microsoft::Xna::Framework::Vector3;
    // Literal arithmetic oracle: sqrt(3*3 + 4*4) = 5, including across a
    // suspended C++ stack. Exception catching requires the installed JS ABI.
    volatile int resumed = 0;
    emscripten_sleep(1);
    resumed = 1;
    try
    {
        if (resumed != 1 || Vector3::Distance(Vector3::Zero, Vector3(3, 4, 0)) != 5)
            return 1;
        throw std::runtime_error("l19-installed-exception");
    }
    catch (const std::runtime_error&)
    {
        std::puts("[PASS] installed C++ export: Asyncify resume, Math and exception ABI");
        return 0;
    }
}
