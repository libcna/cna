/* SPDX-License-Identifier: MS-PL */
#include <CNA/C/texture.h>
#include <CNA/C/display.h>
#include "CnaTestReport.h"
#include <stdint.h>

/* Test-only C++ bridge computes the native query results, without a device. */
void cna_test_texture_profile_native(
    CNA_GraphicsProfile profile, CNA_SurfaceFormat format, CNA_Bool results[3]);

int cna_test_texture_profile_foreign_thread(void);

typedef CNA_Result (*ProfileQuery)(CNA_GraphicsProfile, CNA_SurfaceFormat, CNA_Bool*);

static const ProfileQuery queries[] = {
    cna_texture_is_format_allowed_by_profile_ext,
    cna_texture_is_cube_format_allowed_by_profile_ext,
    cna_texture_is_render_target_format_allowed_by_profile_ext
};

static int validate_all_pairs(void)
{
    unsigned pairs = 0;
    for (CNA_GraphicsProfile profile = CNA_GRAPHICS_PROFILE_REACH;
         profile <= CNA_GRAPHICS_PROFILE_HI_DEF; ++profile) {
        for (CNA_SurfaceFormat format = CNA_SURFACE_FORMAT_COLOR;
             format <= CNA_SURFACE_FORMAT_USHORT_EXT; ++format) {
            CNA_Bool native[3];
            cna_test_texture_profile_native(profile, format, native);
            for (unsigned query = 0; query < 3U; ++query) {
                CNA_Bool actual = UINT8_C(99);
                if (queries[query](profile, format, &actual) != CNA_RESULT_SUCCESS ||
                    actual != native[query] || actual > CNA_TRUE) {
                    (void)fprintf(stderr, "query %u profile %u format %u: got %u, native %u\n",
                        query, profile, format, actual, native[query]);
                    return 0;
                }
                ++pairs;
            }
        }
    }
    (void)printf("native parity: %u query/profile/format combinations\n", pairs);
    return 1;
}

static int validate_contrasts(void)
{
    /* Explicit expectations also catch accidental changes shared by C and C++. */
    const CNA_SurfaceFormat formats[] = {CNA_SURFACE_FORMAT_COLOR,
        CNA_SURFACE_FORMAT_SINGLE, CNA_SURFACE_FORMAT_NORMALIZED_BYTE2,
        CNA_SURFACE_FORMAT_NORMALIZED_BYTE4, CNA_SURFACE_FORMAT_DXT1,
        CNA_SURFACE_FORMAT_DXT3, CNA_SURFACE_FORMAT_DXT5, CNA_SURFACE_FORMAT_BYTE_EXT};
    const CNA_Bool expected[2][3][8] = {
        {{1, 0, 1, 1, 1, 1, 1, 1}, {1, 1, 0, 0, 1, 1, 1, 1}, {1, 0, 1, 1, 0, 0, 0, 1}},
        {{1, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 0, 0, 1, 1, 1, 1}, {1, 1, 1, 1, 0, 0, 0, 1}}
    };
    for (unsigned profile = 0; profile < 2U; ++profile) {
        for (unsigned query = 0; query < 3U; ++query) {
            for (unsigned format = 0; format < sizeof(formats) / sizeof(formats[0]); ++format) {
                CNA_Bool actual = UINT8_C(99);
                if (queries[query](profile, formats[format], &actual) != CNA_RESULT_SUCCESS ||
                    actual != expected[profile][query][format]) return 0;
            }
        }
    }
    return 1;
}

static int validate_failures(void)
{
    const CNA_GraphicsProfile bad_profiles[] = {CNA_GRAPHICS_PROFILE_HI_DEF + UINT32_C(1),
        INT32_MAX, UINT32_C(0x80000000), UINT32_MAX};
    const CNA_SurfaceFormat bad_formats[] = {CNA_SURFACE_FORMAT_USHORT_EXT + UINT32_C(1),
        INT32_MAX, UINT32_C(0x80000000), UINT32_MAX};
    for (unsigned query = 0; query < 3U; ++query) {
        for (CNA_GraphicsProfile profile = CNA_GRAPHICS_PROFILE_REACH;
             profile <= CNA_GRAPHICS_PROFILE_HI_DEF; ++profile) {
            for (unsigned i = 0; i < sizeof(bad_formats) / sizeof(bad_formats[0]); ++i) {
                CNA_Bool output = UINT8_C(99);
                if (queries[query](profile, bad_formats[i], &output) != CNA_RESULT_INVALID_ARGUMENT ||
                    output != UINT8_C(99)) return 0;
            }
            for (CNA_SurfaceFormat format = CNA_SURFACE_FORMAT_COLOR;
                 format <= CNA_SURFACE_FORMAT_USHORT_EXT; ++format) {
                if (queries[query](profile, format, NULL) != CNA_RESULT_INVALID_ARGUMENT) return 0;
            }
        }
        for (unsigned i = 0; i < sizeof(bad_profiles) / sizeof(bad_profiles[0]); ++i) {
            for (CNA_SurfaceFormat format = CNA_SURFACE_FORMAT_COLOR;
                 format <= CNA_SURFACE_FORMAT_USHORT_EXT; ++format) {
                CNA_Bool output = UINT8_C(99);
                if (queries[query](bad_profiles[i], format, &output) != CNA_RESULT_INVALID_ARGUMENT ||
                    output != UINT8_C(99)) return 0;
            }
            if (queries[query](bad_profiles[i], UINT32_MAX, NULL) != CNA_RESULT_INVALID_ARGUMENT)
                return 0;
        }
    }
    return 1;
}

int main(void)
{
    const int parity = CNA_TEST_STAGE(validate_all_pairs());
    const int contrasts = CNA_TEST_STAGE(validate_contrasts());
    const int failures = CNA_TEST_STAGE(validate_failures());
    const int thread = CNA_TEST_STAGE(cna_test_texture_profile_foreign_thread());
    return parity && contrasts && failures && thread ? 0 : 1;
}
