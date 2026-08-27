// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbByteReader.hpp"

#include "CNA/Content/Cnb/CnbArithmetic.hpp"

#include <bit>
#include <string_view>
#include <utility>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace CNA::Content::Cnb
{
    CnbByteReader::CnbByteReader(std::span<const std::uint8_t> data, std::string context,
                                 const CnbReadLimits& limits)
        : data_(data), context_(std::move(context)), limits_(limits)
    {
    }

    std::size_t CnbByteReader::Remaining() const noexcept { return data_.size() - position_; }
    std::size_t CnbByteReader::Position() const noexcept { return position_; }
    std::size_t CnbByteReader::Size() const noexcept { return data_.size(); }
    const std::string& CnbByteReader::Context() const noexcept { return context_; }

    void CnbByteReader::Fail(const std::string& detail) const
    {
        throw ContentLoadException(context_ + " at byte offset " + std::to_string(position_) +
                                   ": " + detail);
    }

    void CnbByteReader::Require(std::uint64_t count, const char* what) const
    {
        if (count > static_cast<std::uint64_t>(Remaining()))
        {
            throw ContentLoadException(
                context_ + " is truncated: reading " + what + " needs " + std::to_string(count) +
                " byte(s) at offset " + std::to_string(position_) + ", but only " +
                std::to_string(Remaining()) + " byte(s) remain.");
        }
    }

    std::uint8_t CnbByteReader::ReadU8()
    {
        Require(1u, "u8");
        return data_[position_++];
    }

    std::uint16_t CnbByteReader::ReadU16()
    {
        Require(2u, "u16");
        const auto b0 = static_cast<std::uint16_t>(data_[position_]);
        const auto b1 = static_cast<std::uint16_t>(data_[position_ + 1]);
        position_ += 2;
        return static_cast<std::uint16_t>(b0 | static_cast<std::uint16_t>(b1 << 8));
    }

    std::uint32_t CnbByteReader::ReadU32()
    {
        Require(4u, "u32");
        std::uint32_t value = 0;
        for (int i = 3; i >= 0; --i)
        {
            value = (value << 8) | static_cast<std::uint32_t>(data_[position_ + static_cast<std::size_t>(i)]);
        }
        position_ += 4;
        return value;
    }

    std::uint64_t CnbByteReader::ReadU64()
    {
        Require(8u, "u64");
        std::uint64_t value = 0;
        for (int i = 7; i >= 0; --i)
        {
            value = (value << 8) | static_cast<std::uint64_t>(data_[position_ + static_cast<std::size_t>(i)]);
        }
        position_ += 8;
        return value;
    }

    std::int32_t CnbByteReader::ReadI32()
    {
        // Via std::bit_cast rather than a cast that would be implementation-defined for values
        // above INT32_MAX before C++20 and is easy to get subtly wrong afterwards.
        return std::bit_cast<std::int32_t>(ReadU32());
    }

    float CnbByteReader::ReadF32()
    {
        static_assert(sizeof(float) == 4, "CNB requires a 32-bit IEEE-754 float.");
        return std::bit_cast<float>(ReadU32());
    }

    double CnbByteReader::ReadF64()
    {
        static_assert(sizeof(double) == 8, "CNB requires a 64-bit IEEE-754 double.");
        return std::bit_cast<double>(ReadU64());
    }

    bool CnbByteReader::IsWellFormedUtf8(std::string_view text)
    {
        std::size_t i = 0;
        const std::size_t n = text.size();
        while (i < n)
        {
            const auto lead = static_cast<std::uint8_t>(text[i]);
            std::size_t extra = 0;
            std::uint32_t codePoint = 0;
            std::uint32_t lowestLegal = 0;

            if (lead < 0x80u)
            {
                ++i;
                continue;
            }
            if ((lead & 0xE0u) == 0xC0u) { extra = 1; codePoint = lead & 0x1Fu; lowestLegal = 0x80u; }
            else if ((lead & 0xF0u) == 0xE0u) { extra = 2; codePoint = lead & 0x0Fu; lowestLegal = 0x800u; }
            else if ((lead & 0xF8u) == 0xF0u) { extra = 3; codePoint = lead & 0x07u; lowestLegal = 0x10000u; }
            else { return false; } // continuation byte in lead position, or a 5/6-byte form

            if (i + extra >= n) { return false; }
            for (std::size_t k = 1; k <= extra; ++k)
            {
                const auto cont = static_cast<std::uint8_t>(text[i + k]);
                if ((cont & 0xC0u) != 0x80u) { return false; }
                codePoint = (codePoint << 6) | (cont & 0x3Fu);
            }

            if (codePoint < lowestLegal) { return false; }                       // overlong
            if (codePoint >= 0xD800u && codePoint <= 0xDFFFu) { return false; }  // surrogate
            if (codePoint > 0x10FFFFu) { return false; }                         // out of range

            i += extra + 1;
        }
        return true;
    }

    std::string CnbByteReader::ReadString()
    {
        const std::uint32_t byteLength = ReadU32();
        if (byteLength > limits_.maxStringBytes)
        {
            Fail("a string declares " + std::to_string(byteLength) +
                 " byte(s), above the configured limit of " +
                 std::to_string(limits_.maxStringBytes) + ".");
        }
        Require(byteLength, "string bytes");
        const auto* first = reinterpret_cast<const char*>(data_.data() + position_);
        const std::string_view view(first, byteLength);
        if (!IsWellFormedUtf8(view))
        {
            Fail("a string is not well-formed UTF-8.");
        }
        position_ += byteLength;
        return std::string(view);
    }

    std::uint32_t CnbByteReader::ReadCount(std::uint64_t elementSize, const char* whatIsBeingCounted)
    {
        const std::uint32_t count = ReadU32();
        if (count > limits_.maxArrayElementCount)
        {
            Fail(std::string("declares ") + std::to_string(count) + " " + whatIsBeingCounted +
                 ", above the configured limit of " +
                 std::to_string(limits_.maxArrayElementCount) + ".");
        }
        if (elementSize != 0u)
        {
            // Checked against what physically remains before anything is reserved, so a huge count
            // in a small chunk fails immediately instead of after an enormous allocation.
            //
            // Through CheckedMultiply rather than a bare `*`: every current caller passes a small
            // constant element size, so the product provably fits -- but that is a property of the
            // callers, not of this function, and the specification promises the operation itself is
            // safe. Making it unconditionally true costs one predictable branch.
            const std::uint64_t needed = CheckedMultiply(
                count, elementSize, context_ + " " + whatIsBeingCounted);
            if (needed > static_cast<std::uint64_t>(Remaining()))
            {
                Fail(std::string("declares ") + std::to_string(count) + " " + whatIsBeingCounted +
                     " (" + std::to_string(needed) + " byte(s)), but only " +
                     std::to_string(Remaining()) + " byte(s) remain.");
            }
        }
        return count;
    }

    std::span<const std::uint8_t> CnbByteReader::ReadBytes(std::uint64_t count)
    {
        Require(count, "raw bytes");
        const auto view = data_.subspan(position_, static_cast<std::size_t>(count));
        position_ += static_cast<std::size_t>(count);
        return view;
    }

    void CnbByteReader::Skip(std::uint64_t count)
    {
        Require(count, "skipped bytes");
        position_ += static_cast<std::size_t>(count);
    }

    void CnbByteReader::RequireExhausted()
    {
        if (Remaining() != 0u)
        {
            throw ContentLoadException(
                context_ + " has " + std::to_string(Remaining()) +
                " unexpected trailing byte(s) after its declared contents.");
        }
    }
}
