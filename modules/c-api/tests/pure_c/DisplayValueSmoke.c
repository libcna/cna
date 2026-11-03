/* SPDX-License-Identifier: MS-PL */
#include <CNA/C/runtime_graphics_manager.h>
#include "CnaTestReport.h"
#include <string.h>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            return CNA_TEST_FAIL(1);                                                                         \
    } while (0)
uint64_t cna_test_display_native(const CNA_DisplayMode *, int32_t *, CNA_Rectangle *, char *);
int32_t cna_test_information_hash(const CNA_GraphicsDeviceInformation *, uint64_t);
CNA_Bool cna_test_information_equal(const CNA_GraphicsDeviceInformation *, uint64_t,
                                    const CNA_GraphicsDeviceInformation *, uint64_t);
int cna_test_display_worker(int (*test)(void));
/* Fixed display contract: width XOR height XOR Color(0), ratio 640/320 = 2.
 * The API text includes AspectRatio, as recorded by CNA's XNA value contract. */
static int literal_values(void)
{
    CNA_DisplayMode mode; int32_t hash = 99; char text[96] = {0}; uint64_t size = 99;
    const char expected[] = "{Width:640 Height:320 Format:Color AspectRatio:2}";
    CHECK(cna_display_mode_init(640, 320, CNA_SURFACE_FORMAT_COLOR, &mode) == CNA_RESULT_SUCCESS);
    CHECK(cna_display_mode_get_hash_code(&mode, &hash) == CNA_RESULT_SUCCESS && hash == 960);
    CHECK(cna_display_mode_copy_string(&mode, text, sizeof(text), &size) == CNA_RESULT_SUCCESS && size == sizeof(expected)-1 && memcmp(text, expected, sizeof(expected)-1) == 0);
    return 0;
}
static int values(void)
{
    const int32_t widths[] = {0, 1, 641, 1920, -1, INT32_MAX};
    for (unsigned i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
        for (uint32_t format = 0; format <= CNA_SURFACE_FORMAT_USHORT_EXT; ++format) {
            CNA_DisplayMode mode;
            CNA_Rectangle area, native_area;
            char text[1024], native[1024];
            uint64_t size = 0, expected;
            int32_t hash, native_hash;
            CHECK(cna_display_mode_init(widths[i], i == 0 ? 0 : 479, format, &mode) == CNA_RESULT_SUCCESS);
            mode.aspect_ratio = 123.0F;
            expected = cna_test_display_native(&mode, &native_hash, &native_area, native);
            CHECK(cna_display_mode_get_hash_code(&mode, &hash) == CNA_RESULT_SUCCESS && hash == native_hash);
            CHECK(cna_display_mode_get_title_safe_area(&mode, &area) == CNA_RESULT_SUCCESS);
            CHECK(area.x == native_area.x && area.y == native_area.y && area.width == native_area.width &&
                  area.height == native_area.height);
            CHECK(area.x == 0 && area.y == 0 && area.width == mode.width && area.height == mode.height);
            CHECK(cna_display_mode_get_string_size(&mode, &size) == CNA_RESULT_SUCCESS && size == expected);
            memset(text, 'Z', sizeof(text));
            CHECK(cna_display_mode_copy_string(&mode, text, expected, &size) == CNA_RESULT_SUCCESS &&
                  memcmp(text, native, (size_t)size) == 0 && text[size] == 'Z');
            memset(text, 'Z', sizeof(text));
            CHECK(cna_display_mode_copy_string(&mode, text, expected - 1, &size) ==
                      CNA_RESULT_BUFFER_TOO_SMALL &&
                  size == expected);
            for (unsigned j = 0; j < sizeof(text); ++j)
                CHECK(text[j] == 'Z');
            CHECK(cna_display_mode_copy_string(&mode, NULL, 0, &size) == CNA_RESULT_BUFFER_TOO_SMALL);
            CHECK(cna_display_mode_copy_string(&mode, NULL, 1, &size) == CNA_RESULT_INVALID_ARGUMENT);
            CHECK(cna_display_mode_copy_string(&mode, text, sizeof(text), NULL) ==
                  CNA_RESULT_INVALID_ARGUMENT);
            CHECK(cna_display_mode_get_hash_code(&mode, NULL) == CNA_RESULT_INVALID_ARGUMENT);
            CHECK(cna_display_mode_get_title_safe_area(&mode, NULL) == CNA_RESULT_INVALID_ARGUMENT);
            CHECK(cna_display_mode_get_string_size(&mode, NULL) == CNA_RESULT_INVALID_ARGUMENT);
            mode.struct_size = 0;
            hash = 99;
            size = 999;
            area = (CNA_Rectangle){1, 2, 3, 4};
            CHECK(cna_display_mode_get_hash_code(&mode, &hash) == CNA_RESULT_INVALID_ARGUMENT && hash == 99);
            CHECK(cna_display_mode_get_title_safe_area(&mode, &area) == CNA_RESULT_INVALID_ARGUMENT &&
                  area.width == 3);
            CHECK(cna_display_mode_get_string_size(&mode, &size) == CNA_RESULT_INVALID_ARGUMENT &&
                  size == 999);
            CHECK(cna_display_mode_copy_string(&mode, text, sizeof(text), &size) ==
                      CNA_RESULT_INVALID_ARGUMENT &&
                  size == 999 && text[0] == 'Z');
            mode.struct_size = sizeof(mode);
            mode.format = UINT32_MAX;
            CHECK(cna_display_mode_get_hash_code(&mode, &hash) == CNA_RESULT_INVALID_ARGUMENT && hash == 99);
            CHECK(cna_display_mode_get_hash_code(NULL, &hash) == CNA_RESULT_INVALID_ARGUMENT && hash == 99);
        }
    }
    {
        CNA_GraphicsDeviceInformation left, right;
        CHECK(cna_graphics_device_information_init(&left) == CNA_RESULT_SUCCESS);
        for (unsigned field = 0; field < 15; ++field) {
            CNA_Bool equal = 99;
            int32_t hash = 99;
            uint64_t window = 0;
            right = left;
            switch (field) {
            case 0:
                break;
            case 1:
                right.adapter_index = 0;
                break;
            case 2:
                right.graphics_profile = CNA_GRAPHICS_PROFILE_HI_DEF;
                break;
            case 3:
                right.presentation_parameters.back_buffer_width += 1;
                break;
            case 4:
                right.presentation_parameters.back_buffer_height += 1;
                break;
            case 5:
                right.presentation_parameters.back_buffer_format = CNA_SURFACE_FORMAT_BGR565;
                break;
            case 6:
                right.presentation_parameters.depth_stencil_format = CNA_DEPTH_FORMAT_DEPTH24;
                break;
            case 7:
                right.presentation_parameters.multi_sample_count = 4;
                break;
            case 8:
                right.presentation_parameters.display_orientation = CNA_DISPLAY_ORIENTATION_PORTRAIT;
                break;
            case 9:
                right.presentation_parameters.presentation_interval = CNA_PRESENT_INTERVAL_TWO;
                break;
            case 10:
                right.presentation_parameters.render_target_usage = CNA_RENDER_TARGET_USAGE_PRESERVE_CONTENTS;
                break;
            case 11:
                right.presentation_parameters.is_full_screen = CNA_FALSE;
                break;
            case 12:
                window = UINT64_C(0x1234567887654321);
                break;
            case 13:
                right.presentation_parameters.headless_ext = CNA_TRUE;
                break;
            default:
                right.adapter_index = -99;
                break;
            }
            CHECK(cna_graphics_device_information_equals(&left, 0, &right, window, &equal) ==
                  CNA_RESULT_SUCCESS);
            CHECK(equal == cna_test_information_equal(&left, 0, &right, window));
            CHECK(equal == ((field == 0 || field == 13 || field == 14) ? CNA_TRUE : CNA_FALSE));
            CHECK(cna_graphics_device_information_get_hash_code(&right, window, &hash) ==
                      CNA_RESULT_SUCCESS &&
                  hash == cna_test_information_hash(&right, window));
            CHECK(cna_graphics_device_information_equals(&right, window, &right, window, &equal) ==
                      CNA_RESULT_SUCCESS &&
                  equal == CNA_TRUE);
        }
        for (unsigned bad = 0; bad < 5; ++bad) {
            CNA_Bool equal = 99;
            int32_t hash = 99;
            right = left;
            if (bad == 0)
                right.struct_size = 0;
            if (bad == 1)
                right.struct_version = 0;
            if (bad == 2)
                right.graphics_profile = UINT32_MAX;
            if (bad == 3)
                right.presentation_parameters.struct_size = 0;
            if (bad == 4)
                right.presentation_parameters.is_full_screen = 2;
            CHECK(cna_graphics_device_information_equals(&left, 0, &right, 0, &equal) ==
                      CNA_RESULT_INVALID_ARGUMENT &&
                  equal == 99);
            CHECK(cna_graphics_device_information_get_hash_code(&right, 0, &hash) ==
                      CNA_RESULT_INVALID_ARGUMENT &&
                  hash == 99);
        }
        CHECK(cna_graphics_device_information_equals(&left, 0, &left, 0, NULL) ==
              CNA_RESULT_INVALID_ARGUMENT);
        CHECK(cna_graphics_device_information_get_hash_code(&left, 0, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    }
    return 0;
}
int main(void)
{
    CHECK(literal_values() == 0);
    CHECK(values() == 0);
    CHECK(cna_test_display_worker(values) == 0);
    puts("DisplayValueSmoke: native text/hash/bounds; all ten presentation fields and ignored fields: PASS");
    return 0;
}
