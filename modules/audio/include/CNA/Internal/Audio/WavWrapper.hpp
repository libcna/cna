// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <vector>

namespace CNA::Internal::Audio
{
    /**
     * Builds a minimal, valid in-memory RIFF/WAVE file from raw WAVEFORMATEX fields plus a
     * complete PCM/compressed payload, so it can be handed to the engine's encoded-audio loader
     * instead of its raw-PCM-only path, which only understands uncompressed sample formats.
     *
     * The WAV decoder natively handles PCM (8/16/24/32-bit), IEEE float (32-bit), and MS/IMA
     * ADPCM (4-bit) -- this wrapper is the shared mechanism that lets CNA reach the decoder for
     * every one of those formats from both the XNB `SoundEffectReader` and XACT `WaveBank`
     * entries, without CNA needing its own ADPCM/float decoder.
     *
     * `extensionData` is copied verbatim into the "fmt " chunk after the base 16-byte
     * WAVEFORMATEX fields (as the `cbSize`-prefixed extension), exactly as it would appear in a
     * real WAV file's fmt chunk -- for MS-ADPCM/IMA-ADPCM this must contain a valid
     * `wSamplesPerBlock` (and, for MS-ADPCM, `wNumCoef` plus its coefficient table); pass an empty
     * vector for formats with no extension (PCM, IEEE float).
     *
     * `factSampleFrames`, if nonzero, is written as a "fact" chunk's `dwSampleLength` -- not
     * strictly required by the non-strict WAV loading mode, but included when known since it
     * improves decode accuracy for compressed formats. Pass 0 to omit the fact chunk entirely.
     */
    std::vector<uint8_t> BuildWavFromWaveFormatEx(
        const uint8_t* audioData, uint32_t audioLen,
        uint16_t wFormatTag, uint16_t channels, uint32_t sampleRate, uint32_t avgBytesPerSec,
        uint16_t blockAlign, uint16_t bitsPerSample,
        const std::vector<uint8_t>& extensionData,
        uint32_t factSampleFrames);

    /**
     * Builds the standard MS-ADPCM WAVEFORMATEX extension (`wSamplesPerBlock` + `wNumCoef` + the
     * 7 industry-standard coefficient pairs every MS-ADPCM encoder, including XACT's, uses) --
     * for callers (like XACT `WaveBank`'s compact entries) that only have `samplesPerBlock`
     * derived from the container format, not a real per-file WAVEFORMATEX extension to forward.
     */
    std::vector<uint8_t> BuildStandardMsAdpcmExtension(uint16_t samplesPerBlock);

    /**
     * Appends a minimal "smpl" chunk encoding one loop point (start/end, in decoded PCM sample
     * frames) to an already-built WAV file, so `SoundEffect::FromStream`'s own
     * `TryParseWavSmplChunk` picks it up automatically. This is the only way to carry an
     * authored loop region through `SoundEffect::FromStream` (which uses the encoded-audio load
     * path, not a raw-buffer constructor that takes loop points directly) -- shared
     * by both the XNB `SoundEffectReader` and XACT `WaveBank` entries, whose WAV-wrapped formats
     * (PCM8/float/MS-ADPCM/IMA-ADPCM) both need this to honor an authored loop region.
     *
     * No-op if `loopLength <= 0` (nothing to encode).
     */
    void AppendSmplChunkIfLooped(std::vector<uint8_t>& wav, int32_t loopStart, int32_t loopLength);
}
