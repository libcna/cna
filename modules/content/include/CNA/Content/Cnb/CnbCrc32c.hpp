// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace CNA::Content::Cnb
{
    /**
     * @brief Computes the CRC-32C (Castagnoli) checksum used by every `.cnb` header, table of
     *        contents and chunk (plans/plan_cnb.md `CNBF-002`).
     *
     * Reflected polynomial `0x82F63B78` (normal form `0x1EDC6F41`), initial register
     * `0xFFFFFFFF`, reflected input and output, final XOR `0xFFFFFFFF` -- the same parameter set
     * as iSCSI/ext4/SSE4.2's `CRC32C`. The implementation is a portable software table with no
     * intrinsics, so the value is bit-identical on every platform CNA targets, which is what a
     * stored-in-the-file checksum requires.
     *
     * This detects **accidental** corruption -- a truncated download, a half-written build
     * artifact, a bad offset. It is not a message authentication code and must never be presented
     * as one: anyone who can rewrite a chunk can trivially rewrite its checksum.
     *
     * @param data Bytes to checksum.
     * @return The CRC-32C of @p data.
     */
    [[nodiscard]] std::uint32_t Crc32c(std::span<const std::uint8_t> data) noexcept;

    /**
     * @brief Continues a running CRC-32C over a further span of bytes.
     *
     * Lets a caller checksum a logically contiguous region that is physically split across two
     * buffers without concatenating them first.
     *
     * @param previous The value returned by a previous Crc32c()/Crc32cContinue() call, or
     *                 Crc32cSeed() to start a fresh running computation.
     * @param data     The next bytes of the same logical region.
     * @return The running CRC-32C after appending @p data.
     */
    [[nodiscard]] std::uint32_t Crc32cContinue(std::uint32_t previous,
                                               std::span<const std::uint8_t> data) noexcept;

    /**
     * @brief The starting value for a running CRC-32C built up with Crc32cContinue().
     *
     * @return The seed value (the CRC-32C of an empty byte range).
     */
    [[nodiscard]] constexpr std::uint32_t Crc32cSeed() noexcept { return 0u; }
}
