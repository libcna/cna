/* SPDX-License-Identifier: MS-PL */
#include <CNA/C/input_gamepad.h>
#include "CnaTestReport.h"
#include <string.h>
#include <math.h>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            return CNA_TEST_FAIL(1);                                                                         \
    } while (0)
uint64_t cna_test_input_text(unsigned kind, uint32_t mask, const CNA_GamePadThumbSticks *sticks,
                             const CNA_GamePadTriggers *triggers, char *text);
int cna_test_input_worker(int (*test)(void));
static int values(void)
{
    const float samples[] = {0, -0.0F, 0.25F, -1, 1, 2, -2, NAN, INFINITY, -INFINITY};
    for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        CNA_GamePadThumbSticks sticks = {{samples[i], -0.25F}, {0.75F, samples[i]}};
        CNA_GamePadTriggers triggers = {samples[i], 0.75F};
        uint32_t buttons = i == 0 ? 0 : CNA_GAMEPAD_BUTTON_A | CNA_GAMEPAD_BUTTON_START;
        uint32_t dpad = i & 15U;
        char native[1024], actual[1024];
        uint64_t expected, size = 999;
        expected = cna_test_input_text(0, buttons, &sticks, &triggers, native);
        CHECK(cna_gamepad_buttons_get_string_size(buttons, &size) == CNA_RESULT_SUCCESS && size == expected);
        memset(actual, 'Z', sizeof(actual));
        CHECK(cna_gamepad_buttons_copy_string(buttons, actual, size, &size) == CNA_RESULT_SUCCESS);
        CHECK(memcmp(actual, native, (size_t)size) == 0 && actual[size] == 'Z');
        memset(actual, 'Z', sizeof(actual));
        CHECK(cna_gamepad_buttons_copy_string(buttons, actual, expected - 1, &size) ==
                  CNA_RESULT_BUFFER_TOO_SMALL &&
              size == expected);
        for (unsigned j = 0; j < sizeof(actual); ++j)
            CHECK(actual[j] == 'Z');
        CHECK(cna_gamepad_buttons_copy_string(buttons, NULL, 0, &size) == CNA_RESULT_BUFFER_TOO_SMALL &&
              size == expected);
        CHECK(cna_gamepad_buttons_copy_string(buttons, NULL, 1, &size) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(cna_gamepad_buttons_copy_string(buttons, actual, sizeof(actual), NULL) ==
              CNA_RESULT_INVALID_ARGUMENT);
        CHECK(cna_gamepad_buttons_get_string_size(buttons, NULL) == CNA_RESULT_INVALID_ARGUMENT);
        size = 999;
        CHECK(cna_gamepad_buttons_get_string_size(UINT32_MAX, &size) == CNA_RESULT_INVALID_ARGUMENT &&
              size == 999);
        CHECK(cna_gamepad_buttons_copy_string(UINT32_MAX, actual, sizeof(actual), &size) ==
                  CNA_RESULT_INVALID_ARGUMENT &&
              size == 999 && actual[0] == 'Z');
        expected = cna_test_input_text(1, dpad, &sticks, &triggers, native);
        CHECK(cna_gamepad_dpad_get_string_size(dpad, &size) == CNA_RESULT_SUCCESS && size == expected);
        memset(actual, 'Z', sizeof(actual));
        CHECK(cna_gamepad_dpad_copy_string(dpad, actual, size, &size) == CNA_RESULT_SUCCESS);
        CHECK(memcmp(actual, native, (size_t)size) == 0 && actual[size] == 'Z');
        memset(actual, 'Z', sizeof(actual));
        CHECK(cna_gamepad_dpad_copy_string(dpad, actual, expected - 1, &size) ==
                  CNA_RESULT_BUFFER_TOO_SMALL &&
              size == expected);
        for (unsigned j = 0; j < sizeof(actual); ++j)
            CHECK(actual[j] == 'Z');
        CHECK(cna_gamepad_dpad_copy_string(dpad, NULL, 0, &size) == CNA_RESULT_BUFFER_TOO_SMALL &&
              size == expected);
        CHECK(cna_gamepad_dpad_copy_string(dpad, NULL, 1, &size) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(cna_gamepad_dpad_copy_string(dpad, actual, sizeof(actual), NULL) ==
              CNA_RESULT_INVALID_ARGUMENT);
        CHECK(cna_gamepad_dpad_get_string_size(dpad, NULL) == CNA_RESULT_INVALID_ARGUMENT);
        size = 999;
        CHECK(cna_gamepad_dpad_get_string_size(UINT32_MAX, &size) == CNA_RESULT_INVALID_ARGUMENT &&
              size == 999);
        CHECK(cna_gamepad_dpad_copy_string(UINT32_MAX, actual, sizeof(actual), &size) ==
                  CNA_RESULT_INVALID_ARGUMENT &&
              size == 999 && actual[0] == 'Z');
        expected = cna_test_input_text(2, buttons, &sticks, &triggers, native);
        CHECK(cna_gamepad_thumb_sticks_get_string_size(&sticks, &size) == CNA_RESULT_SUCCESS &&
              size == expected);
        memset(actual, 'Z', sizeof(actual));
        CHECK(cna_gamepad_thumb_sticks_copy_string(&sticks, actual, size, &size) == CNA_RESULT_SUCCESS);
        CHECK(memcmp(actual, native, (size_t)size) == 0 && actual[size] == 'Z');
        memset(actual, 'Z', sizeof(actual));
        CHECK(cna_gamepad_thumb_sticks_copy_string(&sticks, actual, expected - 1, &size) ==
                  CNA_RESULT_BUFFER_TOO_SMALL &&
              size == expected);
        for (unsigned j = 0; j < sizeof(actual); ++j)
            CHECK(actual[j] == 'Z');
        CHECK(cna_gamepad_thumb_sticks_copy_string(&sticks, NULL, 0, &size) == CNA_RESULT_BUFFER_TOO_SMALL &&
              size == expected);
        CHECK(cna_gamepad_thumb_sticks_copy_string(&sticks, NULL, 1, &size) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(cna_gamepad_thumb_sticks_copy_string(&sticks, actual, sizeof(actual), NULL) ==
              CNA_RESULT_INVALID_ARGUMENT);
        CHECK(cna_gamepad_thumb_sticks_get_string_size(&sticks, NULL) == CNA_RESULT_INVALID_ARGUMENT);
        size = 999;
        CHECK(cna_gamepad_thumb_sticks_get_string_size(NULL, &size) == CNA_RESULT_INVALID_ARGUMENT &&
              size == 999);
        CHECK(cna_gamepad_thumb_sticks_copy_string(NULL, actual, sizeof(actual), &size) ==
                  CNA_RESULT_INVALID_ARGUMENT &&
              size == 999 && actual[0] == 'Z');
        expected = cna_test_input_text(3, buttons, &sticks, &triggers, native);
        CHECK(cna_gamepad_triggers_get_string_size(&triggers, &size) == CNA_RESULT_SUCCESS &&
              size == expected);
        memset(actual, 'Z', sizeof(actual));
        CHECK(cna_gamepad_triggers_copy_string(&triggers, actual, size, &size) == CNA_RESULT_SUCCESS);
        CHECK(memcmp(actual, native, (size_t)size) == 0 && actual[size] == 'Z');
        memset(actual, 'Z', sizeof(actual));
        CHECK(cna_gamepad_triggers_copy_string(&triggers, actual, expected - 1, &size) ==
                  CNA_RESULT_BUFFER_TOO_SMALL &&
              size == expected);
        for (unsigned j = 0; j < sizeof(actual); ++j)
            CHECK(actual[j] == 'Z');
        CHECK(cna_gamepad_triggers_copy_string(&triggers, NULL, 0, &size) == CNA_RESULT_BUFFER_TOO_SMALL &&
              size == expected);
        CHECK(cna_gamepad_triggers_copy_string(&triggers, NULL, 1, &size) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(cna_gamepad_triggers_copy_string(&triggers, actual, sizeof(actual), NULL) ==
              CNA_RESULT_INVALID_ARGUMENT);
        CHECK(cna_gamepad_triggers_get_string_size(&triggers, NULL) == CNA_RESULT_INVALID_ARGUMENT);
        size = 999;
        CHECK(cna_gamepad_triggers_get_string_size(NULL, &size) == CNA_RESULT_INVALID_ARGUMENT &&
              size == 999);
        CHECK(cna_gamepad_triggers_copy_string(NULL, actual, sizeof(actual), &size) ==
                  CNA_RESULT_INVALID_ARGUMENT &&
              size == 999 && actual[0] == 'Z');
    }
    return 0;
}
int main(void)
{
    CHECK(values() == 0);
    CHECK(cna_test_input_worker(values) == 0);
    puts("InputValueSmoke: 4 components, 10 samples, native text and failure atomicity: PASS");
    return 0;
}
