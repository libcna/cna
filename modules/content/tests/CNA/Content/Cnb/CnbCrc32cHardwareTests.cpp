// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-108: the hardware CRC-32C path.
//
// A faster checksum that computes a different number is not a faster checksum, it is a corrupted
// format. Every existing .cnb carries CRCs produced by the table implementation, so the only thing
// that matters here is that the hardware path agrees with it EXACTLY -- on every input length, not
// just on convenient multiples of eight, because the tail handling is where such a path goes wrong.

#include <cstdint>
#include <gtest/gtest.h>
#include <numeric>
#include <span>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbCrc32c.hpp"

using CNA::Content::Cnb::Crc32c;
using CNA::Content::Cnb::Crc32cContinue;
using CNA::Content::Cnb::Crc32cPortableEXT;
using CNA::Content::Cnb::Crc32cSeed;
using CNA::Content::Cnb::Crc32cUsesHardwareEXT;

namespace
{
    /// Deterministic pseudo-random bytes: a fixed LCG, so a failure is reproducible rather than a
    /// story about "some input once".
    std::vector<std::uint8_t> Bytes(std::size_t count, std::uint32_t seed = 0xC0FFEEu)
    {
        std::vector<std::uint8_t> data(count);
        std::uint32_t state = seed;
        for (std::uint8_t& byte : data)
        {
            state = state * 1664525u + 1013904223u;
            byte = static_cast<std::uint8_t>(state >> 24);
        }
        return data;
    }
}

TEST(CnbCrc32cHardwareTest, TheKnownAnswerIsUnchanged)
{
    // CRC-32C of "123456789" is 0xE3069283 in every reference implementation. If this moves,
    // every .cnb ever written becomes unreadable, so it is checked before anything else.
    const std::string check = "123456789";
    const std::span<const std::uint8_t> bytes(
        reinterpret_cast<const std::uint8_t*>(check.data()), check.size());
    EXPECT_EQ(Crc32c(bytes), 0xE3069283u);
    EXPECT_EQ(Crc32cPortableEXT(bytes), 0xE3069283u);
}

TEST(CnbCrc32cHardwareTest, TheHardwarePathAgreesWithTheTableAtEveryLength)
{
    // Lengths 0..64 cover every remainder modulo 8 and modulo 4 several times over, which is
    // exactly where a wide-fold implementation's tail handling breaks.
    for (std::size_t length = 0; length <= 64u; ++length)
    {
        const std::vector<std::uint8_t> data = Bytes(length, 0x1234u + static_cast<std::uint32_t>(length));
        EXPECT_EQ(Crc32c(data), Crc32cPortableEXT(data)) << "length " << length;
    }
}

TEST(CnbCrc32cHardwareTest, TheHardwarePathAgreesWithTheTableOnLargeAndAwkwardBuffers)
{
    for (const std::size_t length : {1023u, 1024u, 1025u, 4095u, 65535u, 65536u, 1048577u})
    {
        const std::vector<std::uint8_t> data = Bytes(length);
        EXPECT_EQ(Crc32c(data), Crc32cPortableEXT(data)) << "length " << length;
    }
}

TEST(CnbCrc32cHardwareTest, ARunningChecksumSplitAnywhereMatchesOneComputedInOnePass)
{
    // Crc32cContinue() is the incremental entry point, and a wide-fold path that is correct in one
    // pass can still be wrong when resumed at an offset that is not a multiple of its stride.
    const std::vector<std::uint8_t> data = Bytes(4096u);
    const std::uint32_t whole = Crc32c(data);
    for (const std::size_t split : {0u, 1u, 3u, 7u, 8u, 9u, 15u, 1000u, 4095u, 4096u})
    {
        std::uint32_t running = Crc32cSeed();
        running = Crc32cContinue(running,
                                 std::span<const std::uint8_t>(data).first(split));
        running = Crc32cContinue(running,
                                 std::span<const std::uint8_t>(data).subspan(split));
        EXPECT_EQ(running, whole) << "split at " << split;
    }
}

TEST(CnbCrc32cHardwareTest, ASingleFlippedBitIsAlwaysDetected)
{
    // The property the format actually relies on. Checked against the hardware path specifically,
    // since that is the one that is new.
    const std::vector<std::uint8_t> data = Bytes(1000u);
    const std::uint32_t reference = Crc32c(data);
    for (std::size_t i = 0; i < data.size(); i += 37u)
    {
        std::vector<std::uint8_t> mutated = data;
        mutated[i] ^= 0x01u;
        EXPECT_NE(Crc32c(mutated), reference) << "byte " << i;
    }
}

TEST(CnbCrc32cHardwareTest, WhichPathIsInUseIsAnswerableWithoutGuessing)
{
    // Not an assertion about which path wins -- that is the machine's business. The point is that
    // a benchmark or a bug report can say which one it measured, instead of inferring it from a
    // build flag it cannot see.
    const bool hardware = Crc32cUsesHardwareEXT();
    SUCCEED() << "CRC-32C is using the " << (hardware ? "hardware" : "portable table") << " path";
#if defined(__x86_64__)
    // Every x86-64 CPU has had SSE4.2 since 2008, so on this architecture the portable path
    // should be the fallback nobody reaches, and silently falling back would waste the whole
    // point of CNBF-108.
    EXPECT_TRUE(hardware)
        << "x86-64 has had SSE4.2 since 2008; falling back here means the detection is broken";
#endif
}
