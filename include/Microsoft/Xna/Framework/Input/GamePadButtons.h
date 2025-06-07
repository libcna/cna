//
// Created by robertvokac on 5/28/25.
//

#ifndef GAMEPADBUTTONS_H
#define GAMEPADBUTTONS_H
#include "ButtonState.h"
#include "Keys.h"
#include "CNA/Prop.h"


namespace Microsoft::Xna::Framework::Input {
    struct GamePadButtons {
    public:
    private:
        dgetter(ButtonState, Back)

        GamePadButtons();
    };
}


#endif //GAMEPADBUTTONS_H
