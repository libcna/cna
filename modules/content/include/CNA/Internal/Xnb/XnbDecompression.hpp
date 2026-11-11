// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <exception>
#include <string>
#include <vector>

#include "CNA/Internal/Xnb/XnbReadLimits.hpp"

namespace CNA::Internal::Xnb
{
    /**
     * @brief What @ref DecodeXnbLzxPayload produced: the decoded prefix and the failure, if any,
     *        that a read past that prefix raises.
     */
    struct XnbLzxDecodeResult
    {
        /** @brief Every byte served before decoding stopped. */
        std::vector<uint8_t> bytes;

        /**
         * @brief The failure a read past @ref bytes raises; null when the served bytes spend
         *        the container's declared size, so that such a read simply finds the end.
         */
        std::exception_ptr failure;
    };

    /**
     * @brief Decodes a `.xnb` LZX payload the way Refresh's `DecompressStream` serves it to the
     *        content reader, one native call's buffer at a time.
     *
     * Refresh decompresses lazily: each `DecompressNextBuffer` (made when the reader has used the
     * previous buffer) calls XnaNative.dll's `Decompress` with the unconsumed part of the current
     * 64 KiB compressed chunk and a 64 KiB output buffer, and serves what that call produced.
     * Measured on XnaNative.dll 4.0.30901.0 (the Refresh redistributable), one call decodes every
     * frame whose bytes it has while it holds at most 64 KiB of undelivered output, takes the
     * bytes of a frame it cannot finish yet, decodes nothing after a frame whose size is not
     * 32 KiB, delivers at most 64 KiB and keeps the rest for the next call -- and fails as a whole
     * when any frame it decodes fails, losing the good frames decoded with it (IL_0089-0095).
     * So: nothing is decoded once `decompressedTodo` is spent (IL_0000-000a), and a declared size
     * of zero or less is an empty body; the buffer that crosses the declared size is served
     * whole; a call that takes and makes nothing (IL_009c-00ad) and a short compressed read
     * (`ReadBufferFromDisk` IL_0023-0030) throw `InvalidOperationException`. CNA decodes eagerly,
     * so it keeps the served prefix and records the failure for the read that would have reached
     * it. One bound of CNA's own also stops decoding, with
     * Microsoft::Xna::Framework::Content::ContentLoadException rather than an XNA type because
     * Refresh has no such bound: more output than `limits.maxDecompressedSize` (Refresh streams
     * through fixed 64 KiB buffers and never holds the whole body, CNA does).
     *
     * The compressed bytes are fetched as Refresh's `ReadBufferFromDisk` (IL_0000-0042) fetches
     * them, in 64 KiB chunks up to the declared size: when fewer than @p compressedSize bytes are
     * present -- possible only for a stream that cannot seek, whose declared size Refresh's
     * `PrepareStream` does not check (IL_009f-00a5) -- fetching the chunk the input ends inside
     * fails, and what the decoder held undelivered is lost with it.
     *
     * @param compressedData Pointer to the compressed payload bytes (immediately following the
     *                       10-byte container header and the 4-byte decompressed-size field).
     * @param compressedSize Number of compressed bytes the container declares; a negative value
     *                       is no input (`ReadNextBufferFromDisk` IL_0000-0009).
     * @param availableSize  Number of bytes actually present at @p compressedData; nothing past
     *                       them is read.
     * @param declaredSize   The container's decompressed-size field.
     * @param path           File path, used only to build exception messages.
     * @param limits         Bounds applied to the decoded output.
     * @return The decoded prefix and the failure a read past it raises.
     */
    [[nodiscard]] XnbLzxDecodeResult DecodeXnbLzxPayload(
        const uint8_t* compressedData,
        int32_t compressedSize,
        int32_t availableSize,
        int32_t declaredSize,
        const std::string& path,
        const XnbReadLimits& limits = DefaultXnbReadLimits());

    /**
     * @brief Decompresses a `.xnb` file's LZX-compressed payload (XNB-29), matching
     *        FNA's own block-framing loop in `ContentManager.GetContentReaderFromXnb`
     *        byte-for-byte.
     *
     * The compressed payload is split into blocks, each normally decompressing to 32KB
     * (`0x8000`) unless a block explicitly overrides its own frame size (signaled by a leading
     * `0xFF` byte, followed by an explicit big-endian frame size and block size). Each block is
     * fed to a single `LzxDecoder` instance in turn -- the decoder's sliding window and
     * repeated-offset state persist across blocks within one file, matching FNA/LZX's own
     * stateful, sequential-only decompression model (there is no way to "jump to an offset" in
     * an LZX-compressed stream).
     *
     * @param compressedData Pointer to the compressed payload bytes (immediately following the
     *                       10-byte container header and the 4-byte decompressed-size field).
     * @param compressedSize Number of compressed bytes available at @p compressedData.
     * @param decompressedSize Expected decompressed size, as recorded in the `.xnb` header.
     * @param path           File path, used only to build exception messages.
     * @param limits         Bounds applied to @p compressedSize/@p decompressedSize (XNB-10A).
     * This strict form refuses everything @ref DecodeXnbLzxPayload would defer: it is for
     * callers that need the whole payload at once (tools, round-trip tests), not the content
     * route.
     *
     * @return The decompressed bytes, exactly @p decompressedSize long.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if either size is negative
     *         or exceeds @p limits, or the frames decode to more than @p decompressedSize bytes.
     * @throws System::InvalidOperationException if decompression fails or produces fewer bytes
     *         than @p decompressedSize (Refresh `DecompressNextBuffer`'s DecompressionError).
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
