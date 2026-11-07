// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/XactTypes.hpp"
#include "Internal/AudioLog.hpp"
#include "Internal/XactStreamingHeader.hpp"
#include "CNA/Internal/PathUtf8.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Audio
{
    // ── Binary reading helpers ────────────────────────────────────────────────

    struct Ctx
    {
        const uint8_t* start;
        const uint8_t* end;
        const uint8_t* cur;

        bool valid() const { return cur < end; }

        uint8_t u8()
        {
            if (cur >= end) throw std::runtime_error("XACT parse: read past end");
            return *cur++;
        }
        uint16_t u16()
        {
            if (static_cast<std::size_t>(end - cur) < 2) throw std::runtime_error("XACT parse: read past end");
            uint16_t v; std::memcpy(&v, cur, 2); cur += 2; return v;
        }
        uint32_t u32()
        {
            if (static_cast<std::size_t>(end - cur) < 4) throw std::runtime_error("XACT parse: read past end");
            uint32_t v; std::memcpy(&v, cur, 4); cur += 4; return v;
        }
        int16_t s16()
        {
            if (static_cast<std::size_t>(end - cur) < 2) throw std::runtime_error("XACT parse: read past end");
            int16_t v; std::memcpy(&v, cur, 2); cur += 2; return v;
        }
        int32_t s32()
        {
            if (static_cast<std::size_t>(end - cur) < 4) throw std::runtime_error("XACT parse: read past end");
            int32_t v; std::memcpy(&v, cur, 4); cur += 4; return v;
        }
        float f32()
        {
            if (static_cast<std::size_t>(end - cur) < 4) throw std::runtime_error("XACT parse: read past end");
            float v; std::memcpy(&v, cur, 4); cur += 4; return v;
        }

        void skip(std::size_t n)
        {
            // AUD-11-005: matches seek()'s own AUDIO-PARSER-001 fix below -- validate in the
            // integer domain BEFORE ever forming `cur + n`. Computing that pointer first is
            // undefined behavior for an adversarially large `n` (e.g. a caller-side field-size
            // subtraction that underflowed to a huge uint32_t/size_t value), even though it
            // doesn't reliably trip ASan/UBSan on a typical 64-bit target (the resulting pointer
            // value usually still lands within the process's mapped address space, so neither the
            // pointer-overflow nor address sanitizers have anything to flag unless it's actually
            // dereferenced) -- it's still real UB per the standard, and defense-in-depth against a
            // caller relying solely on this bounds check the way the compact XWB entry loop
            // (XactParser.cpp) does.
            const std::size_t remaining = static_cast<std::size_t>(end - cur);
            if (n > remaining) throw std::runtime_error("XACT parse: skip past end");
            cur += n;
        }

        void seek(uint32_t absOffset)
        {
            // AUDIO-PARSER-001 (external audit, 2026-07-16): validate against the buffer's own
            // size as a plain integer comparison BEFORE ever forming `start + absOffset` --
            // computing that pointer first (the old `if (start + absOffset > end)`) is undefined
            // behavior for a corrupt/attacker-controlled absOffset large enough to send the
            // pointer arithmetic outside the buffer, which a malformed .xsb/.xwb/.xgs offset
            // field can trivially supply.
            const std::size_t size = static_cast<std::size_t>(end - start);
            if (static_cast<std::size_t>(absOffset) > size)
                throw std::runtime_error("XACT parse: seek past end");
            cur = start + absOffset;
        }

        uint32_t pos() const { return static_cast<uint32_t>(cur - start); }

        /// Read a null-terminated string from the current position, advance past it.
        std::string cstr()
        {
            // AUDIO-PARSER-001 (external audit, 2026-07-16): strnlen() returning exactly `maxlen`
            // means no null terminator was found within the remaining buffer at all (a corrupt or
            // truncated file) -- the old code didn't check for this and unconditionally advanced
            // `cur += len + 1`, pushing `cur` one byte past `end`. That single overrun then made
            // every later use of `end - cur` in this same function compute a negative
            // std::ptrdiff_t that wraps to a huge std::size_t once cast to `maxlen`, turning the
            // *next* cstr() call's strnlen() into a real out-of-bounds read over corrupt input.
            // Throwing here instead matches every other Ctx accessor's own out-of-bounds
            // contract (u8/u16/u32/skip/seek all already throw rather than silently continue).
            const char* p = reinterpret_cast<const char*>(cur);
            std::size_t maxlen = static_cast<std::size_t>(end - cur);
            std::size_t len = strnlen(p, maxlen);
            if (len == maxlen)
                throw std::runtime_error("XACT parse: unterminated string");
            std::string s(p, len);
            cur += len + 1;
            return s;
        }
    };

    // ── Volume conversion (FAudio FACT formula) ───────────────────────────────

    static float ReadVolByte(Ctx& ctx)
    {
        uint8_t b = ctx.u8();
        if (b == 0) return 0.0f;
        // Returns centibels
        return static_cast<float>((3969.0 * std::log10(b / 28240.0)) + 8715.0);
    }

    static float CentibelsToAmplitude(float centibels)
    {
        return static_cast<float>(std::pow(10.0, centibels / 2000.0));
    }

    static float ReadAuthoredVolumeCentibels(Ctx& ctx)
    {
        uint8_t b = ctx.u8();
        if (b == 0) return -std::numeric_limits<float>::infinity();
        return static_cast<float>((3969.0 * std::log10(b / 28240.0)) + 8715.0);
    }

    static float ReadVolByteAsAmplitude(Ctx& ctx)
    {
        return CentibelsToAmplitude(ReadAuthoredVolumeCentibels(ctx));
    }

    // ── XACT flags ────────────────────────────────────────────────────────────

    static constexpr uint8_t  SOUND_FLAG_COMPLEX       = 0x01;
    static constexpr uint8_t  SOUND_FLAG_HAS_RPC       = 0x02;
    static constexpr uint8_t  SOUND_FLAG_HAS_TRACK_RPC = 0x04;
    static constexpr uint8_t  SOUND_FLAG_RPC_MASK      = 0x0E;
    static constexpr uint8_t  SOUND_FLAG_HAS_DSP       = 0x10;
    static constexpr uint8_t  CUE_FLAG_SINGLE_SOUND    = 0x04;

    // P9-XACT-014: sentinel for "this cue/variation-entry's sound code didn't resolve to any
    // parsed sound" (a corrupt/malformed .xsb -- every code a real XACT-tool-built file emits
    // always resolves, since `soundCodeMap` is built from the exact same file's sound entries).
    // Deliberately NOT 0 -- falling back to 0 would silently alias an unresolvable reference onto
    // whatever sound happens to be first in the bank and play it, instead of playing nothing.
    // `XsbCue`/`XsbVariEntry`'s `soundIndex` bounds checks in Cue.cpp (`< xsb->sounds.size()`)
    // already treat any out-of-range value as "no sound" -- this sentinel just relies on that
    // same path instead of adding a new one.
    static constexpr uint32_t kInvalidSoundIndex = 0xFFFFFFFFu;

    static constexpr uint8_t  FACTEVENT_STOP                        = 0;
    static constexpr uint8_t  FACTEVENT_PLAYWAVE                    = 1;
    static constexpr uint8_t  FACTEVENT_PLAYWAVETRACKVARIATION      = 3;
    static constexpr uint8_t  FACTEVENT_PLAYWAVEEFFECTVARIATION     = 4;
    static constexpr uint8_t  FACTEVENT_PLAYWAVETRACKEFFECTVARIATION = 6;
    static constexpr uint8_t  FACTEVENT_PITCH                       = 7;
    static constexpr uint8_t  FACTEVENT_VOLUME                      = 8;
    static constexpr uint8_t  FACTEVENT_MARKER                      = 9;
    static constexpr uint8_t  FACTEVENT_PITCHREPEATING              = 16;
    static constexpr uint8_t  FACTEVENT_VOLUMEREPEATING             = 17;
    static constexpr uint8_t  FACTEVENT_MARKERREPEATING             = 18;

    static constexpr uint8_t  EVENT_SETTINGS_RAMP = 0x01;

    // ── Track event parser ────────────────────────────────────────────────────

    // FACT_internal.c:2283-2486 defines the versioned layouts. Keep file order and
    // advance the shared sound cursor through the LAST track's entire event block.
    static XsbTrack ParseTrackEvents(Ctx& ctx, XsbTrack track,uint16_t contentVersion)
    {
        ctx.seek(track.code);
        track.declaredEventCount = ctx.u8();
        track.events.reserve(track.declaredEventCount);
        for (uint16_t i = 0; i < track.declaredEventCount; ++i)
        {
            XsbEvent event;
            event.info = ctx.u32();
            event.type = static_cast<XsbEventType>(event.info & 0x1Fu);
            event.timestamp = static_cast<uint16_t>((event.info >> 5) & 0xFFFFu);
            event.randomOffset = ctx.u16();
            if (ctx.u8() != 0xFF)
                throw std::runtime_error("XSB: invalid event separator");
            const uint8_t type = static_cast<uint8_t>(event.type);
            if (type == FACTEVENT_STOP)
            {
                event.payload = XsbStopEvent{ctx.u8()};
            }
            else if (type == FACTEVENT_PLAYWAVE ||
                     type == FACTEVENT_PLAYWAVEEFFECTVARIATION ||
                     type == FACTEVENT_PLAYWAVETRACKVARIATION ||
                     type == FACTEVENT_PLAYWAVETRACKEFFECTVARIATION)
            {
                XsbPlayWaveEvent play;
                auto& wave = play.wave;
                wave.volume = track.volume;
                wave.soundVolumeCentibels = track.soundVolumeCentibels;
                wave.trackVolumeCentibels = track.trackVolumeCentibels;
                wave.filterType = track.filterType;
                wave.filterFrequencyHz = track.filterFrequencyHz;
                wave.filterQFactorRaw = track.filterQFactorRaw;
                play.flags = ctx.u8();
                const bool complex = type == FACTEVENT_PLAYWAVETRACKVARIATION ||
                                     type == FACTEVENT_PLAYWAVETRACKEFFECTVARIATION;
                if (!complex)
                {
                    wave.waveIndex = ctx.u16();
                    wave.wavebankIndex = ctx.u8();
                }
                wave.loopCount = ctx.u8();
                play.position = ctx.u16();
                play.angle = ctx.u16();
                if (type == FACTEVENT_PLAYWAVEEFFECTVARIATION ||
                    type == FACTEVENT_PLAYWAVETRACKEFFECTVARIATION)
                {
                    wave.effectMinPitch = ctx.s16();
                    wave.effectMaxPitch = ctx.s16();
                    Ctx volumeCtx = ctx;
                    play.minVolumeByte = volumeCtx.u8();
                    play.maxVolumeByte = volumeCtx.u8();
                    wave.effectMinVolume = ReadVolByte(ctx);
                    wave.effectMaxVolume = ReadVolByte(ctx);
                    if(contentVersion>43)
                    {
                        wave.effectMinFrequency = ctx.f32();
                        wave.effectMaxFrequency = ctx.f32();
                        wave.effectMinQFactor = ctx.f32();
                        wave.effectMaxQFactor = ctx.f32();
                        wave.effectVariationFlags = ctx.u16();
                    }
                    else
                    {
                        //3.0 has only pitch/volume axes; unknown0x02/0x08 map to no flags.
                        const uint8_t oldFlags=ctx.u8();
                        wave.effectVariationFlags=static_cast<uint16_t>(((oldFlags&0xC0u)<<6)
                            |((oldFlags&0x30u)<<4)|(oldFlags&0x05u));
                    }
                }
                if (complex)
                {
                    play.variationInfo = ctx.u32();
                    const uint16_t count = static_cast<uint16_t>(play.variationInfo & 0xFFFFu);
                    wave.trackVariationType = static_cast<XsbTrackVariationType>(
                        (play.variationInfo >> 16) & 0x07u);
                    for (auto& byte : play.variationReserved) byte = ctx.u8();
                    // Each candidate occupies five bytes; refuse truncated counts before allocation.
                    if (count > static_cast<std::size_t>(ctx.end - ctx.cur) / 5)
                        throw std::runtime_error("XSB: truncated wave candidate table");
                    wave.trackVariationEntries.reserve(count);
                    for (uint16_t j = 0; j < count; ++j)
                    {
                        const uint16_t index = ctx.u16();
                        const uint8_t bank = ctx.u8();
                        const uint8_t minWeight = ctx.u8();
                        const uint8_t maxWeight = ctx.u8();
                        wave.trackVariationEntries.push_back({index, bank,
                            static_cast<uint8_t>(maxWeight - minWeight), minWeight, maxWeight});
                    }
                    if (!wave.trackVariationEntries.empty())
                    {
                        wave.waveIndex = wave.trackVariationEntries[0].waveIndex;
                        wave.wavebankIndex = wave.trackVariationEntries[0].wavebankIndex;
                    }
                }
                event.payload = std::move(play);
            }
            else if (type == FACTEVENT_PITCH || type == FACTEVENT_VOLUME ||
                     type == FACTEVENT_PITCHREPEATING || type == FACTEVENT_VOLUMEREPEATING)
            {
                XsbValueEvent value;
                value.settings = ctx.u8();
                if (value.settings & EVENT_SETTINGS_RAMP)
                {
                    XsbRamp ramp;
                    ramp.initialValue = ctx.f32();
                    ramp.initialSlope = ctx.f32() * 100.0f; // FACT_internal.c:2445.
                    ramp.slopeDelta = ctx.f32();
                    ramp.duration = ctx.u16();
                    value.value = ramp;
                }
                else
                {
                    XsbEquation equation;
                    equation.flags = ctx.u8();
                    equation.value1 = ctx.f32();
                    equation.value2 = ctx.f32();
                    for (auto& byte : equation.reserved) byte = ctx.u8();
                    value.value = equation;
                    if (type == FACTEVENT_PITCHREPEATING || type == FACTEVENT_VOLUMEREPEATING)
                    {
                        value.repeats = ctx.u16();
                        value.frequency = ctx.u16();
                    }
                }
                event.payload = value;
            }
            else if (type == FACTEVENT_MARKER || type == FACTEVENT_MARKERREPEATING)
            {
                XsbMarkerEvent marker;
                marker.marker = ctx.u32();
                if (type == FACTEVENT_MARKERREPEATING)
                {
                    marker.repeats = ctx.u16();
                    marker.frequency = ctx.u16();
                }
                event.payload = marker;
            }
            else
            {
                // Preserve the existing unknown-type stop-scan contract. Its payload size
                // is undefined, so expose the incomplete sequence instead of inventing bytes.
                track.complete = false;
                track.events.push_back(std::move(event));
                break;
            }
            track.events.push_back(std::move(event));
        }
        return track;
    }

    static XsbWaveRef FirstPlayWave(const XsbTrack& track)
    {
        for (const auto& event : track.events)
            if (const auto* play = std::get_if<XsbPlayWaveEvent>(&event.payload))
                return play->wave;
        XsbWaveRef wave{0xFF, 0xFFFF, 0, track.volume};
        wave.soundVolumeCentibels = track.soundVolumeCentibels;
        wave.trackVolumeCentibels = track.trackVolumeCentibels;
        wave.filterType = track.filterType;
        wave.filterFrequencyHz = track.filterFrequencyHz;
        wave.filterQFactorRaw = track.filterQFactorRaw;
        return wave;
    }

    // ── XGS Parser ───────────────────────────────────────────────────────────

    XgsData ParseXgs(const std::vector<uint8_t>& data)
    {
        XgsData result;

        if (data.size() < 77)
            throw std::runtime_error("XGS: file too small");

        Ctx ctx{data.data(), data.data() + data.size(), data.data()};

        uint32_t magic = ctx.u32();
        // P9-AUDIT-003: accepting the BE "FSGX" magic is cosmetic only -- every other multi-byte
        // field below is read via Ctx::u16()/u32()/f32(), a raw memcpy with no byte-swap logic
        // anywhere in this file. A genuinely BE-authored (e.g. Xbox 360-built) file would pass
        // this check and then silently misparse every subsequent field, not throw. ParseXwb's own
        // magic check only accepts the LE form, so this "BE support" isn't even applied uniformly
        // across the three parsers. Not fixed here: real byte-swap support is new feature work
        // outside this audit's scope, not a one-line correction.
        if (magic != 0x46534758u && magic != 0x58475346u)
            throw std::runtime_error("XGS: invalid magic");

        uint16_t contentVersion = ctx.u16();
        if (contentVersion != 46)
            LogAudio(LogLevel::WARN, "[XGS] Warning: unexpected contentVersion ", contentVersion);

        ctx.u16(); // toolVersion
        ctx.u16(); // unknown
        ctx.skip(8); // lastModified
        ctx.u8();  // platform (3=Xbox, 7=Xbox360)

        uint16_t categoryCount      = ctx.u16();
        uint16_t variableCount      = ctx.u16();
        ctx.u16(); // blob1Count
        ctx.u16(); // blob2Count
        uint16_t rpcCount           = ctx.u16();
        const uint16_t dspPresetCount = ctx.u16();
        const uint16_t dspParameterCount = ctx.u16();

        uint32_t categoryOffset         = ctx.u32();
        uint32_t variableOffset         = ctx.u32();
        ctx.u32(); // blob1Offset
        ctx.u32(); // categoryNameIndexOffset (sorted index, not used directly)
        ctx.u32(); // blob2Offset
        ctx.u32(); // variableNameIndexOffset
        uint32_t categoryNameOffset     = ctx.u32();
        uint32_t variableNameOffset     = ctx.u32();
        uint32_t rpcOffset              = ctx.u32();
        uint32_t dspPresetOffset = 0;
        uint32_t dspParameterOffset = 0;
        // Empty DSP tables need no offsets. Preserve existing minimal no-DSP fixtures.
        if (dspPresetCount != 0 || dspParameterCount != 0)
        {
            dspPresetOffset = ctx.u32();
            dspParameterOffset = ctx.u32();
        }

        // Category names (sequential null-terminated at categoryNameOffset)
        std::vector<std::string> catNames;
        catNames.reserve(categoryCount);
        {
            Ctx nc = ctx;
            nc.seek(categoryNameOffset);
            for (uint16_t i = 0; i < categoryCount; ++i)
                catNames.push_back(nc.cstr());
        }

        // Category data (10 bytes each at categoryOffset): instanceLimit, fadeInMS, fadeOutMS,
        // maxInstanceBehavior, parentIndex, volume, visibility.
        result.categories.resize(categoryCount);
        {
            Ctx cc = ctx;
            cc.seek(categoryOffset);
            for (uint16_t i = 0; i < categoryCount; ++i)
            {
                auto& cat = result.categories[i];
                cat.name          = (i < catNames.size()) ? catNames[i] : ("cat" + std::to_string(i));
                cat.instanceLimit = cc.u8();           // 1 byte
                cat.fadeInMS      = cc.u16();          // 2 bytes
                cat.fadeOutMS     = cc.u16();          // 2 bytes
                // P9-CATEGORY-005: retained (was discarded) -- see FACT_internal.c's
                // ParseXGSHeader, which shifts the same byte right by 3 to isolate
                // max_instance_behavior from its low 3 bits (unused/reserved by FACT itself).
                cat.maxInstanceBehavior = cc.u8() >> 3; // 1 byte
                cat.parentIndex   = cc.u16();          // 2 bytes
                cat.volume        = ReadVolByteAsAmplitude(cc); // 1 byte
                cc.u8();                               // visibility
                // Total: 1+2+2+1+2+1+1 = 10 bytes ✓
            }
        }

        // Variable names
        std::vector<std::string> varNames;
        varNames.reserve(variableCount);
        {
            Ctx nc = ctx;
            nc.seek(variableNameOffset);
            for (uint16_t i = 0; i < variableCount; ++i)
                varNames.push_back(nc.cstr());
        }

        // Variable data (13 bytes each: 1+4+4+4 = 13)
        result.variables.resize(variableCount);
        {
            Ctx vc = ctx;
            vc.seek(variableOffset);
            for (uint16_t i = 0; i < variableCount; ++i)
            {
                auto& v = result.variables[i];
                v.name         = (i < varNames.size()) ? varNames[i] : ("var" + std::to_string(i));
                v.accessibility = vc.u8();
                v.initialValue  = vc.f32();
                v.minValue      = vc.f32();
                v.maxValue      = vc.f32();
            }
        }

        // RPC data (rpcOffset): variable:u16 + pointCount:u8 + parameter:u16, then pointCount
        // points of {x:f32, y:f32, type:u8} (FACT_internal.c FACT_INTERNAL_ParseAudioEngine).
        // Each entry's own starting offset is recorded as its "code" (rpcCodeMap), the same
        // absolute-offset lookup scheme XsbSound::rpcCodes and XsbVariation entries both use
        // for cross-referencing into a sibling table (P9-XACT-005/006).
        result.rpcs.resize(rpcCount);
        if (rpcCount > 0)
        {
            Ctx rc = ctx;
            rc.seek(rpcOffset);
            for (uint16_t i = 0; i < rpcCount; ++i)
            {
                const uint32_t code = rc.pos();
                auto& rpc = result.rpcs[i];
                rpc.variable  = rc.u16();
                uint8_t pointCount = rc.u8();
                rpc.parameter = rc.u16();
                rpc.points.resize(pointCount);
                for (uint8_t j = 0; j < pointCount; ++j)
                {
                    rpc.points[j].x    = rc.f32();
                    rpc.points[j].y    = rc.f32();
                    rpc.points[j].type = rc.u8();
                }
                result.rpcCodeMap[code] = i;
            }
        }

        // Oracle: FAudio FACT_internal.c:2166-2207, preset headers are 5 bytes;
        // each parameter is type:u8, value/min/max:f32, unknown:u16 = 15 bytes.
        // Validate counts before allocating. Malformed-table refusal is CNA's bounded-input
        // contract, not a claim about native XACT's malformed input acceptance.
        const auto requireTable = [&](uint32_t offset, std::size_t count, std::size_t stride)
        {
            if (count != 0 && (offset > data.size() || count > (data.size() - offset) / stride))
                throw std::runtime_error("XGS: truncated DSP table");
        };
        requireTable(dspPresetOffset, dspPresetCount, 5);
        requireTable(dspParameterOffset, dspParameterCount, 15);
        if (dspPresetCount != 0)
        {
            Ctx dc = ctx;
            Ctx pc = ctx;
            dc.seek(dspPresetOffset);
            pc.seek(dspParameterOffset);
            result.dspPresets.resize(dspPresetCount);
            std::size_t consumed = 0;
            for (uint16_t i = 0; i < dspPresetCount; ++i)
            {
                const uint32_t code = dc.pos();
                auto& preset = result.dspPresets[i];
                preset.accessibility = dc.u8();
                const uint16_t count = dc.u16();
                preset.unknown = dc.u16();
                if (count > static_cast<std::size_t>(dspParameterCount) - consumed)
                    throw std::runtime_error("XGS: DSP parameter count exceeds table");
                consumed += count;
                preset.parameters.resize(count);
                for (auto& parameter : preset.parameters)
                {
                    parameter.type = pc.u8();
                    parameter.value = pc.f32();
                    parameter.minValue = pc.f32();
                    parameter.maxValue = pc.f32();
                    parameter.unknown = pc.u16();
                }
                result.dspCodeMap.emplace(code, i);
            }
            if (consumed != dspParameterCount)
                throw std::runtime_error("XGS: DSP parameter count disagrees with table");
        }
        else if (dspParameterCount != 0)
        {
            throw std::runtime_error("XGS: DSP parameters without presets");
        }

        // Build lookup maps
        for (uint16_t i = 0; i < categoryCount; ++i)
            result.categoryNameMap[result.categories[i].name] = i;
        for (uint16_t i = 0; i < variableCount; ++i)
            result.variableNameMap[result.variables[i].name] = i;

        return result;
    }

    // ── XWB Parser ───────────────────────────────────────────────────────────

    namespace
    {
        // Reads magic + version + (optional) headerVersion + the fixed 5-entry segment table,
        // shared by ParseXwb and ParseXwbStreamingHeader so both agree on segment layout.
        uint32_t ReadXwbSegmentTable(Ctx& ctx, uint32_t (&segOffset)[5], uint32_t (&segLength)[5],
                                     uint32_t* headerVersion = nullptr)
        {
            uint32_t magic = ctx.u32();
            if (magic != 0x444E4257u)
                throw std::runtime_error("XWB: invalid magic (expected WBND)");

            uint32_t version = ctx.u32();
            if (version > 46)
                LogAudio(LogLevel::WARN,
                    "[XWB] Warning: version ", version, " may not be fully supported");

            const uint32_t header = version > 43 ? ctx.u32() : 0;
            if (headerVersion) *headerVersion = header;

            // 5 segments: each {dwOffset: u32, dwLength: u32}
            for (int i = 0; i < 5; ++i)
            {
                segOffset[i] = ctx.u32();
                segLength[i] = ctx.u32();
            }
            return version;
        }
    }

    XwbData ParseXwb(std::vector<uint8_t> fileData)
    {
        XwbData result;
        result.fileData = std::move(fileData);
        const auto& fd = result.fileData;

        if (fd.size() < 52)
            throw std::runtime_error("XWB: file too small");

        Ctx ctx{fd.data(), fd.data() + fd.size(), fd.data()};

        uint32_t segOffset[5], segLength[5];
        result.version = ReadXwbSegmentTable(ctx, segOffset, segLength, &result.headerVersion);

        // BANKDATA segment
        // Layout: flags(u32), entryCount(u32), bankName[64], entryMetaDataSize(u32),
        //         entryNameElementSize(u32), alignment(u32), compactFormat(u32), buildTime(u64)
        ctx.seek(segOffset[0]);
        uint32_t wbFlags             = ctx.u32();
        uint32_t entryCount          = ctx.u32();
        // AUD-11-026 (found via fuzzing prep): every other count field this parser reads
        // (categoryCount/variableCount/rpcCount/pointCount/wavebankCount/soundCount/totalCues)
        // is naturally bounded by its own 8/16-bit field width, but entryCount is a full
        // uint32_t (up to ~4.29 billion) fed straight into `result.entries.resize(entryCount)`
        // below with no validation at all -- unlike a quick, clean std::bad_alloc, a resize this
        // large can succeed via Linux's virtual-memory overcommit (reserving address space
        // without committing physical pages) and then hang for a long time -- or trigger the OOM
        // killer -- while default-constructing billions of entries, a real DoS-class defect, not
        // just a theoretical one. A legitimate entryCount can never exceed the number of bytes
        // actually left in the file (each entry needs at least 1 real byte to exist) -- this
        // bound is deliberately generous (the real per-entry minimum is at least 4 bytes even for
        // the most compact format) but cheap, exact, and sufficient to keep the allocation on the
        // same order of magnitude as the file itself. Matches this plan's established D7 policy
        // (throw rather than silently attempt a huge allocation) and XnbContainerFuzzTests.cpp's
        // own explicit standard that an uncaught std::bad_alloc is "an allocation-bomb guard gap,
        // not an acceptable outcome."
        if (static_cast<uint64_t>(entryCount) > static_cast<uint64_t>(ctx.end - ctx.start))
            throw std::runtime_error("XWB: corrupt wave bank (entryCount implausibly large for the file size)");
        {
            // AUD-11-017/018 (found via ASan): Bank name (64 bytes, null-terminated). The old
            // `strnlen(p, 64)` scanned up to 64 bytes from `p` regardless of how many real bytes
            // actually remained in the buffer -- for a truncated/corrupt file with fewer than 64
            // bytes left here, this reads past the end of `fileData`'s heap allocation before the
            // `ctx.skip(64)` right below ever gets a chance to catch the truncation. Confirmed as
            // a genuine, empirically reproduced ASan heap-buffer-overflow (a standalone repro
            // against a deliberately truncated fixture), not just a theoretical concern -- capping
            // the scan to the real remaining bytes first, mirroring `cstr()`'s own established
            // AUDIO-PARSER-001 pattern, means `strnlen` never reads past `ctx.end`; the `skip(64)`
            // immediately after still throws cleanly on a genuinely truncated file, exactly as
            // before, just without an OOB read on the way there.
            const char* p = reinterpret_cast<const char*>(ctx.cur);
            const std::size_t nameMaxLen = std::min<std::size_t>(64, static_cast<std::size_t>(ctx.end - ctx.cur));
            result.bankName = std::string(p, strnlen(p, nameMaxLen));
            ctx.skip(64);
        }
        uint32_t entryMetaDataSize   = ctx.u32();
        uint32_t entryNameElemSize   = ctx.u32();
        uint32_t alignment           = ctx.u32();
        uint32_t compactFormat       = ctx.u32();
        ctx.skip(8); // BuildTime

        bool isCompact  = (wbFlags & 0x00020000u) != 0;
        bool hasNames   = (wbFlags & 0x00010000u) != 0;

        // ENTRYMETADATA segment
        result.entries.resize(entryCount);
        ctx.seek(segOffset[1]);

        if (isCompact)
        {
            // AUD-11-003 (2026-07-17 deep audit): `alignment` is an unvalidated uint32_t read
            // straight from the file. Zero would silently make every compact entry's offset
            // collapse to 0 (every entry claiming the same start, not a crash but genuinely wrong
            // data -- a real "distorted/missing audio" symptom class, not just a theoretical
            // hardening concern); an oversized value could overflow the `rawOffsetUnits[i] *
            // alignment` multiplication below in 32-bit arithmetic and silently wrap to a wrong
            // (but still in-bounds-looking) offset. D7: throw rather than silently produce wrong
            // data, same policy already applied to the deviation/offset checks further down.
            if (alignment == 0)
                throw std::runtime_error("XWB: corrupt compact wave bank (zero alignment)");

            // AUD-11-005: entryMetaDataSize must be at least 4 to hold the compact per-entry u32
            // (offsetUnits:21, deviation:11) the loop below unconditionally reads via ctx.u32().
            // Unlike the non-compact branch's carefully graduated conditional reads (which
            // structurally guarantee the bytes consumed never exceed entryMetaDataSize, however
            // small it is), this branch has no such guard: an adversarial/corrupt value smaller
            // than 4 would make `entryMetaDataSize - 4` (both uint32_t) underflow to a huge value
            // passed straight to ctx.skip(). ctx.skip()'s own bounds check already prevents this
            // from ever reading/writing out of bounds (confirmed empirically: neither GCC's nor
            // Clang's ASan+UBSan flag anything for this exact pattern, since the resulting pointer
            // value stays within the process's mapped address space and is only ever compared,
            // never dereferenced) -- but relying on that generic "skip past end" catch instead of
            // failing right here, with a diagnostic naming the actual problem, is worse for
            // anyone debugging a real corrupt file. D7: throw rather than silently produce wrong
            // data, same policy as the alignment check just above.
            if (entryMetaDataSize < 4)
                throw std::runtime_error("XWB: corrupt compact wave bank (entryMetaDataSize too small)");

            // Compact format: 32-bit per entry: dwOffset (21 bits, units of `alignment`),
            // dwLengthDeviation (11 bits). The deviation is how many bytes shorter than the
            // aligned span the real audio is, not the length itself, so a non-last entry's
            // length comes from the gap to the next entry's offset minus that deviation.
            //
            // A-12 (2026-07-17 deep audit): the LAST entry is different -- verified against the
            // real, current, actively-maintained FAudio source (FACT_internal.c's compact-entry
            // parsing, ~line 3106-3124): its length is the remainder of the wave-data segment
            // with NO deviation subtracted at all, which is exactly what the `else` branch below
            // already does. (FAudio's own non-last-entry computation in that same function
            // reads as a genuine, long-standing bug -- it subtracts an entry's own
            // just-computed offset from itself, always yielding zero, unchanged since at least a
            // 2018-12-18 commit -- CNA deliberately does not replicate that, computing a real
            // length from the gap to the next entry's offset instead; see
            // CompactWaveBankComputesLengthsFromConsecutiveOffsets/
            // CompactWaveBankLastEntryLengthIgnoresItsOwnDeviation in XactParserTests.cpp.)
            std::vector<uint32_t> rawOffsetUnits(entryCount);
            std::vector<uint32_t> deviations(entryCount);
            for (uint32_t i = 0; i < entryCount; ++i)
            {
                uint32_t ce = ctx.u32();
                rawOffsetUnits[i] = ce & 0x1FFFFFu;
                deviations[i]     = (ce >> 21) & 0x7FFu;
                ctx.skip(entryMetaDataSize - 4); // advance past entry
            }

            uint8_t wba          = static_cast<uint8_t>((compactFormat >> 23) & 0xFFu);
            uint8_t compactFmtTag = static_cast<uint8_t>(compactFormat & 0x3u);
            uint8_t compactChannels = static_cast<uint8_t>((compactFormat >> 2) & 0x7u);

            // Compact-bank format bits are shared by every entry, so this is computed once --
            // same ADPCM formula the non-compact branch below applies per entry (IN-10: the
            // compact branch previously never derived these for ADPCM-encoded compact banks).
            uint16_t compactBlockAlign;
            uint16_t compactSamplesPerBlock;
            if (compactFmtTag == 2) // ADPCM
            {
                compactSamplesPerBlock = static_cast<uint16_t>((wba + 16) * 2);
                compactBlockAlign      = static_cast<uint16_t>((wba + 22) * compactChannels);
            }
            else // PCM
            {
                compactSamplesPerBlock = 0;
                compactBlockAlign      = wba;
            }

            for (uint32_t i = 0; i < entryCount; ++i)
            {
                // AUD-11-003: compute in 64-bit and validate before narrowing -- rawOffsetUnits[i]
                // is up to 21 bits and alignment is a fully unconstrained uint32_t from the file,
                // so their product can exceed UINT32_MAX and silently wrap in 32-bit arithmetic,
                // producing a wrong-but-plausible-looking dataOffset instead of failing loudly.
                const uint64_t offset64 = static_cast<uint64_t>(rawOffsetUnits[i]) * alignment;
                if (offset64 > 0xFFFFFFFFu)
                    throw std::runtime_error("XWB: corrupt compact entry (offset overflows 32 bits)");
                uint32_t offset = static_cast<uint32_t>(offset64);

                result.entries[i].miniFormat      = compactFormat;
                result.entries[i].format          = static_cast<XwbFormat>(compactFmtTag);
                result.entries[i].channels        = compactChannels;
                result.entries[i].sampleRate      = (compactFormat >> 5) & 0x3FFFFu;
                result.entries[i].bitsPerSample   = ((compactFormat >> 31) & 1u) ? 16 : 8;
                result.entries[i].blockAlign      = compactBlockAlign;
                result.entries[i].samplesPerBlock = compactSamplesPerBlock;

                const uint64_t absoluteOffset = static_cast<uint64_t>(segOffset[4]) + offset;
                if (absoluteOffset > std::numeric_limits<uint32_t>::max())
                    throw std::runtime_error("XWB: wave entry offset exceeds 32 bits");
                result.entries[i].dataOffset = static_cast<uint32_t>(absoluteOffset);

                // dwLengthDeviation is the aligned span's slack in bytes, not the length
                // itself, so a non-last entry's real length must not exceed the gap to the next
                // entry's offset. Check in 64-bit before narrowing so a corrupt/adversarial file
                // can't underflow this into a huge uint32_t value (D7: throw rather than
                // silently clamp).
                if (i + 1 < entryCount)
                {
                    const uint64_t nextOffset = static_cast<uint64_t>(rawOffsetUnits[i + 1]) * alignment;
                    const uint64_t consumed   = static_cast<uint64_t>(offset) + deviations[i];
                    if (nextOffset < consumed)
                        throw std::runtime_error("XWB: corrupt compact entry (deviation exceeds gap to next entry)");
                    result.entries[i].dataLength = static_cast<uint32_t>(nextOffset - consumed);
                }
                else
                {
                    // Last entry: remainder of the wave-data segment, deviation NOT subtracted --
                    // confirmed to match real FAudio exactly (see the comment above this loop).
                    if (offset > segLength[4])
                        throw std::runtime_error("XWB: corrupt compact entry (offset exceeds wave-data segment)");
                    result.entries[i].dataLength = segLength[4] - offset;
                }
            }
        }
        else
        {
            // Normal format: FACTWaveBankEntry (up to 24 bytes: dwFlagsAndDuration,
            // Format, PlayRegion{dwOffset,dwLength}, LoopRegion{dwStartSample,dwTotalSamples}).
            // Older XWB versions may use a narrower entryMetaDataSize; fields at or beyond
            // that boundary belong to the next entry (or lie outside the segment for the
            // last entry) and must be treated as absent (zero), matching FAudio's
            // zero-init-then-partial-read of FACTWaveBankEntry.
            for (uint32_t i = 0; i < entryCount; ++i)
            {
                const uint8_t* entryPtr = ctx.cur;

                uint32_t flagsAndDuration = 0;
                uint32_t fmt              = 0;
                uint32_t playOffset       = 0;
                uint32_t playLength       = 0;
                uint32_t loopStart        = 0;
                uint32_t loopTotal        = 0;

                if (entryMetaDataSize >= 4)  flagsAndDuration = ctx.u32();
                if (entryMetaDataSize >= 8)  fmt              = ctx.u32();
                if (entryMetaDataSize >= 12) playOffset       = ctx.u32();
                if (entryMetaDataSize >= 16) playLength       = ctx.u32();
                if (entryMetaDataSize >= 20) loopStart        = ctx.u32();
                if (entryMetaDataSize >= 24) loopTotal        = ctx.u32();

                // Advance to the next entry's true start -- covers both narrower
                // (<24) and wider (>24, vendor-extended) element sizes.
                ctx.skip(entryMetaDataSize - static_cast<uint32_t>(ctx.cur - entryPtr));

                if (entryMetaDataSize < 24)
                {
                    playLength = segLength[4]; // use entire wave data
                }

                // Decode FACTWaveBankMiniWaveFormat bitfield
                uint8_t  fmtTag    = static_cast<uint8_t>(fmt & 0x3u);
                uint8_t  channels  = static_cast<uint8_t>((fmt >> 2) & 0x7u);
                uint32_t sampleRate = (fmt >> 5) & 0x3FFFFu;
                uint8_t  wBlockAlign = static_cast<uint8_t>((fmt >> 23) & 0xFFu);
                uint8_t  bps        = static_cast<uint8_t>((fmt >> 31) & 0x1u);

                auto& e = result.entries[i];
                e.miniFormat        = fmt;
                e.durationSamples   = flagsAndDuration >> 4;
                e.format            = static_cast<XwbFormat>(fmtTag);
                e.channels          = channels;
                e.sampleRate        = sampleRate;
                e.bitsPerSample     = bps ? 16 : 8;
                const uint64_t absoluteOffset = static_cast<uint64_t>(segOffset[4]) + playOffset;
                if (absoluteOffset > std::numeric_limits<uint32_t>::max())
                    throw std::runtime_error("XWB: wave entry offset exceeds 32 bits");
                e.dataOffset        = static_cast<uint32_t>(absoluteOffset);
                e.dataLength        = playLength;
                e.loopStartSample   = loopStart;
                e.loopTotalSamples  = loopTotal;

                if (fmtTag == 2) // ADPCM
                {
                    // wSamplesPerBlock = (wBlockAlign + 16) * 2
                    // nBlockAlign      = (wBlockAlign + 22) * channels
                    e.samplesPerBlock = static_cast<uint16_t>((wBlockAlign + 16) * 2);
                    e.blockAlign      = static_cast<uint16_t>((wBlockAlign + 22) * channels);
                }
                else // PCM
                {
                    e.samplesPerBlock = 0;
                    e.blockAlign      = static_cast<uint16_t>(channels * (bps ? 2 : 1));
                }
            }
        }

        // FACT_internal.c:3230-3288: offsets are relative to the end of the
        // per-entry offset array, not the segment start. Restrict every read to
        // this segment even when more file bytes follow it.
        if ((wbFlags & 0x00080000u) != 0 && segLength[2] != 0)
        {
            const uint64_t begin = segOffset[2];
            const uint64_t length = segLength[2];
            const uint64_t offsetBytes = static_cast<uint64_t>(entryCount) * 4;
            if (begin + length > fd.size() || offsetBytes > length)
                throw std::runtime_error("XWB: truncated seek-table segment");
            const uint64_t records = begin + offsetBytes;
            const uint64_t end = begin + length;
            for (uint32_t i = 0; i < entryCount; ++i)
            {
                ctx.seek(static_cast<uint32_t>(begin + static_cast<uint64_t>(i) * 4));
                const uint32_t relative = ctx.u32();
                if (relative == 0xFFFFFFFFu) continue;
                const uint64_t table = records + relative;
                if (table > end || end - table < 4 ||
                    table > std::numeric_limits<uint32_t>::max())
                    throw std::runtime_error("XWB: seek-table offset out of segment");
                ctx.seek(static_cast<uint32_t>(table));
                const uint32_t count = ctx.u32();
                if (static_cast<uint64_t>(count) * 4 > end - table - 4)
                    throw std::runtime_error("XWB: seek-table count out of segment");
                auto& values = result.entries[i].seekTable;
                values.reserve(count);
                for (uint32_t j = 0; j < count; ++j) values.push_back(ctx.u32());
            }
        }

        // ENTRYNAMES segment (optional)
        if (hasNames && entryNameElemSize > 0 && segLength[3] > 0)
        {
            result.entryNames.resize(entryCount);
            ctx.seek(segOffset[3]);
            for (uint32_t i = 0; i < entryCount; ++i)
            {
                // AUD-11-017/018: entryNameElemSize is a fully unvalidated uint32_t read straight
                // from the file -- capping the strnlen scan to the real remaining bytes first
                // (same fix and same reasoning as the bankName field above) means a corrupt/huge
                // value can't drive an out-of-bounds read here either; the ctx.skip() right after
                // still throws cleanly on a genuinely truncated file.
                const char* p = reinterpret_cast<const char*>(ctx.cur);
                const std::size_t nameMaxLen = std::min<std::size_t>(
                    entryNameElemSize, static_cast<std::size_t>(ctx.end - ctx.cur));
                result.entryNames[i] = std::string(p, strnlen(p, nameMaxLen));
                ctx.skip(entryNameElemSize);
            }
        }

        return result;
    }

    XwbData ParseXwbStreamingHeader(const std::string& path)
    {
        std::ifstream f(CNA::Internal::PathFromUtf8(path), std::ios::binary);
        if (!f.is_open())
            throw std::runtime_error("XWB: cannot open file for streaming");

        return ParseXwbStreamingHeader(f, path);
    }

    XwbData ParseXwbStreamingHeader(std::istream& f, const std::string& path)
    {
        return ParseXwbStreamingHeaderAtOffset(f, path, 0);
    }

    XwbData ParseXwbStreamingHeaderAtOffset(
        std::istream& f, const std::string& path, std::uint32_t offset)
    {
        f.clear();
        f.seekg(static_cast<std::streamoff>(offset));

        // Large enough to safely cover magic + version + optional headerVersion + the fixed
        // 5-entry segment table (at most 4+4+4+40 = 52 bytes) without knowing the version yet.
        std::vector<uint8_t> preamble(64);
        f.read(reinterpret_cast<char*>(preamble.data()), static_cast<std::streamsize>(preamble.size()));
        const auto preambleRead = f.gcount();
        if (preambleRead < 52)
            throw std::runtime_error("XWB: file too small");
        preamble.resize(static_cast<std::size_t>(preambleRead));

        Ctx headCtx{preamble.data(), preamble.data() + preamble.size(), preamble.data()};
        uint32_t segOffset[5], segLength[5];
        ReadXwbSegmentTable(headCtx, segOffset, segLength);

        // Segments 0-3 (bank data, entry metadata, seek tables, entry names) must be resident
        // for ParseXwb to build entries/names; segment 4 (wave data) is by far the largest part
        // of a real .xwb and is read lazily from the retained stream in WaveBank::GetSoundEffect.
        uint64_t prefixLen = 0;
        for (int i = 0; i < 4; ++i)
            prefixLen = std::max<uint64_t>(prefixLen, static_cast<uint64_t>(segOffset[i]) + segLength[i]);

        // Validate untrusted segment extents before allocating their resident prefix.
        f.clear();
        f.seekg(0, std::ios::end);
        const std::streamoff sourceSize = f.tellg();
        if (sourceSize < 0 || static_cast<uint64_t>(offset) + prefixLen > static_cast<uint64_t>(sourceSize) ||
            prefixLen > std::numeric_limits<std::size_t>::max() ||
            prefixLen > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
            throw std::runtime_error("XWB: metadata segments exceed the source file");
        std::vector<uint8_t> prefix(static_cast<std::size_t>(prefixLen));
        f.clear();
        f.seekg(static_cast<std::streamoff>(offset));
        f.read(reinterpret_cast<char*>(prefix.data()), static_cast<std::streamsize>(prefixLen));
        if (static_cast<uint64_t>(f.gcount()) != prefixLen)
            throw std::runtime_error("XWB: truncated header/metadata segments");

        XwbData result = ParseXwb(std::move(prefix));
        // XACT segment/play offsets are relative to the embedded bank; lazy reads
        // address the retained whole file. Refuse offsets outside XwbEntry's 32-bit domain.
        for (auto& entry : result.entries)
        {
            const auto absoluteOffset = static_cast<std::uint64_t>(entry.dataOffset) + offset;
            if (absoluteOffset > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("XWB: wave entry file offset exceeds 32 bits");
            entry.dataOffset = static_cast<std::uint32_t>(absoluteOffset);
        }
        result.streaming  = true;
        result.sourcePath = path;
        return result;
    }

    // ── XSB Parser ───────────────────────────────────────────────────────────

    XsbData ParseXsb(const std::vector<uint8_t>& data)
    {
        XsbData result;

        if (data.size() < 0x50)
            throw std::runtime_error("XSB: file too small");

        Ctx ctx{data.data(), data.data() + data.size(), data.data()};

        uint32_t magic = ctx.u32();
        // P9-AUDIT-003: BE magic acceptance here is cosmetic only, same as ParseXgs above --
        // see that function's comment.
        if (magic != 0x4B424453u && magic != 0x5344424Bu)
            throw std::runtime_error("XSB: invalid magic (expected SDBK)");

        uint16_t contentVersion = ctx.u16();
        if (contentVersion != 46)
            LogAudio(LogLevel::WARN, "[XSB] Warning: contentVersion ", contentVersion);

        ctx.u16(); // toolVersion
        ctx.u16(); // CRC
        ctx.skip(8); // lastModified
        ctx.u8();  // platform

        uint16_t cueSimpleCount  = ctx.u16();
        uint16_t cueComplexCount = ctx.u16();
        ctx.u16(); // unknown
        ctx.u16(); // cueTotalAlign
        uint8_t  wavebankCount   = ctx.u8();
        uint16_t soundCount      = ctx.u16();
        ctx.u16(); // cueNameLength
        ctx.u16(); // unknown

        int32_t cueSimpleOffset     = ctx.s32();
        int32_t cueComplexOffset    = ctx.s32();
        int32_t cueNameOffset       = ctx.s32();
        ctx.s32(); // unknown
        int32_t variationOffset     = ctx.s32();
        ctx.s32(); // transitionOffset
        int32_t wavebankNameOffset  = ctx.s32();
        ctx.s32(); // cueHashOffset
        int32_t cueNameIndexOffset  = ctx.s32();
        int32_t soundOffset         = ctx.s32();

        // SoundBank name (64-byte null-terminated follows header)
        ctx.skip(64);

        // Wavebank names
        result.wavebankNames.resize(wavebankCount);
        if (wavebankNameOffset >= 0)
        {
            Ctx wc = ctx;
            wc.seek(static_cast<uint32_t>(wavebankNameOffset));
            for (uint8_t i = 0; i < wavebankCount; ++i)
            {
                // AUD-11-017/018: same fix and reasoning as XWB's bankName/entryNames fields --
                // cap the strnlen scan to the real remaining bytes first so a truncated/corrupt
                // file can't drive an out-of-bounds read here; wc.skip(64) right after still
                // throws cleanly on genuine truncation.
                const char* p = reinterpret_cast<const char*>(wc.cur);
                const std::size_t nameMaxLen = std::min<std::size_t>(64, static_cast<std::size_t>(wc.end - wc.cur));
                result.wavebankNames[i] = std::string(p, strnlen(p, nameMaxLen));
                wc.skip(64);
            }
        }

        // ── Sound parsing ────────────────────────────────────────────────────
        // soundCodes[i] = byte offset of sound i from file start
        std::vector<uint32_t> soundCodes(soundCount);
        result.sounds.resize(soundCount);

        if (soundOffset >= 0)
        {
            Ctx sc = ctx;
            sc.seek(static_cast<uint32_t>(soundOffset));

            for (uint16_t i = 0; i < soundCount; ++i)
            {
                soundCodes[i] = sc.pos();
                auto& sound = result.sounds[i];

                uint8_t flags        = sc.u8();
                sound.categoryIndex  = sc.u16();
                sound.volumeCentibels = ReadAuthoredVolumeCentibels(sc);
                sound.volume         = CentibelsToAmplitude(sound.volumeCentibels);
                sound.pitchCents     = sc.s16();
                sound.priority       = sc.u8();
                sc.u16(); // soundLength — skip

                // FACT reads (per FACTSoundBank_Prepare, FACT_internal.c): trackCount (complex)
                // or the inline simple wave ref -- THEN the RPC block -- THEN the DSP block --
                // and only THEN, for complex sounds, the per-track metadata array and track
                // events (IN-8: this used to read per-track metadata/events immediately after
                // trackCount, before the RPC/DSP blocks, misinterpreting RPC/DSP bytes as track
                // metadata and cascading corruption into every sound parsed after this one).
                uint8_t trackCount = 0;
                if (flags & SOUND_FLAG_COMPLEX)
                {
                    trackCount = sc.u8();
                }
                else
                {
                    // Simple sound: one inline wave reference
                    uint16_t waveIdx = sc.u16();
                    uint8_t  wbIdx   = sc.u8();
                    sound.waves.push_back({wbIdx, waveIdx, 0, sound.volume});
                    sound.waves.back().soundVolumeCentibels = sound.volumeCentibels;
                    // FACT_internal.c:2646-2664 synthesizes one zero-time PlayWave for simple sounds.
                    XsbTrack track;
                    track.volume = sound.volume;
                    track.soundVolumeCentibels = sound.volumeCentibels;
                    track.declaredEventCount = 1;
                    XsbPlayWaveEvent play;
                    play.wave = sound.waves.back();
                    XsbEvent event;
                    event.payload = std::move(play);
                    track.events.push_back(std::move(event));
                    sound.tracks.push_back(std::move(track));
                }

                std::vector<std::vector<uint32_t>> trackRpcCodes(
                    (flags & SOUND_FLAG_COMPLEX) ? trackCount : 1);

                // RPC code references. Layout (FACT_internal.c, FACTSoundBank_Prepare's
                // SOUND_FLAG_RPC_MASK branch, parse_rpc_codes): rpcDataLength:u16 (unused --
                // parsed structurally instead of skipped by length), then if HAS_RPC a
                // sound-level list (count:u8 + count*code:u32), then if HAS_TRACK_RPC one such
                // list per track. Retain both owner levels separately for reverb-send application.
                // Absolute codes keep the same lookup scheme as sound-level references.
                if (flags & SOUND_FLAG_RPC_MASK)
                {
                    sc.u16(); // rpcDataLength -- unused

                    if (flags & SOUND_FLAG_HAS_RPC)
                    {
                        const uint8_t rpcCodeCount = sc.u8();
                        sound.rpcCodes.reserve(rpcCodeCount);
                        for (uint8_t r = 0; r < rpcCodeCount; ++r)
                            sound.rpcCodes.push_back(sc.u32());
                    }

                    if (flags & SOUND_FLAG_HAS_TRACK_RPC)
                    {
                        const uint8_t trackRpcOwnerCount =
                            (flags & SOUND_FLAG_COMPLEX) ? trackCount : static_cast<uint8_t>(1);
                        for (uint8_t t = 0; t < trackRpcOwnerCount; ++t)
                        {
                            const uint8_t trackRpcCodeCount = sc.u8();
                            for (uint8_t r = 0; r < trackRpcCodeCount; ++r)
                                trackRpcCodes[t].push_back(sc.u32());
                        }
                    }
                }

                // Oracle: FACT_internal.c:2696-2714. The length is unused;
                // retain the authored absolute preset codes instead of merely skipping them.
                if (flags & SOUND_FLAG_HAS_DSP)
                {
                    sc.u16(); // DSP presets length -- unused
                    const uint8_t dspCodeCount = sc.u8();
                    sound.dspCodes.reserve(dspCodeCount);
                    for (uint8_t d = 0; d < dspCodeCount; ++d)
                        sound.dspCodes.push_back(sc.u32());
                }

                if (flags & SOUND_FLAG_COMPLEX)
                {
                    struct TrackMeta
                    {
                        float    volumeCentibels;
                        uint32_t code;
                        uint8_t  filterType=0xFF;
                        uint16_t frequency=0;
                        uint8_t  qfactor=0;
                    };
                    std::vector<TrackMeta> tracks(trackCount);

                    for (uint8_t t = 0; t < trackCount; ++t)
                    {
                        tracks[t].volumeCentibels = ReadAuthoredVolumeCentibels(sc);
                        tracks[t].code = sc.u32();
                        if(contentVersion==43)continue;

                        // filterData (2 bytes) + frequency (2 bytes) (P9-XACT-010/011). Bit
                        // layout (FACT_internal.c, FACTSoundBank_Prepare): bit0 = has-filter;
                        // when set, filter type is (filterData>>1)&0x02 -- FAudio's own math,
                        // which structurally only ever yields 0 (low-pass) or 2 (high-pass);
                        // band-pass (1) is never reachable this way. qfactor occupies the upper
                        // byte regardless of the has-filter bit. Replicated exactly, not "fixed."
                        const uint16_t filterData = sc.u16();
                        tracks[t].qfactor    = static_cast<uint8_t>((filterData >> 8) & 0xFF);
                        tracks[t].filterType = (filterData & 0x0001)
                            ? static_cast<uint8_t>((filterData >> 1) & 0x02)
                            : 0xFF;
                        tracks[t].frequency  = sc.u16();
                    }

                    // Parse track events (they're at absolute offsets)
                    sound.waves.reserve(trackCount);
                    sound.tracks.reserve(trackCount);
                    for (uint8_t t = 0; t < trackCount; ++t)
                    {
                        XsbTrack track;
                        track.code = tracks[t].code;
                        track.soundVolumeCentibels = sound.volumeCentibels;
                        track.trackVolumeCentibels = tracks[t].volumeCentibels;
                        track.volume = CentibelsToAmplitude(track.trackVolumeCentibels) * sound.volume;
                        track.filterType = tracks[t].filterType;
                        track.filterFrequencyHz = tracks[t].frequency;
                        track.filterQFactorRaw = tracks[t].qfactor;
                        track.rpcCodes = std::move(trackRpcCodes[t]);
                        track = ParseTrackEvents(sc, std::move(track),contentVersion);
                        sound.waves.push_back(FirstPlayWave(track));
                        sound.tracks.push_back(std::move(track));
                    }
                }
                else
                {
                    sound.tracks[0].rpcCodes = std::move(trackRpcCodes[0]);
                }
            }
        }

        // soundCode → index map
        std::unordered_map<uint32_t, uint32_t> soundCodeMap;
        for (uint32_t i = 0; i < soundCount; ++i)
            soundCodeMap[soundCodes[i]] = i;

        // ── Variation tables ─────────────────────────────────────────────────
        std::unordered_map<uint32_t, uint32_t> varCodeMap;
        if (variationOffset >= 0)
        {
            Ctx vc = ctx;
            vc.seek(static_cast<uint32_t>(variationOffset));

            // We don't know how many variation tables there are ahead of time.
            // Count them by iterating until we run out of interesting data.
            // For now, parse until we reach the next known section.
            // (FAudio computes variationCount from the cue complex entries.)
            // We'll parse variation tables on demand during cue parsing instead.
        }

        // ── Simple cues ──────────────────────────────────────────────────────
        uint16_t totalCues = cueSimpleCount + cueComplexCount;
        result.cues.resize(totalCues);
        uint16_t cueIdx = 0;

        if (cueSimpleOffset >= 0)
        {
            Ctx cc = ctx;
            cc.seek(static_cast<uint32_t>(cueSimpleOffset));

            for (uint16_t i = 0; i < cueSimpleCount; ++i, ++cueIdx)
            {
                uint8_t  flags  = cc.u8();
                uint32_t sbCode = cc.u32();

                result.cues[cueIdx].isSingleSound = true;
                auto it = soundCodeMap.find(sbCode);
                result.cues[cueIdx].soundIndex = (it != soundCodeMap.end()) ? it->second : kInvalidSoundIndex;
                result.cues[cueIdx].varIndex   = 0;
                // fadeOutMS/fadeInMS (0) and instanceLimit (0xFF)/maxInstanceBehavior (0, FAIL)
                // all stay at their struct defaults: a simple cue's format has no such fields at
                // all, matching FAudio's own hardcoded defaults for simple cues (P9-STOP-010,
                // P9-CATEGORY-011).
            }
        }

        // ── Complex cues ─────────────────────────────────────────────────────
        // Variation tables are parsed inline as we encounter their codes.
        if (cueComplexOffset >= 0)
        {
            Ctx cc = ctx;
            cc.seek(static_cast<uint32_t>(cueComplexOffset));

            for (uint16_t i = 0; i < cueComplexCount; ++i, ++cueIdx)
            {
                uint8_t  flags           = cc.u8();
                uint32_t sbCode          = cc.u32();
                uint32_t transitionOffset = cc.u32();
                // P9-CATEGORY-011: instanceLimit/fadeInMS/maxInstanceBehavior retained (were
                // discarded) alongside fadeOutMS (P9-STOP-010), same 6-byte block.
                uint8_t  instanceLimit   = cc.u8();
                uint16_t fadeInMS        = cc.u16();
                uint16_t fadeOutMS       = cc.u16();
                uint8_t  maxInstanceBehavior = cc.u8() >> 3; // same bit-shift as XgsCategory's

                bool isSingle = (flags & CUE_FLAG_SINGLE_SOUND) != 0;
                result.cues[cueIdx].isSingleSound = isSingle;
                result.cues[cueIdx].fadeOutMS = fadeOutMS;
                result.cues[cueIdx].instanceLimit = instanceLimit;
                result.cues[cueIdx].fadeInMS = fadeInMS;
                result.cues[cueIdx].maxInstanceBehavior = maxInstanceBehavior;

                if (isSingle)
                {
                    auto it = soundCodeMap.find(sbCode);
                    result.cues[cueIdx].soundIndex = (it != soundCodeMap.end()) ? it->second : kInvalidSoundIndex;
                    result.cues[cueIdx].varIndex   = 0;
                }
                else
                {
                    // sbCode points to a variation table; parse it now
                    auto vit = varCodeMap.find(sbCode);
                    if (vit != varCodeMap.end())
                    {
                        result.cues[cueIdx].varIndex = vit->second;
                    }
                    else if (variationOffset >= 0 && sbCode >= static_cast<uint32_t>(variationOffset))
                    {
                        // Parse this variation table
                        Ctx vc = ctx;
                        vc.seek(sbCode);

                        XsbVariation var;
                        var.code = sbCode; // store absolute offset as code

                        uint32_t entryCountAndFlags = vc.u32();
                        uint16_t entryCount = static_cast<uint16_t>(entryCountAndFlags & 0xFFFFu);
                        var.type   = static_cast<uint8_t>((entryCountAndFlags >> (16 + 3)) & 0x07u);
                        vc.u16();   // unknown
                        var.variable = vc.s16();

                        for (uint16_t j = 0; j < entryCount; ++j)
                        {
                            XsbVariEntry entry{};
                            if (var.type == 0 || var.type == 4) // WAVE or COMPACT_WAVE
                            {
                                entry.isSoundEntry   = false;
                                entry.waveIndex      = vc.u16();
                                entry.wavebankIndex  = vc.u8();
                                if (var.type == 0) {
                                    entry.weightMin  = vc.u8();
                                    entry.weightMax  = vc.u8();
                                } else {
                                    entry.weightMin  = 0;
                                    entry.weightMax  = 255;
                                }
                            }
                            else if (var.type == 1) // SOUND
                            {
                                entry.isSoundEntry  = true;
                                uint32_t code       = vc.u32();
                                entry.weightMin     = vc.u8();
                                entry.weightMax     = vc.u8();
                                auto sit = soundCodeMap.find(code);
                                entry.soundIndex = (sit != soundCodeMap.end()) ? sit->second : kInvalidSoundIndex;
                            }
                            else if (var.type == 3) // INTERACTIVE
                            {
                                entry.isSoundEntry  = true;
                                uint32_t code       = vc.u32();
                                entry.varMin        = vc.f32();
                                entry.varMax        = vc.f32();
                                // "linger" (FACT_internal.c) is only surfaced via FACTGetCueProperties,
                                // which has no XNA-public equivalent on Cue -- read and discarded.
                                vc.u32(); // linger
                                auto sit = soundCodeMap.find(code);
                                entry.soundIndex = (sit != soundCodeMap.end()) ? sit->second : kInvalidSoundIndex;
                            }
                            else
                            {
                                // type 2 (CLIP) and any other reserved value: FAudio itself has
                                // no known byte layout for these (FACT_internal.c's variation-
                                // table switch only handles 0/1/3/4 and asserts on default).
                                // Guessing a layout here would silently misalign the cursor for
                                // every subsequent variation table/cue in the file.
                                throw std::runtime_error(
                                    "XSB: unsupported variation table type " + std::to_string(var.type));
                            }
                            var.entries.push_back(entry);
                        }

                        uint32_t varIdx = static_cast<uint32_t>(result.variations.size());
                        varCodeMap[sbCode] = varIdx;
                        result.variations.push_back(std::move(var));
                        result.cues[cueIdx].varIndex = varIdx;
                    }
                    else
                    {
                        // Fallback: treat as single sound
                        result.cues[cueIdx].isSingleSound = true;
                        auto it = soundCodeMap.find(sbCode);
                        result.cues[cueIdx].soundIndex = (it != soundCodeMap.end()) ? it->second : kInvalidSoundIndex;
                    }
                }
            }
        }

        // ── Cue names ────────────────────────────────────────────────────────
        // cueNameIndexOffset: array of {u32 nameOffset, u16 unknown} per cue
        if (cueNameIndexOffset >= 0)
        {
            for (uint16_t i = 0; i < totalCues; ++i)
            {
                Ctx nc = ctx;
                nc.seek(static_cast<uint32_t>(cueNameIndexOffset) + static_cast<uint32_t>(i) * 6u);
                uint32_t nameAbsOffset = nc.u32();
                nc.u16(); // unknown (may encode alphabetical sort position)

                Ctx sc2 = ctx;
                sc2.seek(nameAbsOffset);
                std::string name = sc2.cstr();
                if (!name.empty())
                    result.cueNameMap[name] = i;
            }
        }

        return result;
    }

} // namespace CNA::Internal::Audio
