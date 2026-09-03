// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>

namespace CNA::Internal::Xnb
{
    /**
     * @brief Sanity bounds consulted by every count-driven `.xnb` write
     *        (plans/plan_xnapipeline.md `XNAP-11`).
     *
     * The writer's inputs are build-time content, which is no more trustworthy than a runtime
     * asset: a source file, an imported document, or a processor parameter can all carry a count
     * or length that would otherwise be emitted verbatim into a file no reader can load. These
     * bounds are the writing counterpart of @ref XnbReadLimits and are deliberately chosen so a
     * file CNA writes always satisfies the default reading bounds as well.
     */
    struct XnbWriteLimits
    {
        /** @brief Largest complete `.xnb` file this writer will produce, in bytes. */
        std::int32_t maxFileSize = 64 * 1024 * 1024;

        /** @brief Largest uncompressed payload this writer will assemble, in bytes. */
        std::int32_t maxPayloadSize = 256 * 1024 * 1024;

        /** @brief Largest single string this writer will emit, in UTF-8 bytes. */
        std::int32_t maxStringBytes = 1 * 1024 * 1024;

        /** @brief Largest type-reader table this writer will emit. */
        std::int32_t maxTypeWriterCount = 4096;

        /** @brief Largest shared-resource table this writer will emit. */
        std::int32_t maxSharedResourceCount = 1'000'000;

        /** @brief Largest single array/list/dictionary element count this writer will emit. */
        std::int32_t maxCollectionElementCount = 10'000'000;

        /** @brief Deepest nested-object graph this writer will follow before failing. */
        std::int32_t maxObjectNestingDepth = 256;
    };

    /** @brief The process-wide default XnbWriteLimits, used unless a caller supplies its own. */
    inline const XnbWriteLimits& DefaultXnbWriteLimits()
    {
        static const XnbWriteLimits limits;
        return limits;
    }
}
