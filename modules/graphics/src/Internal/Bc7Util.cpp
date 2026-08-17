// SPDX-License-Identifier: MS-PL
//
// Implements the BC7 (BPTC) decode algorithm exactly as documented in the public Khronos Data
// Format Specification's BPTC section (bit layout table, partition/anchor tables, interpolation
// formula) -- independent of any third-party decoder implementation. The partition and anchor
// tables below are transcribed verbatim from that specification's own numeric tables.
#include "CNA/Internal/Graphics/Bc7Util.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Graphics
{
    namespace
    {
        // clang-format off
        static constexpr std::uint8_t kBc7Partition2[64][16] = {
            {0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1},
            {0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1},
            {0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1},
            {0,0,0,1,0,0,1,1,0,0,1,1,0,1,1,1},
            {0,0,0,0,0,0,0,1,0,0,0,1,0,0,1,1},
            {0,0,1,1,0,1,1,1,0,1,1,1,1,1,1,1},
            {0,0,0,1,0,0,1,1,0,1,1,1,1,1,1,1},
            {0,0,0,0,0,0,0,1,0,0,1,1,0,1,1,1},
            {0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,1},
            {0,0,1,1,0,1,1,1,1,1,1,1,1,1,1,1},
            {0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1},
            {0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,1},
            {0,0,0,1,0,1,1,1,1,1,1,1,1,1,1,1},
            {0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1},
            {0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1},
            {0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1},
            {0,0,0,0,1,0,0,0,1,1,1,0,1,1,1,1},
            {0,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,1,0,0,0,1,1,1,0},
            {0,1,1,1,0,0,1,1,0,0,0,1,0,0,0,0},
            {0,0,1,1,0,0,0,1,0,0,0,0,0,0,0,0},
            {0,0,0,0,1,0,0,0,1,1,0,0,1,1,1,0},
            {0,0,0,0,0,0,0,0,1,0,0,0,1,1,0,0},
            {0,1,1,1,0,0,1,1,0,0,1,1,0,0,0,1},
            {0,0,1,1,0,0,0,1,0,0,0,1,0,0,0,0},
            {0,0,0,0,1,0,0,0,1,0,0,0,1,1,0,0},
            {0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0},
            {0,0,1,1,0,1,1,0,0,1,1,0,1,1,0,0},
            {0,0,0,1,0,1,1,1,1,1,1,0,1,0,0,0},
            {0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0},
            {0,1,1,1,0,0,0,1,1,0,0,0,1,1,1,0},
            {0,0,1,1,1,0,0,1,1,0,0,1,1,1,0,0},
            {0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1},
            {0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1},
            {0,1,0,1,1,0,1,0,0,1,0,1,1,0,1,0},
            {0,0,1,1,0,0,1,1,1,1,0,0,1,1,0,0},
            {0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0},
            {0,1,0,1,0,1,0,1,1,0,1,0,1,0,1,0},
            {0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1},
            {0,1,0,1,1,0,1,0,1,0,1,0,0,1,0,1},
            {0,1,1,1,0,0,1,1,1,1,0,0,1,1,1,0},
            {0,0,0,1,0,0,1,1,1,1,0,0,1,0,0,0},
            {0,0,1,1,0,0,1,0,0,1,0,0,1,1,0,0},
            {0,0,1,1,1,0,1,1,1,1,0,1,1,1,0,0},
            {0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0},
            {0,0,1,1,1,1,0,0,1,1,0,0,0,0,1,1},
            {0,1,1,0,0,1,1,0,1,0,0,1,1,0,0,1},
            {0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0},
            {0,1,0,0,1,1,1,0,0,1,0,0,0,0,0,0},
            {0,0,1,0,0,1,1,1,0,0,1,0,0,0,0,0},
            {0,0,0,0,0,0,1,0,0,1,1,1,0,0,1,0},
            {0,0,0,0,0,1,0,0,1,1,1,0,0,1,0,0},
            {0,1,1,0,1,1,0,0,1,0,0,1,0,0,1,1},
            {0,0,1,1,0,1,1,0,1,1,0,0,1,0,0,1},
            {0,1,1,0,0,0,1,1,1,0,0,1,1,1,0,0},
            {0,0,1,1,1,0,0,1,1,1,0,0,0,1,1,0},
            {0,1,1,0,1,1,0,0,1,1,0,0,1,0,0,1},
            {0,1,1,0,0,0,1,1,0,0,1,1,1,0,0,1},
            {0,1,1,1,1,1,1,0,1,0,0,0,0,0,0,1},
            {0,0,0,1,1,0,0,0,1,1,1,0,0,1,1,1},
            {0,0,0,0,1,1,1,1,0,0,1,1,0,0,1,1},
            {0,0,1,1,0,0,1,1,1,1,1,1,0,0,0,0},
            {0,0,1,0,0,0,1,0,1,1,1,0,1,1,1,0},
            {0,1,0,0,0,1,0,0,0,1,1,1,0,1,1,1},
        };

        static constexpr std::uint8_t kBc7Partition3[64][16] = {
            {0,0,1,1,0,0,1,1,0,2,2,1,2,2,2,2},
            {0,0,0,1,0,0,1,1,2,2,1,1,2,2,2,1},
            {0,0,0,0,2,0,0,1,2,2,1,1,2,2,1,1},
            {0,2,2,2,0,0,2,2,0,0,1,1,0,1,1,1},
            {0,0,0,0,0,0,0,0,1,1,2,2,1,1,2,2},
            {0,0,1,1,0,0,1,1,0,0,2,2,0,0,2,2},
            {0,0,2,2,0,0,2,2,1,1,1,1,1,1,1,1},
            {0,0,1,1,0,0,1,1,2,2,1,1,2,2,1,1},
            {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2},
            {0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2},
            {0,0,0,0,1,1,1,1,2,2,2,2,2,2,2,2},
            {0,0,1,2,0,0,1,2,0,0,1,2,0,0,1,2},
            {0,1,1,2,0,1,1,2,0,1,1,2,0,1,1,2},
            {0,1,2,2,0,1,2,2,0,1,2,2,0,1,2,2},
            {0,0,1,1,0,1,1,2,1,1,2,2,1,2,2,2},
            {0,0,1,1,2,0,0,1,2,2,0,0,2,2,2,0},
            {0,0,0,1,0,0,1,1,0,1,1,2,1,1,2,2},
            {0,1,1,1,0,0,1,1,2,0,0,1,2,2,0,0},
            {0,0,0,0,1,1,2,2,1,1,2,2,1,1,2,2},
            {0,0,2,2,0,0,2,2,0,0,2,2,1,1,1,1},
            {0,1,1,1,0,1,1,1,0,2,2,2,0,2,2,2},
            {0,0,0,1,0,0,0,1,2,2,2,1,2,2,2,1},
            {0,0,0,0,0,0,1,1,0,1,2,2,0,1,2,2},
            {0,0,0,0,1,1,0,0,2,2,1,0,2,2,1,0},
            {0,1,2,2,0,1,2,2,0,0,1,1,0,0,0,0},
            {0,0,1,2,0,0,1,2,1,1,2,2,2,2,2,2},
            {0,1,1,0,1,2,2,1,1,2,2,1,0,1,1,0},
            {0,0,0,0,0,1,1,0,1,2,2,1,1,2,2,1},
            {0,0,2,2,1,1,0,2,1,1,0,2,0,0,2,2},
            {0,1,1,0,0,1,1,0,2,0,0,2,2,2,2,2},
            {0,0,1,1,0,1,2,2,0,1,2,2,0,0,1,1},
            {0,0,0,0,2,0,0,0,2,2,1,1,2,2,2,1},
            {0,0,0,0,0,0,0,2,1,1,2,2,1,2,2,2},
            {0,2,2,2,0,0,2,2,0,0,1,2,0,0,1,1},
            {0,0,1,1,0,0,1,2,0,0,2,2,0,2,2,2},
            {0,1,2,0,0,1,2,0,0,1,2,0,0,1,2,0},
            {0,0,0,0,1,1,1,1,2,2,2,2,0,0,0,0},
            {0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0},
            {0,1,2,0,2,0,1,2,1,2,0,1,0,1,2,0},
            {0,0,1,1,2,2,0,0,1,1,2,2,0,0,1,1},
            {0,0,1,1,1,1,2,2,2,2,0,0,0,0,1,1},
            {0,1,0,1,0,1,0,1,2,2,2,2,2,2,2,2},
            {0,0,0,0,0,0,0,0,2,1,2,1,2,1,2,1},
            {0,0,2,2,1,1,2,2,0,0,2,2,1,1,2,2},
            {0,0,2,2,0,0,1,1,0,0,2,2,0,0,1,1},
            {0,2,2,0,1,2,2,1,0,2,2,0,1,2,2,1},
            {0,1,0,1,2,2,2,2,2,2,2,2,0,1,0,1},
            {0,0,0,0,2,1,2,1,2,1,2,1,2,1,2,1},
            {0,1,0,1,0,1,0,1,0,1,0,1,2,2,2,2},
            {0,2,2,2,0,1,1,1,0,2,2,2,0,1,1,1},
            {0,0,0,2,1,1,1,2,0,0,0,2,1,1,1,2},
            {0,0,0,0,2,1,1,2,2,1,1,2,2,1,1,2},
            {0,2,2,2,0,1,1,1,0,1,1,1,0,2,2,2},
            {0,0,0,2,1,1,1,2,1,1,1,2,0,0,0,2},
            {0,1,1,0,0,1,1,0,0,1,1,0,2,2,2,2},
            {0,0,0,0,0,0,0,0,2,1,1,2,2,1,1,2},
            {0,1,1,0,0,1,1,0,2,2,2,2,2,2,2,2},
            {0,0,2,2,0,0,1,1,0,0,1,1,0,0,2,2},
            {0,0,2,2,1,1,2,2,1,1,2,2,0,0,2,2},
            {0,0,0,0,0,0,0,0,0,0,0,0,2,1,1,2},
            {0,0,0,2,0,0,0,1,0,0,0,2,0,0,0,1},
            {0,2,2,2,1,2,2,2,0,2,2,2,1,2,2,2},
            {0,1,0,1,2,2,2,2,2,2,2,2,2,2,2,2},
            {0,1,1,1,2,0,1,1,2,2,0,1,2,2,2,0},
        };

        static constexpr std::uint8_t kBc7Anchor2[64] = {
            15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
            15,2,8,2,2,8,8,15,2,8,2,2,8,8,2,2,
            15,15,6,8,2,8,15,15,2,8,2,2,2,15,15,6,
            6,2,6,8,15,15,2,2,15,15,15,15,15,2,2,15,
        };

        static constexpr std::uint8_t kBc7Anchor3Second[64] = {
            3,3,15,15,8,3,15,15,8,8,6,6,6,5,3,3,
            3,3,8,15,3,3,6,10,5,8,8,6,8,5,15,15,
            8,15,3,5,6,10,8,15,15,3,15,5,15,15,15,15,
            3,15,5,5,5,8,5,10,5,10,8,13,15,12,3,3,
        };

        static constexpr std::uint8_t kBc7Anchor3Third[64] = {
            15,8,8,3,15,15,3,8,15,15,15,15,15,15,15,8,
            15,8,15,3,15,8,15,8,3,15,6,10,15,15,10,8,
            15,3,15,10,10,8,9,10,6,15,8,15,3,6,6,8,
            15,3,15,15,15,15,15,15,15,15,15,15,3,15,15,8,
        };
        // clang-format on

        static constexpr int kWeights2[4] = {0, 21, 43, 64};
        static constexpr int kWeights3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
        static constexpr int kWeights4[16] = {
            0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

        struct Bc7ModeParams final
        {
            int numSubsets;
            int partitionBits;
            int rotationBits;
            int indexSelectionBits;
            int colorBits;
            int alphaBits;
            int endpointPBits;   // 1 if unique per-endpoint P-bits are present
            int sharedPBits;     // 1 if shared per-subset P-bits are present (mode 1 only)
            int indexBits;
            int indexBits2;
        };

        static constexpr Bc7ModeParams kBc7Modes[8] = {
            {3, 4, 0, 0, 4, 0, 1, 0, 3, 0},
            {2, 6, 0, 0, 6, 0, 0, 1, 3, 0},
            {3, 6, 0, 0, 5, 0, 0, 0, 2, 0},
            {2, 6, 0, 0, 7, 0, 1, 0, 2, 0},
            {1, 0, 2, 1, 5, 6, 0, 0, 2, 3},
            {1, 0, 2, 0, 7, 8, 0, 0, 2, 2},
            {1, 0, 0, 0, 7, 7, 1, 0, 4, 0},
            {2, 6, 0, 0, 5, 5, 1, 0, 2, 0},
        };

        [[nodiscard]] int ExtractMode(const std::uint8_t* block) noexcept
        {
            const std::uint8_t low = block[0];
            for (int bit = 0; bit < 8; ++bit)
            {
                if ((low & (1u << bit)) != 0u) return bit;
            }
            return -1; // reserved (low byte entirely zero)
        }

        // LSB-first bit reader over the 16-byte (128-bit) little-endian block.
        [[nodiscard]] std::uint32_t ReadBits(const std::uint8_t* block, int& bitPos,
                                             int count) noexcept
        {
            std::uint32_t result = 0;
            for (int i = 0; i < count; ++i)
            {
                const int bit = bitPos + i;
                const std::uint8_t byteValue = block[bit >> 3];
                const std::uint32_t bitValue = (byteValue >> (bit & 7)) & 1u;
                result |= (bitValue << i);
            }
            bitPos += count;
            return result;
        }

        [[nodiscard]] int AnchorIndexForSubset(int subsetInPartition, int numSubsets,
                                               int partition) noexcept
        {
            if (subsetInPartition == 0) return 0;
            if (numSubsets == 2) return kBc7Anchor2[partition];
            if (subsetInPartition == 1) return kBc7Anchor3Second[partition];
            return kBc7Anchor3Third[partition];
        }

        [[nodiscard]] std::uint8_t Interpolate(std::uint8_t e0, std::uint8_t e1, int index,
                                               int indexBits) noexcept
        {
            const int weight = indexBits == 2 ? kWeights2[index]
                : indexBits == 3 ? kWeights3[index]
                : kWeights4[index];
            return static_cast<std::uint8_t>(
                (((64 - weight) * static_cast<int>(e0)) + (weight * static_cast<int>(e1)) + 32)
                >> 6);
        }
    }

    void Bc7Util::DecompressBc7Block(const std::uint8_t* block, int blockX, int blockY,
                                     int width, int height, std::uint8_t* imageData)
    {
        const int mode = ExtractMode(block);
        if (mode < 0)
        {
            // Reserved encoding (low byte all zero). The specification requires hardware to
            // return zero for all channels; alpha may be either 0 or 1.0. Zero is the exact,
            // deterministic choice made here.
            for (int by = 0; by < 4; ++by)
            {
                for (int bx = 0; bx < 4; ++bx)
                {
                    const int px = (blockX << 2) + bx;
                    const int py = (blockY << 2) + by;
                    if (px < width && py < height)
                    {
                        const int offset = ((py * width) + px) << 2;
                        imageData[offset] = imageData[offset + 1] = imageData[offset + 2]
                            = imageData[offset + 3] = 0;
                    }
                }
            }
            return;
        }

        const Bc7ModeParams& params = kBc7Modes[mode];
        int bitPos = mode + 1;

        const int partition = params.partitionBits > 0
            ? static_cast<int>(ReadBits(block, bitPos, params.partitionBits)) : 0;
        const int rotation = params.rotationBits > 0
            ? static_cast<int>(ReadBits(block, bitPos, params.rotationBits)) : 0;
        const int indexSelector = params.indexSelectionBits > 0
            ? static_cast<int>(ReadBits(block, bitPos, params.indexSelectionBits)) : 0;

        const int numEndpoints = params.numSubsets * 2;
        // [endpoint][channel] with channel 0=R,1=G,2=B,3=A. Values start as raw low-order bits
        // and are progressively refined into full 8-bit components below.
        std::array<std::array<int, 4>, 6> endpoint{};
        for (int channel = 0; channel < 3; ++channel)
        {
            for (int e = 0; e < numEndpoints; ++e)
                endpoint[static_cast<std::size_t>(e)][static_cast<std::size_t>(channel)] =
                    static_cast<int>(ReadBits(block, bitPos, params.colorBits));
        }
        if (params.alphaBits > 0)
        {
            for (int e = 0; e < numEndpoints; ++e)
                endpoint[static_cast<std::size_t>(e)][3] =
                    static_cast<int>(ReadBits(block, bitPos, params.alphaBits));
        }
        else
        {
            for (int e = 0; e < numEndpoints; ++e) endpoint[static_cast<std::size_t>(e)][3] = 0;
        }

        int componentPrecision = params.colorBits;
        int alphaPrecision = params.alphaBits > 0 ? params.alphaBits : 8;
        if (params.endpointPBits != 0)
        {
            std::array<int, 6> pbit{};
            for (int e = 0; e < numEndpoints; ++e)
                pbit[static_cast<std::size_t>(e)] = static_cast<int>(ReadBits(block, bitPos, 1));
            for (int e = 0; e < numEndpoints; ++e)
            {
                for (int channel = 0; channel < 3; ++channel)
                {
                    auto& value = endpoint[static_cast<std::size_t>(e)][static_cast<std::size_t>(channel)];
                    value = (value << 1) | pbit[static_cast<std::size_t>(e)];
                }
                if (params.alphaBits > 0)
                {
                    auto& value = endpoint[static_cast<std::size_t>(e)][3];
                    value = (value << 1) | pbit[static_cast<std::size_t>(e)];
                }
            }
            componentPrecision = params.colorBits + 1;
            if (params.alphaBits > 0) alphaPrecision = params.alphaBits + 1;
        }
        else if (params.sharedPBits != 0)
        {
            const int pbitZero = static_cast<int>(ReadBits(block, bitPos, 1));
            const int pbitOne = static_cast<int>(ReadBits(block, bitPos, 1));
            for (int e = 0; e < numEndpoints; ++e)
            {
                const int subset = e / 2;
                const int pbit = subset == 0 ? pbitZero : pbitOne;
                for (int channel = 0; channel < 3; ++channel)
                {
                    auto& value = endpoint[static_cast<std::size_t>(e)][static_cast<std::size_t>(channel)];
                    value = (value << 1) | pbit;
                }
            }
            componentPrecision = params.colorBits + 1;
        }

        // Left-shift so the MSB lands in bit 7, then replicate the MSB into the revealed LSBs.
        for (int e = 0; e < numEndpoints; ++e)
        {
            for (int channel = 0; channel < 3; ++channel)
            {
                auto& value = endpoint[static_cast<std::size_t>(e)][static_cast<std::size_t>(channel)];
                value <<= (8 - componentPrecision);
                value |= (value >> componentPrecision);
            }
            if (params.alphaBits > 0)
            {
                auto& value = endpoint[static_cast<std::size_t>(e)][3];
                value <<= (8 - alphaPrecision);
                value |= (value >> alphaPrecision);
            }
            else
            {
                endpoint[static_cast<std::size_t>(e)][3] = 255;
            }
        }

        std::array<int, 16> primaryIndex{};
        std::array<int, 16> secondaryIndex{};
        const std::uint8_t* partitionTable = params.numSubsets == 3
            ? kBc7Partition3[partition] : params.numSubsets == 2 ? kBc7Partition2[partition]
                                                                  : nullptr;
        const auto subsetForTexel = [&](int texel) -> int {
            return params.numSubsets == 1 ? 0 : partitionTable[texel];
        };
        for (int texel = 0; texel < 16; ++texel)
        {
            const int subset = subsetForTexel(texel);
            const bool isAnchor = texel == AnchorIndexForSubset(subset, params.numSubsets, partition);
            const int bits = params.indexBits - (isAnchor ? 1 : 0);
            primaryIndex[static_cast<std::size_t>(texel)] =
                static_cast<int>(ReadBits(block, bitPos, bits));
        }
        if (params.indexBits2 > 0)
        {
            for (int texel = 0; texel < 16; ++texel)
            {
                const int subset = subsetForTexel(texel);
                const bool isAnchor =
                    texel == AnchorIndexForSubset(subset, params.numSubsets, partition);
                const int bits = params.indexBits2 - (isAnchor ? 1 : 0);
                secondaryIndex[static_cast<std::size_t>(texel)] =
                    static_cast<int>(ReadBits(block, bitPos, bits));
            }
        }

        const bool hasSecondary = params.indexBits2 > 0;
        const bool colorUsesSecondary = params.indexSelectionBits > 0 && indexSelector == 1;
        const bool alphaUsesSecondary = hasSecondary
            && (params.indexSelectionBits == 0 || indexSelector == 0);
        const int colorIndexBits = colorUsesSecondary ? params.indexBits2 : params.indexBits;
        const int alphaIndexBits = alphaUsesSecondary ? params.indexBits2 : params.indexBits;

        for (int texel = 0; texel < 16; ++texel)
        {
            const int subset = subsetForTexel(texel);
            const std::array<int, 4>& e0 = endpoint[static_cast<std::size_t>(subset * 2)];
            const std::array<int, 4>& e1 = endpoint[static_cast<std::size_t>(subset * 2 + 1)];
            const int colorIndex = colorUsesSecondary
                ? secondaryIndex[static_cast<std::size_t>(texel)]
                : primaryIndex[static_cast<std::size_t>(texel)];
            const int alphaIndex = alphaUsesSecondary
                ? secondaryIndex[static_cast<std::size_t>(texel)]
                : primaryIndex[static_cast<std::size_t>(texel)];

            std::uint8_t rgba[4];
            for (int channel = 0; channel < 3; ++channel)
            {
                rgba[channel] = Interpolate(
                    static_cast<std::uint8_t>(e0[static_cast<std::size_t>(channel)]),
                    static_cast<std::uint8_t>(e1[static_cast<std::size_t>(channel)]), colorIndex,
                    colorIndexBits);
            }
            rgba[3] = Interpolate(static_cast<std::uint8_t>(e0[3]),
                                  static_cast<std::uint8_t>(e1[3]), alphaIndex, alphaIndexBits);

            if (rotation == 1) std::swap(rgba[3], rgba[0]);
            else if (rotation == 2) std::swap(rgba[3], rgba[1]);
            else if (rotation == 3) std::swap(rgba[3], rgba[2]);

            const int bx = texel & 3;
            const int by = texel >> 2;
            const int px = (blockX << 2) + bx;
            const int py = (blockY << 2) + by;
            if (px < width && py < height)
            {
                const int offset = ((py * width) + px) << 2;
                imageData[offset] = rgba[0];
                imageData[offset + 1] = rgba[1];
                imageData[offset + 2] = rgba[2];
                imageData[offset + 3] = rgba[3];
            }
        }
    }

    std::vector<std::uint8_t> Bc7Util::DecompressBc7(const std::uint8_t* data,
                                                     std::size_t dataSize, int width, int height)
    {
        const int blockCountX = (width + 3) / 4;
        const int blockCountY = (height + 3) / 4;
        // BC7 is 16 bytes/block, one block per 4x4 texels -- see DxtUtil::DecompressDxt1's own
        // note on why this upfront check exists (reject a too-small buffer before any block read).
        const std::size_t requiredBytes = static_cast<std::size_t>(blockCountX) * blockCountY * 16;
        if (dataSize < requiredBytes)
        {
            throw std::out_of_range(
                "Bc7Util::DecompressBc7: dataSize (" + std::to_string(dataSize) +
                ") is smaller than the " + std::to_string(requiredBytes) + " bytes " +
                std::to_string(width) + "x" + std::to_string(height) + " requires.");
        }

        std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * height * 4, 0);
        for (int by = 0; by < blockCountY; ++by)
        {
            for (int bx = 0; bx < blockCountX; ++bx)
            {
                const std::uint8_t* block = data
                    + (static_cast<std::size_t>(by) * blockCountX + bx) * 16;
                DecompressBc7Block(block, bx, by, width, height, out.data());
            }
        }
        return out;
    }
}
