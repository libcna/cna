//
// Created by robertvokac on 5/25/25.
//

#ifndef MOUSE_H
#define MOUSE_H
#include "MouseCursor.h"
#include "MouseState.h"


namespace Microsoft::Xna::Framework::Input {
class Mouse {
public:
    static MouseState GetState();

    static void SetCursor(MouseCursor arrow);
};
}


#endif //MOUSE_H
