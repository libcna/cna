// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/XnbContainerBody.hpp"

#include <limits>

#include "CNA/Internal/Xnb/XnbDecompression.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "System/IO/BinaryReader.hpp"
#include "System/IO/IOException.hpp"
#include "System/IO/MemoryStream.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentLoadException;

    std::span<const std::uint8_t> XnbContainerBody::Bytes() const
    {
        if (const auto* borrowed = std::get_if<std::span<const std::uint8_t>>(&storage))
        {
            return *borrowed;
        }
        const auto& owned = std::get<std::vector<std::uint8_t>>(storage);
        return {owned.data(), owned.size()};
    }

    XnbContainerBody DecodeXnbContainerBody(
        const std::span<const std::uint8_t> file,
        const std::string& origin,
        const XnbReadLimits& limits)
    {
        if (limits.maxFileSize < 0 ||
            file.size() > static_cast<std::size_t>(limits.maxFileSize))
        {
            throw ContentLoadException(
                "'" + origin + "' exceeds the maximum XNB file size (" +
                std::to_string(limits.maxFileSize) + " bytes).");
        }
        if (file.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            throw ContentLoadException("ContentManager: '" + origin + "' is too large to load.");
        }

        XnbHeader header;
        try
        {
            System::IO::MemoryStream stream(
                file.data(), static_cast<std::int32_t>(file.size()));
            System::IO::BinaryReader reader(&stream, true);
            header = ParseXnbHeader(reader, origin);
        }
        catch (const System::IO::IOException& error)
        {
            // The low-level parser keeps its EOF contract; the content boundary wraps I/O.
            throw ContentLoadException(
                "Error loading '" + origin + "'. Invalid XNB header.", error);
        }

        if (header.totalLength < 10 ||
            static_cast<std::size_t>(header.totalLength) > file.size())
        {
            throw ContentLoadException(
                "'" + origin + "' declares a totalLength (" +
                std::to_string(header.totalLength) +
                ") inconsistent with its actual file size (" +
                std::to_string(file.size()) + ").");
        }
        if (header.compression != XnbCompression::None && header.totalLength < 14)
        {
            throw ContentLoadException(
                "'" + origin + "' is truncated before its decompressed-size field.");
        }

        if (header.compression == XnbCompression::None)
        {
            return {
                header,
                std::span<const std::uint8_t>(
                    file.data() + 10u,
                    static_cast<std::size_t>(header.totalLength) - 10u)};
        }
        if (header.compression == XnbCompression::Unknown)
        {
            throw ContentLoadException(
                "'" + origin + "' has an unrecognized compression flag combination.");
        }

        System::IO::MemoryStream sizeStream(file.data() + 10u, 4);
        System::IO::BinaryReader sizeReader(&sizeStream, true);
        const std::int32_t decompressedSize = sizeReader.ReadInt32();
        const std::int32_t compressedSize = header.totalLength - 14;
        std::vector<std::uint8_t> body;
        if (header.compression == XnbCompression::Lzx)
        {
            body = DecompressXnbPayload(
                file.data() + 14u, compressedSize, decompressedSize, origin, limits);
        }
        else
        {
            body = DecompressXnbLz4Payload(
                file.data() + 14u, compressedSize, decompressedSize, origin, limits);
        }
        return {header, std::move(body)};
    }
}
