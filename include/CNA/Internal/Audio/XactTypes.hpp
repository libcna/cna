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
        /**
         * @brief Behavior applied when a new cue would exceed instanceLimit: 0=fail (reject the
         * new cue), 1=queue, 2=replace oldest, 3=replace quietest, 4=replace lowest priority.
         * Matches FAudio's own max_instance_behavior enum (FACT_internal.h).
         */
        uint8_t     maxInstanceBehavior;
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

    /** @brief One control point in an RPC (Runtime Parameter Control) curve. */
    struct XgsRpcPoint
    {
        /** @brief X axis: the bound variable's value at this point. */
        float   x;
        /** @brief Y axis: the resulting parameter value (centibels for volume, cents for pitch) at this point. */
        float   y;
        /** @brief Interpolation shape from this point to the next: 0=linear, 1=fast, 2=slow, 3=sin/cos. */
        uint8_t type;
    };

    /**
     * @brief One RPC (Runtime Parameter Control) curve parsed from a .XGS file: maps a bound
     * variable's value to a sound parameter via a piecewise curve.
     */
    struct XgsRpc
    {
        /** @brief Index into XgsData::variables that this curve's X axis reads from. */
        uint16_t                 variable;
        /**
         * @brief Target parameter: 0=volume, 1=pitch, 2=reverb send, 3=filter frequency,
         * 4=filter Q factor; 5 or greater targets a DSP preset parameter (unsupported).
         */
        uint16_t                 parameter;
        /** @brief Curve control points, in file (X-ascending) order. */
        std::vector<XgsRpcPoint> points;
    };

    /** @brief Parsed contents of a .XGS global settings file. */
    struct XgsData
    {
        /** @brief All categories, in file order. */
        std::vector<XgsCategory>                  categories;
        /** @brief All global variables, in file order. */
        std::vector<XgsVariable>                  variables;
        /** @brief All RPC curves, in file order. */
        std::vector<XgsRpc>                        rpcs;
        /** @brief Category name to index into categories. */
        std::unordered_map<std::string, uint16_t> categoryNameMap;
        /** @brief Variable name to index into variables. */
        std::unordered_map<std::string, uint16_t> variableNameMap;
        /** @brief Absolute byte offset of each RPC (as referenced by XsbSound::rpcCodes) to index into rpcs. */
        std::unordered_map<uint32_t, uint16_t>    rpcCodeMap;
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
        /**
         * @brief Header/metadata segments of the file (everything except the wave-data
         * segment). When @ref streaming is false, this holds the entire file, and entry audio
         * is sliced directly out of it. When true, entry audio must instead be read lazily
         * from @ref sourcePath (see WaveBank's streaming constructor).
         */
        std::vector<uint8_t>     fileData;
        /** @brief True if this bank was parsed via ParseXwbStreamingHeader (see @ref fileData). */
        bool                     streaming = false;
        /** @brief Source file path; only meaningful when @ref streaming is true. */
        std::string              sourcePath;
    };

    // ── XSB ─────────────────────────────────────────────────────────────────

    /**
     * @brief Selection algorithm for a `PlayWaveTrackVariation`-family event's candidate wave
     * list, matching FAudio's own `variation_type` values (`FACT_internal.h`).
     */
    enum class XsbTrackVariationType : uint8_t
    {
        /** @brief Cycle through entries in authored order, wrapping at the end. */
        Ordered = 0,
        /** @brief Like Ordered, but the starting point is chosen by a weighted random pick. */
        OrderedFromRandom = 1,
        /** @brief Weighted random pick among all entries every time. */
        Random = 2,
        /** @brief Weighted random pick, excluding whichever entry was just selected. */
        RandomNoRepeats = 3,
        /** @brief Same selection rule as RandomNoRepeats (FAudio treats both identically). */
        Shuffle = 4,
    };

    /** @brief One candidate wave entry in a `PlayWaveTrackVariation`-family event's variation list. */
    struct XsbTrackVariationEntry
    {
        /** @brief Index of the wave within its wave bank. */
        uint16_t waveIndex;
        /** @brief Index of the wave bank this wave belongs to. */
        uint8_t  wavebankIndex;
        /** @brief Selection weight (authored maxWeight - minWeight), matching FAudio's own conversion. */
        uint8_t  weight;
    };

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
        /**
         * @brief This track's filter type: 0=low-pass, 1=band-pass, 2=high-pass (matches
         * FAudioFilterType); 0xFF = no filter on this track. Only ever populated for complex
         * sounds (see XactParser.cpp's per-track metadata parsing); simple sounds never carry
         * filter data in the format at all. FAudio's own bit-decode of the source filterData
         * field structurally never produces 1 (band-pass) -- replicated as-is, see P9-XACT-010.
         */
        uint8_t  filterType = 0xFF;
        /** @brief Desired filter cutoff frequency in Hz, as authored. Valid only when filterType != 0xFF. */
        uint16_t filterFrequencyHz = 0;
        /**
         * @brief Raw XACT Q-factor byte (0..255). Converted to FAudioFilterParameters::OneOverQ
         * via min(3.0f / qfactor, 1.0f) at apply time (FACT_internal.c). Valid only when
         * filterType != 0xFF.
         */
        uint8_t  filterQFactorRaw = 0;

        /**
         * @brief Full candidate list for a `PlayWaveTrackVariation`-family event
         * (`P11-XACT-002`), matching FAudio's own per-instance selection
         * (`FACT_internal.c`'s `FACT_INTERNAL_GetNextWave`). Empty for a plain `PlayWave`/
         * `PlayWaveEffectVariation` event -- in that case @ref waveIndex/@ref wavebankIndex
         * above are used directly, unchanged. When non-empty, the real selection algorithm
         * (@ref trackVariationType) is run once at `Cue::Play()` time and the chosen entry
         * overwrites @ref waveIndex/@ref wavebankIndex, instead of this struct's own
         * always-entry-0 parse-time fallback.
         */
        std::vector<XsbTrackVariationEntry> trackVariationEntries;
        /** @brief Selection algorithm for @ref trackVariationEntries; meaningless when that list is empty. */
        XsbTrackVariationType trackVariationType = XsbTrackVariationType::Ordered;

        /**
         * @brief `variationFlags` bitmask for a `PlayWaveEffectVariation`-family event
         * (`P11-XACT-003`), matching FAudio's own `VARIATION_FLAG_PITCH`/`VARIATION_FLAG_VOLUME`/
         * `VARIATION_FLAG_FREQUENCY_Q` bits (`FACT_internal.h`). Zero (the default) means "no
         * per-play randomization on any axis" -- true for every plain `PlayWave`/
         * `PlayWaveTrackVariation` event, not just ones that happen to author an empty range.
         */
        uint16_t effectVariationFlags = 0;
        /** @brief Authored pitch-randomization range, in cents (matches FAudio's raw int16 units). Meaningful only when @ref effectVariationFlags has the pitch bit set. */
        int16_t effectMinPitch = 0;
        /** @brief See @ref effectMinPitch. */
        int16_t effectMaxPitch = 0;
        /** @brief Authored volume-randomization range, in centibels (matches FAudio's `read_volbyte` units). Meaningful only when @ref effectVariationFlags has the volume bit set. */
        float effectMinVolume = 0.0f;
        /** @brief See @ref effectMinVolume. */
        float effectMaxVolume = 0.0f;
        /** @brief Authored filter cutoff frequency-randomization range, in Hz. Meaningful only when @ref effectVariationFlags has the frequency/Q bit set. */
        float effectMinFrequency = 0.0f;
        /** @brief See @ref effectMinFrequency. */
        float effectMaxFrequency = 0.0f;
        /**
         * @brief Authored filter Q-factor-randomization range, already in reciprocal-ready Q
         * units, unlike the per-track @ref filterQFactorRaw byte (which needs the
         * raw-byte-decode formula `min(3.0f/qfactor, 1.0f)` instead of a plain reciprocal).
         * Meaningful only when @ref effectVariationFlags has the frequency/Q bit set.
         */
        float effectMinQFactor = 0.0f;
        /** @brief See @ref effectMinQFactor. */
        float effectMaxQFactor = 0.0f;
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
        /**
         * @brief Absolute byte offsets into the owning AudioEngine's parsed XgsData::rpcs
         * (resolved via XgsData::rpcCodeMap); empty if this sound has no bound RPC curves.
         * Sound-level only -- per-track RPC references are parsed for cursor alignment but not
         * retained (CHECKLIST.md).
         */
        std::vector<uint32_t>    rpcCodes;
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
        /** @brief Lower bound of the interactive variable's value range; valid only when the owning XsbVariation::type is 3 (interactive). */
        float    varMin = 0.0f;
        /** @brief Upper bound of the interactive variable's value range; valid only when the owning XsbVariation::type is 3 (interactive). */
        float    varMax = 0.0f;
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
        /**
         * @brief Authored release fade duration in milliseconds (P9-STOP-010), 0 if none.
         *
         * Only present on a COMPLEX cue record; a "simple" cue's format has no such field at all
         * (matches FAudio's FACT_internal.c, which hardcodes 0 for simple cues).
         */
        uint16_t fadeOutMS = 0;
        /**
         * @brief Maximum concurrent instances allowed for this specific cue definition
         * (cue-level instanceLimit, independent of any category-level limit).
         *
         * Defaults to 0xFF (effectively unlimited), matching FAudio's own hardcoded default for
         * a "simple" cue record, which has no such field at all (`FACT_internal.c`).
         */
        uint8_t  instanceLimit = 0xFF;
        /** @brief Authored fade-in duration in milliseconds when this cue's own instanceLimit forces a fade-in; 0 for a simple cue. */
        uint16_t fadeInMS = 0;
        /**
         * @brief Behavior applied when this cue's own instanceLimit would be exceeded: same
         * encoding as XgsCategory::maxInstanceBehavior (0=fail, 1=queue, 2=replace oldest,
         * 3=replace quietest, 4=replace lowest priority). Defaults to 0 (fail), matching
         * FAudio's hardcoded default for a simple cue -- irrelevant there since instanceLimit
         * defaults to 0xFF (never reached).
         */
        uint8_t  maxInstanceBehavior = 0;
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
     * @brief Parses a .XWB wave bank's header/metadata segments only, reading them directly
     * from disk (the wave-data segment is left unread; entry audio must be read lazily from
     * @p path later, per entry, via its dataOffset/dataLength).
     *
     * @param path Path to the .XWB file on disk.
     * @return Parsed wave bank data with XwbData::streaming set to true.
     * @throws std::runtime_error if @p path cannot be opened or the header is truncated/invalid.
     */
    XwbData ParseXwbStreamingHeader(const std::string& path);

    /**
     * @brief Parses a .XSB sound bank file.
     *
     * @param data Raw file bytes.
     * @return Parsed sound bank data.
     */
    XsbData ParseXsb(const std::vector<uint8_t>& data);

} // namespace CNA::Internal::Audio
