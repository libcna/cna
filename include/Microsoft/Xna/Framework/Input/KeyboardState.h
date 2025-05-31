//
// Created by robertvokac on 5/26/25.
//

#ifndef KEYBOARDSTATE_H
#define KEYBOARDSTATE_H
#include "Keys.h"

namespace Microsoft::Xna::Framework::Input {

struct KeyboardState {
    bool IsKeyDown(Keys key);
};

}



#endif //KEYBOARDSTATE_H
