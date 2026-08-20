// SPDX-License-Identifier: MS-PL
//
// plans/plan_audio.md AUD-06-022: property-based XNB WAVEFORMATEX mutation tests. Unlike
// SoundEffectContentTypeReaderTests.cpp's individual unit tests (each hand-picks one meaningful
// value per field) or XnbContainerFuzzTests.cpp's whole-file random byte-flip fuzzing (which can
// miss a narrow single-field edge case by chance -- see AUD-06-024's follow-up fix, found by a
// *targeted* zero-sample-rate probe after 1500 random mutations of a real fixture never happened
// to hit it), this deterministically sweeps a small cross-product of boundary values for the core
// WAVEFORMATEX fields (wFormatTag, nChannels, wBitsPerSample, nSamplesPerSec) and asserts the
// parser invariant holds for every combination: either a `SoundEffect` is constructed successfully,
// or the failure is exactly `ContentLoadException`/`EndOfStreamException` -- never a crash, hang,
// or any other exception type escaping uncaught. `formatLength` is held fixed at 16 (the simplest,
// most common real case) since its own boundary values are already covered by
// AUD-06-011/AUD-06-016. Also meant to be run under an ASan+UBSan build
// (-DCNA_SANITIZE=address,undefined) as the real net for memory-safety/UB issues a plain
// exception-type check cannot detect on its own (AUD-06-023).

#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <vector>

#include "CNA/Internal/Xnb/SoundEffectContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/EndOfStreamException.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    // Builds one complete, well-formed XNB object stream (type-reader preamble + a formatLength=16
    // WAVEFORMATEX + a small fixed data buffer + no loop + no stored duration) for one boundary
    // value combination.
    std::vector<uint8_t> BuildPayload(
        uint16_t wFormatTag, uint16_t nChannels, uint32_t nSamplesPerSec, uint16_t wBitsPerSample)
    {
        std::vector<uint8_t> bytes;
        auto w16 = [&bytes](uint16_t v) { bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8)); };
        auto w32 = [&bytes](uint32_t v) {
            bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8));
            bytes.push_back(static_cast<uint8_t>(v >> 16)); bytes.push_back(static_cast<uint8_t>(v >> 24));
        };

        const std::string readerName = "Microsoft.Xna.Framework.Content.SoundEffectReader";
        bytes.push_back(1);
        bytes.push_back(static_cast<uint8_t>(readerName.size()));
        bytes.insert(bytes.end(), readerName.begin(), readerName.end());
        w32(0);
        bytes.push_back(0);
        bytes.push_back(1);

        w32(16); // formatLength: fixed, bare WAVEFORMAT -- its own boundary values are covered
                 // separately by AUD-06-011/AUD-06-016.
        w16(wFormatTag);
        w16(nChannels);
        w32(nSamplesPerSec);
        w32(nSamplesPerSec); // nAvgBytesPerSec: not independently swept, informational only
        w16(2);              // nBlockAlign: a plausible fixed value, not this test's target field
        w16(wBitsPerSample);

        constexpr int kDataBytes = 64; // small, fixed regardless of channels/bits -- large enough
                                        // to be a plausible one-or-more-frame buffer for any
                                        // combination, small enough to never itself be the reason
                                        // for a slow/huge allocation.
        w32(kDataBytes);
        for (int i = 0; i < kDataBytes; ++i) bytes.push_back(static_cast<uint8_t>(i));
        w32(0); // loopStart
        w32(0); // loopLength
        w32(0); // stored duration -- skip the AUD-06-010 oracle, not this test's target
        return bytes;
    }

    class SoundEffectContentTypeReaderPropertyTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterSoundEffectXnbReader();
        }
        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
        ContentManager cm_;
    };
}

TEST_F(SoundEffectContentTypeReaderPropertyTest, WaveFormatExBoundaryValueSweepNeverCrashesAndOnlyFailsCleanly)
{
    // Deliberately includes both "known good" and "known bad" values per field so the sweep
    // covers the full transition boundary, not just the pathological side.
    const std::vector<uint16_t> formatTags = {0, 1 /*PCM*/, 2 /*MS-ADPCM*/, 3 /*IEEE float*/,
                                               0x11 /*IMA-ADPCM*/, 0x166 /*XMA2*/, 0xFFFF};
    const std::vector<uint16_t> channelCounts = {0, 1, 2, 3, 0xFFFF};
    const std::vector<uint32_t> sampleRates = {0, 1, 44100, std::numeric_limits<uint32_t>::max()};
    const std::vector<uint16_t> bitDepths = {0, 1, 4, 8, 16, 24, 32, 0xFFFF};

    int total = 0;
    int completed = 0;
    int cleanlyRejected = 0;

    for (uint16_t formatTag : formatTags)
    for (uint16_t channels : channelCounts)
    for (uint32_t sampleRate : sampleRates)
    for (uint16_t bits : bitDepths)
    {
        ++total;
        const std::vector<uint8_t> payload = BuildPayload(formatTag, channels, sampleRate, bits);
        System::IO::MemoryStream body(payload.data(), static_cast<int32_t>(payload.size()));
        ContentReader reader(&cm_, &body, "test", 5, 'w');

        try
        {
            reader.ReadAsset<Microsoft::Xna::Framework::Audio::SoundEffect>();
            ++completed;
        }
        catch (const ContentLoadException&)
        {
            ++cleanlyRejected;
        }
        catch (const System::IO::EndOfStreamException&)
        {
            ++cleanlyRejected; // reachable if a future field/format combination reads past the
                                // fixed 64-byte data region -- not expected today, but a clean
                                // failure mode rather than a crash if it ever became reachable.
        }
        // Any other exception type, or a crash/hang, fails this test (uncaught exception aborts
        // it; a crash is caught by the process exit code / a sanitizer run, not by this catch
        // list) -- exactly the invariant this property-based sweep exists to prove.
    }

    EXPECT_EQ(completed + cleanlyRejected, total);
    // Not asserting a specific split -- what matters is that every combination in the boundary
    // sweep resolved cleanly one way or the other, not the exact proportion accepted vs rejected.
}
