// SPDX-License-Identifier: MS-PL
#include "CNA/C/cna.h"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"
#include "Microsoft/Devices/Sensors/Accelerometer.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include <thread>
extern "C" CNA_Bool cna_test_switch_native(unsigned kind, CNA_Bool expected)
{
    using namespace Microsoft::Xna::Framework;
    switch (kind) {
    case 0:
        Input::GamePad::setKeyboardEmulationEnabledEXT(expected != CNA_FALSE);
        return Input::GamePad::getKeyboardEmulationEnabledEXT() ? CNA_TRUE : CNA_FALSE;
    case 1:
        Input::Touch::TouchPanel::setInputSuppressedEXT(expected != CNA_FALSE);
        return Input::Touch::TouchPanel::getInputSuppressedEXT() ? CNA_TRUE : CNA_FALSE;
    case 2:
        Microsoft::Devices::Sensors::Accelerometer::setKeyboardEmulationEnabledEXT(expected != CNA_FALSE);
        return Microsoft::Devices::Sensors::Accelerometer::getKeyboardEmulationEnabledEXT() ? CNA_TRUE
                                                                                            : CNA_FALSE;
    default: {
        GameWindow window;
        window.setKeyboardOrientationEmulationEnabledEXT(expected != CNA_FALSE);
        return window.getKeyboardOrientationEmulationEnabledEXT() ? CNA_TRUE : CNA_FALSE;
    }
    }
}
extern "C" CNA_Result cna_test_switch_thread(CNA_Handle game, unsigned kind)
{
    CNA_Result result = CNA_RESULT_INTERNAL;
    std::thread worker([&] {
        CNA_Bool value = 99;
        switch (kind) {
        case 0:
            result = cna_gamepad_get_keyboard_emulation_enabled_ext(game, &value);
            break;
        case 1:
            result = cna_touch_panel_get_input_suppressed_ext(game, &value);
            break;
        case 2:
            result = cna_accelerometer_get_keyboard_emulation_enabled_ext(game, &value);
            break;
        default:
            result = cna_game_window_get_keyboard_orientation_emulation_enabled_ext(game, &value);
            break;
        }
        if (value != 99)
            result = CNA_RESULT_INTERNAL;
    });
    worker.join();
    return result;
}
