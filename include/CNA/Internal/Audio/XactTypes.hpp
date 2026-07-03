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

    /** @brief One category entry parsed from a .XGS global settings file. */
    struct XgsCategory
    {
        /** @brief Category name. */
        std::string name;
        /** @brief Linear amplitude [0..1]. */
        float       volume;
        /** @brief Parent category index; 0xFFFF = no parent. */
        uint16_t    parentIndex;
        /** @brief Maximum concurrent instances allowed for this category. */
        uint8_t     instanceLimit;
        /** @brief Fade-in duration in milliseconds. */
        uint16_t    fadeInMS;
        /** @brief Fade-out duration in milliseconds. */
        uint16_t    fadeOutMS;
    };

    /** @brief One global variable entry parsed from a .XGS global settings file. */
    struct XgsVariable
    {
        /** @brief Variable name. */
        std::string name;
        /** @brief Initial value. */
        float       initialValue;
        /** @brief Minimum allowed value. */
        float       minValue;
        /** @brief Maximum allowed value. */
        float       maxValue;
        /** @brief Accessibility flags: bit0=public, bit1=global, bit2=read-only. */
        uint8_t     accessibility;
    };

    /** @brief Parsed contents of a .XGS global settings file. */
    struct XgsData
    {
        /** @brief All categories, in file order. */
        std::vector<XgsCategory>                  categories;
        /** @brief All global variables, in file order. */
        std::vector<XgsVariable>                  variables;
        /** @brief Category name to index into categories. */
        std::unordered_map<std::string, uint16_t> categoryNameMap;
        /** @brief Variable name to index into variables. */
        std::unordered_map<std::string, uint16_t> variableNameMap;
    };

    // ── XWB ─────────────────────────────────────────────────────────────────

    /** @brief Wave data encoding format, as stored in a .XWB wave bank entry. */
    enum class XwbFormat : uint8_t { PCM = 0, XMA = 1, ADPCM = 2, WMA = 3 };

    /** @brief One wave entry parsed from a .XWB wave bank file. */
    struct XwbEntry
    {
        /** @brief Encoding format. */
        XwbFormat format;
        /** @brief Channel count. */
        uint8_t   channels;
        /** @brief Sample rate in Hz. */
        uint32_t  sampleRate;
        /** @brief 8 or 16 (PCM only; ADPCM is always 4-bit encoded). */
        uint8_t   bitsPerSample;
        /** @brief PCM: channels * bitsPerSample / 8. ADPCM: (wBlockAlign + 22) * channels. */
        uint16_t  blockAlign;
        /** @brief ADPCM only: (wBlockAlign + 16) * 2. */
        uint16_t  samplesPerBlock;
        /** @brief Byte offset from the XWB file start into wave data. */
        uint32_t  dataOffset;
        /** @brief Bytes of encoded audio. */
        uint32_t  dataLength;
        /** @brief Loop start position, in samples. */
        uint32_t  loopStartSample;
        /** @brief Loop length in samples; 0 = no loop. */
        uint32_t  loopTotalSamples;
    };

    /** @brief Parsed contents of a .XWB wave bank file. */
    struct XwbData
    {
        /** @brief Wave bank name. */
        std::string              bankName;
        /** @brief All wave entries, in file order. */
        std::vector<XwbEntry>    entries;
        /** @brief Per-entry names; may be empty if the bank has no name table. */
        std::vector<std::string> entryNames;
        /** @brief Entire XWB file, kept in memory for audio extraction. */
        std::vector<uint8_t>     fileData;
    };

    // ── XSB ─────────────────────────────────────────────────────────────────

    /** @brief Reference to a single wave, as resolved from a sound's track events. */
    struct XsbWaveRef
    {
        /** @brief Index of the wave bank this wave belongs to. */
        uint8_t  wavebankIndex;
        /** @brief Index of the wave within its wave bank. */
        uint16_t waveIndex;
        /** @brief 0 = no loop, 255 = infinite. */
        uint8_t  loopCount;
        /** @brief Per-track amplitude, already combined with the sound's own volume. */
        float    volume;
    };

    /** @brief One sound entry parsed from a .XSB sound bank file. */
    struct XsbSound
    {
        /** @brief Index into AudioEngine's parsed category list. */
        uint16_t                categoryIndex;
        /** @brief Amplitude ratio [0..1]. */
        float                    volume;
        /** @brief Pitch offset in cents, -1200..+1200. */
        int16_t                  pitchCents;
        /** @brief Playback priority; higher values win instance-limit contention. */
        uint8_t                  priority;
        /** @brief One entry per track (simplified: first PlayWave event each). */
        std::vector<XsbWaveRef>  waves;
    };

    /** @brief One entry in a variation table (see XsbVariation). */
    struct XsbVariEntry
    {
        /** @brief True if soundIndex is valid (sound entry); false for a direct wave reference. */
        bool     isSoundEntry;
        /** @brief Index into XsbData::sounds; valid when isSoundEntry is true. */
        uint32_t soundIndex;
        /** @brief Wave bank index; valid when isSoundEntry is false. */
        uint8_t  wavebankIndex;
        /** @brief Wave index; valid when isSoundEntry is false. */
        uint16_t waveIndex;
        /** @brief Lower bound of this entry's selection weight range. */
        uint8_t  weightMin;
        /** @brief Upper bound of this entry's selection weight range. */
        uint8_t  weightMax;
    };

    /** @brief A cue's variation table: the set of sounds/waves it can pick from on Play(). */
    struct XsbVariation
    {
        /** @brief Absolute byte offset in the XSB file; used as a lookup key. */
        uint32_t                  code;
        /** @brief 0=wave, 1=sound, 2=clip (unsupported by FAudio), 3=interactive, 4=compact_wave. */
        uint8_t                   type;
        /** @brief Global/cue variable index used to select an entry when type is interactive. */
        int16_t                   variable;
        /** @brief All entries in this table, in file order. */
        std::vector<XsbVariEntry> entries;
        /** @brief Index of the entry picked by the last selection; 0xFFFF = none yet. */
        mutable uint16_t          lastSelected = 0xFFFF;
    };

    /** @brief One cue entry parsed from a .XSB sound bank file. */
    struct XsbCue
    {
        /** @brief CUE_FLAG_SINGLE_SOUND (0x04): true if this cue plays a single sound directly. */
        bool     isSingleSound;
        /** @brief Index into XsbData::sounds; valid when isSingleSound is true. */
        uint32_t soundIndex;
        /** @brief Index into XsbData::variations; valid when isSingleSound is false. */
        uint32_t varIndex;
    };

    /** @brief Parsed contents of a .XSB sound bank file. */
    struct XsbData
    {
        /** @brief Wave bank names referenced by this sound bank, in file order. */
        std::vector<std::string>                  wavebankNames;
        /** @brief All sounds, in file order. */
        std::vector<XsbSound>                     sounds;
        /** @brief All variation tables, in file order. */
        std::vector<XsbVariation>                 variations;
        /** @brief All cues, in file order. */
        std::vector<XsbCue>                       cues;
        /** @brief Cue name to index into cues. */
        std::unordered_map<std::string, uint16_t> cueNameMap;
    };

    // ── Parsers ──────────────────────────────────────────────────────────────

    /**
     * @brief Parses a .XGS global settings file.
     *
     * @param data Raw file bytes.
     * @return Parsed global settings data.
     */
    XgsData ParseXgs(const std::vector<uint8_t>& data);

    /**
     * @brief Parses a .XWB wave bank file.
     *
     * @param fileData Raw file bytes; consumed and retained (see XwbData::fileData).
     * @return Parsed wave bank data.
     */
    XwbData ParseXwb(std::vector<uint8_t> fileData);

    /**
     * @brief Parses a .XSB sound bank file.
     *
     * @param data Raw file bytes.
     * @return Parsed sound bank data.
     */
    XsbData ParseXsb(const std::vector<uint8_t>& data);

} // namespace CNA::Internal::Audio
