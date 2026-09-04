// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace CNA::Internal::Xnb
{
    /**
     * @brief Tuning for CNA's LZX encoder (plans/plan_xnapipeline.md `XNAP-81`).
     *
     * Every field has a default that matches what an `.xnb` container needs; there is no reason
     * for a content build to change one, and they exist so that the encoder can be exercised at
     * its own boundaries by a test rather than only through a 32 KiB frame.
     */
    struct LzxEncodeOptions
    {
        /**
         * @brief Sliding-window size exponent. The `.xnb` container always uses 16 (64 KiB).
         *
         * The decoder accepts 15 through 21; the reading side of an `.xnb` hard-codes 16, so
         * anything else would produce a file CNA could not read back and is refused.
         */
        int windowBits = 16;

        /**
         * @brief Uncompressed bytes per output frame.
         *
         * The `.xnb` block-framing loop defaults a frame to `0x8000` and reads an explicit frame
         * size only when a block is prefixed with `0xFF`. A value above `0x8000` cannot be
         * expressed by the default form and is refused.
         */
        std::uint32_t frameSize = 0x8000u;

        /**
         * @brief How many hash-chain candidates one match search may examine.
         *
         * Bounds the encoder's time, not its correctness: a smaller value produces a larger but
         * equally valid stream. Fixed rather than adaptive so the output is reproducible.
         */
        std::uint32_t matchSearchDepth = 24u;
    };

    /**
     * @brief Compresses @p payload into the exact LZX block stream an `.xnb` container carries.
     *
     * The result is what follows the 10-byte container header and the 4-byte decompressed-size
     * field: a sequence of framed LZX blocks, each prefixed with the container's own big-endian
     * block-size header (two bytes for a full-size frame, or the five-byte `0xFF` form that
     * carries an explicit frame size). @ref DecompressXnbPayload is its exact counterpart and is
     * what every round-trip test checks it against.
     *
     * **What the encoder emits.** One LZX *verbatim* block per output frame, with freshly
     * transmitted main and length Huffman trees delta-coded against the previous block's, real
     * repeated-offset (`R0`/`R1`/`R2`) matching, position-slot offset encoding, and greedy
     * longest-match search over a bounded hash chain. It does **not** emit aligned-offset blocks
     * or uncompressed blocks: both are legal LZX and neither is necessary, and a verbatim-only
     * stream is a genuinely compressed conforming subset rather than a compressed flag around
     * unchanged bytes. The Intel `E8` call-translation header bit is written as zero, which is
     * what every `.xnb` uses and the only value CNA's decoder accepts.
     *
     * The output depends on nothing but @p payload and @p options: the hash function, the chain
     * depth, the Huffman tie-breaking and the block partitioning are all fixed, so the same
     * source always compresses to the same bytes.
     *
     * @param payload The bytes to compress; may be empty, which produces no blocks at all.
     * @param options Encoder tuning; the defaults are what an `.xnb` needs.
     * @return The framed LZX block stream.
     * @throws XnbWriteException if @p options names a window or frame size the `.xnb` container
     *         cannot express, or if a frame's compressed form would exceed the container's
     *         16-bit block-size field.
     */
    [[nodiscard]] std::vector<std::uint8_t> CompressXnbLzxPayload(
        std::span<const std::uint8_t> payload, const LzxEncodeOptions& options = {});
}
