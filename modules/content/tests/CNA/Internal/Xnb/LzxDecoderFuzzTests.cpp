// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-30A: deterministic fuzz test for CNA::Internal::Xnb::DecompressXnbPayload().
// Mutates real, externally-produced LZX-compressed payloads (never hand-crafted -- valid LZX
// bytes cannot be authored by hand, so mutation of real data is the only practical way to fuzz
// this format) and asserts every call either completes or fails with a clean, expected exception
// -- never crashes, hangs, or corrupts memory. The stream is deterministic (fixed seed; no wall
// clock, no std::random_device) so it is reproducible. Also run under an ASan+UBSan build
// (-DCNA_SANITIZE=address,undefined) as the real net for memory-safety/UB issues a plain
// exception-type check cannot detect on its own -- a mutation that merely "doesn't throw" could
// still have read out of bounds without a sanitizer watching.

#include <algorithm>
#include <cstring>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

#include "CNA/Internal/Xnb/XnbDecompression.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "System/IO/EndOfStreamException.hpp"

namespace
{
    // Small deterministic LCG -- matches the pattern already used in
    // tests/CNA/Internal/Input/SdlInputBridgeFuzzTests.cpp, kept consistent rather than inventing
    // a second PRNG convention.
    struct Rng
    {
        std::uint64_t state;
        explicit Rng(std::uint64_t seed) : state(seed) {}
        std::uint32_t next()
        {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<std::uint32_t>(state >> 33);
        }
        std::uint32_t below(std::uint32_t n) { return next() % n; }
    };

    std::string ReadWholeFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return {};
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    // Extracts the raw compressed payload (past the 14-byte header+decompressed-size prefix) from
    // a real LZX-compressed .xnb fixture, plus the decompressed size it should produce.
    struct SeedPayload
    {
        std::vector<uint8_t> compressed;
        int32_t decompressedSize;
    };

    SeedPayload ExtractSeedPayload(const std::string& xnbPath)
    {
        const std::string fileBytes = ReadWholeFile(xnbPath);
        if (fileBytes.size() < 14) return {};
        int32_t decompressedSize;
        std::memcpy(&decompressedSize, fileBytes.data() + 10, 4);
        SeedPayload seed;
        seed.compressed.assign(fileBytes.begin() + 14, fileBytes.end());
        seed.decompressedSize = decompressedSize;
        return seed;
    }

    // Applies a handful of random mutations to a copy of a seed payload: byte flips, truncation,
    // and (occasionally) also corrupting the decompressed-size hint fed alongside it -- covering
    // both "corrupt compressed bytes" and "adversarial size field" input classes together.
    void Mutate(Rng& rng, std::vector<uint8_t>& bytes, int32_t& decompressedSize)
    {
        if (bytes.empty()) return;

        const uint32_t mutationCount = 1 + rng.below(8);
        for (uint32_t m = 0; m < mutationCount; ++m)
        {
            switch (rng.below(4))
            {
                case 0: // flip a random byte
                    bytes[rng.below(static_cast<uint32_t>(bytes.size()))] ^= static_cast<uint8_t>(1u << rng.below(8));
                    break;
                case 1: // truncate to a random shorter length
                    if (bytes.size() > 1)
                    {
                        bytes.resize(1 + rng.below(static_cast<uint32_t>(bytes.size()) - 1));
                    }
                    break;
                case 2: // overwrite a random byte with a random value
                    bytes[rng.below(static_cast<uint32_t>(bytes.size()))] = static_cast<uint8_t>(rng.below(256));
                    break;
                case 3: // corrupt the decompressed-size hint, including adversarially huge values
                    switch (rng.below(3))
                    {
                        case 0: decompressedSize = 0; break;
                        case 1: decompressedSize = -1; break;
                        default: decompressedSize = static_cast<int32_t>(rng.next()); break;
                    }
                    break;
                default: break;
            }
        }
    }
}

TEST(LzxDecoderFuzzTest, MutatedRealPayloadsNeverCrashAndOnlyFailCleanly)
{
    std::vector<SeedPayload> seeds{
        ExtractSeedPayload("tests/assets/xnb/monogame/windows/lzx/Explosion.xnb"),
        ExtractSeedPayload("tests/assets/xnb/monogame/windows/lzx/FontCalibri14.xnb"),
    };
    for (const auto& seed : seeds)
    {
        ASSERT_FALSE(seed.compressed.empty()) << "Real compressed .xnb fixtures not found relative to CWD";
    }

    Rng rng(0x4C5A58ULL); // "LZX" in hex-ish, arbitrary fixed seed
    int completed = 0;
    int cleanlyRejected = 0;

    for (int i = 0; i < 2000; ++i)
    {
        const SeedPayload& seed = seeds[rng.below(static_cast<uint32_t>(seeds.size()))];
        std::vector<uint8_t> mutated = seed.compressed;
        int32_t decompressedSize = seed.decompressedSize;
        Mutate(rng, mutated, decompressedSize);

        try
        {
            const auto result = CNA::Internal::Xnb::DecompressXnbPayload(
                mutated.data(), static_cast<int32_t>(mutated.size()), decompressedSize, "fuzz.xnb");
            // A mutation that still happens to decompress successfully must still honor its own
            // declared contract: exactly decompressedSize bytes, never more or less.
            EXPECT_EQ(result.size(), static_cast<std::size_t>(decompressedSize)) << "iteration #" << i;
            ++completed;
        }
        catch (const Microsoft::Xna::Framework::Content::ContentLoadException&)
        {
            ++cleanlyRejected; // expected: malformed/adversarial input rejected with a clean error
        }
        catch (const System::IO::EndOfStreamException&)
        {
            ++cleanlyRejected; // expected: truncated input ran past the buffer during a stream read
        }
        // Any other exception type, or a crash/hang, fails this test (uncaught exception aborts
        // it; a crash is caught by the process exit code / a sanitizer run, not by this catch list).
    }

    EXPECT_EQ(completed + cleanlyRejected, 2000);
    // Not asserting a specific split -- what matters is that every one of the 2000 mutated inputs
    // resolved cleanly one way or the other, not the exact proportion.
}
