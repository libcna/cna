// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/XnbDecompression.hpp"

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
}
