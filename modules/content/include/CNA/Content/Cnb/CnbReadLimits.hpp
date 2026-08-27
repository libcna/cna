// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>

namespace CNA::Content::Cnb
{
    /**
     * @brief Sanity bounds consulted by every count-driven `.cnb` read (plans/plan_cnb.md `CNBF-003`).
     *
     * A correctly bounds-checked binary reader can still be told, by one corrupted or adversarial
     * count field, to allocate an enormous buffer before any further validation gets a chance to
     * reject the file. These limits exist to fail fast with a clear message instead of attempting
     * that allocation. They are generous relative to any real asset, not tuned to a fixture.
     *
     * Modelled on `CNA::Internal::Xnb::XnbReadLimits`, which exists for the same reason on the
     * `.xnb` side.
     */
    struct CnbReadLimits
    {
        /** @brief Largest `.cnb` file this reader will open, in bytes. */
        std::uint64_t maxFileSize = 512ull * 1024ull * 1024ull;

        /** @brief Largest number of table-of-contents entries this reader will accept. */
        std::uint32_t maxChunkCount = 65536u;

        /** @brief Largest single chunk this reader will accept, in bytes. */
        std::uint64_t maxChunkSize = 384ull * 1024ull * 1024ull;

        /**
         * @brief Largest total of every chunk's **logical** (post-decompression) size, in bytes.
         *
         * The per-chunk ceilings alone do not bound a file's total expansion
         * (plans/plan_cnb.md `CNBF-114`). `maxChunkSize` caps one chunk and `maxChunkCount` caps how
         * many there are, so their product -- 24 PiB at the defaults -- is what a reader without
         * this limit would be willing to allocate for a file of a few kilobytes of individually
         * legal compressed frames. Every chunk counts toward the total, compressed or not, so the
         * invariant reads the same way whatever a file's codec is; for a wholly uncompressed file
         * the sum is bounded by @ref maxFileSize anyway, because chunks do not overlap.
         *
         * The default is deliberately **larger** than @ref maxFileSize, so compression can
         * genuinely expand a file rather than being cancelled out by this bound, and far smaller
         * than `maxChunkCount * maxChunkSize`, which is the unbounded case it closes.
         */
        std::uint64_t maxTotalUncompressedSize = 1024ull * 1024ull * 1024ull;

        /** @brief Largest single serialized string this reader will allocate, in bytes. */
        std::uint32_t maxStringBytes = 1024u * 1024u;

        /** @brief Largest element count this reader will reserve for any single serialized array. */
        std::uint32_t maxArrayElementCount = 16u * 1024u * 1024u;

        /** @brief Largest chunk alignment a table-of-contents entry may declare, in bytes. */
        std::uint32_t maxChunkAlignment = 4096u;
    };

    /**
     * @brief The process-wide default limits, used unless a caller supplies its own.
     *
     * @return A reference to the shared default CnbReadLimits instance.
     */
    [[nodiscard]] inline const CnbReadLimits& DefaultCnbReadLimits()
    {
        static const CnbReadLimits limits;
        return limits;
    }
}
