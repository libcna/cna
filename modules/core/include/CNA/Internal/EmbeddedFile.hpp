// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstddef>
#include <span>

namespace CNA::Internal
{
    /** @brief A data file compiled into a library by cmake/EmbedBinaryFiles.cmake. */
    struct EmbeddedFile
    {
        /** @brief File name without directories. */
        const char* name;
        /** @brief File contents. */
        const unsigned char* data;
        /** @brief Length of data in bytes. */
        std::size_t size;
        /** @brief The original length when data is a raw deflate stream (cna_embed_binary_files
         * COMPRESS), else 0 (data is the file itself). */
        std::size_t inflatedSize = 0;

        /** @brief Views the contents. @return Bytes. */
        [[nodiscard]] std::span<const unsigned char> bytes() const { return {data, size}; }
    };
}
