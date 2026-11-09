// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "CNA/Internal/Xnb/XnbHeader.hpp"
#include "CNA/Internal/Xnb/XnbReadLimits.hpp"

namespace System::IO
{
    class Stream;
}

namespace CNA::Internal::Xnb
{
    /**
     * @brief A validated XNB header and its uncompressed content-reader body.
     *
     * Uncompressed containers borrow their body from the input span. Compressed containers own
     * the decoded body, so callers must keep this object alive while using @ref Bytes.
     */
    struct XnbContainerBody
    {
        /** @brief The parsed and validated XNB container header. */
        XnbHeader header;

        /** @brief Borrowed uncompressed bytes or owned decompressed bytes. */
        std::variant<std::span<const std::uint8_t>, std::vector<std::uint8_t>> storage;

        /**
         * @brief The failure a read past @ref Bytes raises, or null.
         *
         * Set only for an LZX body whose decoding stopped early (see DecodeXnbLzxPayload):
         * Refresh's DecompressStream raises it when the reader reaches that point, not when the
         * container is opened.
         */
        std::exception_ptr deferredReadFailure;

        /**
         * @brief Gets the uncompressed content-reader body.
         * @return A view valid while this object and, for an uncompressed container, its input
         *         bytes remain alive.
         */
        [[nodiscard]] std::span<const std::uint8_t> Bytes() const;

        /**
         * @brief Opens the seekable stream a ContentReader reads @ref Bytes through.
         *
         * A read that finds the stream exhausted raises @ref deferredReadFailure when one is
         * set, which is where Refresh's reader meets a decompression failure; otherwise it
         * reports the end of the stream as any stream does.
         *
         * @return A read-only stream over a copy of @ref Bytes.
         */
        [[nodiscard]] std::unique_ptr<System::IO::Stream> OpenBodyStream() const;
    };

    /**
     * @brief Validates an XNB container and exposes its uncompressed content-reader body.
     *
     * The declared `totalLength` is compared with the bytes after the header exactly as Refresh
     * PrepareStream IL_009f-00cd does: only a declared length whose body would end past the input
     * is refused. An uncompressed body then runs to the end of the input whatever the declared
     * length says (Refresh hands the content reader the input stream itself, IL_00ed); an LZX body
     * is read from the `totalLength - 14` compressed bytes the declaration names. Callers that
     * require a file to end exactly at `totalLength` must enforce that separately.
     *
     * @param file Complete bytes available for the XNB container.
     * @param origin File or asset name used in diagnostics.
     * @param limits File, compressed-payload, and decompressed-payload limits.
     * @return The validated header and uncompressed body.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the header, declared
     *         range, compression flags, or a size exceeding @p limits is invalid, or an LZ4
     *         payload does not decode. LZX decoding failures are deferred to the body stream.
     */
    [[nodiscard]] XnbContainerBody DecodeXnbContainerBody(
        std::span<const std::uint8_t> file,
        const std::string& origin,
        const XnbReadLimits& limits = DefaultXnbReadLimits());
}
