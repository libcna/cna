/* SPDX-License-Identifier: MS-PL */
#include <CNA/C/cna.h>
#include "CnaTestReport.h"
#include <string.h>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            return CNA_TEST_FAIL(1);                                                                         \
    } while (0)
uint32_t cna_test_project_profile_native(void);
void cna_test_project_profile_registration(uint32_t);
int cna_test_project_profile_worker(void);
static int values(void)
{
    CNA_GraphicsProfile profile = 99;
    CHECK(cna_project_graphics_profile_get_ext(NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_project_graphics_profile_get_ext(&profile) == CNA_RESULT_SUCCESS &&
          profile == CNA_GRAPHICS_PROFILE_REACH && profile == cna_test_project_profile_native());
    for (uint32_t value = 0; value < 2; ++value) {
        CHECK(cna_project_graphics_profile_set_ext(value) == CNA_RESULT_SUCCESS);
        CHECK(cna_project_graphics_profile_get_ext(&profile) == CNA_RESULT_SUCCESS && profile == value);
        cna_test_project_profile_registration(value);
        CHECK(cna_project_graphics_profile_get_ext(&profile) == CNA_RESULT_SUCCESS && profile == value);
        CHECK(cna_project_graphics_profile_set_ext(value) == CNA_RESULT_SUCCESS);
        CHECK(cna_project_graphics_profile_set_ext(value) == CNA_RESULT_SUCCESS);
        CHECK(cna_project_graphics_profile_get_ext(&profile) == CNA_RESULT_SUCCESS && profile == value &&
              profile == cna_test_project_profile_native());
        CHECK(cna_project_graphics_profile_set_ext(2) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(cna_project_graphics_profile_set_ext(UINT32_MAX) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(cna_project_graphics_profile_get_ext(&profile) == CNA_RESULT_SUCCESS && profile == value);
    }
    CHECK(cna_test_project_profile_worker());
    CHECK(cna_project_graphics_profile_get_ext(&profile) == CNA_RESULT_SUCCESS &&
          profile == CNA_GRAPHICS_PROFILE_HI_DEF);
    CHECK(cna_project_graphics_profile_set_ext(CNA_GRAPHICS_PROFILE_REACH) == CNA_RESULT_SUCCESS);
    return 0;
}
static int runtime(void)
{
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_GraphicsDeviceManagerHandle manager = CNA_INVALID_HANDLE;
    CNA_GraphicsProfile profile = 99;
    const CNA_GameCreateInfo info = {
        sizeof(CNA_GameCreateInfo), 1, CNA_TRUE, {0, 0, 0, 0, 0, 0, 0}, 166667, {NULL, 0}, NULL};
    CHECK(cna_project_graphics_profile_set_ext(CNA_GRAPHICS_PROFILE_HI_DEF) == CNA_RESULT_SUCCESS);
    CHECK(cna_game_create(&info, &game) == CNA_RESULT_SUCCESS);
    CHECK(cna_project_graphics_profile_set_ext(CNA_GRAPHICS_PROFILE_REACH) == CNA_RESULT_INVALID_STATE);
    CHECK(cna_project_graphics_profile_get_ext(&profile) == CNA_RESULT_INVALID_STATE && profile == 99);
    CHECK(cna_test_project_profile_native() == CNA_GRAPHICS_PROFILE_HI_DEF);
    CHECK(cna_graphics_device_manager_create(game, &manager) == CNA_RESULT_SUCCESS);
    CHECK(cna_graphics_device_manager_get_graphics_profile(manager, &profile) == CNA_RESULT_SUCCESS &&
          profile == CNA_GRAPHICS_PROFILE_HI_DEF);
    CHECK(cna_graphics_device_manager_destroy(manager) == CNA_RESULT_SUCCESS);
    CHECK(cna_game_destroy(game) == CNA_RESULT_SUCCESS);
    CHECK(cna_project_graphics_profile_get_ext(&profile) == CNA_RESULT_SUCCESS &&
          profile == CNA_GRAPHICS_PROFILE_HI_DEF);
    CHECK(cna_project_graphics_profile_set_ext(CNA_GRAPHICS_PROFILE_REACH) == CNA_RESULT_SUCCESS);
    return 0;
}
int main(int argc, char **argv)
{
    CHECK(values() == 0);
    if (argc > 1 && strcmp(argv[1], "--runtime") == 0)
        CHECK(runtime() == 0);
    puts("ProfileValueSmoke: PASS");
    return 0;
}
