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
    class DxtUtil
    {
    public:
        static std::vector<uint8_t> DecompressDxt1(const uint8_t* data, std::size_t dataSize,
                                                    int width, int height);
        static std::vector<uint8_t> DecompressDxt3(const uint8_t* data, std::size_t dataSize,
                                                    int width, int height);
        static std::vector<uint8_t> DecompressDxt5(const uint8_t* data, std::size_t dataSize,
                                                    int width, int height);

    private:
        static void ConvertRgb565ToRgb888(uint16_t color,
                                          uint8_t& r, uint8_t& g, uint8_t& b);

        static void DecompressDxt1Block(const uint8_t* data, std::size_t& pos,
                                        int x, int y, int width, int height,
                                        uint8_t* imageData);
        static void DecompressDxt3Block(const uint8_t* data, std::size_t& pos,
                                        int x, int y, int width, int height,
                                        uint8_t* imageData);
        static void DecompressDxt5Block(const uint8_t* data, std::size_t& pos,
                                        int x, int y, int width, int height,
                                        uint8_t* imageData);
    };
}
