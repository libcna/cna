// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>

namespace CNA::Internal
{
    /**
     * @brief Magic at the start of the optional tangent-delta trailer in a `_morph.bin` sidecar.
     *
     * The bytes spell `MTAN` in the little-endian sidecar encoding. The original CNB-82 prefix
     * contains position and normal deltas and remains byte-for-byte readable by older CNA builds;
     * GLTF-289 appends this independently versioned block only when a target authors tangents.
     */
    inline constexpr std::int32_t CnjMorphTangentTrailerMagicEXT = 0x4E41544D;

    /** @brief Current version of the `_morph.bin` tangent-delta trailer. */
    inline constexpr std::int32_t CnjMorphTangentTrailerVersionEXT = 1;
}
