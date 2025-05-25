//
// Created by robertvokac on 5/25/25.
//

#ifndef MOUSE_H
#define MOUSE_H
#include "MouseState.h"


namespace Microsoft::Xna::Framework::Input {
class Mouse {
public:
    static MouseState GetState();
};
}


#endif //MOUSE_H
