// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "CNA/Content/Cnb/CnbReadLimits.hpp"

namespace CNA::Content::Cnb
{
    /**
     * @brief A bounded, little-endian cursor over one region of a `.cnb` file
     *        (plans/plan_cnb.md `CNBF-006`).
     *
     * Every read is checked against the region's end before a byte is touched, and every failure
     * is a `ContentLoadException` naming the region, the offset and how many bytes were wanted --
     * never undefined behaviour and never a silently truncated value. Integers are assembled from
     * individual bytes rather than `memcpy`'d, so the decoded value does not depend on the host's
     * byte order, and floats are produced by `std::bit_cast` from an explicitly little-endian
     * integer, so they do not depend on the host's floating-point storage order either.
     *
     * The cursor does not own its bytes; the caller must keep the underlying buffer alive.
     */
    class CnbByteReader
    {
    public:
        /**
         * @brief Constructs a cursor over @p data.
         *
         * @param data    The region to read. Not copied; must outlive this cursor.
         * @param context Text prefixed verbatim to every exception message, naming the region
         *                (e.g. `"'walk.cnb' chunk ACLK"`).
         * @param limits  Sanity bounds applied to string lengths and array element counts.
         */
        CnbByteReader(std::span<const std::uint8_t> data, std::string context,
                      const CnbReadLimits& limits = DefaultCnbReadLimits());

        /**
         * @brief Number of bytes not yet consumed.
         *
         * @return The remaining byte count.
         */
        [[nodiscard]] std::size_t Remaining() const noexcept;

        /**
         * @brief Current read offset within the region.
         *
         * @return The offset in bytes from the start of the region.
         */
        [[nodiscard]] std::size_t Position() const noexcept;

        /**
         * @brief Total size of the region.
         *
         * @return The region size in bytes.
         */
        [[nodiscard]] std::size_t Size() const noexcept;

        /**
         * @brief The context string this cursor prefixes to its exception messages.
         *
         * @return The context string supplied at construction.
         */
        [[nodiscard]] const std::string& Context() const noexcept;

        /**
         * @brief Reads one unsigned byte.
         *
         * @return The byte read.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException on truncation.
         */
        [[nodiscard]] std::uint8_t ReadU8();

        /**
         * @brief Reads a little-endian unsigned 16-bit integer.
         *
         * @return The value read.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException on truncation.
         */
        [[nodiscard]] std::uint16_t ReadU16();

        /**
         * @brief Reads a little-endian unsigned 32-bit integer.
         *
         * @return The value read.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException on truncation.
         */
        [[nodiscard]] std::uint32_t ReadU32();

        /**
         * @brief Reads a little-endian unsigned 64-bit integer.
         *
         * @return The value read.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException on truncation.
         */
        [[nodiscard]] std::uint64_t ReadU64();

        /**
         * @brief Reads a little-endian two's-complement signed 32-bit integer.
         *
         * @return The value read.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException on truncation.
         */
        [[nodiscard]] std::int32_t ReadI32();

        /**
         * @brief Reads a little-endian IEEE-754 binary32 value.
         *
         * @return The value read.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException on truncation.
         */
        [[nodiscard]] float ReadF32();

        /**
         * @brief Reads a little-endian IEEE-754 binary64 value.
         *
         * @return The value read.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException on truncation.
         */
        [[nodiscard]] double ReadF64();

        /**
         * @brief Reads a length-prefixed UTF-8 string: a `u32` byte length followed by that many
         *        bytes, with no terminator.
         *
         * The length is checked against `CnbReadLimits::maxStringBytes` and against the region's
         * remaining size before any allocation, and the bytes are validated as well-formed UTF-8
         * (no overlong encodings, no surrogate code points, nothing above `U+10FFFF`, no truncated
         * sequence). A `.cnb` string can end up as a filesystem path or an effect name, so letting
         * malformed UTF-8 through would push the problem into code far less prepared for it.
         *
         * @return The decoded string.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException on truncation, an
         *         over-long declared length, or malformed UTF-8.
         */
        [[nodiscard]] std::string ReadString();

        /**
         * @brief Reads a `u32` element count and checks it against the limits and against the
         *        number of elements that could actually fit in the remaining bytes.
         *
         * @param elementSize      Size in bytes of one element that follows the count. Pass 0 when
         *                         the elements are variable-length, which skips the fit check.
         * @param whatIsBeingCounted Noun used in the exception message (e.g. `"tracks"`).
         * @return The validated element count.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the count exceeds
         *         the configured limit or cannot fit in the remaining bytes.
         */
        [[nodiscard]] std::uint32_t ReadCount(std::uint64_t elementSize,
                                              const char* whatIsBeingCounted);

        /**
         * @brief Returns a view of the next @p count bytes and advances past them.
         *
         * @param count Number of bytes to take.
         * @return A view into the underlying buffer; valid as long as that buffer is.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException on truncation.
         */
        [[nodiscard]] std::span<const std::uint8_t> ReadBytes(std::uint64_t count);

        /**
         * @brief Advances the cursor without reading.
         *
         * @param count Number of bytes to skip.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException on truncation.
         */
        void Skip(std::uint64_t count);

        /**
         * @brief Requires that the region has been consumed exactly.
         *
         * Used at the end of every fixed-layout chunk decoder: trailing bytes mean the file and
         * this reader disagree about the layout, which must be an error rather than something
         * silently ignored.
         *
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException if bytes remain.
         */
        void RequireExhausted();

        /**
         * @brief Throws a `ContentLoadException` whose message is this cursor's context, the
         *        current offset and @p detail.
         *
         * @param detail Description of what was wrong.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException always.
         */
        [[noreturn]] void Fail(const std::string& detail) const;

        /**
         * @brief Checks that the bytes of @p text are well-formed UTF-8.
         *
         * Exposed separately from ReadString() so a writer can reject a malformed string before
         * committing it to a file, keeping both ends of the format honest.
         *
         * @param text Bytes to validate.
         * @return True when @p text is well-formed UTF-8.
         */
        [[nodiscard]] static bool IsWellFormedUtf8(std::string_view text);

    private:
        void Require(std::uint64_t count, const char* what) const;

        std::span<const std::uint8_t> data_;
        std::size_t position_ = 0;
        std::string context_;
        const CnbReadLimits* limits_;
    };
}
