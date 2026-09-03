// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/XnbByteWriter.hpp"

#include <bit>
#include <utility>

#include "CNA/Content/Cnb/CnbByteReader.hpp"

namespace CNA::Internal::Xnb
{
    namespace
    {
        /**
         * @brief Formats one UTF-16 code unit as four uppercase hexadecimal digits.
         *
         * @param code The code unit value.
         * @return Exactly four hexadecimal digits, independent of the process locale.
         */
        [[nodiscard]] std::string FormatCodeUnit(const std::uint32_t code)
        {
            static constexpr char kDigits[] = "0123456789ABCDEF";
            std::string text(4u, '0');
            for (std::size_t index = 0u; index < 4u; ++index)
            {
                text[index] = kDigits[(code >> ((3u - index) * 4u)) & 0xFu];
            }
            return text;
        }
    }

    XnbByteWriter::XnbByteWriter(XnbWriteLimits limits) : limits_(limits) {}

    void XnbByteWriter::RequireCapacity(const std::size_t additionalBytes)
    {
        const auto ceiling = static_cast<std::size_t>(limits_.maxPayloadSize);
        if (buffer_.size() > ceiling || additionalBytes > ceiling - buffer_.size())
        {
            throw XnbWriteException(
                "XNB: the payload would exceed the configured maximum of " +
                std::to_string(limits_.maxPayloadSize) + " bytes.");
        }
    }

    void XnbByteWriter::WriteByte(const std::uint8_t value)
    {
        RequireCapacity(1u);
        buffer_.push_back(value);
    }

    void XnbByteWriter::WriteSByte(const std::int8_t value)
    {
        WriteByte(static_cast<std::uint8_t>(value));
    }

    void XnbByteWriter::WriteBoolean(const bool value)
    {
        WriteByte(value ? std::uint8_t{1} : std::uint8_t{0});
    }

    void XnbByteWriter::WriteInt16(const std::int16_t value)
    {
        WriteUInt16(static_cast<std::uint16_t>(value));
    }

    void XnbByteWriter::WriteUInt16(const std::uint16_t value)
    {
        RequireCapacity(2u);
        buffer_.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        buffer_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    }

    void XnbByteWriter::WriteInt32(const std::int32_t value)
    {
        WriteUInt32(static_cast<std::uint32_t>(value));
    }

    void XnbByteWriter::WriteUInt32(const std::uint32_t value)
    {
        RequireCapacity(4u);
        for (int shift = 0; shift < 32; shift += 8)
        {
            buffer_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
        }
    }

    void XnbByteWriter::WriteInt64(const std::int64_t value)
    {
        WriteUInt64(static_cast<std::uint64_t>(value));
    }

    void XnbByteWriter::WriteUInt64(const std::uint64_t value)
    {
        RequireCapacity(8u);
        for (int shift = 0; shift < 64; shift += 8)
        {
            buffer_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
        }
    }

    void XnbByteWriter::WriteSingle(const float value)
    {
        WriteUInt32(std::bit_cast<std::uint32_t>(value));
    }

    void XnbByteWriter::WriteDouble(const double value)
    {
        WriteUInt64(std::bit_cast<std::uint64_t>(value));
    }

    void XnbByteWriter::Write7BitEncodedInt(const std::int32_t value)
    {
        // .NET writes the *unsigned* bit pattern, so a negative value legitimately occupies five
        // bytes rather than being an error. CNA's Read7BitEncodedInt() accepts exactly that form.
        auto remaining = static_cast<std::uint32_t>(value);
        while (remaining > 0x7Fu)
        {
            WriteByte(static_cast<std::uint8_t>(remaining | ~0x7Fu));
            remaining >>= 7;
        }
        WriteByte(static_cast<std::uint8_t>(remaining));
    }

    void XnbByteWriter::WriteString(const std::string& value)
    {
        if (value.size() > static_cast<std::size_t>(limits_.maxStringBytes))
        {
            throw XnbWriteException(
                "XNB: refusing to write a " + std::to_string(value.size()) +
                "-byte string; the configured maximum is " +
                std::to_string(limits_.maxStringBytes) + " bytes.");
        }
        // Validated on the way out as well as on the way in: emitting text the reader refuses
        // produces an unloadable file, and catching it here names the value while the producer
        // still holds it.
        if (!CNA::Content::Cnb::CnbByteReader::IsWellFormedUtf8(value))
        {
            throw XnbWriteException(
                "XNB: refusing to write a string that is not well-formed UTF-8.");
        }
        Write7BitEncodedInt(static_cast<std::int32_t>(value.size()));
        WriteBytes(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
    }

    void XnbByteWriter::WriteChar(const SharpRuntime::charcs value)
    {
        const auto code = static_cast<std::uint32_t>(value);
        if (code >= 0xD800u && code <= 0xDFFFu)
        {
            throw XnbWriteException(
                "XNB: refusing to write the unpaired surrogate U+" + FormatCodeUnit(code) +
                " as a System.Char; it has no UTF-8 encoding.");
        }
        if (code < 0x80u)
        {
            WriteByte(static_cast<std::uint8_t>(code));
            return;
        }
        if (code < 0x800u)
        {
            WriteByte(static_cast<std::uint8_t>(0xC0u | (code >> 6)));
            WriteByte(static_cast<std::uint8_t>(0x80u | (code & 0x3Fu)));
            return;
        }
        WriteByte(static_cast<std::uint8_t>(0xE0u | (code >> 12)));
        WriteByte(static_cast<std::uint8_t>(0x80u | ((code >> 6) & 0x3Fu)));
        WriteByte(static_cast<std::uint8_t>(0x80u | (code & 0x3Fu)));
    }

    void XnbByteWriter::WriteBytes(const std::span<const std::uint8_t> bytes)
    {
        RequireCapacity(bytes.size());
        buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    }

    std::size_t XnbByteWriter::Size() const noexcept { return buffer_.size(); }

    std::span<const std::uint8_t> XnbByteWriter::View() const noexcept { return buffer_; }

    const XnbWriteLimits& XnbByteWriter::Limits() const noexcept { return limits_; }

    std::vector<std::uint8_t> XnbByteWriter::Take() { return std::move(buffer_); }
}
