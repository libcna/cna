// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Xnb/XnbByteWriter.hpp"

#include <bit>
#include <limits>

namespace CNA::Content::Xnb
{
    std::int64_t XnbCheckedMultiply(std::initializer_list<std::int64_t> factors,
                                    const std::string& context)
    {
        std::int64_t product = 1;
        for (const std::int64_t factor : factors)
        {
            if (factor < 0)
            {
                throw XnbWriteException(context + ": negative factor in a size calculation.");
            }
            if (factor != 0 && product > std::numeric_limits<std::int64_t>::max() / factor)
            {
                throw XnbWriteException(
                    context + ": the requested sizes overflow the maximum representable byte "
                    "count.");
            }
            product *= factor;
        }
        return product;
    }

    std::int64_t XnbCheckedAdd(const std::int64_t left, const std::int64_t right,
                               const std::string& context)
    {
        if (left < 0 || right < 0)
        {
            throw XnbWriteException(context + ": negative term in a size calculation.");
        }
        if (left > std::numeric_limits<std::int64_t>::max() - right)
        {
            throw XnbWriteException(
                context + ": the requested sizes overflow the maximum representable byte count.");
        }
        return left + right;
    }

    XnbByteWriter::XnbByteWriter(XnbWriteLimits limits)
        : limits_(limits)
    {
    }

    void XnbByteWriter::RequireCapacity(const std::int64_t additionalBytes, const char* what)
    {
        const std::int64_t total =
            XnbCheckedAdd(static_cast<std::int64_t>(buffer_.size()), additionalBytes, what);
        if (total > limits_.maxFileSize)
        {
            throw XnbWriteException(
                std::string(what) + ": writing " + std::to_string(additionalBytes) +
                " more bytes would exceed the maximum .xnb size of " +
                std::to_string(limits_.maxFileSize) + " bytes.");
        }
    }

    void XnbByteWriter::WriteByte(const std::uint8_t value)
    {
        RequireCapacity(1, "XnbByteWriter");
        buffer_.push_back(value);
    }

    void XnbByteWriter::WriteSByte(const std::int8_t value)
    {
        WriteByte(static_cast<std::uint8_t>(value));
    }

    void XnbByteWriter::WriteUInt16(const std::uint16_t value)
    {
        RequireCapacity(2, "XnbByteWriter");
        buffer_.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        buffer_.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
    }

    void XnbByteWriter::WriteInt16(const std::int16_t value)
    {
        WriteUInt16(static_cast<std::uint16_t>(value));
    }

    void XnbByteWriter::WriteUInt32(const std::uint32_t value)
    {
        RequireCapacity(4, "XnbByteWriter");
        buffer_.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        buffer_.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
        buffer_.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xFFu));
        buffer_.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xFFu));
    }

    void XnbByteWriter::WriteInt32(const std::int32_t value)
    {
        WriteUInt32(static_cast<std::uint32_t>(value));
    }

    void XnbByteWriter::WriteUInt64(const std::uint64_t value)
    {
        RequireCapacity(8, "XnbByteWriter");
        for (unsigned shift = 0u; shift < 64u; shift += 8u)
        {
            buffer_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
        }
    }

    void XnbByteWriter::WriteInt64(const std::int64_t value)
    {
        WriteUInt64(static_cast<std::uint64_t>(value));
    }

    void XnbByteWriter::WriteSingle(const float value)
    {
        WriteUInt32(std::bit_cast<std::uint32_t>(value));
    }

    void XnbByteWriter::WriteDouble(const double value)
    {
        WriteUInt64(std::bit_cast<std::uint64_t>(value));
    }

    void XnbByteWriter::WriteBoolean(const bool value)
    {
        WriteByte(value ? std::uint8_t{1} : std::uint8_t{0});
    }

    void XnbByteWriter::Write7BitEncodedInt(const std::int32_t value)
    {
        std::uint32_t remaining = static_cast<std::uint32_t>(value);
        while (remaining > 0x7Fu)
        {
            WriteByte(static_cast<std::uint8_t>((remaining & 0x7Fu) | 0x80u));
            remaining >>= 7u;
        }
        WriteByte(static_cast<std::uint8_t>(remaining));
    }

    void XnbByteWriter::WriteChar(const char16_t value)
    {
        const std::uint32_t code = static_cast<std::uint32_t>(value);
        if (code >= 0xD800u && code <= 0xDFFFu)
        {
            // A lone surrogate has no UTF-8 form. XNA's own writer would emit the replacement
            // character silently; refusing is the better build-time behavior, because a font or
            // character map containing one is an input defect the author needs to see.
            throw XnbWriteException(
                "XnbByteWriter: U+" + std::to_string(code) +
                " is an unpaired UTF-16 surrogate and cannot be encoded as a .xnb character.");
        }
        if (code < 0x80u)
        {
            WriteByte(static_cast<std::uint8_t>(code));
        }
        else if (code < 0x800u)
        {
            WriteByte(static_cast<std::uint8_t>(0xC0u | (code >> 6u)));
            WriteByte(static_cast<std::uint8_t>(0x80u | (code & 0x3Fu)));
        }
        else
        {
            WriteByte(static_cast<std::uint8_t>(0xE0u | (code >> 12u)));
            WriteByte(static_cast<std::uint8_t>(0x80u | ((code >> 6u) & 0x3Fu)));
            WriteByte(static_cast<std::uint8_t>(0x80u | (code & 0x3Fu)));
        }
    }

    void XnbByteWriter::WriteString(const std::string& value)
    {
        if (static_cast<std::int64_t>(value.size()) > limits_.maxStringBytes)
        {
            throw XnbWriteException(
                "XnbByteWriter: a string of " + std::to_string(value.size()) +
                " bytes exceeds the maximum of " + std::to_string(limits_.maxStringBytes) + ".");
        }
        RequireCapacity(static_cast<std::int64_t>(value.size()) + 5, "XnbByteWriter");
        Write7BitEncodedInt(static_cast<std::int32_t>(value.size()));
        buffer_.insert(buffer_.end(), value.begin(), value.end());
    }

    void XnbByteWriter::WriteBytes(const std::span<const std::uint8_t> bytes)
    {
        if (static_cast<std::int64_t>(bytes.size()) > limits_.maxPayloadBytes)
        {
            throw XnbWriteException(
                "XnbByteWriter: a payload of " + std::to_string(bytes.size()) +
                " bytes exceeds the maximum of " + std::to_string(limits_.maxPayloadBytes) + ".");
        }
        RequireCapacity(static_cast<std::int64_t>(bytes.size()), "XnbByteWriter");
        buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    }

    void XnbByteWriter::PatchUInt32(const std::size_t offset, const std::uint32_t value)
    {
        if (offset > buffer_.size() || buffer_.size() - offset < 4u)
        {
            throw XnbWriteException(
                "XnbByteWriter: cannot patch four bytes at offset " + std::to_string(offset) +
                " of a " + std::to_string(buffer_.size()) + "-byte buffer.");
        }
        buffer_[offset] = static_cast<std::uint8_t>(value & 0xFFu);
        buffer_[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
        buffer_[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
        buffer_[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
    }

    std::size_t XnbByteWriter::Size() const noexcept
    {
        return buffer_.size();
    }

    std::span<const std::uint8_t> XnbByteWriter::View() const noexcept
    {
        return std::span<const std::uint8_t>(buffer_.data(), buffer_.size());
    }

    const XnbWriteLimits& XnbByteWriter::Limits() const noexcept
    {
        return limits_;
    }

    std::vector<std::uint8_t> XnbByteWriter::Take()
    {
        return std::move(buffer_);
    }
}
