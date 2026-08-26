// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace CNA::Content::Cnb
{
    /**
     * @brief Emits `.cnb` primitives in their canonical little-endian encoding
     *        (plans/plan_cnb.md `CNBF-007`).
     *
     * The exact counterpart of CnbByteReader: integers are decomposed into individual bytes rather
     * than `memcpy`'d, and floats go through `std::bit_cast` to an integer first, so the bytes
     * produced never depend on the host's byte order or floating-point storage order. Nothing here
     * consults the clock, a random source, or any pointer value, which is what makes CnbWriter's
     * output byte-deterministic.
     */
    class CnbByteWriter
    {
    public:
        /** @brief Constructs an empty writer. */
        CnbByteWriter() = default;

        /**
         * @brief Constructs a writer that appends to an existing buffer.
         *
         * @param initial Bytes already written; the writer continues after them.
         */
        explicit CnbByteWriter(std::vector<std::uint8_t> initial);

        /**
         * @brief Appends one byte.
         *
         * @param value The byte to append.
         */
        void WriteU8(std::uint8_t value);

        /**
         * @brief Appends a little-endian unsigned 16-bit integer.
         *
         * @param value The value to append.
         */
        void WriteU16(std::uint16_t value);

        /**
         * @brief Appends a little-endian unsigned 32-bit integer.
         *
         * @param value The value to append.
         */
        void WriteU32(std::uint32_t value);

        /**
         * @brief Appends a little-endian unsigned 64-bit integer.
         *
         * @param value The value to append.
         */
        void WriteU64(std::uint64_t value);

        /**
         * @brief Appends a little-endian two's-complement signed 32-bit integer.
         *
         * @param value The value to append.
         */
        void WriteI32(std::int32_t value);

        /**
         * @brief Appends a little-endian IEEE-754 binary32 value.
         *
         * @param value The value to append.
         */
        void WriteF32(float value);

        /**
         * @brief Appends a little-endian IEEE-754 binary64 value.
         *
         * @param value The value to append.
         */
        void WriteF64(double value);

        /**
         * @brief Appends a `u32` byte length followed by @p value's UTF-8 bytes.
         *
         * @param value The string to append. Must be well-formed UTF-8 and no longer than
         *              4294967295 bytes.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException if @p value is not
         *         well-formed UTF-8 or is too long to encode.
         */
        void WriteString(const std::string& value);

        /**
         * @brief Appends raw bytes verbatim.
         *
         * @param bytes The bytes to append.
         */
        void WriteBytes(std::span<const std::uint8_t> bytes);

        /**
         * @brief Appends @p count zero bytes.
         *
         * @param count Number of zero bytes to append.
         */
        void WriteZeros(std::size_t count);

        /**
         * @brief Number of bytes written so far.
         *
         * @return The current buffer size.
         */
        [[nodiscard]] std::size_t Size() const noexcept;

        /**
         * @brief Read-only view of everything written so far.
         *
         * @return A view of the buffer.
         */
        [[nodiscard]] std::span<const std::uint8_t> View() const noexcept;

        /**
         * @brief Takes ownership of the written bytes, leaving the writer empty.
         *
         * @return The written bytes.
         */
        [[nodiscard]] std::vector<std::uint8_t> Take();

    private:
        std::vector<std::uint8_t> buffer_;
    };
}
