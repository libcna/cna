//
// Created by robertvokac on 5/25/25.
//

#ifndef TOUCHCOLLECTION_H
#define TOUCHCOLLECTION_H
#include "CNA/ReadonlyProperty.h"


namespace Microsoft::Xna::Framework::Input::Touch {
struct TouchCollection {
public:
    DEF_PROP_CUSTOM(bool, State);
    DEF_PROP_AUTO(int, Count, 0)
    TouchCollection():
    IMPL_PROP_CUSTOM_READONLY(bool, State, {return 0;}),
    IMPL_PROP_AUTO(int, Count)
    {

    };

};
}



#endif //TOUCHCOLLECTION_H
