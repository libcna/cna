// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/XnbDecompression.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

#include "CNA/Internal/Xnb/LzxDecoder.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/InvalidOperationException.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentLoadException;

    namespace
    {
        // Refresh DecompressStream raises one message for every decoder failure:
        // FrameworkResources.DecompressionError, which names neither the asset nor the cause.
        [[nodiscard]] std::exception_ptr DecompressionError()
        {
            return std::make_exception_ptr(
                System::InvalidOperationException("Error decompressing content data."));
        }

        // DecompressStream.CompressedBufferSize and DecompressedBufferSize: ReadBufferFromDisk
        // fetches at most this many compressed bytes, and one XnaNative Decompress call writes at
        // most this many decoded bytes.
        constexpr int64_t BufferSize = 0x10000;

        // The output size of a frame whose header does not name one.
        constexpr int DefaultFrameSize = 0x8000;

        enum class FrameStep
        {
            Decoded,     // the frame was decoded
            Incomplete,  // its bytes run past those fetched, and more may be fetched
            Ended,       // no frame can follow: an end marker, bytes that never come, CNA's bound
            Failed,      // the LZX decoder refused it
        };

        // The .xnb LZX framing: each frame is a big-endian block size, or 0xFF followed by a
        // big-endian frame size and block size, and then the block. One LzxDecoder decodes the
        // frames in order, its window and trees carrying over from frame to frame.
        class LzxFrames
        {
        public:
            LzxFrames(const uint8_t* data, const int64_t readable, const int64_t declaredEnd,
                      const std::string& path, const XnbReadLimits& limits)
                : compressed_(data, static_cast<int32_t>(readable), false),
                  declaredEnd_(declaredEnd), path_(path), limits_(limits),
                  decoder_(16) // The .xnb window is 64 KiB (window exponent 16).
            {
            }

            /// Decodes the next frame if its bytes end within the first @p fetched bytes.
            [[nodiscard]] FrameStep Decode(const int64_t fetched)
            {
                const int64_t left = fetched - next_;
                const FrameStep notYet =
                    fetched < declaredEnd_ ? FrameStep::Incomplete : FrameStep::Ended;
                if (left < 2) { return notYet; }
                compressed_.setPositionProperty(static_cast<int32_t>(next_));
                int hi = compressed_.ReadByte();
                int lo = compressed_.ReadByte();
                int header = 2;
                int blockSize = (hi << 8) | lo;
                int frameSize = DefaultFrameSize;
                if (hi == 0xFF)
                {
                    if (left < 5) { return notYet; }
                    hi = lo;
                    lo = compressed_.ReadByte();
                    frameSize = (hi << 8) | lo;
                    hi = compressed_.ReadByte();
                    lo = compressed_.ReadByte();
                    blockSize = (hi << 8) | lo;
                    header = 5;
                }
                if (blockSize == 0 || frameSize == 0) { return FrameStep::Ended; } // end marker
                if (left - header < blockSize) { return notYet; }
                // CNA's own bound: Refresh never holds the whole body, CNA does (see the header).
                if (frameSize > limits_.maxDecompressedSize - Decoded())
                {
                    endFailure_ = std::make_exception_ptr(ContentLoadException(
                        "'" + path_ + "' decompresses beyond the maximum of " +
                        std::to_string(limits_.maxDecompressedSize) + " bytes."));
                    return FrameStep::Ended;
                }
                if (decoder_.Decompress(compressed_, blockSize, decoded_, frameSize) != 0)
                {
                    return FrameStep::Failed;
                }
                next_ += header + blockSize;
                last_ = frameSize != DefaultFrameSize;
                return FrameStep::Decoded;
            }

            /// Where the next frame starts in the compressed bytes.
            [[nodiscard]] int64_t Next() const { return next_; }

            /// Whether the frame decoded last had an output size other than 32 KiB.
            [[nodiscard]] bool DecodedAShortFrame() const { return last_; }

            /// How many bytes the decoded frames hold.
            [[nodiscard]] int64_t Decoded() const { return decoded_.getLengthProperty(); }

            /// The failure CNA's bound recorded, or null.
            [[nodiscard]] std::exception_ptr EndFailure() const { return endFailure_; }

            /// The first @p count decoded bytes.
            [[nodiscard]] std::vector<uint8_t> Take(const int64_t count)
            {
                std::vector<uint8_t> bytes = decoded_.ToArray();
                bytes.resize(static_cast<std::size_t>(count));
                return bytes;
            }

        private:
            System::IO::MemoryStream compressed_;
            System::IO::MemoryStream decoded_;
            int64_t declaredEnd_;
            const std::string& path_;
            const XnbReadLimits& limits_;
            LzxDecoder decoder_;
            int64_t next_ = 0;
            bool last_ = false;
            std::exception_ptr endFailure_;
        };
    }

    XnbLzxDecodeResult DecodeXnbLzxPayload(
        const uint8_t* compressedData,
        int32_t compressedSize,
        int32_t availableSize,
        int32_t declaredSize,
        const std::string& path,
        const XnbReadLimits& limits)
    {
        // Refresh's DecompressStream, replayed over XnaNative.dll's Decompress as measured on
        // 4.0.30901.0 (see the header): what each DecompressNextBuffer serves, and where the
        // first failing one stops.
        XnbLzxDecodeResult result;
        // ReadNextBufferFromDisk IL_0000-0009: a compressedTodo of zero or less reads nothing.
        const int64_t todo = compressedSize > 0 ? compressedSize : 0;
        const int64_t present = availableSize > 0 ? availableSize : 0;
        LzxFrames frames(compressedData, std::min(todo, present), todo, path, limits);

        int64_t fetched = 0;  // compressed bytes ReadBufferFromDisk has fetched
        int64_t consumed = 0; // of those, the bytes the native decoder has taken
        int64_t served = 0;   // decoded bytes handed out in DecompressStream buffers
        int64_t decompressedTodo = declaredSize;
        bool ended = false;   // the native decoder will decode no further frame

        // One DecompressNextBuffer, made when the reader has used every byte served before it.
        // IL_0000-000a: none once decompressedTodo is spent -- the end, not a failure.
        while (!result.failure && decompressedTodo > 0)
        {
            int64_t produced = 0;
            do
            {
                // IL_000b-001f: the next chunk once the decoder has taken all of this one.
                if (consumed >= fetched && fetched < todo)
                {
                    const int64_t chunk = std::min(BufferSize, todo - fetched);
                    if (fetched + chunk > present)
                    {
                        // ReadBufferFromDisk IL_0023-0030: the stream ends inside the chunk.
                        result.failure = DecompressionError();
                        break;
                    }
                    fetched += chunk;
                }

                // One native Decompress call over the fetched bytes it has not taken yet. It
                // decodes whole frames while what it holds undelivered is at most one buffer,
                // takes the bytes of a frame it cannot finish yet, and decodes nothing after a
                // frame whose size is not 32 KiB (the last one).
                const int64_t consumedBefore = consumed;
                while (!ended && frames.Decoded() - served <= BufferSize)
                {
                    const FrameStep step = frames.Decode(fetched);
                    if (step == FrameStep::Decoded)
                    {
                        consumed = frames.Next();
                        ended = frames.DecodedAShortFrame();
                    }
                    else if (step == FrameStep::Incomplete)
                    {
                        consumed = fetched;
                        break;
                    }
                    else if (step == FrameStep::Failed)
                    {
                        // IL_0089-0095: the call fails, and everything it would have served --
                        // the good frames it decoded with the failing one -- is lost.
                        result.failure = DecompressionError();
                        break;
                    }
                    else
                    {
                        ended = true;
                    }
                }
                if (result.failure) { break; }

                produced = std::min(frames.Decoded() - served, BufferSize);
                served += produced;
                decompressedTodo -= produced;
                if (produced == 0 && consumed == consumedBefore)
                {
                    // IL_009c-00ad: a call that takes nothing and makes nothing.
                    result.failure = frames.EndFailure() ? frames.EndFailure() : DecompressionError();
                    break;
                }
            } while (produced == 0);
        }

        result.bytes = frames.Take(served);
        return result;
    }

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

        // Every frame in order until the declared size is reached, whatever its size.
        LzxFrames frames(compressedData, compressedSize, compressedSize, path, limits);
        while (frames.Decoded() < decompressedSize)
        {
            if (frames.Decode(compressedSize) != FrameStep::Decoded)
            {
                if (frames.EndFailure()) { std::rethrow_exception(frames.EndFailure()); }
                std::rethrow_exception(DecompressionError());
            }
        }
        if (frames.Decoded() != decompressedSize)
        {
            throw ContentLoadException(
                "LZX frame of '" + path + "' exceeds its declared range.");
        }
        return frames.Take(decompressedSize);
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
