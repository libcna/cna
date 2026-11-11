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

    namespace
    {
        // The body a ContentReader reads. Exhausting it raises the decoding failure Refresh's
        // DecompressStream would have raised at this point (DecompressNextBuffer IL_0089-00ad,
        // ReadBufferFromDisk IL_0023-0030), and otherwise ends like any stream.
        class XnbBodyStream final : public System::IO::MemoryStream
        {
        public:
            XnbBodyStream(const std::span<const std::uint8_t> bytes, std::exception_ptr failure)
                : System::IO::MemoryStream(
                      bytes.data(), static_cast<SharpRuntime::intcs>(bytes.size()), false),
                  failure_(std::move(failure))
            {
            }

            SharpRuntime::intcs Read(SharpRuntime::bytecs buffer[], SharpRuntime::intcs offset,
                                     SharpRuntime::intcs count) override
            {
                const SharpRuntime::intcs read = System::IO::MemoryStream::Read(buffer, offset, count);
                if (read == 0 && count > 0 && failure_)
                {
                    std::rethrow_exception(failure_);
                }
                return read;
            }

        private:
            std::exception_ptr failure_;
        };
    }

    std::unique_ptr<System::IO::Stream> XnbContainerBody::OpenBodyStream() const
    {
        return std::make_unique<XnbBodyStream>(Bytes(), deferredReadFailure);
    }

    namespace
    {
        // Refresh ContentReader.PrepareStream up to IL_00cd: everything checked before the body.
        [[nodiscard]] XnbHeader ValidateXnbContainerHeader(
            const std::span<const std::uint8_t> file, const std::string& origin,
            const XnbReadLimits& limits, const bool inputCanSeek)
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
            catch (const System::IO::IOException&)
            {
                // The low-level parser keeps its EOF contract; the content boundary wraps I/O.
                throw ContentLoadException(
                    "Error loading \"" + origin + "\".", std::current_exception());
            }

            // Refresh PrepareStream IL_009f-00cd: `(totalLength - 10)` in Int32 arithmetic,
            // widened, against Length - Position. A smaller or even negative declaration is
            // accepted, and a stream that cannot seek is not checked at all (IL_009f-00a5).
            const std::int64_t declaredBody = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(header.totalLength) - 10u);
            if (inputCanSeek && declaredBody > static_cast<std::int64_t>(file.size()) - 10)
            {
                throw ContentLoadException(
                    "'" + origin + "' declares a totalLength (" +
                    std::to_string(header.totalLength) +
                    ") inconsistent with its actual file size (" +
                    std::to_string(file.size()) + ").");
            }
            return header;
        }
    }

    XnbContainerBody DecodeXnbContainerBody(
        const std::span<const std::uint8_t> file,
        const std::string& origin,
        const XnbReadLimits& limits,
        const bool inputCanSeek)
    {
        const XnbHeader header = ValidateXnbContainerHeader(file, origin, limits, inputCanSeek);

        if (header.compression == XnbCompression::None)
        {
            // IL_00ed: the uncompressed reader reads the input stream itself, to its end.
            return {
                header,
                std::span<const std::uint8_t>(file.data() + 10u, file.size() - 10u),
                nullptr};
        }
        if (header.compression == XnbCompression::Unknown)
        {
            throw ContentLoadException(
                "'" + origin + "' has an unrecognized compression flag combination.");
        }
        if (file.size() < 14u)
        {
            // IL_00d8: the decompressed-size ReadInt32 ends the stream inside PrepareStream's
            // IOException handler, which reports BadXnb.
            throw ContentLoadException(
                "'" + origin + "' is truncated before its decompressed-size field.");
        }

        System::IO::MemoryStream sizeStream(file.data() + 10u, 4);
        System::IO::BinaryReader sizeReader(&sizeStream, true);
        const std::int32_t decompressedSize = sizeReader.ReadInt32();
        // IL_00d2-00d6: compressedTodo is totalLength - 14, in Int32 arithmetic.
        const std::int32_t compressedSize = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(header.totalLength) - 14u);
        // The file size fits in Int32 and is at least 14 (both checked above).
        const auto present = static_cast<std::int32_t>(file.size() - 14u);
        if (header.compression == XnbCompression::Lzx)
        {
            XnbLzxDecodeResult decoded = DecodeXnbLzxPayload(
                file.data() + 14u, compressedSize, present, decompressedSize, origin, limits);
            return {header, std::move(decoded.bytes), std::move(decoded.failure)};
        }
        // MonoGame's LZ4 body has no Refresh counterpart; it stays an exact, eager decode. Only
        // an unchecked declaration (a stream that cannot seek) can name more than is present.
        if (compressedSize > present)
        {
            throw ContentLoadException(
                "'" + origin + "' declares more LZ4 payload (" + std::to_string(compressedSize) +
                " bytes) than its stream holds (" + std::to_string(present) + ").");
        }
        std::vector<std::uint8_t> body = DecompressXnbLz4Payload(
            file.data() + 14u, compressedSize, decompressedSize, origin, limits);
        return {header, std::move(body), nullptr};
    }
}
