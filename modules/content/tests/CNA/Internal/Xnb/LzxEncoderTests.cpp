// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-81: CNA's LZX encoder.
//
// Three independent oracles judge it, and they are deliberately independent:
//
//   * `DecompressXnbPayload` -- CNA's own LZX decoder, a port of an implementation written years
//     before this encoder and shipped against real Microsoft-produced files. A round trip through
//     it is the primary correctness check.
//   * `tools/xnb/xnb_conformance.py` -- a second LZX decoder in a second language, sharing no
//     code, constants or tables, exercised by `XnbConformanceTests`. That decoder reproduces both
//     externally produced LZX fixtures byte for byte against FNA's own reference output, which is
//     what earns it the right to judge CNA's encoder.
//   * the container framing itself, asserted here field by field against the layout the two
//     committed externally produced LZX fixtures demonstrate.

#include <cstdint>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/LzxEncoder.hpp"
#include "CNA/Internal/Xnb/XnbAssetWriter.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInWriters.hpp"
#include "CNA/Internal/Xnb/XnbByteWriter.hpp"
#include "CNA/Internal/Xnb/XnbDecompression.hpp"
#include "CNA/Internal/Xnb/XnbFileOptions.hpp"
#include "CNA/Internal/Xnb/XnbHeader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/CollectionContentTypeReaders.hpp"
#include "System/IO/MemoryStream.hpp"

using namespace CNA::Internal::Xnb;

namespace
{
    /** @brief Compresses and decompresses @p payload, returning what came back. */
    [[nodiscard]] std::vector<std::uint8_t> RoundTrip(const std::vector<std::uint8_t>& payload,
                                                      const LzxEncodeOptions& options = {})
    {
        const std::vector<std::uint8_t> compressed = CompressXnbLzxPayload(payload, options);
        if (payload.empty())
        {
            EXPECT_TRUE(compressed.empty());
            return {};
        }
        return DecompressXnbPayload(compressed.data(),
                                    static_cast<std::int32_t>(compressed.size()),
                                    static_cast<std::int32_t>(payload.size()), "round-trip");
    }

    /** @brief Deterministic pseudo-random bytes; a fixed seed keeps every assertion reproducible. */
    [[nodiscard]] std::vector<std::uint8_t> Noise(const std::size_t count,
                                                  const std::uint32_t seed)
    {
        std::mt19937 engine(seed);
        std::vector<std::uint8_t> bytes(count);
        for (std::uint8_t& byte : bytes) { byte = static_cast<std::uint8_t>(engine() & 0xFFu); }
        return bytes;
    }

    /** @brief One parsed container block header, as the reading side sees it. */
    struct FramedBlock
    {
        std::uint32_t frameSize = 0u;
        std::uint32_t blockSize = 0u;
        bool explicitFrameSize = false;
    };

    /** @brief Parses the container's own block framing out of a compressed payload. */
    [[nodiscard]] std::vector<FramedBlock> ParseFraming(
        const std::vector<std::uint8_t>& compressed)
    {
        std::vector<FramedBlock> blocks;
        std::size_t cursor = 0u;
        while (cursor + 2u <= compressed.size())
        {
            FramedBlock block;
            if (compressed[cursor] == 0xFFu)
            {
                if (cursor + 5u > compressed.size()) { break; }
                block.explicitFrameSize = true;
                block.frameSize = static_cast<std::uint32_t>(
                    (compressed[cursor + 1u] << 8) | compressed[cursor + 2u]);
                block.blockSize = static_cast<std::uint32_t>(
                    (compressed[cursor + 3u] << 8) | compressed[cursor + 4u]);
                cursor += 5u;
            }
            else
            {
                block.frameSize = 0x8000u;
                block.blockSize = static_cast<std::uint32_t>(
                    (compressed[cursor] << 8) | compressed[cursor + 1u]);
                cursor += 2u;
            }
            blocks.push_back(block);
            cursor += block.blockSize;
        }
        EXPECT_EQ(cursor, compressed.size()) << "the framing does not tile the payload exactly";
        return blocks;
    }
}

// -- round trips -------------------------------------------------------------------------------

TEST(LzxEncoderTest, AnEmptyPayloadProducesNoBlocksAtAll)
{
    // The container's framing loop runs while there are bytes left, so zero blocks is the correct
    // encoding of zero bytes rather than a special case anybody has to know about.
    EXPECT_TRUE(CompressXnbLzxPayload({}).empty());
}

TEST(LzxEncoderTest, ASingleByteRoundTrips)
{
    const std::vector<std::uint8_t> payload{0x41u};
    EXPECT_EQ(RoundTrip(payload), payload);
}

TEST(LzxEncoderTest, ATinyPayloadWithNoPossibleMatchRoundTrips)
{
    std::vector<std::uint8_t> payload(10u);
    std::iota(payload.begin(), payload.end(), std::uint8_t{1});
    EXPECT_EQ(RoundTrip(payload), payload);
}

TEST(LzxEncoderTest, AHighlyRepetitivePayloadRoundTripsAndCompressesHard)
{
    const std::vector<std::uint8_t> payload(70000u, 0x5Au);
    EXPECT_EQ(RoundTrip(payload), payload);
    const std::vector<std::uint8_t> compressed = CompressXnbLzxPayload(payload);
    // 70 KB of one byte is the easiest input there is: three frames of repeated-offset matches.
    EXPECT_LT(compressed.size(), payload.size() / 100u) << compressed.size();
}

TEST(LzxEncoderTest, RepetitiveTextRoundTripsAndCompressesWell)
{
    const std::string line = "the quick brown fox jumps over the lazy dog. ";
    std::vector<std::uint8_t> payload;
    while (payload.size() < 200000u)
    {
        payload.insert(payload.end(), line.begin(), line.end());
    }
    EXPECT_EQ(RoundTrip(payload), payload);
    EXPECT_LT(CompressXnbLzxPayload(payload).size(), payload.size() / 50u);
}

TEST(LzxEncoderTest, AnIncompressiblePayloadRoundTripsAndGrowsOnlySlightly)
{
    const std::vector<std::uint8_t> payload = Noise(100000u, 12345u);
    EXPECT_EQ(RoundTrip(payload), payload);
    const std::vector<std::uint8_t> compressed = CompressXnbLzxPayload(payload);
    // Uniform random bytes have no redundancy for either the match finder or the Huffman coder,
    // so the file grows by the trees plus the framing. Stated as a bound rather than hidden: a
    // caller who cares picks no compression for data like this.
    EXPECT_GT(compressed.size(), payload.size());
    EXPECT_LT(compressed.size(), payload.size() + payload.size() / 50u) << compressed.size();
}

TEST(LzxEncoderTest, ALongMatchAtTheMaximumEncodableLengthRoundTrips)
{
    // The longest match LZX can express is MIN_MATCH + NUM_PRIMARY_LENGTHS + 248 = 257 bytes. A
    // payload built as a 1000-byte pattern repeated forces matches at that ceiling, so the
    // encoder must split a longer run into several maximum-length matches.
    std::vector<std::uint8_t> payload(20000u, 0u);
    for (std::size_t index = 0u; index < 1000u; ++index)
    {
        payload[index] = static_cast<std::uint8_t>(index * 7u);
    }
    for (std::size_t index = 1000u; index < payload.size(); ++index)
    {
        payload[index] = payload[index - 1000u];
    }
    EXPECT_EQ(RoundTrip(payload), payload);
}

TEST(LzxEncoderTest, EveryFrameBoundarySizeRoundTrips)
{
    for (const std::size_t size : {std::size_t{0x7FFFu}, std::size_t{0x8000u},
                                   std::size_t{0x8001u}, std::size_t{0x10000u},
                                   std::size_t{0x10001u}, std::size_t{0x18000u}})
    {
        std::mt19937 engine(7u);
        std::vector<std::uint8_t> payload(size);
        for (std::size_t index = 0u; index < size; ++index)
        {
            payload[index] = static_cast<std::uint8_t>((index / 7u) ^ (engine() & 0x0Fu));
        }
        EXPECT_EQ(RoundTrip(payload), payload) << "size " << size;
    }
}

TEST(LzxEncoderTest, APayloadLargerThanTheWindowRoundTrips)
{
    // Past 64 KiB the decoder's window wraps and every match offset is measured modulo the window,
    // which is the part a smaller test cannot reach.
    std::vector<std::uint8_t> payload = Noise(90000u, 99u);
    for (std::size_t index = 80000u; index < payload.size(); ++index)
    {
        payload[index] = payload[index - 40000u];
    }
    EXPECT_EQ(RoundTrip(payload), payload);
}

// -- framing -----------------------------------------------------------------------------------

TEST(LzxEncoderTest, AFullFrameUsesTheTwoByteHeaderAndAShortFrameTheExplicitOne)
{
    // Both forms are demonstrated by the committed externally produced fixtures: one carries a
    // single explicit-size frame, the other a full 0x8000 frame followed by an explicit-size one.
    const std::vector<std::uint8_t> payload = Noise(0x8000u + 1000u, 5u);
    const std::vector<FramedBlock> blocks = ParseFraming(CompressXnbLzxPayload(payload));
    ASSERT_EQ(blocks.size(), 2u);
    EXPECT_FALSE(blocks[0].explicitFrameSize);
    EXPECT_EQ(blocks[0].frameSize, 0x8000u);
    EXPECT_TRUE(blocks[1].explicitFrameSize);
    EXPECT_EQ(blocks[1].frameSize, 1000u);
    EXPECT_EQ(blocks[0].frameSize + blocks[1].frameSize, payload.size());
}

TEST(LzxEncoderTest, EveryFrameIsAWholeNumberOf16BitWordsAndNeverAliasesTheFrameMarker)
{
    // LZX writes its bitstream in 16-bit words, so a block size is always even; and a block size
    // whose high byte were 0xFF would be misread as the explicit-frame marker by the container's
    // own framing loop.
    const std::vector<std::uint8_t> payload = Noise(3u * 0x8000u + 17u, 4242u);
    const std::vector<FramedBlock> blocks = ParseFraming(CompressXnbLzxPayload(payload));
    ASSERT_EQ(blocks.size(), 4u);
    std::uint32_t total = 0u;
    for (const FramedBlock& block : blocks)
    {
        EXPECT_EQ(block.blockSize % 2u, 0u);
        EXPECT_LT(block.blockSize >> 8, 0xFFu);
        EXPECT_GT(block.blockSize, 0u);
        total += block.frameSize;
    }
    EXPECT_EQ(total, payload.size());
}

TEST(LzxEncoderTest, TheEncodedStreamNeverRequestsIntelCallTranslation)
{
    // The first bit of the first frame is the E8 translation flag. CNA's decoder -- like FNA's,
    // which never finished that transform -- fails outright on a non-zero file size there, so a
    // one would produce a file CNA cannot read. The bit lives in the top bit of the second byte
    // of the first 16-bit word, because words are little-endian and bits are consumed MSB first.
    const std::vector<std::uint8_t> compressed =
        CompressXnbLzxPayload(std::vector<std::uint8_t>(4096u, 0x11u));
    ASSERT_GT(compressed.size(), 8u);
    EXPECT_EQ(compressed[6] & 0x80u, 0u) << "the E8 translation flag must be zero";
}

// -- determinism -------------------------------------------------------------------------------

TEST(LzxEncoderTest, TheSamePayloadAlwaysCompressesToTheSameBytes)
{
    const std::vector<std::uint8_t> payload = Noise(50000u, 2026u);
    EXPECT_EQ(CompressXnbLzxPayload(payload), CompressXnbLzxPayload(payload));

    const std::vector<std::uint8_t> repetitive(40000u, 0x33u);
    EXPECT_EQ(CompressXnbLzxPayload(repetitive), CompressXnbLzxPayload(repetitive));
}

TEST(LzxEncoderTest, ADifferentSearchDepthChangesOnlyTheSizeAndNotTheContent)
{
    std::vector<std::uint8_t> payload = Noise(60000u, 31337u);
    for (std::size_t index = 30000u; index < payload.size(); ++index)
    {
        payload[index] = payload[index - 12345u];
    }
    LzxEncodeOptions shallow;
    shallow.matchSearchDepth = 1u;
    LzxEncodeOptions deep;
    deep.matchSearchDepth = 64u;
    EXPECT_EQ(RoundTrip(payload, shallow), payload);
    EXPECT_EQ(RoundTrip(payload, deep), payload);
    EXPECT_LE(CompressXnbLzxPayload(payload, deep).size(),
              CompressXnbLzxPayload(payload, shallow).size());
}

TEST(LzxEncoderTest, ASmallFrameSizeStillRoundTripsThroughTheContainerFraming)
{
    LzxEncodeOptions small;
    small.frameSize = 1024u;
    const std::vector<std::uint8_t> payload = Noise(5000u, 6u);
    EXPECT_EQ(RoundTrip(payload, small), payload);
    const std::vector<FramedBlock> blocks = ParseFraming(CompressXnbLzxPayload(payload, small));
    EXPECT_EQ(blocks.size(), 5u);
    for (const FramedBlock& block : blocks) { EXPECT_TRUE(block.explicitFrameSize); }
}

// -- refusals ----------------------------------------------------------------------------------

TEST(LzxEncoderTest, AWindowTheContainerCannotExpressIsRefused)
{
    LzxEncodeOptions options;
    for (const int windowBits : {14, 15, 17, 21})
    {
        options.windowBits = windowBits;
        EXPECT_THROW(CompressXnbLzxPayload(std::vector<std::uint8_t>(16u, 0u), options),
                     XnbWriteException)
            << windowBits;
    }
}

TEST(LzxEncoderTest, AFrameLargerThanTheContainersDefaultIsRefused)
{
    LzxEncodeOptions options;
    options.frameSize = 0x8001u;
    EXPECT_THROW(CompressXnbLzxPayload(std::vector<std::uint8_t>(16u, 0u), options),
                 XnbWriteException);
    options.frameSize = 0u;
    EXPECT_THROW(CompressXnbLzxPayload(std::vector<std::uint8_t>(16u, 0u), options),
                 XnbWriteException);
}

TEST(LzxEncoderTest, AZeroSearchDepthIsRefusedRatherThanSilentlyDisablingMatching)
{
    LzxEncodeOptions options;
    options.matchSearchDepth = 0u;
    EXPECT_THROW(CompressXnbLzxPayload(std::vector<std::uint8_t>(16u, 0u), options),
                 XnbWriteException);
}

TEST(LzxEncoderTest, ATruncatedCompressedPayloadNeverPassesForTheOriginal)
{
    // **LZX carries no integrity check, and neither does the XNB container.** A truncated stream
    // can therefore decode to the declared number of bytes and be wrong, because the decoder's
    // bit reader treats end-of-input as 0xFF and keeps decoding valid-looking symbols out of it.
    // That is a property of the format rather than of this encoder, and the honest contract is
    // the one asserted here: a truncated stream either refuses or produces different bytes. It
    // never quietly passes for the original.
    //
    // Truncation of the very last bytes of a highly compressible frame is genuinely harmless --
    // those bytes are the bit-buffer padding the decoder reads and discards -- so the cut has to
    // be somewhere the symbols actually live.
    const std::vector<std::uint8_t> payload = Noise(20000u, 8181u);
    const std::vector<std::uint8_t> compressed = CompressXnbLzxPayload(payload);
    ASSERT_GT(compressed.size(), 64u);
    for (const std::size_t keep : {compressed.size() / 4u, compressed.size() / 2u,
                                   compressed.size() - 8u})
    {
        std::vector<std::uint8_t> truncated(compressed.begin(),
                                            compressed.begin() +
                                                static_cast<std::ptrdiff_t>(keep));
        try
        {
            const std::vector<std::uint8_t> back = DecompressXnbPayload(
                truncated.data(), static_cast<std::int32_t>(truncated.size()),
                static_cast<std::int32_t>(payload.size()), "truncated");
            EXPECT_NE(back, payload) << "keeping " << keep << " of " << compressed.size();
        }
        catch (const std::exception&)
        {
            // A clean refusal is the other acceptable outcome.
        }
    }
}

TEST(LzxEncoderTest, ADeclaredSizeThatDisagreesWithTheStreamIsRefused)
{
    const std::vector<std::uint8_t> payload(20000u, 0x77u);
    const std::vector<std::uint8_t> compressed = CompressXnbLzxPayload(payload);
    EXPECT_ANY_THROW(DecompressXnbPayload(compressed.data(),
                                          static_cast<std::int32_t>(compressed.size()),
                                          static_cast<std::int32_t>(payload.size() + 1u),
                                          "wrong size"));
}

TEST(LzxEncoderTest, EveryFlippedBitEitherDecodesOrFailsCleanly)
{
    // A codec's decoder is an attack surface, so a corrupted stream must fail rather than crash.
    // The decoder's own hardening is plans/plan_xnb.md XNB-30; this checks the pairing holds for
    // streams *this* encoder produced, which are the ones a CNA game will actually be loading.
    const std::vector<std::uint8_t> payload(4096u, 0x21u);
    const std::vector<std::uint8_t> original = CompressXnbLzxPayload(payload);
    ASSERT_FALSE(original.empty());
    for (std::size_t index = 0u; index < original.size(); index += 7u)
    {
        for (const std::uint8_t mask : {std::uint8_t{0x01u}, std::uint8_t{0x80u}})
        {
            std::vector<std::uint8_t> corrupt = original;
            corrupt[index] = static_cast<std::uint8_t>(corrupt[index] ^ mask);
            try
            {
                const std::vector<std::uint8_t> back = DecompressXnbPayload(
                    corrupt.data(), static_cast<std::int32_t>(corrupt.size()),
                    static_cast<std::int32_t>(payload.size()), "corrupt");
                EXPECT_EQ(back.size(), payload.size());
            }
            catch (const std::exception&)
            {
                // A clean refusal is the expected outcome for most flips.
            }
        }
    }
}

// -- through the writer ------------------------------------------------------------------------

TEST(LzxEncoderTest, TheXnbWriterProducesACompressedFileTheReaderLoadsBack)
{
    Microsoft::Xna::Framework::Content::ContentTypeReaderManager::ClearTypeCreators();
    RegisterPrimitiveXnbReaders();

    std::vector<std::string> strings;
    for (int index = 0; index < 400; ++index)
    {
        strings.push_back("Content/Textures/tile_" + std::to_string(index) + ".xnb");
    }

    XnbFileOptions compressedOptions;
    compressedOptions.compression = XnbOutputCompression::Lzx;
    const std::vector<std::uint8_t> file =
        WriteXnbAsset(strings, compressedOptions, "manifest");
    const std::vector<std::uint8_t> plain = WriteXnbAsset(strings, {}, "manifest");

    // The header must announce LZX, the file must be smaller than the uncompressed one, and the
    // declared total length must still cover the whole file.
    ASSERT_GT(file.size(), 14u);
    EXPECT_EQ(file[5] & 0x80u, 0x80u);
    EXPECT_EQ(file[5] & 0x40u, 0u);
    EXPECT_LT(file.size(), plain.size());

    System::IO::MemoryStream headerStream(file.data(), static_cast<std::int32_t>(file.size()));
    System::IO::BinaryReader headerReader(&headerStream, true);
    const XnbHeader header = ParseXnbHeader(headerReader, "compressed");
    EXPECT_EQ(header.totalLength, static_cast<std::int32_t>(file.size()));
    EXPECT_EQ(header.compression, XnbCompression::Lzx);

    const std::int32_t declared =
        static_cast<std::int32_t>(file[10]) | (static_cast<std::int32_t>(file[11]) << 8) |
        (static_cast<std::int32_t>(file[12]) << 16) | (static_cast<std::int32_t>(file[13]) << 24);
    EXPECT_EQ(declared, static_cast<std::int32_t>(plain.size() - 10u));

    const std::vector<std::uint8_t> body = DecompressXnbPayload(
        file.data() + 14, static_cast<std::int32_t>(file.size() - 14u), declared, "compressed");
    // The decompressed body must be exactly the uncompressed file's body: compression is a
    // container concern and may not change one byte of the object graph.
    EXPECT_EQ(body, std::vector<std::uint8_t>(plain.begin() + 10, plain.end()));

    System::IO::MemoryStream bodyStream(body.data(), static_cast<std::int32_t>(body.size()));
    Microsoft::Xna::Framework::Content::ContentReader reader(
        nullptr, &bodyStream, "compressed", header.version, header.platform);
    EXPECT_EQ(reader.ReadAsset<std::vector<std::string>>(), strings);

    Microsoft::Xna::Framework::Content::ContentTypeReaderManager::ClearTypeCreators();
}

TEST(LzxEncoderTest, LzxIsAcceptedOnAnXna40TargetWhereLz4IsNot)
{
    // LZX is the scheme Microsoft XNA 4.0 itself produced, so unlike LZ4 it is exactly what an
    // XNA 4.0 target platform should carry.
    XnbFileOptions options;
    options.platform = XnbTargetPlatform::Windows;
    options.compression = XnbOutputCompression::Lzx;
    EXPECT_NO_THROW(ValidateXnbFileOptions(options));

    options.compression = XnbOutputCompression::Lz4;
    EXPECT_THROW(ValidateXnbFileOptions(options), XnbWriteException);

    // Container version 4 stays uncompressed either way: CNA has no evidence for a compressed
    // pre-XNA-4.0 container layout.
    options.version = XnbContainerVersion::Legacy4;
    options.compression = XnbOutputCompression::Lzx;
    EXPECT_THROW(ValidateXnbFileOptions(options), XnbWriteException);
}
