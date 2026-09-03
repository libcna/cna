// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace CNA::Internal::Xnb
{
    /**
     * @brief Compresses a payload into one raw LZ4 block (plans/plan_xnapipeline.md `XNAP-80`).
     *
     * This is the exact counterpart of @ref DecompressXnbLz4Payload: a **single raw block**, not
     * an LZ4 frame. There is no frame magic, no checksum, no dictionary and no block
     * concatenation, because none of those is part of the representation the extended XNB
     * ecosystem uses — the container's own four-byte decompressed-size field takes the place of a
     * frame header.
     *
     * The output is a conforming LZ4 block: any LZ4 block decompressor produces @p payload from
     * it. The encoder is a single-pass greedy matcher over a 64 KiB window with a fixed hash
     * table, so its output depends on nothing but its input and the same payload always
     * compresses to the same bytes.
     *
     * **This is not an XNA 4.0 format.** Microsoft's XNA 4.0 used LZX (`0x80`); the LZ4 flag
     * (`0x40`) was introduced by a later implementation, and `ValidateXnbFileOptions()` refuses
     * the combination of LZ4 with an XNA 4.0 target platform for that reason.
     *
     * @param payload The bytes to compress; may be empty.
     * @return One raw LZ4 block.
     */
    [[nodiscard]] std::vector<std::uint8_t> CompressXnbLz4Block(
        std::span<const std::uint8_t> payload);
}
