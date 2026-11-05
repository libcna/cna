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
        /* Null wins; hardware has no required renderer (Refresh CurrentDeviceType). */
        CHECK(renderer == ((mask & 1U) ? CNA_GRAPHICS_RENDERER_HEADLESS
                         : (mask & 2U) ? CNA_GRAPHICS_RENDERER_SOFTWARE
                                       : CNA_GRAPHICS_RENDERER_UNKNOWN));
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
/* Adapter queries accept a game handle before the first frame. Game construction
 * creates a device and latches selection: run once per process under an explicitly
 * selected HEADLESS, STUB or SOFTWARE renderer, not by switching after creation.
 * Literal no-hook table: FNA GraphicsAdapter.cs:191-257 plus Refresh profile
 * lists (.cctor IL_00d4-013b, IL_030f-0405), including TQ-024's Alpha8 decision.
 * QueryFormat IL_003a-0055 requires equality of every selected value. */
static int literal_adapter_queries_for_renderer(CNA_GraphicsRendererType renderer)
{
    CNA_GameCreateInfo info = {0};
    CNA_Handle game = CNA_INVALID_HANDLE;
    const int32_t sample_requests[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 32, INT32_MAX};
    info.struct_size = sizeof(info);
    info.struct_version = 1;
    info.is_fixed_time_step = CNA_TRUE;
    info.target_elapsed_time_ticks = INT64_C(166667);
    CHECK(cna_game_create(&info, &game) == CNA_RESULT_SUCCESS);
    for (uint32_t profile = 0; profile < 2; ++profile) {
        CNA_Bool supported = 99;
        CHECK(cna_graphics_adapter_is_profile_supported(game, 0, profile, &supported) == CNA_RESULT_SUCCESS);
        CHECK(supported == CNA_TRUE);
        for (uint32_t format = 0; format <= CNA_SURFACE_FORMAT_USHORT_EXT; ++format)
        for (uint32_t depth = 0; depth < 4; ++depth)
        for (unsigned sample_index = 0; sample_index < sizeof(sample_requests) / sizeof(sample_requests[0]); ++sample_index)
        for (unsigned backbuffer = 0; backbuffer < 2; ++backbuffer) {
            const int32_t samples = sample_requests[sample_index];
            CNA_GraphicsFormatSelection out;
            const CNA_SurfaceFormat expected = !backbuffer && profile == CNA_GRAPHICS_PROFILE_HI_DEF &&
                format >= CNA_SURFACE_FORMAT_RGBA1010102 && format <= CNA_SURFACE_FORMAT_HDR_BLENDABLE
                    ? format : CNA_SURFACE_FORMAT_COLOR;
            const int32_t expected_samples = renderer == CNA_GRAPHICS_RENDERER_SOFTWARE && samples >= 4 ? 4 : 0;
            memset(&out, 0x5a, sizeof(out));
            out.struct_size = sizeof(out);
            out.struct_version = 1;
            const CNA_Result result = backbuffer
                ? cna_graphics_adapter_query_backbuffer_format(game, 0, profile, format, depth, samples, &out)
                : cna_graphics_adapter_query_render_target_format(game, 0, profile, format, depth, samples, &out);
            CHECK(result == CNA_RESULT_SUCCESS);
            CHECK(out.struct_size == sizeof(out) && out.struct_version == 1);
            CHECK(out.format == expected && out.depth_format == depth && out.multi_sample_count == expected_samples);
            CHECK(out.exact_match == (format == expected && samples == expected_samples ? CNA_TRUE : CNA_FALSE));
            CHECK(out.reserved[0] == 0 && out.reserved[1] == 0 && out.reserved[2] == 0);
        }
    }
    for (unsigned bad = 0; bad < 8; ++bad)
    for (unsigned backbuffer = 0; backbuffer < 2; ++backbuffer) {
        CNA_GraphicsFormatSelection out;
        unsigned char saved[sizeof(out)];
        uint32_t adapter = 0, profile = 0, format = 0, depth = 0;
        int32_t samples = 0;
        memset(&out, 0x5a, sizeof(out));
        out.struct_size = sizeof(out);
        out.struct_version = 1;
        switch (bad) {
        case 0: profile = UINT32_MAX; break;
        case 1: format = UINT32_MAX; break;
        case 2: depth = UINT32_MAX; break;
        case 3: samples = -1; break;
        case 4: out.struct_size = 0; break;
        case 5: out.struct_version = 0; break;
        case 6: adapter = UINT32_MAX; break;
        default: profile = 2; break;
        }
        memcpy(saved, &out, sizeof(out));
        const CNA_Result result = backbuffer
            ? cna_graphics_adapter_query_backbuffer_format(game, adapter, profile, format, depth, samples, &out)
            : cna_graphics_adapter_query_render_target_format(game, adapter, profile, format, depth, samples, &out);
        CHECK(result == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(memcmp(saved, &out, sizeof(out)) == 0);
    }
    CHECK(cna_game_destroy(game) == CNA_RESULT_SUCCESS);
    return 0;
}

static int literal_adapter_queries(void)
{
    CNA_GraphicsRendererType renderer;
    CHECK(cna_graphics_renderer_get_selected_ext(&renderer) == CNA_RESULT_SUCCESS);
    if (renderer != CNA_GRAPHICS_RENDERER_HEADLESS && renderer != CNA_GRAPHICS_RENDERER_STUB &&
        renderer != CNA_GRAPHICS_RENDERER_SOFTWARE) {
        puts("literal_adapter_queries: not exercised; requires HEADLESS, STUB or SOFTWARE");
        return 0;
    }
    CHECK(literal_adapter_queries_for_renderer(renderer) == 0);
    CNA_GraphicsRendererType active;
    CHECK(cna_graphics_renderer_get_active_ext(&active) == CNA_RESULT_SUCCESS && active == renderer);
    return 0;
}

int main(int argc, char **argv)
{
    CHECK(literal_adapter_queries() == 0);
    CHECK(values() == 0);
    CHECK(cna_test_adapter_worker(values) == 0);
    if (argc > 1 && strcmp(argv[1], "--runtime") == 0)
        CHECK(runtime() == 0);
    puts("AdapterValueSmoke: PASS");
    return 0;
}
