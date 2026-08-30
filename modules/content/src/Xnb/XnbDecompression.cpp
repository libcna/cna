// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/XnbDecompression.hpp"

#include <cstring>
#include <limits>

#include "CNA/Internal/Xnb/LzxDecoder.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "System/IO/MemoryStream.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentLoadException;

    std::vector<uint8_t> DecompressXnbPayload(
        const uint8_t* compressedData,
        int32_t compressedSize,
        int32_t decompressedSize,
        const std::string& path,
        const XnbReadLimits& limits)
    {
        if (compressedSize < 0 || compressedSize > limits.maxFileSize)
        {
            throw ContentLoadException(
                "'" + path + "' has an invalid compressed payload size (" +
                std::to_string(compressedSize) + ").");
        }
        if (decompressedSize < 0 || decompressedSize > limits.maxDecompressedSize)
        {
            throw ContentLoadException(
                "'" + path + "' has an invalid decompressed size (" +
                std::to_string(decompressedSize) + ").");
        }

        System::IO::MemoryStream compressedStream(compressedData, compressedSize);
        System::IO::MemoryStream decompressedStream;

        // Default window size for XNB-encoded files is 64KB (window exponent 16).
        LzxDecoder dec(16);
        int64_t pos = 0;

        while (pos < compressedSize)
        {
            /* The compressed stream is separated into blocks that will decompress into 32KB or
             * some other size if specified. Normal, 32KB output blocks will have a short
             * indicating the size of the block before the block starts. Blocks that have a
             * defined output will be preceded by a byte of value 0xFF (255), then a short
             * indicating the output size and another for the block size. All shorts for these
             * cases are encoded in big-endian order. */
            int hi = compressedStream.ReadByte();
            int lo = compressedStream.ReadByte();
            int block_size = (hi << 8) | lo;
            int frame_size = 0x8000; // Frame size is 32KB by default.
            if (hi == 0xFF)
            {
                hi = lo;
                lo = static_cast<uint8_t>(compressedStream.ReadByte());
                frame_size = (hi << 8) | lo;
                hi = static_cast<uint8_t>(compressedStream.ReadByte());
                lo = static_cast<uint8_t>(compressedStream.ReadByte());
                block_size = (hi << 8) | lo;
                pos += 5;
            }
            else
            {
                pos += 2;
            }

            if (block_size == 0 || frame_size == 0)
            {
                break;
            }

            if (dec.Decompress(compressedStream, block_size, decompressedStream, frame_size) != 0)
            {
                throw ContentLoadException("Decompression of '" + path + "' failed.");
            }
            pos += block_size;

            // Reset the position of the input just in case the bit buffer read in some unused bytes.
            compressedStream.setPositionProperty(static_cast<int32_t>(pos));
        }

        if (decompressedStream.getPositionProperty() != decompressedSize)
        {
            throw ContentLoadException("Decompression of '" + path + "' failed.");
        }

        return decompressedStream.ToArray();
    }

    std::vector<uint8_t> DecompressXnbLz4Payload(
        const uint8_t* compressedData,
        int32_t compressedSize,
        int32_t decompressedSize,
        const std::string& path,
        const XnbReadLimits& limits)
    {
        const auto fail = [&path](const std::string& problem) -> void
        {
            throw ContentLoadException(
                "LZ4 decompression of '" + path + "' failed: " + problem + ".");
        };
        if (compressedSize <= 0 || compressedSize > limits.maxFileSize)
        {
            fail("invalid compressed payload size " + std::to_string(compressedSize));
        }
        if (decompressedSize < 0 || decompressedSize > limits.maxDecompressedSize)
        {
            fail("invalid decompressed size " + std::to_string(decompressedSize));
        }
        if (compressedData == nullptr) { fail("null compressed payload"); }

        const std::size_t inputSize = static_cast<std::size_t>(compressedSize);
        const std::size_t outputSize = static_cast<std::size_t>(decompressedSize);
        std::vector<uint8_t> output(outputSize);
        std::size_t inputPosition = 0u;
        std::size_t outputPosition = 0u;

        const auto readLength = [&](std::size_t base, const char* kind)
        {
            std::size_t length = base;
            if (base != 15u) { return length; }
            while (true)
            {
                if (inputPosition >= inputSize)
                {
                    fail(std::string("truncated ") + kind + " length");
                }
                const std::size_t extension = compressedData[inputPosition++];
                if (length > std::numeric_limits<std::size_t>::max() - extension)
                {
                    fail(std::string(kind) + " length overflow");
                }
                length += extension;
                if (extension != 255u) { return length; }
            }
        };

        while (inputPosition < inputSize)
        {
            const std::uint8_t token = compressedData[inputPosition++];
            const std::size_t literalLength = readLength(token >> 4u, "literal");
            if (literalLength > inputSize - inputPosition)
            {
                fail("literal run exceeds the compressed payload");
            }
            if (literalLength > outputSize - outputPosition)
            {
                fail("literal run exceeds the declared output size");
            }
            if (literalLength != 0u)
            {
                std::memcpy(output.data() + outputPosition,
                            compressedData + inputPosition, literalLength);
                inputPosition += literalLength;
                outputPosition += literalLength;
            }

            // A raw LZ4 block's final sequence consists only of literals.
            if (inputPosition == inputSize) { break; }
            if (inputSize - inputPosition < 2u) { fail("truncated match offset"); }
            const std::size_t matchOffset =
                static_cast<std::size_t>(compressedData[inputPosition]) |
                (static_cast<std::size_t>(compressedData[inputPosition + 1u]) << 8u);
            inputPosition += 2u;
            if (matchOffset == 0u) { fail("zero match offset"); }
            if (matchOffset > outputPosition)
            {
                fail("match offset precedes the decompressed history");
            }

            const std::size_t encodedMatchLength = readLength(token & 0x0Fu, "match");
            if (encodedMatchLength > std::numeric_limits<std::size_t>::max() - 4u)
            {
                fail("match length overflow");
            }
            const std::size_t matchLength = encodedMatchLength + 4u;
            if (matchLength > outputSize - outputPosition)
            {
                fail("match exceeds the declared output size");
            }
            for (std::size_t index = 0u; index < matchLength; ++index)
            {
                output[outputPosition] = output[outputPosition - matchOffset];
                ++outputPosition;
            }
        }

        if (inputPosition != inputSize || outputPosition != outputSize)
        {
            fail("decoded size " + std::to_string(outputPosition) +
                 " does not match declared size " + std::to_string(outputSize));
        }
        return output;
    }
}
