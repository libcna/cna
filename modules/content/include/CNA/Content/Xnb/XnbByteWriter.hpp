// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "CNA/Content/Xnb/XnbWriteLimits.hpp"

namespace CNA::Content::Xnb
{
    /**
     * @brief Emits the `.xnb` primitive encodings, checked and byte-deterministically
     *        (plans/plan_xnapipeline.md `XNAP-002`).
     *
     * The exact write-side counterpart of the primitives `System::IO::BinaryReader` supplies to
     * every CNA `.xnb` reader. Integers are decomposed into individual bytes rather than
     * `memcpy`'d, and floating-point values go through `std::bit_cast` to a fixed-width integer
     * first, so the bytes produced never depend on the host's byte order or floating-point
     * storage order. Nothing here consults the clock, a random source or a pointer value, which
     * is what lets `XnbWriter` promise byte-identical output for an identical object graph.
     *
     * This is a separate class from `CNA::Content::Cnb::CnbByteWriter` on purpose: the two
     * formats disagree on the primitives themselves (`.xnb` strings carry a 7-bit-encoded byte
     * count, `.cnb` strings a `u32`), so sharing an implementation would mean one class with two
     * mutually exclusive personalities.
     */
    class XnbByteWriter
    {
    public:
        /** @brief Constructs an empty writer using the default write limits. */
        XnbByteWriter() = default;

        /**
         * @brief Constructs an empty writer bound to explicit limits.
         *
         * @param limits Bounds consulted by every length-driven write.
         */
        explicit XnbByteWriter(XnbWriteLimits limits);

        /**
         * @brief Appends one raw byte.
         *
         * @param value The byte to append.
         */
        void WriteByte(std::uint8_t value);

        /**
         * @brief Appends one signed byte in two's-complement form.
         *
         * @param value The value to append.
         */
        void WriteSByte(std::int8_t value);

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
         * @brief Appends one byte, `1` for true and `0` for false.
         *
         * @param value The value to append.
         */
        void WriteBoolean(bool value);

        /**
         * @brief Appends a variable-length 7-bit encoded 32-bit integer.
         *
         * The encoding `System::IO::BinaryReader::Read7BitEncodedInt()` consumes: base-128 groups
         * least-significant first, with the high bit set on every byte but the last. A negative
         * value is encoded from its unsigned two's-complement bit pattern and therefore always
         * occupies the full five bytes, exactly as .NET's own writer does.
         *
         * @param value The value to append.
         */
        void Write7BitEncodedInt(std::int32_t value);

        /**
         * @brief Appends a UTF-16 code unit encoded as a single UTF-8 character.
         *
         * `.xnb` stores `System.Char` as one UTF-8 encoded character, which is one to three bytes
         * for the code points a UTF-16 code unit can express.
         *
         * @param value The code unit to append.
         * @throws XnbWriteException if @p value is an unpaired surrogate, which has no UTF-8 form.
         */
        void WriteChar(char16_t value);

        /**
         * @brief Appends a 7-bit encoded UTF-8 byte count followed by the string's bytes.
         *
         * @param value UTF-8 text to append; no terminator or byte-order mark is written.
         * @throws XnbWriteException if @p value exceeds `XnbWriteLimits::maxStringBytes`.
         */
        void WriteString(const std::string& value);

        /**
         * @brief Appends raw bytes verbatim, with no length prefix.
         *
         * @param bytes The bytes to append.
         * @throws XnbWriteException if @p bytes exceeds `XnbWriteLimits::maxPayloadBytes`.
         */
        void WriteBytes(std::span<const std::uint8_t> bytes);

        /**
         * @brief Overwrites four bytes already written with a little-endian unsigned 32-bit value.
         *
         * Used to patch the container header's total-size field once the body length is known.
         *
         * @param offset Byte offset of the first of the four bytes to overwrite.
         * @param value The value to store.
         * @throws XnbWriteException if the four-byte window is not fully inside the buffer.
         */
        void PatchUInt32(std::size_t offset, std::uint32_t value);

        /**
         * @brief Returns the number of bytes written so far.
         *
         * @return The current buffer size.
         */
        [[nodiscard]] std::size_t Size() const noexcept;

        /**
         * @brief Returns a read-only view of everything written so far.
         *
         * @return A view valid until the next mutating call.
         */
        [[nodiscard]] std::span<const std::uint8_t> View() const noexcept;

        /**
         * @brief Returns the bounds this writer enforces.
         *
         * @return A reference valid for this writer's lifetime.
         */
        [[nodiscard]] const XnbWriteLimits& Limits() const noexcept;

        /**
         * @brief Takes ownership of the written bytes, leaving the writer empty.
         *
         * @return The written bytes.
         */
        [[nodiscard]] std::vector<std::uint8_t> Take();

    private:
        void RequireCapacity(std::int64_t additionalBytes, const char* what);

        XnbWriteLimits limits_{};
        std::vector<std::uint8_t> buffer_;
    };
}
