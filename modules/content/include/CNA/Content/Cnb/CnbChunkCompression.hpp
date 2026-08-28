// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbFormat.hpp"

namespace CNA::Content::Cnb
{
    /**
     * @brief Chunk compression for `.cnb` (plans/plan_cnb.md `CNBF-105`).
     *
     * Compression is **opt-in and off by default**, and that is a measured decision rather than
     * caution: `docs/cnb-compression-measurements.md` records roughly half off a texture payload,
     * three quarters off audio and six sevenths off vertex data — but also that decompression only
     * *saves load time* on storage slower than 456–1469 MB/s, so on desktop NVMe it makes loading
     * slower. Size always wins; time only sometimes does. The codec is therefore chosen per chunk,
     * which is what the container's per-chunk `compression` field was always for.
     *
     * A compressed chunk cannot be read by a CNA built without the codec, or by any CNA from
     * before it existed. Enabling compression on a file raises that file's minimum runtime.
     */

    /**
     * @brief Whether this build can compress and decompress @p codec.
     *
     * `CnbCompression::None` is always supported. Zstandard depends on `CNA_CNB_ZSTD`, which is
     * `AUTO` by default and quietly off when the system library is missing.
     *
     * @param codec The codec to query.
     * @return True when this build implements the codec in both directions.
     */
    [[nodiscard]] bool IsCnbCompressionSupported(CnbCompression codec);

    /**
     * @brief Renders a codec identifier for diagnostics.
     *
     * @param codec The codec to render.
     * @return The codec's name, or `"unknown codec N"`.
     */
    [[nodiscard]] std::string CnbCompressionToString(CnbCompression codec);

    /**
     * @brief Compresses @p raw with @p codec.
     *
     * @param raw   The bytes to compress.
     * @param codec The codec to use. `CnbCompression::None` returns a copy of @p raw.
     * @param level Codec-specific effort. For Zstandard, 1-19; 3 is the measured sweet spot.
     * @return The compressed bytes.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if this build does not
     *         implement @p codec, or the codec itself fails.
     */
    [[nodiscard]] std::vector<std::uint8_t> CompressCnbChunk(std::span<const std::uint8_t> raw,
                                                              CnbCompression codec, int level = 3);

    /**
     * @brief Decompresses @p stored, which must expand to exactly @p uncompressedSize bytes.
     *
     * The exact-size requirement is the whole safety story. `uncompressedSize` comes from the
     * file's table of contents, so it is attacker-controlled: it is checked against
     * @p maxUncompressedSize **before** anything is allocated, which is what stops a few kilobytes
     * of hostile input from asking for gigabytes. The codec is then required to produce exactly
     * that many bytes, so a stream that expands to a different size is a corrupt file rather than
     * a short read someone later treats as data.
     *
     * @param stored             The chunk's stored bytes, exactly as they appear in the file.
     * @param codec              The codec named by the chunk's table-of-contents entry.
     * @param uncompressedSize   The size the chunk must expand to.
     * @param maxUncompressedSize The configured ceiling (`CnbReadLimits::maxChunkSize`).
     * @param where              Text placed at the front of any diagnostic, naming the file and chunk.
     * @return Exactly @p uncompressedSize bytes.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if this build does not
     *         implement @p codec, @p uncompressedSize exceeds the ceiling, or the stream does not
     *         expand to exactly @p uncompressedSize.
     */
    [[nodiscard]] std::vector<std::uint8_t> DecompressCnbChunk(std::span<const std::uint8_t> stored,
                                                               CnbCompression codec,
                                                               std::uint64_t uncompressedSize,
                                                               std::uint64_t maxUncompressedSize,
                                                               const std::string& where);
}
