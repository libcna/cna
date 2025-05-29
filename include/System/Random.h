//
// Created by robertvokac on 5/28/25.
//

#ifndef RANDOM_H
#define RANDOM_H
#include "WindowsPhoneSpeedyBlupi/Decor.h"

namespace System {

class Random {
public:
    int Next(int i);

    int Next(int i, int i1);

    int Next();
};

} // System

#endif //RANDOM_H
