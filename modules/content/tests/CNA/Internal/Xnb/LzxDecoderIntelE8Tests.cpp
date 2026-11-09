// SPDX-License-Identifier: MS-PL
// GAP-21: independent, hand-authored streams; no CNA encoder or CNA-generated expectations.
// Oracle: Microsoft [MS-PATCH] 2.2.2 (E8 reversal arithmetic), and regular LZX
// libmspack 55d501976171397ccd5d5a7a1ca7da065b1d9a06, lzxd_decompress's E8 stage.
// https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-patch/e6c8366d-a8c3-4f34-aa5d-56f44f938a09
// https://github.com/kyz/libmspack/blob/55d501976171397ccd5d5a7a1ca7da065b1d9a06/libmspack/mspack/lzxd.c
// At opcode position P, signed operand A is rewritten iff -P <= A < F:
// A >= 0 becomes A-P, A < 0 becomes A+F. Skip its four operand bytes, and
// inspect opcode positions strictly before frame_size-10, for frames 0..32767.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/LzxDecoder.hpp"
#include "CNA/Internal/Xnb/XnbDecompression.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/InvalidOperationException.hpp"

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    // LZX words are little endian on the wire, with bits consumed most significant first.
    class Bits
    {
    public:
        void Put(const std::uint32_t value, const int count)
        {
            for (int bit = count - 1; bit >= 0; --bit)
            {
                word_ = static_cast<std::uint16_t>((word_ << 1) | ((value >> bit) & 1u));
                if (++pending_ == 16)
                {
                    bytes_.push_back(static_cast<std::uint8_t>(word_));
                    bytes_.push_back(static_cast<std::uint8_t>(word_ >> 8));
                    word_ = 0;
                    pending_ = 0;
                }
            }
        }
        Bytes Finish()
        {
            if (pending_ != 0) { Put(0, 16 - pending_); }
            return bytes_;
        }
    private:
        Bytes bytes_;
        std::uint16_t word_ = 0;
        int pending_ = 0;
    };

    void Append(Bytes& destination, const Bytes& source)
    {
        destination.insert(destination.end(), source.begin(), source.end());
    }

    void Store32(Bytes& data, const std::size_t offset, const std::uint32_t value)
    {
        for (int byte = 0; byte < 4; ++byte)
            data[offset + static_cast<std::size_t>(byte)] = static_cast<std::uint8_t>(value >> (8 * byte));
    }

    void Call(Bytes& data, const std::size_t opcode, const std::uint32_t operand)
    {
        data[opcode] = 0xE8;
        Store32(data, opcode + 1, operand);
    }

    void Header(Bits& bits, const int type, const std::uint32_t length,
                const bool first, const std::uint32_t filesize, const bool intel = true)
    {
        if (first)
        {
            bits.Put(intel ? 1u : 0u, 1);
            if (intel) { bits.Put(filesize >> 16, 16); bits.Put(filesize & 0xFFFFu, 16); }
        }
        bits.Put(static_cast<std::uint32_t>(type), 3);
        bits.Put(length >> 8, 16);
        bits.Put(length & 0xFFu, 8);
    }

    Bytes UncompressedHeader(const std::uint32_t length, const bool first,
                             const std::uint32_t filesize, const bool intel = true,
                             const std::uint32_t r0 = 1)
    {
        Bits bits;
        Header(bits, 3, length, first, filesize, intel);
        Bytes block = bits.Finish();
        const std::size_t registers = block.size();
        block.resize(registers + 12, 0);
        Store32(block, registers, r0);
        Store32(block, registers + 4, 1);
        Store32(block, registers + 8, 1);
        return block;
    }

    Bytes Uncompressed(const Bytes& raw, const bool first = true,
                       const std::uint32_t filesize = 100, const bool intel = true,
                       const std::uint32_t r0 = 1)
    {
        Bytes block = UncompressedHeader(static_cast<std::uint32_t>(raw.size()), first, filesize, intel, r0);
        Append(block, raw);
        // All new blocks in these fixtures follow even-length uncompressed blocks.
        return block;
    }

    Bytes Frame(const Bytes& block, const std::uint16_t length)
    {
        Bytes frame{0xFF, static_cast<std::uint8_t>(length >> 8), static_cast<std::uint8_t>(length),
                    static_cast<std::uint8_t>(block.size() >> 8), static_cast<std::uint8_t>(block.size())};
        Append(frame, block);
        return frame;
    }

    Bytes Decode(const Bytes& payload, const std::int32_t length)
    {
        return CNA::Internal::Xnb::DecompressXnbPayload(
            payload.data(), static_cast<std::int32_t>(payload.size()), length, "intel-e8.xnb");
    }

    // First compressed block only: all main-tree lengths initially zero.
    // Pretree symbols 0,8,16,18 have length 2, hence codes 00,01,10,11.
    // Delta 8 gives length 9; delta 16 gives length 1; delta 0 leaves zero.
    void Lengths(Bits& bits, const std::vector<int>& lengths)
    {
        for (int symbol = 0; symbol < 20; ++symbol)
            bits.Put(symbol == 0 || symbol == 8 || symbol == 16 || symbol == 18 ? 2u : 0u, 4);
        for (const int length : lengths) { bits.Put(length == 9 ? 1u : length == 1 ? 2u : 0u, 2); }
    }

    // A complete 512-symbol main tree with length 9 means canonical code == symbol.
    // The length tree is empty (no extended matches); aligned codes are 000..111.
    // literalOnly instead uses just 0x90/0x91 at length 1, so E8 is absent from its tree.
    Bytes Compressed(const int type, const std::uint32_t length, const bool first,
                     const std::uint32_t filesize, const std::vector<int>& symbols,
                     const bool literalOnly = false)
    {
        Bits bits;
        Header(bits, type, length, first, filesize);
        if (type == 2) { for (int i = 0; i < 8; ++i) { bits.Put(3, 3); } }
        std::vector<int> low(256, literalOnly ? 0 : 9);
        std::vector<int> high(256, literalOnly ? 0 : 9);
        if (literalOnly) { low[0x90] = low[0x91] = 1; }
        Lengths(bits, low);
        Lengths(bits, high);
        Lengths(bits, std::vector<int>(249, 0));
        for (const int symbol : symbols)
            bits.Put(static_cast<std::uint32_t>(literalOnly ? symbol - 0x90 : symbol), literalOnly ? 1 : 9);
        Bytes block = bits.Finish();
        block.insert(block.end(), 4, 0); // word lookahead padding, never output
        return block;
    }

    std::vector<int> Literals(const Bytes& bytes) { return {bytes.begin(), bytes.end()}; }
}

TEST(LzxDecoderIntelE8Test, SignedOperandsRespectBothRangeEndpoints)
{
    struct Case { std::uint32_t filesize, operand, expected; };
    const Case cases[]{
        {100, 25, 17},                    // 25 - P(8) = 17
        {100, 0, 0xFFFFFFF8u},            // 0 - 8 = -8
        {100, 0xFFFFFFFFu, 99},           // -1 + 100 = 99
        {100, 0xFFFFFFF8u, 92},           // A == -P: -8 + 100 = 92
        {100, 0xFFFFFFF7u, 0xFFFFFFF7u},   // A == -P-1: unchanged
        {100, 99, 91},                    // F-1 - 8 = 91
        {100, 100, 100},                  // A == F: unchanged
        {100, 0x80000000u, 0x80000000u},   // INT32_MIN is below -P
        {100, 0x7FFFFFFFu, 0x7FFFFFFFu},   // INT32_MAX is above F
        {0x7FFFFFFFu, 0x7FFFFFFEu, 0x7FFFFFF6u}, // (INT32_MAX-1) - 8
        {0x00010020u, 0x00010010u, 0x00010008u}, // high and low filesize words matter
    };
    for (const auto& c : cases)
    {
        SCOPED_TRACE(c.operand);
        Bytes raw(32, 0x90);
        Call(raw, 8, c.operand);
        Bytes expected = raw;
        Store32(expected, 9, c.expected);
        EXPECT_EQ(Decode(Frame(Uncompressed(raw, true, c.filesize), 32), 32), expected);
    }
}

TEST(LzxDecoderIntelE8Test, AbsentIntelFlagAndZeroFilesizePreserveOperands)
{
    Bytes raw(32, 0x90);
    Call(raw, 8, 25);
    EXPECT_EQ(Decode(Frame(Uncompressed(raw, true, 100, false), 32), 32), raw);
    EXPECT_EQ(Decode(Frame(Uncompressed(raw, true, 0), 32), 32), raw);
}

TEST(LzxDecoderIntelE8Test, EnabledFrameWithoutE8IsAccepted)
{
    const Bytes raw(32, 0x90);
    EXPECT_EQ(Decode(Frame(Uncompressed(raw), 32), 32), raw);
}

TEST(LzxDecoderIntelE8Test, ShortFramesAndTailOpcodesAreNotScanned)
{
    for (int length = 1; length <= 10; ++length)
    {
        SCOPED_TRACE(length);
        const Bytes raw(static_cast<std::size_t>(length), 0xE8);
        EXPECT_EQ(Decode(Frame(Uncompressed(raw), static_cast<std::uint16_t>(length)), length), raw);
    }
    for (int length = 11; length <= 16; ++length)
    {
        SCOPED_TRACE(length);
        Bytes raw(static_cast<std::size_t>(length), 0x90);
        Call(raw, static_cast<std::size_t>(length - 10), 25); // first excluded opcode position
        raw.back() = 0xE8; // incomplete tail operand must never be inspected
        EXPECT_EQ(Decode(Frame(Uncompressed(raw), static_cast<std::uint16_t>(length)), length), raw);
    }
}

TEST(LzxDecoderIntelE8Test, OpcodeImmediatelyBeforeTailIsTranslated)
{
    Bytes raw(16, 0x90);
    Call(raw, 5, 25); // 16-11 is the final eligible opcode position
    Bytes expected = raw;
    Store32(expected, 6, 20); // 25 - 5 = 20
    EXPECT_EQ(Decode(Frame(Uncompressed(raw), 16), 16), expected);
}

TEST(LzxDecoderIntelE8Test, OperandBytesContainingE8AreSkipped)
{
    Bytes raw(32, 0);
    Call(raw, 1, 0x0000E8E9u);
    // Zero bytes after the first operand make its embedded E8 a live candidate if rescanned.
    Call(raw, 12, 100);
    Bytes expected = raw;
    Store32(expected, 2, 0x0000E8E8u); // 59625 - 1 = 59624; two embedded E8 bytes survive
    Store32(expected, 13, 88);         // next instruction: 100 - 12 = 88
    EXPECT_EQ(Decode(Frame(Uncompressed(raw, true, 100000), 32), 32), expected);
}

TEST(LzxDecoderIntelE8Test, VerbatimAndAlignedLiteralBlocksActivateTranslation)
{
    Bytes raw(32, 0x90);
    Call(raw, 8, 100);
    Bytes expected = raw;
    Store32(expected, 9, 92); // P=8, A=100, F=200: 100-8=92
    for (const int type : {1, 2})
    {
        SCOPED_TRACE(type);
        EXPECT_EQ(Decode(Frame(Compressed(type, 32, true, 200, Literals(raw)), 32), 32), expected);
    }
}

TEST(LzxDecoderIntelE8Test, CursorAdvancesBeforeTranslationHasStarted)
{
    // No E8 code in the first main tree: intel_started remains false for its 16 bytes.
    const Bytes prefix(16, 0x90);
    Bytes raw(32, 0x90);
    Call(raw, 8, 100);
    Bytes payload = Frame(Compressed(1, 16, true, 200, Literals(prefix), true), 16);
    Append(payload, Frame(Uncompressed(raw, false), 32));
    Bytes expected = prefix;
    Store32(raw, 9, 76); // P=16+8=24: 100-24=76
    Append(expected, raw);
    EXPECT_EQ(Decode(payload, 48), expected);
}

TEST(LzxDecoderIntelE8Test, CursorAdvancesAcrossNoE8AndShortFrames)
{
    // One uncompressed block spans four XNB frames. Prefix lengths 32768 and 6 both count.
    const Bytes prefix(32768, 0x90);
    Bytes shortFrame(6, 0x90);
    shortFrame.back() = 0xE8;
    Bytes third(32, 0x90), fourth(32, 0x90);
    Call(third, 8, 40000);
    Call(fourth, 8, 0xFFFF7FD2u); // -32814 == -(32768+6+32+8)
    Bytes first = UncompressedHeader(32838, true, 100000);
    Append(first, prefix);
    Bytes payload = Frame(first, 32768);
    Append(payload, Frame(shortFrame, 6));
    Append(payload, Frame(third, 32));
    Append(payload, Frame(fourth, 32));
    Bytes expected = prefix;
    Append(expected, shortFrame);
    Store32(third, 9, 7218);   // 40000 - (32768+6+8) = 7218
    Store32(fourth, 9, 67186); // -32814 + 100000 = 67186
    Append(expected, third);
    Append(expected, fourth);
    EXPECT_EQ(Decode(payload, 32838), expected);
}

TEST(LzxDecoderIntelE8Test, CursorDoesNotResetWhenTheWindowWraps)
{
    Bytes first(32768, 0x90), second(32768, 0x90), third(32, 0x90);
    Call(first, 8, 100000);
    Call(second, 8, 100000);
    Call(third, 8, 100000);
    Bytes block = UncompressedHeader(65568, true, 200000);
    Append(block, first);
    Bytes payload = Frame(block, 32768);
    Append(payload, Frame(second, 32768));
    Append(payload, Frame(third, 32));
    Store32(first, 9, 99992);  // 100000 - 8
    Store32(second, 9, 67224); // 100000 - (32768+8)
    Store32(third, 9, 34456);  // 100000 - (65536+8), although dictionary offset is 8 again
    Bytes expected = first;
    Append(expected, second);
    Append(expected, third);
    EXPECT_EQ(Decode(payload, 65568), expected);
}

TEST(LzxDecoderIntelE8Test, MultipleBlocksWithinOneFrameAreTranslatedOnce)
{
    Bytes raw(16, 0x90);
    Call(raw, 1, 100);
    Bytes block = Uncompressed(raw, true, 200);
    Append(block, Uncompressed(raw, false));
    Bytes expected = raw;
    Append(expected, raw);
    Store32(expected, 2, 99);  // first opcode P=1
    Store32(expected, 18, 83); // second opcode P=16+1
    EXPECT_EQ(Decode(Frame(block, 32), 32), expected);
}

TEST(LzxDecoderIntelE8Test, TranslatedOutputDoesNotModifyTheMatchDictionary)
{
    Bytes raw(32, 0x90);
    Call(raw, 1, 100);
    Bytes payload = Frame(Uncompressed(raw, true, 200, true, 32), 32);
    // Four length-8 matches at repeated offset R0=32: symbol 256+6, no footer.
    Append(payload, Frame(Compressed(1, 32, false, 200, {262, 262, 262, 262}), 32));
    Bytes expected = raw;
    Append(expected, raw);
    Store32(expected, 2, 99);  // first opcode P=1: 100-1
    Store32(expected, 34, 67); // copied ORIGINAL A=100 at P=33: 100-33; in-place rewriting gives 66
    EXPECT_EQ(Decode(payload, 64), expected);
}

TEST(LzxDecoderIntelE8Test, TranslationStopsAfterThe32768thFrame)
{
    // Many small frames exercise the counter without a 1 GiB fixture; XNB explicit sizes and
    // LzxDecoder both accept these sizes. This is a decoder frame-count witness, not an XNA
    // native-oracle claim about nonfinal short chunks.
    Bytes payload;
    Bytes expected(32766, 0x90);
    Bytes first = UncompressedHeader(32814, true, 100000);
    first.push_back(0x90);
    Append(payload, Frame(first, 1));
    for (int frame = 1; frame < 32766; ++frame) { Append(payload, Frame(Bytes{0x90}, 1)); }
    Bytes raw(16, 0x90);
    Call(raw, 1, 50000);
    for (int frame = 0; frame < 3; ++frame) { Append(payload, Frame(raw, 16)); Append(expected, raw); }
    Store32(expected, 32768, 17233); // index 32766: P=32767, 50000-32767=17233
    Store32(expected, 32784, 17217); // index 32767: P=32783, 50000-32783=17217
    // Index 32768 retains 50000, with all bytes checked by vector equality.
    EXPECT_EQ(Decode(payload, 32814), expected);
}

TEST(LzxDecoderIntelE8Test, AppendsTranslatedBytesAtCurrentOutputPosition)
{
    Bytes raw(32, 0x90);
    Call(raw, 1, 100);
    const Bytes block = Uncompressed(raw, true, 200);
    System::IO::MemoryStream input(block.data(), static_cast<std::int32_t>(block.size()));
    System::IO::MemoryStream output;
    const Bytes prefix{0xE8, 1, 2, 3, 4, 5, 6};
    output.Write(prefix.data(), 0, static_cast<int>(prefix.size()));
    CNA::Internal::Xnb::LzxDecoder decoder(16);
    ASSERT_EQ(decoder.Decompress(input, static_cast<int>(block.size()), output, 32), 0);
    Store32(raw, 2, 99); // P=1, independent of output's preexisting seven bytes
    Bytes expected = prefix;
    Append(expected, raw);
    EXPECT_EQ(output.ToArray(), expected);
    EXPECT_EQ(output.getPositionProperty(), 39);
}

// HND-TQ-16: every truncation is Refresh DecompressNextBuffer's DecompressionError, an
// InvalidOperationException (IL_0089-0095, IL_009c-00ad).
TEST(LzxDecoderIntelE8Test, TruncatedIntelFramesAreRefused)
{
    Bytes raw(32, 0x90);
    Call(raw, 8, 25);
    const Bytes payload = Frame(Uncompressed(raw), 32);
    Bytes expected = raw;
    Store32(expected, 9, 17);
    ASSERT_EQ(Decode(payload, 32), expected); // readable positive control
    for (std::size_t cut = 0; cut < payload.size(); ++cut)
    {
        SCOPED_TRACE(cut);
        Bytes truncated(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(cut));
        EXPECT_THROW((void)Decode(truncated, 32), System::InvalidOperationException);
        if (cut >= 5)
        {
            // Also reduce the outer compressed-size field so the inner decoder sees the
            // truncation (Intel header, block header, repeated offsets, or raw operand bytes).
            truncated[3] = static_cast<std::uint8_t>((cut - 5) >> 8);
            truncated[4] = static_cast<std::uint8_t>(cut - 5);
            EXPECT_THROW((void)Decode(truncated, 32), System::InvalidOperationException);
        }
    }
}
