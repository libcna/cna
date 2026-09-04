// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "CNA/Internal/Xnb/XnbWriteLimits.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace CNA::Internal::Xnb
{
    /**
     * @brief Raised for every refusal on the `.xnb` writing path.
     *
     * Deliberately distinct from `ContentLoadException`, which names a *load* failure: a writer
     * refusal is a build-time diagnostic about content the producer still holds, and conflating
     * the two makes pipeline diagnostics read as if a finished asset failed to load.
     */
    class XnbWriteException : public std::runtime_error
    {
    public:
        /**
         * @brief Creates a writer refusal carrying a complete diagnostic message.
         *
         * @param message Human-readable reason, already naming the offending field or value.
         */
        explicit XnbWriteException(const std::string& message) : std::runtime_error(message) {}
    };

    /**
     * @brief Emits `.xnb` payload primitives in the exact encoding CNA's `.xnb` reader consumes
     *        (plans/plan_xnapipeline.md `XNAP-10`).
     *
     * The precise counterpart of `System::IO::BinaryReader` as the XNB readers use it: integers
     * are decomposed byte by byte rather than `memcpy`'d, and floating-point values go through
     * `std::bit_cast` to an integer first, so the emitted bytes never depend on the host's byte
     * order or floating-point storage order. Nothing here consults the clock, a random source, a
     * locale or any pointer value, which is what makes XnbAssetWriter's output byte-deterministic.
     *
     * A dedicated writer rather than `System::IO::BinaryWriter`: XNB writing needs an in-memory
     * body buffer that a header is prepended to once its final length is known, needs a UTF-8
     * `charcs` overload that `BinaryWriter` does not provide, and needs every length-driven write
     * bounded by @ref XnbWriteLimits. `CnbByteWriter` sets the same precedent for `.cnb`.
     */
    class XnbByteWriter
    {
    public:
        /** @brief Constructs an empty writer bounded by the process-wide default limits. */
        XnbByteWriter() = default;

        /**
         * @brief Constructs an empty writer bounded by explicit limits.
         *
         * @param limits Output ceilings applied to every length-driven write.
         */
        explicit XnbByteWriter(XnbWriteLimits limits);

        /**
         * @brief Appends one unsigned byte.
         *
         * @param value The byte to append.
         */
        void WriteByte(std::uint8_t value);

        /**
         * @brief Appends one two's-complement signed byte.
         *
         * @param value The value to append.
         */
        void WriteSByte(std::int8_t value);

        /**
         * @brief Appends a .NET `BinaryWriter`-compatible boolean: one byte, `0` or `1`.
         *
         * @param value The value to append.
         */
        void WriteBoolean(bool value);

        /**
         * @brief Appends a little-endian signed 16-bit integer.
         *
         * @param value The value to append.
         */
        void WriteInt16(std::int16_t value);

        /**
         * @brief Appends a little-endian unsigned 16-bit integer.
         *
         * @param value The value to append.
         */
        void WriteUInt16(std::uint16_t value);

        /**
         * @brief Appends a little-endian signed 32-bit integer.
         *
         * @param value The value to append.
         */
        void WriteInt32(std::int32_t value);

        /**
         * @brief Appends a little-endian unsigned 32-bit integer.
         *
         * @param value The value to append.
         */
        void WriteUInt32(std::uint32_t value);

        /**
         * @brief Appends a little-endian signed 64-bit integer.
         *
         * @param value The value to append.
         */
        void WriteInt64(std::int64_t value);

        /**
         * @brief Appends a little-endian unsigned 64-bit integer.
         *
         * @param value The value to append.
         */
        void WriteUInt64(std::uint64_t value);

        /**
         * @brief Appends a little-endian IEEE-754 binary32 value.
         *
         * @param value The value to append.
         */
        void WriteSingle(float value);

        /**
         * @brief Appends a little-endian IEEE-754 binary64 value.
         *
         * @param value The value to append.
         */
        void WriteDouble(double value);

        /**
         * @brief Appends a .NET `BinaryWriter.Write7BitEncodedInt`-compatible variable-length
         *        integer.
         *
         * @param value The value to append. Negative values encode as their unsigned bit pattern
         *              in five bytes, exactly as .NET does.
         */
        void Write7BitEncodedInt(std::int32_t value);

        /**
         * @brief Appends a .NET `BinaryWriter.Write(string)`-compatible string: a 7-bit-encoded
         *        UTF-8 byte length followed by the bytes themselves.
         *
         * @param value Well-formed UTF-8 text no longer than the configured string limit.
         * @throws XnbWriteException if @p value is not well-formed UTF-8 or exceeds the limit.
         */
        void WriteString(const std::string& value);

        /**
         * @brief Appends one UTF-16 code unit as the 1-3 UTF-8 bytes .NET's
         *        `BinaryWriter.Write(char)` would emit.
         *
         * @param value The code unit to append.
         * @throws XnbWriteException if @p value is an unpaired surrogate, which has no UTF-8
         *         encoding and which CNA's reader refuses.
         */
        void WriteChar(SharpRuntime::charcs value);

        /**
         * @brief Appends raw bytes verbatim.
         *
         * @param bytes The bytes to append.
         * @throws XnbWriteException if the payload would exceed the configured payload limit.
         */
        void WriteBytes(std::span<const std::uint8_t> bytes);

        /**
         * @brief Number of bytes written so far.
         *
         * @return The current buffer size.
         */
        [[nodiscard]] std::size_t Size() const noexcept;

        /**
         * @brief Read-only view of everything written so far.
         *
         * @return A view valid until the next mutating call.
         */
        [[nodiscard]] std::span<const std::uint8_t> View() const noexcept;

        /**
         * @brief The output ceilings this writer applies.
         *
         * @return Read-only limits.
         */
        [[nodiscard]] const XnbWriteLimits& Limits() const noexcept;

        /**
         * @brief Takes ownership of the written bytes, leaving the writer empty.
         *
         * @return The written bytes.
         */
        [[nodiscard]] std::vector<std::uint8_t> Take();

    private:
        void RequireCapacity(std::size_t additionalBytes);

        std::vector<std::uint8_t> buffer_;
        XnbWriteLimits limits_{};
    };
}
