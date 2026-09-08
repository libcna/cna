// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace CNA::Internal::Graphics
{
    /**
     * Software decompressor for DXT1, DXT3, and DXT5 block-compressed textures.
     * All methods decode compressed blocks to RGBA8 (4 bytes per pixel, R first).
     * Input data must be the raw compressed block stream; no DDS header.
     */
    /**
     * @brief How a block's 5- and 6-bit endpoint channels reach eight bits.
     *
     * The two rules differ by at most one, and which one is right depends on who is being
     * matched. A GPU replicates the high bits, and every renderer that decodes a block on the
     * CPU has to answer what the hardware beside it would have; D3DX -- and so XNA's content
     * pipeline, which converts a `.dds` through it -- rounds *up*: `ceil(v * 255 / 31)`, measured
     * over all 32 five-bit and all 64 six-bit values
     * (tests/reference/xna40/differential/texture_dds_dxt3_*_sweep.xnb,
     * plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-136`).
     */
    enum class DxtEndpointExpansion
    {
        /** @brief What a GPU does, and what CNA's renderers must keep doing. */
        Hardware,
        /** @brief What D3DX does, and so what a built `.xnb` carries. */
        D3dx,
    };

    class DxtUtil
    {
    public:
        static std::vector<uint8_t> DecompressDxt1(const uint8_t* data, std::size_t dataSize,
                                                    int width, int height,
                                                    DxtEndpointExpansion expansion =
                                                        DxtEndpointExpansion::Hardware);
        static std::vector<uint8_t> DecompressDxt3(const uint8_t* data, std::size_t dataSize,
                                                    int width, int height,
                                                    DxtEndpointExpansion expansion =
                                                        DxtEndpointExpansion::Hardware);
        static std::vector<uint8_t> DecompressDxt5(const uint8_t* data, std::size_t dataSize,
                                                    int width, int height,
                                                    DxtEndpointExpansion expansion =
                                                        DxtEndpointExpansion::Hardware);

    private:
        static void ConvertRgb565ToRgb888(uint16_t color,
                                          uint8_t& r, uint8_t& g, uint8_t& b,
                                          DxtEndpointExpansion expansion);

        static void DecompressDxt1Block(const uint8_t* data, std::size_t& pos,
                                        int x, int y, int width, int height,
                                        uint8_t* imageData,
                                        DxtEndpointExpansion expansion);
        static void DecompressDxt3Block(const uint8_t* data, std::size_t& pos,
                                        int x, int y, int width, int height,
                                        uint8_t* imageData,
                                        DxtEndpointExpansion expansion);
        static void DecompressDxt5Block(const uint8_t* data, std::size_t& pos,
                                        int x, int y, int width, int height,
                                        uint8_t* imageData,
                                        DxtEndpointExpansion expansion);
    };
}
