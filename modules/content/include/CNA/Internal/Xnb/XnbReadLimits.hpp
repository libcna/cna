// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>

namespace CNA::Internal::Xnb
{
    /**
     * @brief Sanity bounds consulted by every count-driven `.xnb` read (plans/plan_xnb.md XNB-10A).
     *
     * A validly bounds-checked binary reader can still be told, by a single corrupted or
     * adversarial count field, to allocate an enormous buffer/vector before any further
     * validation has a chance to reject the file (e.g. a `numberOfReaders` of `0x7FFFFFFF`).
     * These limits exist to fail fast with a clear error instead of attempting that allocation.
     * They are generous relative to any real `.xnb` file, not tuned per-fixture.
     */
    struct XnbReadLimits
    {
        /** @brief Largest `.xnb` file this reader will attempt to open, in bytes. */
        int32_t maxFileSize = 64 * 1024 * 1024;

        /** @brief Largest decompressed XNB payload this reader will allocate, in bytes. */
        int32_t maxDecompressedSize = 256 * 1024 * 1024;

        /** @brief Largest single string this reader will allocate, in bytes. */
        int32_t maxStringBytes = 1 * 1024 * 1024;

        /** @brief Largest type-reader table this reader will accept. */
        int32_t maxTypeReaderCount = 4096;

        /** @brief Largest shared-resource table this reader will accept. */
        int32_t maxSharedResourceCount = 1'000'000;

        /** @brief Largest single array/list/dictionary element count this reader will allocate for (Phase C). */
        int32_t maxCollectionElementCount = 10'000'000;

        /** @brief Deepest nested-object graph this reader will follow before rejecting the file (Phase C+). */
        int32_t maxObjectNestingDepth = 256;
    };

    /** @brief The process-wide default XnbReadLimits, used unless a caller supplies its own. */
    inline const XnbReadLimits& DefaultXnbReadLimits()
    {
        static const XnbReadLimits limits;
        return limits;
    }
}
