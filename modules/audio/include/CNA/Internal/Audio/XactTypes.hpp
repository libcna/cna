// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <variant>
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
        /**
         * @brief Accessibility flags, matching FAudio's ACCESSIBILITY_* bit values exactly
         * (FACT_internal.h): 0x1=public, 0x2=read-only, 0x4=cue-scoped. A variable is either
         * engine-global (public, cue-scoped bit clear -- AudioEngine::GetGlobalVariable/
         * SetGlobalVariable) or cue-scoped (public and cue-scoped bits both set --
         * Cue::GetVariable/SetVariable), never both; read-only only blocks writes, not reads.
         */
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
         * 4=filter Q factor; 5..26 address the 22 reverb preset parameters.
         */
        uint16_t                 parameter;
        /** @brief Curve control points, in file (X-ascending) order. */
        std::vector<XgsRpcPoint> points;
    };

    /** @brief Authored DSP parameter; units depend on the parameter index. */
    struct XgsDspParameter
    {
        uint8_t type = 0;
        float value = 0.0f;
        float minValue = 0.0f;
        float maxValue = 0.0f;
        uint16_t unknown = 0;
    };

    /** @brief One XGS DSP preset, identified by its absolute header byte offset. */
    struct XgsDspPreset
    {
        uint8_t accessibility = 0;
        uint16_t unknown = 0;
        std::vector<XgsDspParameter> parameters;
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
        /** @brief DSP presets in authored order, not yet mutable runtime state. */
        std::vector<XgsDspPreset> dspPresets;
        /** @brief Absolute XGS preset header offset to preset index. */
        std::unordered_map<uint32_t, uint16_t> dspCodeMap;
        /** @brief Category name to index into categories. */
        std::unordered_map<std::string, uint16_t> categoryNameMap;
        /** @brief Variable name to index into variables. */
        std::unordered_map<std::string, uint16_t> variableNameMap;
        /** @brief Absolute byte offset of each RPC (as referenced by XsbSound::rpcCodes) to index into rpcs. */
        std::unordered_map<uint32_t, uint16_t>    rpcCodeMap;
    };

    // ── XWB ─────────────────────────────────────────────────────────────────

    /**
     * @brief Wave data encoding format, as stored in a .XWB wave bank entry.
     *
     * AUD-11-009 (2026-07-17 deep audit): this 2-bit field (confirmed against FACT.h's
     * FACT_WAVEBANKMINIFORMAT_TAG_* constants) has exactly these 4 values -- there is no
     * separate IMA-ADPCM tag at the WaveBank level. `ADPCM` here always means MS-ADPCM;
     * IMA-ADPCM is only a concern for the XNB SoundEffectReader path (a different container
     * format with its own, wider WAVEFORMATEX wFormatTag field), not WaveBank entries.
     */
    enum class XwbFormat : uint8_t { PCM = 0, XMA = 1, ADPCM = 2, WMA = 3 };

    /** @brief One wave entry parsed from a .XWB wave bank file. */
    struct XwbEntry
    {
        /** @brief Original mini-format, including the encoded codec selector and indices. */
        uint32_t  miniFormat = 0;
        /** @brief Authored decoded frame count (flagsAndDuration >> 4); zero when absent. */
        uint32_t  durationSamples = 0;
        /** @brief Raw seek values: WMA cumulative decoded bytes, XMA cumulative frames. */
        std::vector<uint32_t> seekTable;
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
        /** @brief XACT tool/file version from the bank preamble. */
        uint32_t version = 0;
        /** @brief Header version for versions above 41; zero when absent. */
        uint32_t headerVersion = 0;
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
         * from the stream retained by WaveBank's streaming constructor.
         */
        std::vector<uint8_t>     fileData;
        /** @brief True if this bank was parsed via ParseXwbStreamingHeader (see @ref fileData). */
        bool                     streaming = false;
        /** @brief Source file path for diagnostics; not the identity of a retained open stream. */
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
        /** @brief Original selection-weight bounds, retained without normalization. */
        uint8_t  minWeight = 0;
        /** @brief Original upper selection-weight bound. */
        uint8_t  maxWeight = 0;
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
         * (@ref trackVariationType) selects the initial candidate and advances on each
         * event-level loop retrigger. Playback uses that candidate instead of this struct's
         * always-entry-0 parse-time fallback.
         */
        std::vector<XsbTrackVariationEntry> trackVariationEntries{};
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
        /** @brief Authored sound volume in centibels; negative infinity denotes byte-zero silence. */
        float soundVolumeCentibels = 0.0f;
        /** @brief Authored track volume in centibels; zero for a simple sound's implicit track. */
        float trackVolumeCentibels = 0.0f;
    };

    /** @brief XSB event identifiers (FACT_internal.h:121-133). */
    enum class XsbEventType : uint8_t
    {
        Stop = 0, PlayWave = 1, PlayWaveTrackVariation = 3,
        PlayWaveEffectVariation = 4, PlayWaveTrackEffectVariation = 6,
        Pitch = 7, Volume = 8, Marker = 9,
        PitchRepeating = 16, VolumeRepeating = 17, MarkerRepeating = 18
    };

    /** @brief Authored Stop scope and immediate/release flags. */
    struct XsbStopEvent
    {
        /** @brief FACT EVENT_STOP_* bitmask, unmodified. */
        uint8_t flags = 0;
    };

    /** @brief One of the four PlayWave event payloads, before runtime selection. */
    struct XsbPlayWaveEvent
    {
        /** @brief Wave/candidates, loop count, effect ranges and track gain/filter. */
        XsbWaveRef wave{0xFF, 0xFFFF, 0, 1.0f};
        /** @brief Authored wave flags. */
        uint8_t flags = 0;
        /** @brief Authored speaker position. */
        uint16_t position = 0;
        /** @brief Authored speaker angle. */
        uint16_t angle = 0;
        /** @brief Complete packed candidate-count/type/has-variation field; zero for simple waves. */
        uint32_t variationInfo = 0;
        /** @brief Four reserved track-variation bytes. */
        std::array<uint8_t, 4> variationReserved{};
        /** @brief Original effect-volume bytes (wave.effectMin/MaxVolume are centibels). */
        uint8_t minVolumeByte = 0;
        /** @brief Original upper effect-volume byte. */
        uint8_t maxVolumeByte = 0;
    };

    /** @brief Set-value/random-value equation in authored units. */
    struct XsbEquation
    {
        /** @brief FACT EVENT_EQUATION_* bits, including additive mode. */
        uint8_t flags = 0;
        /** @brief Constant value or random lower bound. */
        float value1 = 0;
        /** @brief Random upper bound. */
        float value2 = 0;
        /** @brief Five reserved equation bytes. */
        std::array<uint8_t, 5> reserved{};
    };

    /** @brief Pitch/volume ramp; FACT_internal.c:2444-2447 supplies its units. */
    struct XsbRamp
    {
        /** @brief Initial value in authored pitch/volume units. */
        float initialValue = 0;
        /** @brief Decoded slope: the authored f32 multiplied by 100, as in FACT. */
        float initialSlope = 0;
        /** @brief Authored slope delta. */
        float slopeDelta = 0;
        /** @brief Duration in milliseconds. */
        uint16_t duration = 0;
    };

    /** @brief Pitch/volume payload, preserving equation vs ramp and repeat fields. */
    struct XsbValueEvent
    {
        /** @brief Authored EVENT_SETTINGS_* byte. */
        uint8_t settings = 0;
        /** @brief Equation or ramp selected by settings bit 0. */
        std::variant<XsbEquation, XsbRamp> value;
        /** @brief Equation repeat count; ramp layouts have no repeat trailer, even for repeating types. */
        uint16_t repeats = 0;
        /** @brief Equation repeat interval in milliseconds; zero when absent. */
        uint16_t frequency = 0;
    };

    /** @brief Marker payload; a nonrepeating marker has zero repeat fields. */
    struct XsbMarkerEvent
    {
        /** @brief Authored marker identifier. */
        uint32_t marker = 0;
        /** @brief Authored repeat count. */
        uint16_t repeats = 0;
        /** @brief Authored repeat interval in milliseconds. */
        uint16_t frequency = 0;
    };

    /** @brief A track event in file order; monostate marks an unknown, unsized payload. */
    struct XsbEvent
    {
        /** @brief Complete original packed event header (zero for a synthetic simple-sound event). */
        uint32_t info = 0;
        /** @brief Low five bits of info. */
        XsbEventType type = XsbEventType::PlayWave;
        /** @brief Authored timestamp in milliseconds, (info >> 5) & 0xFFFF. */
        uint16_t timestamp = 0;
        /** @brief Authored random timing offset; no RNG is run by the parser. */
        uint16_t randomOffset = 0;
        /** @brief Payload for the event kind. */
        std::variant<std::monostate, XsbStopEvent, XsbPlayWaveEvent,
                     XsbValueEvent, XsbMarkerEvent> payload;
    };

    /** @brief Track metadata and complete supported event sequence, without scheduling state. */
    struct XsbTrack
    {
        /** @brief Absolute event-block offset; zero for an inline simple sound. */
        uint32_t code = 0;
        /** @brief Track gain combined with sound gain, as used by the legacy wave projection. */
        float volume = 1;
        /** @brief Filter kind, or 0xFF for none. */
        uint8_t filterType = 0xFF;
        /** @brief Authored filter frequency in Hz. */
        uint16_t filterFrequencyHz = 0;
        /** @brief Authored Q-factor byte. */
        uint8_t filterQFactorRaw = 0;
        /** @brief Event count in the file (one for an inline simple sound). */
        uint8_t declaredEventCount = 0;
        /** @brief False if scanning stopped at an unknown event with no defined payload size. */
        bool complete = true;
        /** @brief Events in authored order, including equal or decreasing timestamps. */
        std::vector<XsbEvent> events;
        /** @brief Absolute XGS RPC offsets owned by this track. */
        std::vector<uint32_t> rpcCodes;
        /** @brief Authored sound volume before conversion to an amplitude projection. */
        float soundVolumeCentibels = 0.0f;
        /** @brief This track's authored volume in centibels, before adding sound volume. */
        float trackVolumeCentibels = 0.0f;
    };

    /** @brief One sound entry parsed from a .XSB sound bank file. */
    struct XsbSound
    {
        /** @brief Index into AudioEngine's parsed category list. */
        uint16_t                categoryIndex;
        /** @brief Authored sound amplitude ratio. */
        float                    volume;
        /** @brief Pitch offset in cents, -1200..+1200. */
        int16_t                  pitchCents;
        /** @brief Playback priority; higher values win instance-limit contention. */
        uint8_t                  priority;
        /** @brief Complete typed tracks for future scheduling; playback still uses waves. */
        std::vector<XsbTrack>    tracks;
        /** @brief Compatibility projection: first PlayWave in each track, including no-wave sentinels. */
        std::vector<XsbWaveRef>  waves;
        /**
         * @brief Absolute byte offsets into the owning AudioEngine's parsed XgsData::rpcs
         * (resolved via XgsData::rpcCodeMap); empty if this sound has no bound RPC curves.
         * Sound-level only; track-owned references live in XsbTrack::rpcCodes.
         */
        std::vector<uint32_t>    rpcCodes;
        /** @brief Absolute XGS DSP preset header offsets, preserving authored order. */
        std::vector<uint32_t>    dspCodes;
        /** @brief Authored sound volume in centibels, preserving byte-zero silence. */
        float volumeCentibels = 0.0f;
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
     * @brief Parses header/metadata from an already-open, seekable binary stream.
     *
     * The caller retains ownership and must keep this same stream for lazy wave-data reads;
     * resolving its filename again could select a different file.
     * @param source Open stream positioned by this parser from its beginning.
     * @param path Source name for diagnostics.
     * @return Parsed wave bank data with XwbData::streaming set to true.
     * @throws std::runtime_error if the header is truncated or invalid.
     */
    XwbData ParseXwbStreamingHeader(std::istream& source, const std::string& path);

    /**
     * @brief Parses a .XSB sound bank file.
     *
     * @param data Raw file bytes.
     * @return Parsed sound bank data.
     */
    XsbData ParseXsb(const std::vector<uint8_t>& data);

} // namespace CNA::Internal::Audio
