// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbByteWriter.hpp"

#include <bit>
#include <limits>
#include <utility>

#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace CNA::Content::Cnb
{
    CnbByteWriter::CnbByteWriter(std::vector<std::uint8_t> initial) : buffer_(std::move(initial)) {}

    void CnbByteWriter::WriteU8(std::uint8_t value) { buffer_.push_back(value); }

    void CnbByteWriter::WriteU16(std::uint16_t value)
    {
        buffer_.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        buffer_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    }

    void CnbByteWriter::WriteU32(std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            buffer_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
        }
    }

    void CnbByteWriter::WriteU64(std::uint64_t value)
    {
        for (int shift = 0; shift < 64; shift += 8)
        {
            buffer_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
        }
    }

    void CnbByteWriter::WriteI32(std::int32_t value) { WriteU32(std::bit_cast<std::uint32_t>(value)); }

    void CnbByteWriter::WriteF32(float value)
    {
        WriteU32(std::bit_cast<std::uint32_t>(value));
    }

    void CnbByteWriter::WriteF64(double value)
    {
        WriteU64(std::bit_cast<std::uint64_t>(value));
    }

    void CnbByteWriter::WriteString(const std::string& value)
    {
        if (value.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        {
            throw ContentLoadException("CNB: a string is too long to encode in a .cnb file.");
        }
        // Validated on the way out as well as on the way in: a writer that emits a string the
        // reader will refuse produces an unreadable file, and finding that at write time names the
        // offending value while the producer still has it.
        if (!CnbByteReader::IsWellFormedUtf8(value))
        {
            throw ContentLoadException(
                "CNB: refusing to write a string that is not well-formed UTF-8.");
        }
        WriteU32(static_cast<std::uint32_t>(value.size()));
        buffer_.insert(buffer_.end(), value.begin(), value.end());
    }

    void CnbByteWriter::WriteBytes(std::span<const std::uint8_t> bytes)
    {
        buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    }

    void CnbByteWriter::WriteZeros(std::size_t count) { buffer_.insert(buffer_.end(), count, 0u); }

    std::size_t CnbByteWriter::Size() const noexcept { return buffer_.size(); }

    std::span<const std::uint8_t> CnbByteWriter::View() const noexcept { return buffer_; }

    std::vector<std::uint8_t> CnbByteWriter::Take() { return std::move(buffer_); }
}
