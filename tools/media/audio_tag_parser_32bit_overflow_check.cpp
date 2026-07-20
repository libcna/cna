// SPDX-License-Identifier: MS-PL
//
// REMED-MEDIA-001 / 32-bit CI coverage (BUILD_TEST_CI Wave 2): AudioTagParser.cpp's own
// ExceedsBound(std::size_t pos, std::size_t len, std::size_t bound) bounds check is only actually
// exercised at a genuine 32-bit std::size_t width if this binary itself is compiled and run for a
// 32-bit target. AudioTagParserTests.cpp's own 32-bit-relevant cases (Id3v23FrameSizeOverflow...,
// FlacPictureBlockLengthOverflow..., CraftedId3v23WrapInducingFrameSizeIsRejectedCleanly,
// CraftedFlacWrapInducingVendorLenIsRejectedCleanly) run only on this project's normal 64-bit
// builds, where a 32-bit-only wraparound cannot occur regardless of whether the fix is present --
// per that file's own comment, they were written as the best available evidence "no 32-bit
// toolchain is available in this sandbox." This standalone, non-GTest, SDL/FFmpeg-free executable
// exists specifically so a genuine 32-bit CI job (see .github/workflows/32bit-arithmetic-ci.yml)
// can compile AND execute AudioTagParser's real production code at a real 32-bit std::size_t
// width, not simulate it with an explicit uint32_t stand-in on a 64-bit host.
//
// Deliberately independent of CnaTests/gtest/CNA/SHARP_RUNTIME: AudioTagParser.cpp and
// ContentLoadException.cpp have no SDL/FFmpeg/graphics dependency (confirmed by reading both
// files' own #include lists), so this links only those two translation units directly -- getting
// a full 32-bit CNA library (SDL3/SDL3_image/SDL3_mixer/FFmpeg, all 64-bit-only in this project's
// existing vendored/system toolchain) built for `-m32` would be a much larger, slower undertaking
// for no additional coverage of the actual thing under test.
//
// Uses the exact same crafted fixtures AudioTagParserTests.cpp's own
// CraftedId3v23WrapInducingFrameSizeIsRejectedCleanly / CraftedFlacWrapInducingVendorLenIsRejectedCleanly
// cases use -- see that file for the fixtures' own derivation.
//
// Exit code 0 = all checks passed, 1 = any check failed.

#include "CNA/Internal/Media/AudioTagParser.hpp"

#include <cstdio>
#include <cstdint>
#include <vector>

using CNA::Internal::Media::AudioTagParser;
using CNA::Internal::Media::AudioTags;

namespace
{
    int g_failures = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) ++g_failures;
    }
}

int main()
{
    // Proves this binary is genuinely running at the target width this check exists for -- a
    // silent 64-bit fallback (e.g. a misconfigured CI job dropping -m32) would make every
    // assertion below meaningless without ever failing on its own.
    std::printf("sizeof(std::size_t) = %zu\n", sizeof(std::size_t));
    Check(sizeof(std::size_t) == 4, "sizeof(std::size_t) == 4 (genuinely running as a 32-bit build)");

    // Crafted ID3v2.3 fixture: a single TIT2 frame whose 4-byte, non-synchsafe (v2.3) frameSize
    // field is 0xFFFFFFF6 (2^32 - 10) -- on a real 32-bit size_t, `pos + frameSize` wraps back down
    // to 10, which is <= the 20-byte tagEnd under the pre-fix `pos + len > bound` idiom. Must be
    // rejected cleanly (no crash, no garbage title) here, on a real 32-bit size_t, not merely
    // simulated.
    {
        std::vector<uint8_t> tag = {
            'I', 'D', '3', 3, 0, 0,             // "ID3" + major version 3 (non-synchsafe frame sizes) + revision + flags
            0x00, 0x00, 0x00, 0x14,             // tagSize, synchsafe = 20
            'T', 'I', 'T', '2',                 // frameId
            0xFF, 0xFF, 0xFF, 0xF6,             // frameSize, raw big-endian (v2.3) = 0xFFFFFFF6
            0, 0,                                // frame flags
        };

        AudioTags tags;
        const bool accepted = AudioTagParser::TryReadId3v2(tag, tags);
        Check(!accepted, "crafted ID3v2.3 wrap-inducing frameSize is rejected, not accepted");
        Check(tags.title.empty(), "crafted ID3v2.3 wrap-inducing frameSize leaves title empty");
        Check(!tags.fromRealTags, "crafted ID3v2.3 wrap-inducing frameSize leaves fromRealTags false");
    }

    // Crafted FLAC fixture: a VORBIS_COMMENT metadata block whose 4-byte vendorLen field (read via
    // ReadU32LE, full 32-bit range) is 0xFFFFFFF6 -- the same wrap-inducing value, for
    // TryReadFlacComments's own `p + vendorLen > fileBytes.size()`-shaped check.
    {
        std::vector<uint8_t> data = {
            'f', 'L', 'a', 'C',
            0x84, 0x00, 0x00, 0x04,             // last-block=1, type=4 (VORBIS_COMMENT), blockLen=4
            0xF6, 0xFF, 0xFF, 0xFF,             // vendorLen, little-endian = 0xFFFFFFF6
        };

        AudioTags tags;
        const bool accepted = AudioTagParser::TryReadFlacComments(data, tags);
        Check(!accepted, "crafted FLAC wrap-inducing vendorLen is rejected, not accepted");
        Check(tags.title.empty(), "crafted FLAC wrap-inducing vendorLen leaves title empty");
        Check(!tags.fromRealTags, "crafted FLAC wrap-inducing vendorLen leaves fromRealTags false");
    }

    std::printf("=== %s (%d failure(s)) ===\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
