// SPDX-License-Identifier: MS-PL
#include "CNA/C/input_gamepad.h"
#include "Microsoft/Xna/Framework/Input/GamePadButtons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDPad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadThumbSticks.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadTriggers.hpp"
#include <cstring>
#include <string>
#include <thread>
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;
extern "C" uint64_t cna_test_input_text(unsigned kind, uint32_t mask, const CNA_GamePadThumbSticks *sticks,
                                        const CNA_GamePadTriggers *triggers, char *text)
{
    std::string value;
    switch (kind) {
    case 0:
        value = GamePadButtons(static_cast<Buttons>(mask)).ToString();
        break;
    case 1:
        value =
            GamePadDPad(static_cast<ButtonState>((mask & 1) != 0), static_cast<ButtonState>((mask & 2) != 0),
                        static_cast<ButtonState>((mask & 4) != 0), static_cast<ButtonState>((mask & 8) != 0))
                .ToString();
        break;
    case 2:
        value = GamePadThumbSticks(Vector2(sticks->left.x, sticks->left.y),
                                   Vector2(sticks->right.x, sticks->right.y))
                    .ToString();
        break;
    default:
        value = GamePadTriggers(triggers->left, triggers->right).ToString();
        break;
    }
    std::memcpy(text, value.data(), value.size());
    return value.size();
}
extern "C" int cna_test_input_worker(int (*test)(void))
{
    int result = 0;
    std::thread worker([&] { result = test(); });
    worker.join();
    return result;
}
