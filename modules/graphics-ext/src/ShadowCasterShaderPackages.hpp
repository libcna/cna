// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ShaderPackageEXT.hpp"

namespace CNA::Graphics::detail
{
    /** @brief Creates the rigid directional/cascade shadow-caster package. */
    [[nodiscard]] ShaderPackageEXT CreateDirectionalShadowCasterPackage();

    /** @brief Creates the skinned directional shadow-caster package. */
    [[nodiscard]] ShaderPackageEXT CreateSkinnedDirectionalShadowCasterPackage();

    /** @brief Creates the point-light cube shadow-caster package. */
    [[nodiscard]] ShaderPackageEXT CreateCubeShadowCasterPackage();

    /** @brief Creates the spot-light shadow-caster package. */
    [[nodiscard]] ShaderPackageEXT CreateSpotShadowCasterPackage();
}

#endif // CNA_CNAEXT
