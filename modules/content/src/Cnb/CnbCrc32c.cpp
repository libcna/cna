// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbCrc32c.hpp"

#include <array>

namespace CNA::Content::Cnb
{
    namespace
    {
        // Reflected CRC-32C polynomial. Built once at compile time so the table is a constant in
        // .rodata rather than something a static initializer has to race to fill in.
        constexpr std::uint32_t kReflectedPolynomial = 0x82F63B78u;

        constexpr std::array<std::uint32_t, 256> BuildTable()
        {
            std::array<std::uint32_t, 256> table{};
            for (std::uint32_t i = 0; i < 256u; ++i)
            {
                std::uint32_t crc = i;
                for (int bit = 0; bit < 8; ++bit)
                {
                    crc = (crc & 1u) != 0u ? ((crc >> 1) ^ kReflectedPolynomial) : (crc >> 1);
                }
                table[i] = crc;
            }
            return table;
        }

        constexpr std::array<std::uint32_t, 256> kTable = BuildTable();
    }

    std::uint32_t Crc32cContinue(std::uint32_t previous, std::span<const std::uint8_t> data) noexcept
    {
        std::uint32_t crc = ~previous;
        for (const std::uint8_t byte : data)
        {
            crc = kTable[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
        }
        return ~crc;
    }

    std::uint32_t Crc32c(std::span<const std::uint8_t> data) noexcept
    {
        return Crc32cContinue(Crc32cSeed(), data);
    }
}
