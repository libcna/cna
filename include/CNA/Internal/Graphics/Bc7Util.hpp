// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace CNA::Internal::Graphics
{
    /**
     * Software decompressor for BC7 (BPTC) block-compressed textures, implemented directly from
     * the public Khronos Data Format Specification's BPTC section (bit layout, partition tables,
     * anchor-index tables and interpolation formula), independent of any third-party decoder.
     * Decodes to RGBA8 (4 bytes per pixel, R first). Input data must be the raw compressed block
     * stream; no DDS header. The colour channels carry no colour-space semantics of their own --
     * whether the RGB bytes are interpreted as linear or sRGB is decided by the caller.
     */
    class Bc7Util
    {
    public:
        static std::vector<uint8_t> DecompressBc7(const uint8_t* data, std::size_t dataSize,
                                                   int width, int height);

    private:
        static void DecompressBc7Block(const uint8_t* block, int blockX, int blockY,
                                       int width, int height, uint8_t* imageData);
    };
}
