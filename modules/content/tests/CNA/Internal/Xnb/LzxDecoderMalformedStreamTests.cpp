// SPDX-License-Identifier: MS-PL
//
// TQ-018: hand-built LZX streams that reach the decoder states a corrupt or hostile .xnb can
// reach. LzxDecoderTests.cpp says valid LZX cannot be authored by hand; these streams are written
// bit by bit instead (LzxBitWriter below), and each refusal has a positive control built by the
// same writer that decodes to hand-derived bytes, so a refusal cannot pass merely because the
// stream is unreadable.
//
// Oracles: FNA src/Content/LzxDecoder.cs, whose window is a bounds-checked C# array; libmspack's
// lzxd.c (lzxd_decompress), which FNA's port follows and which refuses a match that runs over the
// window end; and Refresh DecompressStream.DecompressNextBuffer IL_0089-0095, whose decoder
// failure is InvalidOperationException (HND-TQ-16): DecompressXnbPayload() throws it, and the
// container keeps it for the read that reaches the failing frame (XnbContainerBody.hpp).

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbContainerBody.hpp"
#include "CNA/Internal/Xnb/XnbDecompression.hpp"
#include "System/IO/Stream.hpp"
#include "System/InvalidOperationException.hpp"

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    /// MSB-first bits in 16-bit words, each word stored low byte first: what LzxDecoder's
    /// BitBuffer::EnsureBits reads (FNA LzxDecoder.cs BitBuffer.EnsureBits).
    class LzxBitWriter
    {
    public:
        void Bits(const std::uint32_t value, const int count)
        {
            for (int bit = count - 1; bit >= 0; --bit)
            {
                word_ = static_cast<std::uint16_t>((word_ << 1) | ((value >> bit) & 1u));
                if (++pending_ == 16)
                {
                    bytes_.push_back(static_cast<std::uint8_t>(word_ & 0xFFu));
                    bytes_.push_back(static_cast<std::uint8_t>(word_ >> 8));
                    word_ = 0;
                    pending_ = 0;
                }
            }
        }

        Bytes Finish()
        {
            if (pending_ != 0) { Bits(0, 16 - pending_); }
            return bytes_;
        }

    private:
        Bytes bytes_;
        std::uint16_t word_ = 0;
        int pending_ = 0;
    };

    /// One XNB LZX frame with an explicit output size: 0xFF, frame size, block size (big endian).
    Bytes ExplicitFrame(const std::uint16_t frameSize, const Bytes& block)
    {
        Bytes frame{0xFF, static_cast<std::uint8_t>(frameSize >> 8), static_cast<std::uint8_t>(frameSize),
                    static_cast<std::uint8_t>(block.size() >> 8), static_cast<std::uint8_t>(block.size())};
        frame.insert(frame.end(), block.begin(), block.end());
        return frame;
    }

    /// One XNB LZX frame of the default 0x8000 output bytes: block size (big endian), block.
    Bytes DefaultFrame(const Bytes& block)
    {
        Bytes frame{static_cast<std::uint8_t>(block.size() >> 8), static_cast<std::uint8_t>(block.size())};
        frame.insert(frame.end(), block.begin(), block.end());
        return frame;
    }

    /// A verbatim block of length 0xFFFF decoded as one 0xFFFF-byte frame.
    ///
    /// Pretree: symbols 15..18 at length 2, canonical codes 00, 01, 10, 11. Main tree: match
    /// symbol 263 = 256 + (position slot 0 << 3 | length header 7) at length 1 (code 0), literals
    /// 'A' (65) and 'B' (66) at length 2 (codes 10, 11). Length tree: footers 247 and 248 at
    /// length 1 (codes 0, 1). Slot 0 repeats R0, which starts at 1, so every match copies the
    /// previous byte: 7 + 248 + 2 = 257 bytes. Output: 'A', 254 matches (65278 more 'A'), then
    /// @p tailLiterals x 'B', then optionally one more match (of 'B').
    Bytes VerbatimRunBlock(const int tailLiterals, const bool finalMatch)
    {
        LzxBitWriter w;
        w.Bits(0, 1);        // no Intel E8 translation header
        w.Bits(1, 3);        // verbatim block
        w.Bits(0x00FF, 16);  // block length 0xFFFF, high 16 bits
        w.Bits(0xFF, 8);     //                      low 8 bits
        const auto pretree = [&w] {
            for (int symbol = 0; symbol < 20; ++symbol) { w.Bits(symbol >= 15 && symbol <= 18 ? 2u : 0u, 4); }
        };
        const auto zeros18 = [&w](const int n) { w.Bits(0b11, 2); w.Bits(static_cast<std::uint32_t>(n - 20), 5); };
        const auto zeros17 = [&w](const int n) { w.Bits(0b10, 2); w.Bits(static_cast<std::uint32_t>(n - 4), 4); };
        const auto lengthOne = [&w] { w.Bits(0b01, 2); }; // pretree 16: (0 - 16) mod 17 = 1
        const auto lengthTwo = [&w] { w.Bits(0b00, 2); }; // pretree 15: (0 - 15) mod 17 = 2
        // Main tree 0..255: 65 zeros, 'A' = 2, 'B' = 2, 189 zeros.
        pretree(); zeros18(51); zeros17(14); lengthTwo(); lengthTwo(); zeros18(51); zeros18(51); zeros18(51);
        zeros18(36);
        // Main tree 256..511: 7 zeros, 263 = 1, 248 zeros.
        pretree(); zeros17(7); lengthOne(); zeros18(51); zeros18(51); zeros18(51); zeros18(51); zeros18(44);
        // Length tree 0..248: 247 zeros, 247 = 1, 248 = 1.
        pretree(); zeros18(51); zeros18(51); zeros18(51); zeros18(51); zeros18(43); lengthOne(); lengthOne();
        w.Bits(0b10, 2);                                                       // 'A'
        for (int match = 0; match < 254; ++match) { w.Bits(0, 1); w.Bits(1, 1); } // 263, footer 248
        for (int literal = 0; literal < tailLiterals; ++literal) { w.Bits(0b11, 2); } // 'B'
        if (finalMatch) { w.Bits(0, 1); w.Bits(1, 1); }
        Bytes block = w.Finish();
        block.insert(block.end(), 4, 0);
        return block;
    }

    /// A 16-byte verbatim block whose first four main-tree lengths come from one pretree run
    /// (symbol 19, run of 4) with delta symbol @p deltaSymbol: length = (0 - delta) mod 17.
    /// Pretree: symbols 0, 15, 18 and 19 at length 2 (codes 00, 01, 10, 11). Data: 16 x symbol 3.
    Bytes LengthDeltaBlock(const int deltaSymbol)
    {
        const auto code = [](const int symbol) -> std::uint32_t {
            return symbol == 0 ? 0b00u : symbol == 15 ? 0b01u : symbol == 18 ? 0b10u : 0b11u;
        };
        LzxBitWriter w;
        w.Bits(0, 1);
        w.Bits(1, 3);
        w.Bits(0, 16);
        w.Bits(16, 8);       // block length 16
        const auto pretree = [&w] {
            for (int symbol = 0; symbol < 20; ++symbol)
            {
                w.Bits(symbol == 0 || symbol == 15 || symbol == 18 || symbol == 19 ? 2u : 0u, 4);
            }
        };
        const auto zeros18 = [&](const int n) { w.Bits(code(18), 2); w.Bits(static_cast<std::uint32_t>(n - 20), 5); };
        // Main tree 0..255: the run of four, then 252 zeros.
        pretree();
        w.Bits(code(19), 2); w.Bits(0, 1); w.Bits(code(deltaSymbol), 2);
        zeros18(51); zeros18(51); zeros18(51); zeros18(51); zeros18(48);
        // Main tree 256..511 and length tree 0..248: zeros.
        pretree(); zeros18(51); zeros18(51); zeros18(51); zeros18(51); zeros18(32); zeros18(20);
        pretree(); zeros18(51); zeros18(51); zeros18(51); zeros18(51); zeros18(45);
        for (int literal = 0; literal < 16; ++literal) { w.Bits(0b11, 2); }
        Bytes block = w.Finish();
        block.insert(block.end(), 4, 0);
        return block;
    }

    /// The header of an uncompressed LZX block (with the Intel bit when it opens the stream),
    /// followed by R0..R2 = 1.
    Bytes UncompressedBlockHeader(const std::uint32_t length, const bool first)
    {
        LzxBitWriter w;
        if (first) { w.Bits(0, 1); }
        w.Bits(3, 3);
        w.Bits(length >> 8, 16);
        w.Bits(length & 0xFFu, 8);
        Bytes header = w.Finish();
        for (int reg = 0; reg < 3; ++reg) { header.insert(header.end(), {1, 0, 0, 0}); }
        return header;
    }

    Bytes Repeat(const std::uint8_t value, const std::size_t count) { return Bytes(count, value); }

    Bytes Concat(std::initializer_list<Bytes> parts)
    {
        Bytes out;
        for (const Bytes& part : parts) { out.insert(out.end(), part.begin(), part.end()); }
        return out;
    }

    Bytes Decompress(const Bytes& payload, const std::int32_t decompressedSize)
    {
        return CNA::Internal::Xnb::DecompressXnbPayload(
            payload.data(), static_cast<std::int32_t>(payload.size()), decompressedSize, "malformed.xnb");
    }

    /// A whole LZX-compressed .xnb around @p payload.
    Bytes LzxXnb(const Bytes& payload, const std::int32_t decompressedSize)
    {
        Bytes file{'X', 'N', 'B', 'w', 5, 0x80, 0, 0, 0, 0, 0, 0, 0, 0};
        const auto total = static_cast<std::uint32_t>(14 + payload.size());
        for (int i = 0; i < 4; ++i)
        {
            file[6 + i] = static_cast<std::uint8_t>(total >> (8 * i));
            file[10 + i] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(decompressedSize) >> (8 * i));
        }
        file.insert(file.end(), payload.begin(), payload.end());
        return file;
    }
}

TEST(LzxDecoderMalformedStreamTest, HandBuiltVerbatimBlockDecodesToItsHandDerivedBytes)
{
    // 'A' then 254 matches of 257 (R0 = 1 copies the previous byte) = 1 + 65278 = 65279 x 'A',
    // then 256 x 'B': 65535 bytes. The 'B's start exactly at 65279, so a decoder whose match
    // length or window position is off by one byte moves the boundary.
    const Bytes decoded = Decompress(ExplicitFrame(0xFFFF, VerbatimRunBlock(256, false)), 0xFFFF);
    ASSERT_EQ(decoded.size(), 0xFFFFu);
    EXPECT_EQ(std::count(decoded.begin(), decoded.begin() + 65279, std::uint8_t{'A'}), 65279);
    EXPECT_EQ(std::count(decoded.begin() + 65279, decoded.end(), std::uint8_t{'B'}), 256);
}

TEST(LzxDecoderMalformedStreamTest, MatchRunningPastTheWindowEndIsRefused)
{
    // 'A' + 254 x 257 + 255 x 'B' leaves one byte of the 0xFFFF-byte run at window position
    // 65534 of 65536; the next symbol is a 257-byte match, which would write bytes 65536..65790.
    // FNA's array store throws there and libmspack refuses the match; before the fix the decoder
    // wrote 255 bytes past its window (ASan: heap-buffer-overflow WRITE) and returned success.
    const Bytes payload = ExplicitFrame(0xFFFF, VerbatimRunBlock(255, true));
    EXPECT_THROW((void)Decompress(payload, 0xFFFF), System::InvalidOperationException);
    // The container decodes nothing before the failing frame, so the first read meets it.
    const auto container =
        CNA::Internal::Xnb::DecodeXnbContainerBody(LzxXnb(payload, 0xFFFF), "malformed.xnb");
    EXPECT_TRUE(container.Bytes().empty());
    std::uint8_t byte = 0;
    EXPECT_THROW((void)container.OpenBodyStream()->Read(&byte, 0, 1), System::InvalidOperationException);
}

TEST(LzxDecoderMalformedStreamTest, HandBuiltLengthDeltaBlockDecodesToItsHandDerivedBytes)
{
    // Delta symbol 15: (0 - 15) mod 17 = 2, so symbols 0..3 get length 2 (codes 00..11) and the
    // sixteen code-11 symbols decode to sixteen bytes of 3.
    EXPECT_EQ(Decompress(ExplicitFrame(16, LengthDeltaBlock(15)), 16), Repeat(3, 16));
}

TEST(LzxDecoderMalformedStreamTest, CodeLengthDeltaOutsideZeroToSixteenIsRefused)
{
    // Delta symbol 19: (0 - 19) + 17 = -2, which no encoder writes (code lengths are 0..16).
    // Stored as uint8_t it became 254, and decoding then shifted the 32-bit bit buffer by 254
    // (UBSan: shift exponent too large) instead of refusing the block.
    EXPECT_THROW((void)Decompress(ExplicitFrame(16, LengthDeltaBlock(19)), 16),
                 System::InvalidOperationException);
}

TEST(LzxDecoderMalformedStreamTest, HandBuiltUncompressedFramesDecodeToTheirBytes)
{
    // One 0x10000-byte uncompressed block in two default 0x8000-byte frames.
    const Bytes first = Concat({UncompressedBlockHeader(0x10000, true), Repeat(0x11, 0x8000)});
    const Bytes second = Repeat(0x22, 0x8000);
    const Bytes decoded = Decompress(Concat({DefaultFrame(first), DefaultFrame(second)}), 0x10000);
    ASSERT_EQ(decoded.size(), 0x10000u);
    EXPECT_EQ(std::count(decoded.begin(), decoded.begin() + 0x8000, std::uint8_t{0x11}), 0x8000);
    EXPECT_EQ(std::count(decoded.begin() + 0x8000, decoded.end(), std::uint8_t{0x22}), 0x8000);
}

TEST(LzxDecoderMalformedStreamTest, FrameWhoseRunsWrapTheWindowIsRefusedAsInvalidOperation)
{
    // Frame 1 (0xFF00 bytes) leaves 0x100 bytes of block A; frame 2 (0x8000 bytes) takes them
    // (window 0xFF00..0x10000), then block B wraps to window 0 and ends at 0x7F00, so the frame
    // starts at 0x7F00 - 0x8000 < 0. Before the fix MemoryStream::Write threw
    // System::ArgumentOutOfRangeException through DecompressXnbPayload.
    const Bytes first = Concat({UncompressedBlockHeader(0x10000, true), Repeat(0x11, 0xFF00)});
    const Bytes second =
        Concat({Repeat(0x22, 0x100), UncompressedBlockHeader(0x7F00, false), Repeat(0x33, 0x7F00)});
    const Bytes payload = Concat({ExplicitFrame(0xFF00, first), DefaultFrame(second)});
    EXPECT_THROW((void)Decompress(payload, 0xFF00 + 0x8000), System::InvalidOperationException);
}
