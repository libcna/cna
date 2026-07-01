// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Internal XACT binary data structures shared between AudioEngine, WaveBank, SoundBank and Cue.
// These types are part of CNA's internal implementation and are not part of the XNA 4.0 public API.

namespace CNA::Internal::Audio
{
    // ── XGS ─────────────────────────────────────────────────────────────────

    struct XgsCategory
    {
        std::string name;
        float       volume;         ///< Linear amplitude [0..1]
        uint16_t    parentIndex;    ///< 0xFFFF = no parent
        uint8_t     instanceLimit;
        uint16_t    fadeInMS;
        uint16_t    fadeOutMS;
    };

    struct XgsVariable
    {
        std::string name;
        float       initialValue;
        float       minValue;
        float       maxValue;
        uint8_t     accessibility; ///< bit0=public, bit1=global, bit2=read-only
    };

    struct XgsData
    {
        std::vector<XgsCategory>                  categories;
        std::vector<XgsVariable>                  variables;
        std::unordered_map<std::string, uint16_t> categoryNameMap;
        std::unordered_map<std::string, uint16_t> variableNameMap;
    };

    // ── XWB ─────────────────────────────────────────────────────────────────

    enum class XwbFormat : uint8_t { PCM = 0, XMA = 1, ADPCM = 2, WMA = 3 };

    struct XwbEntry
    {
        XwbFormat format;
        uint8_t   channels;
        uint32_t  sampleRate;
        uint8_t   bitsPerSample;    ///< 8 or 16 (PCM only; ADPCM always 4-bit encoded)
        uint16_t  blockAlign;       ///< PCM: ch*bps/8 ; ADPCM: (wBlockAlign+22)*ch
        uint16_t  samplesPerBlock;  ///< ADPCM only: (wBlockAlign+16)*2
        uint32_t  dataOffset;       ///< Byte offset from XWB file start into wave data
        uint32_t  dataLength;       ///< Bytes of encoded audio
        uint32_t  loopStartSample;
        uint32_t  loopTotalSamples; ///< 0 = no loop
    };

    struct XwbData
    {
        std::string            bankName;
        std::vector<XwbEntry>  entries;
        std::vector<std::string> entryNames; ///< may be empty
        std::vector<uint8_t>   fileData;     ///< Entire XWB file (for audio extraction)
    };

    // ── XSB ─────────────────────────────────────────────────────────────────

    struct XsbWaveRef
    {
        uint8_t  wavebankIndex;
        uint16_t waveIndex;
        uint8_t  loopCount; ///< 0=no loop, 255=infinite
        float    volume;    ///< Per-track amplitude (already combined with sound volume)
    };

    struct XsbSound
    {
        uint16_t            categoryIndex;
        float               volume;      ///< Amplitude ratio [0..1]
        int16_t             pitchCents;  ///< -1200..+1200
        uint8_t             priority;
        std::vector<XsbWaveRef> waves;   ///< One per track (simplified: first PlayWave event each)
    };

    struct XsbVariEntry
    {
        bool     isSoundEntry; ///< true = soundCode, false = wave ref
        uint32_t soundIndex;   ///< index into XsbData::sounds (if isSoundEntry)
        uint8_t  wavebankIndex;
        uint16_t waveIndex;
        uint8_t  weightMin;
        uint8_t  weightMax;
    };

    struct XsbVariation
    {
        uint32_t code;    ///< Absolute byte offset in XSB file (used as key)
        uint8_t  type;    ///< 0=wave, 1=sound, 2=interactive, 4=compact_wave
        int16_t  variable;
        std::vector<XsbVariEntry> entries;
        mutable uint16_t lastSelected = 0xFFFF;
    };

    struct XsbCue
    {
        bool     isSingleSound;  ///< CUE_FLAG_SINGLE_SOUND (0x04)
        uint32_t soundIndex;     ///< valid when isSingleSound
        uint32_t varIndex;       ///< valid when !isSingleSound
    };

    struct XsbData
    {
        std::vector<std::string>              wavebankNames;
        std::vector<XsbSound>                 sounds;
        std::vector<XsbVariation>             variations;
        std::vector<XsbCue>                   cues;
        std::unordered_map<std::string, uint16_t> cueNameMap;
    };

    // ── Parsers ──────────────────────────────────────────────────────────────

    XgsData ParseXgs(const std::vector<uint8_t>& data);
    XwbData ParseXwb(std::vector<uint8_t> fileData);
    XsbData ParseXsb(const std::vector<uint8_t>& data);

} // namespace CNA::Internal::Audio
