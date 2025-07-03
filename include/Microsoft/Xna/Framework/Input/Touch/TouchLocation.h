//
// Created by robertvokac on 5/25/25.
//

#ifndef TOUCHLOCATION_H
#define TOUCHLOCATION_H
#include "TouchLocationState.h"
#include "CNA/Prop.h"

#include "Microsoft/Xna/Framework/Vector2.h"

namespace Microsoft::Xna::Framework::Input::Touch {
    struct TouchLocation {
        DEF_PROP(TouchLocationState, State, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(Vector2, Position, getter1, setter0, member1, static0, constret1, ref1, constmet1)


        TouchLocation();
    };
}

#endif //TOUCHLOCATION_H

// #include <SDL3/SDL.h>
//
// void handleTouchEvent(const SDL_Event& event) {
//     if (event.type == SDL_EVENT_FINGER_DOWN ||
//         event.type == SDL_EVENT_FINGER_MOTION ||
//         event.type == SDL_EVENT_FINGER_UP) {
//
//         float x = event.tfinger.x;
//         float y = event.tfinger.y;
//
//         int screenX = static_cast<int>(x * SCREEN_WIDTH);
//         int screenY = static_cast<int>(y * SCREEN_HEIGHT);
//
//         TouchLocationState state;
//         switch (event.type) {
//             case SDL_EVENT_FINGER_DOWN:
//                 state = TouchLocationState::Pressed;
//                 break;
//             case SDL_EVENT_FINGER_MOTION:
//                 state = TouchLocationState::Moved;
//                 break;
//             case SDL_EVENT_FINGER_UP:
//                 state = TouchLocationState::Released;
//                 break;
//             default:
//                 state = TouchLocationState::Invalid;
//         }
//
//         TouchLocation touchLocation = {state, {screenX, screenY}};
//         }
// }
//
//
//
