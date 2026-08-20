// SPDX-License-Identifier: MS-PL
//
// plans/plan_audio.md AUD-11-026: deterministic fuzz test for ParseXwb()/ParseXsb(). Unlike LZX-
// compressed streams (which can't be hand-authored -- see LzxDecoderFuzzTests.cpp's own comment),
// valid XACT container bytes can be, so this mutates a small set of hand-built, well-formed seed
// files (a compact XWB, a non-compact XWB with real names/loop fields, and a minimal XSB) instead
// of requiring real external fixtures. Asserts every mutated input either completes or fails with
// a clean std::runtime_error (the only exception type this parser ever throws, by design -- every
// count field driving a std::vector::resize() is either naturally bounded by its own 8/16-bit
// field width, or explicitly validated against the file's real size, see AUD-11-026's entryCount
// fix) -- never crashes, hangs, or takes an unreasonable amount of time. The stream is
// deterministic (fixed seed; no wall clock, no std::random_device) so it is reproducible. Also run
// under an ASan+UBSan build (-DCNA_SANITIZE=address,undefined) as the real net for memory-safety/
// UB issues a plain exception-type check cannot detect on its own -- this exact technique already
// found two real defects during this task's own investigation (AUD-11-018's heap-buffer-overflow
// in name parsing, AUD-11-026's own unbounded-entryCount allocation-bomb gap).

#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>

#include "CNA/Internal/Audio/XactTypes.hpp"

using CNA::Internal::Audio::ParseXsb;
using CNA::Internal::Audio::ParseXwb;

namespace
{
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

    void AppendU32(std::vector<uint8_t>& buf, uint32_t v)
    {
        uint8_t bytes[4];
        std::memcpy(bytes, &v, 4);
        buf.insert(buf.end(), bytes, bytes + 4);
    }

    void AppendU16(std::vector<uint8_t>& buf, uint16_t v)
    {
        uint8_t bytes[2];
        std::memcpy(bytes, &v, 2);
        buf.insert(buf.end(), bytes, bytes + 2);
    }

    void AppendPadded(std::vector<uint8_t>& buf, const std::string& text, std::size_t totalSize)
    {
        const std::size_t start = buf.size();
        buf.resize(start + totalSize, 0);
        std::memcpy(buf.data() + start, text.data(), text.size());
    }

    // Minimal, well-formed compact XWB: 1 mono 16-bit PCM entry, real name, no loop.
    std::vector<uint8_t> BuildCompactSeed()
    {
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 1;
        constexpr uint32_t entryMetaDataSize = 4;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t entryNameSegSize  = 32;
        constexpr uint32_t waveDataLength    = 40;

        const uint32_t segOffset[5] = {
            headerSize,
            headerSize + bankDataSize,
            headerSize + bankDataSize + entryMetaSegSize,
            headerSize + bankDataSize + entryMetaSegSize + entryNameSegSize,
            headerSize + bankDataSize + entryMetaSegSize + entryNameSegSize,
        };
        const uint32_t segLength[5] = { bankDataSize, entryMetaSegSize, 0, entryNameSegSize, waveDataLength };

        std::vector<uint8_t> data;
        const char magic[4] = { 'W', 'B', 'N', 'D' };
        data.insert(data.end(), magic, magic + 4);
        AppendU32(data, 1); // version
        for (int i = 0; i < 5; ++i) { AppendU32(data, segOffset[i]); AppendU32(data, segLength[i]); }

        AppendU32(data, 0x00030000u); // wbFlags: COMPACT + names
        AppendU32(data, entryCount);
        AppendPadded(data, "FuzzCompact", 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, entryNameSegSize); // entryNameElementSize
        AppendU32(data, 4);                // alignment
        const uint32_t compactFormat = (0u) | (1u << 2) | (44100u << 5) | (2u << 23) | (1u << 31);
        AppendU32(data, compactFormat);
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        AppendU32(data, 0u); // entry 0: offset=0, deviation=0

        AppendPadded(data, "Entry0", entryNameSegSize);

        for (uint32_t i = 0; i < waveDataLength; ++i) data.push_back(static_cast<uint8_t>(i));
        return data;
    }

    // Minimal, well-formed non-compact XWB: 1 mono 16-bit PCM entry with a real loop region.
    std::vector<uint8_t> BuildNonCompactSeed()
    {
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 1;
        constexpr uint32_t entryMetaDataSize = 24;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t waveDataLength    = 40;

        const uint32_t segOffset[5] = {
            headerSize,
            headerSize + bankDataSize,
            headerSize + bankDataSize + entryMetaSegSize,
            headerSize + bankDataSize + entryMetaSegSize,
            headerSize + bankDataSize + entryMetaSegSize,
        };
        const uint32_t segLength[5] = { bankDataSize, entryMetaSegSize, 0, 0, waveDataLength };

        std::vector<uint8_t> data;
        const char magic[4] = { 'W', 'B', 'N', 'D' };
        data.insert(data.end(), magic, magic + 4);
        AppendU32(data, 1);
        for (int i = 0; i < 5; ++i) { AppendU32(data, segOffset[i]); AppendU32(data, segLength[i]); }

        AppendU32(data, 0u); // wbFlags: not compact, no names
        AppendU32(data, entryCount);
        AppendPadded(data, "FuzzNonCompact", 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0);
        AppendU32(data, 4);
        AppendU32(data, 0);
        for (int i = 0; i < 8; ++i) data.push_back(0);

        const uint32_t fmt = (0u) | (1u << 2) | (44100u << 5) | (2u << 23) | (1u << 31);
        AppendU32(data, 0u);
        AppendU32(data, fmt);
        AppendU32(data, 0u);
        AppendU32(data, waveDataLength);
        AppendU32(data, 5u);  // loopStart
        AppendU32(data, 10u); // loopTotal

        for (uint32_t i = 0; i < waveDataLength; ++i) data.push_back(static_cast<uint8_t>(i));
        return data;
    }

    // Minimal, well-formed XSB with one simple cue, no wave reference.
    std::vector<uint8_t> BuildXsbSeed()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;
        const uint32_t cueSimpleOffset    = baseOffset;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName = "FuzzCue";

        std::vector<uint8_t> data;
        const char magic[4] = { 'S', 'D', 'B', 'K' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); AppendU16(data, 0); AppendU16(data, 0);
        for (int i = 0; i < 8; ++i) data.push_back(0);
        data.push_back(0); // platform

        AppendU16(data, 1); AppendU16(data, 0); AppendU16(data, 0); AppendU16(data, 0);
        data.push_back(0); // wavebankCount
        AppendU16(data, 0); AppendU16(data, 0); AppendU16(data, 0);

        auto s32 = [&data](int32_t v) { AppendU32(data, static_cast<uint32_t>(v)); };
        s32(static_cast<int32_t>(cueSimpleOffset));
        s32(-1); s32(-1); s32(0); s32(-1); s32(0); s32(-1); s32(0);
        s32(static_cast<int32_t>(cueNameIndexOffset));
        s32(-1);

        AppendPadded(data, "FuzzSoundBank", bankNameSize);

        data.push_back(0);
        AppendU32(data, 0);

        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0);
        data.insert(data.end(), cueName.begin(), cueName.end());
        data.push_back(0);

        return data;
    }

    void Mutate(Rng& rng, std::vector<uint8_t>& bytes)
    {
        if (bytes.empty()) return;
        const uint32_t mutationCount = 1 + rng.below(8);
        for (uint32_t m = 0; m < mutationCount; ++m)
        {
            switch (rng.below(3))
            {
                case 0:
                    bytes[rng.below(static_cast<uint32_t>(bytes.size()))] ^= static_cast<uint8_t>(1u << rng.below(8));
                    break;
                case 1:
                    if (bytes.size() > 1) bytes.resize(1 + rng.below(static_cast<uint32_t>(bytes.size()) - 1));
                    break;
                case 2:
                    bytes[rng.below(static_cast<uint32_t>(bytes.size()))] = static_cast<uint8_t>(rng.below(256));
                    break;
                default: break;
            }
        }
    }

    template <typename Parser>
    void RunFuzz(Parser parse, const std::vector<uint8_t>& seed, std::uint64_t seedValue, int iterations)
    {
        Rng rng(seedValue);
        int completed = 0;
        int cleanlyRejected = 0;

        for (int i = 0; i < iterations; ++i)
        {
            std::vector<uint8_t> mutated = seed;
            Mutate(rng, mutated);

            const auto start = std::chrono::steady_clock::now();
            try
            {
                parse(mutated);
                ++completed;
            }
            catch (const std::runtime_error&)
            {
                ++cleanlyRejected;
            }
            const auto elapsed = std::chrono::steady_clock::now() - start;
            ASSERT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 2000)
                << "iteration #" << i << ": took too long -- possible hang or unguarded allocation";
            // Any other exception type, a crash, or a hang fails this test (an uncaught exception
            // aborts it; a crash is caught by the process exit code / a sanitizer run, not by this
            // catch list).
        }

        EXPECT_EQ(completed + cleanlyRejected, iterations);
    }
}

TEST(XactParserFuzzTest, MutatedCompactXwbSeedNeverCrashesAndOnlyFailsCleanly)
{
    RunFuzz([](const std::vector<uint8_t>& b) { (void)ParseXwb(b); },
            BuildCompactSeed(), 0x584D42431ULL, 2000);
}

TEST(XactParserFuzzTest, MutatedNonCompactXwbSeedNeverCrashesAndOnlyFailsCleanly)
{
    RunFuzz([](const std::vector<uint8_t>& b) { (void)ParseXwb(b); },
            BuildNonCompactSeed(), 0x584D424E43ULL, 2000);
}

TEST(XactParserFuzzTest, MutatedXsbSeedNeverCrashesAndOnlyFailsCleanly)
{
    RunFuzz([](const std::vector<uint8_t>& b) { (void)ParseXsb(b); },
            BuildXsbSeed(), 0x584D53420ULL, 2000);
}
