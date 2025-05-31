//
// Created by robertvokac on 5/25/25.
//

#ifndef TOUCHCOLLECTION_H
#define TOUCHCOLLECTION_H
#include "CNA/Prop.h"

namespace Microsoft::Xna::Framework::Input::Touch {
    struct TouchCollection {
        dgetter(bool, State)

        dgetter(int, Count)

        TouchCollection();
    };
}


#endif //TOUCHCOLLECTION_H
