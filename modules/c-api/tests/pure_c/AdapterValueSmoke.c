/* SPDX-License-Identifier: MS-PL */
#include <CNA/C/cna.h>
#include "CnaTestReport.h"
#include <math.h>
#include <string.h>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            return CNA_TEST_FAIL(1);                                                                         \
    } while (0)
CNA_Bool cna_test_adapter_wide(float);
void cna_test_adapter_preferences(CNA_Bool, CNA_Bool, uint32_t *, uint32_t *);
int cna_test_adapter_worker(int (*test)(void));
CNA_Bool cna_test_adapter_logical(int32_t *, int32_t *);
static int values(void)
{
    const float samples[] = {0,          1, 4.0F / 3.0F, 16.0F / 9.0F, 1.6F, 1.5999999F,
                             1.6000001F, 2, -1,          INFINITY,     NAN};
    for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        CNA_Bool result = 99;
        CHECK(cna_graphics_adapter_is_wide_screen_aspect_ratio_ext(samples[i], &result) ==
                  CNA_RESULT_SUCCESS &&
              result == cna_test_adapter_wide(samples[i]) &&
              result == (samples[i] > 1.6F ? CNA_TRUE : CNA_FALSE));
    }
    CHECK(cna_graphics_adapter_is_wide_screen_aspect_ratio_ext(1, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    {
        uint32_t type = 99, renderer = 99;
        int32_t w = 99, h = 98;
        CNA_Bool available = 97;
        CHECK(cna_graphics_adapter_get_current_device_type_ext(CNA_INVALID_HANDLE, &type) ==
                  CNA_RESULT_INVALID_HANDLE &&
              type == 99);
        CHECK(cna_graphics_adapter_get_required_renderer_ext(CNA_INVALID_HANDLE, &renderer) ==
                  CNA_RESULT_INVALID_HANDLE &&
              renderer == 99);
        CHECK(cna_graphics_adapter_get_current_device_type_ext(CNA_INVALID_HANDLE, NULL) ==
              CNA_RESULT_INVALID_ARGUMENT);
        CHECK(cna_graphics_adapter_get_required_renderer_ext(CNA_INVALID_HANDLE, NULL) ==
              CNA_RESULT_INVALID_ARGUMENT);
        CHECK(cna_graphics_device_get_logical_size_ext(CNA_INVALID_HANDLE, &w, &h, &available) ==
                  CNA_RESULT_INVALID_HANDLE &&
              w == 99 && h == 98 && available == 97);
        CHECK(cna_graphics_device_get_logical_size_ext(CNA_INVALID_HANDLE, NULL, &h, &available) ==
              CNA_RESULT_INVALID_ARGUMENT);
        CHECK(cna_graphics_device_get_logical_size_ext(CNA_INVALID_HANDLE, &w, NULL, &available) ==
              CNA_RESULT_INVALID_ARGUMENT);
        CHECK(cna_graphics_device_get_logical_size_ext(CNA_INVALID_HANDLE, &w, &h, NULL) ==
              CNA_RESULT_INVALID_ARGUMENT);
    }
    return 0;
}
static int runtime(void)
{
    int32_t nw = 0, nh = 0, w = 0, h = 0;
    CNA_Bool expected = cna_test_adapter_logical(&nw, &nh), available = 99;
    CNA_PresentationParameters pp;
    CNA_Handle device = CNA_INVALID_HANDLE;
    CHECK(cna_presentation_parameters_init(&pp) == CNA_RESULT_SUCCESS);
    pp.is_full_screen = CNA_FALSE;
    pp.back_buffer_width = 320;
    pp.back_buffer_height = 240;
    CHECK(cna_graphics_device_create(0, CNA_GRAPHICS_PROFILE_REACH, &pp, &device) == CNA_RESULT_SUCCESS);
    CHECK(cna_graphics_device_get_logical_size_ext(device, &w, &h, &available) == CNA_RESULT_SUCCESS &&
          w == nw && h == nh && available == expected);
    for (unsigned mask = 0; mask < 4; ++mask) {
        uint32_t type = 99, renderer = 99, nt = 99, nr = 99;
        CHECK(cna_graphics_adapter_set_device_preferences(
                  device, 0, (CNA_Bool)(mask & 1U), (CNA_Bool)((mask >> 1) & 1U)) == CNA_RESULT_SUCCESS);
        CHECK(cna_graphics_adapter_get_current_device_type_ext(device, &type) == CNA_RESULT_SUCCESS);
        CHECK(cna_graphics_adapter_get_required_renderer_ext(device, &renderer) == CNA_RESULT_SUCCESS);
        cna_test_adapter_preferences((CNA_Bool)(mask & 1U), (CNA_Bool)((mask >> 1) & 1U), &nt, &nr);
        CHECK(type == nt && renderer == nr);
        CHECK(type == ((mask & 1U)   ? CNA_GRAPHICS_DEVICE_TYPE_NULL_EXT
                       : (mask & 2U) ? CNA_GRAPHICS_DEVICE_TYPE_REFERENCE_EXT
                                     : CNA_GRAPHICS_DEVICE_TYPE_HARDWARE_EXT));
    }
    CHECK(cna_graphics_adapter_set_device_preferences(device, 0, CNA_FALSE, CNA_FALSE) == CNA_RESULT_SUCCESS);
    CHECK(cna_graphics_device_destroy(device) == CNA_RESULT_SUCCESS);
    w = 99;
    h = 98;
    available = 97;
    CHECK(cna_graphics_device_get_logical_size_ext(device, &w, &h, &available) == CNA_RESULT_INVALID_HANDLE &&
          w == 99 && h == 98 && available == 97);
    return 0;
}
int main(int argc, char **argv)
{
    CHECK(values() == 0);
    CHECK(cna_test_adapter_worker(values) == 0);
    if (argc > 1 && strcmp(argv[1], "--runtime") == 0)
        CHECK(runtime() == 0);
    puts("AdapterValueSmoke: PASS");
    return 0;
}
