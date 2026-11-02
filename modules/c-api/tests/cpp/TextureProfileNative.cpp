// SPDX-License-Identifier: MS-PL
#include "CNA/C/texture.h"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include <thread>

extern "C" void cna_test_texture_profile_native(
    const CNA_GraphicsProfile profile,
    const CNA_SurfaceFormat format,
    CNA_Bool results[3])
{
    using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture;
    const auto nativeProfile = static_cast<GraphicsProfile>(profile);
    const auto nativeFormat = static_cast<SurfaceFormat>(format);
    results[0] = Texture::IsFormatAllowedByProfileEXT(nativeProfile, nativeFormat) ? CNA_TRUE : CNA_FALSE;
    results[1] = Texture::IsCubeFormatAllowedByProfileEXT(nativeProfile, nativeFormat) ? CNA_TRUE : CNA_FALSE;
    results[2] = Texture::IsRenderTargetFormatAllowedByProfileEXT(nativeProfile, nativeFormat) ? CNA_TRUE : CNA_FALSE;
}

extern "C" int cna_test_texture_profile_foreign_thread(void)
{
    int succeeded = 0;
    std::thread worker([&] {
        CNA_Bool texture = CNA_FALSE;
        CNA_Bool cube = CNA_TRUE;
        CNA_Bool target = CNA_TRUE;
        succeeded =
            cna_texture_is_format_allowed_by_profile_ext(
                CNA_GRAPHICS_PROFILE_HI_DEF, CNA_SURFACE_FORMAT_SINGLE, &texture) == CNA_RESULT_SUCCESS &&
            texture == CNA_TRUE &&
            cna_texture_is_cube_format_allowed_by_profile_ext(
                CNA_GRAPHICS_PROFILE_REACH, CNA_SURFACE_FORMAT_NORMALIZED_BYTE2, &cube) == CNA_RESULT_SUCCESS &&
            cube == CNA_FALSE &&
            cna_texture_is_render_target_format_allowed_by_profile_ext(
                CNA_GRAPHICS_PROFILE_HI_DEF, CNA_SURFACE_FORMAT_DXT1, &target) == CNA_RESULT_SUCCESS &&
            target == CNA_FALSE;
    });
    worker.join();
    return succeeded;
}
