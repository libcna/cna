// SPDX-License-Identifier: MS-PL
#include "CNA/C/runtime_graphics_manager.h"
#include "CNA/ProjectGraphicsProfile.hpp"
#include <thread>
extern "C" uint32_t cna_test_project_profile_native(void)
{
    return static_cast<uint32_t>(CNA::GetProjectGraphicsProfileEXT());
}
extern "C" void cna_test_project_profile_registration(uint32_t value)
{
    CNA::ProjectGraphicsProfileEXT registration(
        static_cast<Microsoft::Xna::Framework::Graphics::GraphicsProfile>(value));
}
extern "C" int cna_test_project_profile_worker(void)
{
    int result = 0;
    std::thread worker([&] {
        CNA_GraphicsProfile value = 99;
        result = cna_project_graphics_profile_get_ext(&value) == CNA_RESULT_THREAD && value == 99 &&
                 cna_project_graphics_profile_set_ext(CNA_GRAPHICS_PROFILE_REACH) == CNA_RESULT_THREAD;
    });
    worker.join();
    return result;
}
