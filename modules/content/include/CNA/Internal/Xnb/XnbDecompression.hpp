// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Internal/Xnb/XnbReadLimits.hpp"

namespace CNA::Internal::Xnb
{
    /**
     * @brief Decompresses a `.xnb` file's LZX-compressed payload (plans/plan_xnb.md XNB-29), matching
     *        FNA's own block-framing loop in `ContentManager.GetContentReaderFromXnb`
     *        byte-for-byte.
     *
     * The compressed payload is split into blocks, each normally decompressing to 32KB
     * (`0x8000`) unless a block explicitly overrides its own frame size (signaled by a leading
     * `0xFF` byte, followed by an explicit big-endian frame size and block size). Each block is
     * fed to a single `LzxDecoder` instance in turn -- the decoder's sliding window and
     * repeated-offset state persist across blocks within one file, matching FNA/LZX's own
     * stateful, sequential-only decompression model (there is no way to "jump to an offset" in
     * an LZX-compressed stream; see plans/plan_xnb.md XNB-61b's own note on this).
     *
     * @param compressedData Pointer to the compressed payload bytes (immediately following the
     *                       10-byte container header and the 4-byte decompressed-size field).
     * @param compressedSize Number of compressed bytes available at @p compressedData.
     * @param decompressedSize Expected decompressed size, as recorded in the `.xnb` header.
     * @param path           File path, used only to build exception messages.
     * @param limits         Bounds applied to @p compressedSize/@p decompressedSize (plans/plan_xnb.md XNB-10A).
     * @return The decompressed bytes, exactly @p decompressedSize long.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if decompression fails,
     *         produces the wrong number of bytes, or either size exceeds @p limits.
     */
    std::vector<uint8_t> DecompressXnbPayload(
        const uint8_t* compressedData,
        int32_t compressedSize,
        int32_t decompressedSize,
        const std::string& path,
        const XnbReadLimits& limits = DefaultXnbReadLimits());

    /**
     * @brief Decompresses MonoGame's raw-LZ4 `.xnb` payload exactly to its declared size.
     *
     * MonoGame writes a single raw LZ4 block after the 10-byte XNB header and 4-byte
     * little-endian decompressed-size field. This is deliberately not an LZ4 frame decoder:
     * frame magic, checksums, dictionaries, and concatenated blocks are not part of the XNB
     * representation emitted by MonoGame's ContentWriter.
     *
     * @param compressedData Pointer to the raw LZ4 block bytes.
     * @param compressedSize Number of bytes available at @p compressedData.
     * @param decompressedSize Exact output size recorded in the XNB container.
     * @param path File path used only in diagnostics.
     * @param limits Bounds applied before allocation and throughout decoding.
     * @return The decompressed XNB body, exactly @p decompressedSize bytes long.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if a length, offset,
     *         input extent, output extent, or final size is invalid.
     */
    [[nodiscard]] std::vector<uint8_t> DecompressXnbLz4Payload(
        const uint8_t* compressedData,
        int32_t compressedSize,
        int32_t decompressedSize,
        const std::string& path,
        const XnbReadLimits& limits = DefaultXnbReadLimits());
}
