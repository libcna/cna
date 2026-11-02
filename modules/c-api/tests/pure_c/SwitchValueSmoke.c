/* SPDX-License-Identifier: MS-PL */
#include <CNA/C/cna.h>
#include "CnaTestReport.h"
#include <string.h>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            return CNA_TEST_FAIL(1);                                                                         \
    } while (0)
CNA_Bool cna_test_switch_native(unsigned, CNA_Bool);
CNA_Result cna_test_switch_thread(CNA_Handle, unsigned);
typedef CNA_Result (*Get)(CNA_Handle, CNA_Bool *);
typedef CNA_Result (*Set)(CNA_Handle, CNA_Bool);
static const Get getters[] = {cna_gamepad_get_keyboard_emulation_enabled_ext,
                              cna_touch_panel_get_input_suppressed_ext,
                              cna_accelerometer_get_keyboard_emulation_enabled_ext,
                              cna_game_window_get_keyboard_orientation_emulation_enabled_ext};
static const Set setters[] = {cna_gamepad_set_keyboard_emulation_enabled_ext,
                              cna_touch_panel_set_input_suppressed_ext,
                              cna_accelerometer_set_keyboard_emulation_enabled_ext,
                              cna_game_window_set_keyboard_orientation_emulation_enabled_ext};
static int values(void)
{
    for (unsigned i = 0; i < 4; ++i) {
        CNA_Bool value = 99;
        CNA_Result result = getters[i](CNA_INVALID_HANDLE, &value);
        if (i == 2 && result == CNA_RESULT_NOT_SUPPORTED) {
            CHECK(value == 99);
            CHECK(setters[i](CNA_INVALID_HANDLE, CNA_TRUE) == CNA_RESULT_NOT_SUPPORTED);
            CHECK(setters[i](CNA_INVALID_HANDLE, 2) == CNA_RESULT_NOT_SUPPORTED);
            continue;
        }
        CHECK(result == CNA_RESULT_INVALID_HANDLE && value == 99);
        CHECK(getters[i](CNA_INVALID_HANDLE, NULL) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(setters[i](CNA_INVALID_HANDLE, 2) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(setters[i](CNA_INVALID_HANDLE, CNA_TRUE) == CNA_RESULT_INVALID_HANDLE);
    }
    return 0;
}
static int runtime(void)
{
    CNA_Handle game = CNA_INVALID_HANDLE;
    const CNA_GameCreateInfo info = {
        sizeof(CNA_GameCreateInfo), 1, CNA_TRUE, {0, 0, 0, 0, 0, 0, 0}, 166667, {NULL, 0}, NULL};
    CHECK(cna_game_create(&info, &game) == CNA_RESULT_SUCCESS);
    for (unsigned i = 0; i < 4; ++i) {
        CNA_Bool original = 99, value = 99;
        CNA_Result result = getters[i](game, &original);
        if (i == 2 && result == CNA_RESULT_NOT_SUPPORTED)
            continue;
        CHECK(result == CNA_RESULT_SUCCESS && original <= CNA_TRUE);
        CHECK(cna_test_switch_thread(game, i) == CNA_RESULT_THREAD);
        for (CNA_Bool flag = 0; flag <= 1; ++flag) {
            CHECK(setters[i](game, flag) == CNA_RESULT_SUCCESS);
            CHECK(getters[i](game, &value) == CNA_RESULT_SUCCESS && value == flag &&
                  value == cna_test_switch_native(i, flag));
            CHECK(setters[i](game, flag) == CNA_RESULT_SUCCESS);
            CHECK(setters[i](game, 2) == CNA_RESULT_INVALID_ARGUMENT);
            CHECK(getters[i](game, &value) == CNA_RESULT_SUCCESS && value == flag);
        }
        if (i == 2) {
            CNA_AccelerometerHandle sensor = CNA_INVALID_HANDLE;
            CHECK(cna_accelerometer_create(game, &sensor) == CNA_RESULT_SUCCESS);
            CHECK(cna_accelerometer_start(sensor) == CNA_RESULT_SUCCESS);
            CHECK(setters[i](game, CNA_FALSE) == CNA_RESULT_INVALID_STATE);
            CHECK(getters[i](game, &value) == CNA_RESULT_SUCCESS && value == CNA_TRUE);
            CHECK(setters[i](game, CNA_TRUE) == CNA_RESULT_SUCCESS);
            CHECK(cna_accelerometer_stop(sensor) == CNA_RESULT_SUCCESS);
            CHECK(cna_accelerometer_destroy(sensor) == CNA_RESULT_SUCCESS);
        }
        CHECK(setters[i](game, original) == CNA_RESULT_SUCCESS);
    }
    {
        CNA_GestureSample sample;
        CNA_Bool available = CNA_FALSE;
        CHECK(cna_touch_panel_set_input_suppressed_ext(game, CNA_FALSE) == CNA_RESULT_SUCCESS);
        CHECK(cna_gesture_sample_init(&sample) == CNA_RESULT_SUCCESS);
        sample.gesture_type = CNA_GESTURE_TYPE_TAP;
        CHECK(cna_touch_panel_enqueue_gesture_ext(game, &sample) == CNA_RESULT_SUCCESS);
        CHECK(cna_touch_panel_get_is_gesture_available(game, &available) == CNA_RESULT_SUCCESS &&
              available == CNA_TRUE);
        CHECK(cna_touch_panel_set_input_suppressed_ext(game, CNA_TRUE) == CNA_RESULT_SUCCESS);
        CHECK(cna_touch_panel_get_is_gesture_available(game, &available) == CNA_RESULT_SUCCESS &&
              available == CNA_FALSE);
        CHECK(cna_touch_panel_set_input_suppressed_ext(game, CNA_FALSE) == CNA_RESULT_SUCCESS);
    }
    for (unsigned i = 0; i < 4; ++i) {
        CNA_Result result = setters[i](game, CNA_TRUE);
        CHECK(result == CNA_RESULT_SUCCESS || (i == 2 && result == CNA_RESULT_NOT_SUPPORTED));
    }
    CHECK(cna_game_destroy(game) == CNA_RESULT_SUCCESS);
    for (unsigned i = 0; i < 4; ++i) {
        CNA_Bool value = 99;
        CNA_Result result = getters[i](game, &value);
        CHECK((result == CNA_RESULT_INVALID_HANDLE || (i == 2 && result == CNA_RESULT_NOT_SUPPORTED)) &&
              value == 99);
    }
    CHECK(cna_game_create(&info, &game) == CNA_RESULT_SUCCESS);
    for (unsigned i = 0; i < 4; ++i) {
        CNA_Bool value = 99;
        CNA_Result result = getters[i](game, &value);
        if (i == 2 && result == CNA_RESULT_NOT_SUPPORTED)
            continue;
        CHECK(result == CNA_RESULT_SUCCESS && value == (i == 3 ? CNA_FALSE : CNA_TRUE));
        CHECK(setters[i](game, CNA_FALSE) == CNA_RESULT_SUCCESS);
    }
    CHECK(cna_game_destroy(game) == CNA_RESULT_SUCCESS);
    return 0;
}
int main(int argc, char **argv)
{
    CHECK(values() == 0);
    if (argc > 1 && strcmp(argv[1], "--runtime") == 0)
        CHECK(runtime() == 0);
    puts("SwitchValueSmoke: PASS");
    return 0;
}
