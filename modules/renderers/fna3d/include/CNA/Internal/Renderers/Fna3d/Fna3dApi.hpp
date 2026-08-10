// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file
 * @brief The single place the FNA3D renderer includes the upstream C headers.
 *
 * Include order matters and is not obvious: `FNA3D.h` forward-declares `MOJOSHADER_effect` as an
 * *incomplete* type unless `_INCL_MOJOSHADER_H_` is already defined, so a translation unit that
 * reaches `FNA3D_CreateEffect`'s out-parameter must have included `mojoshader.h` first. The
 * Effect Framework structures themselves live behind `MOJOSHADER_EFFECT_SUPPORT`, and
 * `MOJOSHADER_NO_VERSION_INCLUDE` is required because MojoShader's generated
 * `mojoshader_version.h` is not present in the submodule checkout FNA3D builds from. All three
 * switches are set on the `cna_fna3d` interface target (cmake/ThirdPartyFNA3D.cmake); this header
 * exists so no renderer translation unit can accidentally get the order wrong.
 *
 * Established by fna3d-spike/ before any renderer code existed -- see its README.
 */

#include <mojoshader.h>

#include <FNA3D.h>
