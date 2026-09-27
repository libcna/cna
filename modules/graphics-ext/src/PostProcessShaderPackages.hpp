// SPDX-License-Identifier: MS-PL
#pragma once
#ifdef CNA_CNAEXT
#include "CNA/Graphics/ShaderPackageEXT.hpp"
namespace CNA::Graphics::detail {
    /** @brief Creates the portable CRT shader package. */
    [[nodiscard]] ShaderPackageEXT CreateCrtShaderPackage();
    /** @brief Creates the portable colour-depth reduction shader package. */
    [[nodiscard]] ShaderPackageEXT CreateDepthEffectShaderPackage();
}
#endif // CNA_CNAEXT
