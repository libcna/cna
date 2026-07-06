// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/XactTypes.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
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
            if (cur + 2 > end) throw std::runtime_error("XACT parse: read past end");
            uint16_t v; std::memcpy(&v, cur, 2); cur += 2; return v;
        }
        uint32_t u32()
        {
            if (cur + 4 > end) throw std::runtime_error("XACT parse: read past end");
            uint32_t v; std::memcpy(&v, cur, 4); cur += 4; return v;
        }
        int16_t s16()
        {
            if (cur + 2 > end) throw std::runtime_error("XACT parse: read past end");
            int16_t v; std::memcpy(&v, cur, 2); cur += 2; return v;
        }
        int32_t s32()
        {
            if (cur + 4 > end) throw std::runtime_error("XACT parse: read past end");
            int32_t v; std::memcpy(&v, cur, 4); cur += 4; return v;
        }
        float f32()
        {
            if (cur + 4 > end) throw std::runtime_error("XACT parse: read past end");
            float v; std::memcpy(&v, cur, 4); cur += 4; return v;
        }

        void skip(std::size_t n)
        {
            if (cur + n > end) throw std::runtime_error("XACT parse: skip past end");
            cur += n;
        }

        void seek(uint32_t absOffset)
        {
            if (start + absOffset > end)
                throw std::runtime_error("XACT parse: seek past end");
            cur = start + absOffset;
        }

        uint32_t pos() const { return static_cast<uint32_t>(cur - start); }

        /// Read a null-terminated string from the current position, advance past it.
        std::string cstr()
        {
            const char* p = reinterpret_cast<const char*>(cur);
            std::size_t maxlen = static_cast<std::size_t>(end - cur);
            std::size_t len = strnlen(p, maxlen);
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

    static float ReadVolByteAsAmplitude(Ctx& ctx)
    {
        uint8_t b = ctx.u8();
        if (b == 0) return 0.0f;
        float cb = static_cast<float>((3969.0 * std::log10(b / 28240.0)) + 8715.0);
        return CentibelsToAmplitude(cb);
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

    /// Returns the (wavebankIndex, waveIndex, loopCount) from the first PlayWave
    /// event in the track at absOffset, or {0xFF, 0xFFFF, 0} if none found.
    static XsbWaveRef ParseFirstPlayWave(Ctx& ctx, uint32_t trackCodeAbs, float trackVol)
    {
        uint32_t saved = ctx.pos();
        ctx.seek(trackCodeAbs);

        uint8_t eventCount = ctx.u8();

        XsbWaveRef result{0xFF, 0xFFFF, 0, trackVol};

        for (uint8_t i = 0; i < eventCount; ++i)
        {
            uint32_t evtInfo  = ctx.u32();
            ctx.u16(); // randomOffset
            uint8_t type      = static_cast<uint8_t>(evtInfo & 0x001F);
            // timestamp = (evtInfo >> 5) & 0xFFFF
            ctx.u8(); // separator (0xFF)

            if (type == FACTEVENT_STOP)
            {
                ctx.u8(); // flags
            }
            else if (type == FACTEVENT_PLAYWAVE)
            {
                ctx.u8(); // flags
                uint16_t waveIdx = ctx.u16();
                uint8_t  wbIdx   = ctx.u8();
                uint8_t  loopCnt = ctx.u8();
                ctx.u16(); // position
                ctx.u16(); // angle
                result = {wbIdx, waveIdx, loopCnt, trackVol};
                break; // We only need the first play-wave event
            }
            else if (type == FACTEVENT_PLAYWAVEEFFECTVARIATION)
            {
                ctx.u8(); // flags
                uint16_t waveIdx = ctx.u16();
                uint8_t  wbIdx   = ctx.u8();
                uint8_t  loopCnt = ctx.u8();
                ctx.u16(); ctx.u16(); // position, angle
                // effect variation parameters (skip)
                ctx.s16(); ctx.s16(); // minPitch, maxPitch
                ReadVolByte(ctx); ReadVolByte(ctx); // minVol, maxVol
                ctx.f32(); ctx.f32(); // minFreq, maxFreq
                ctx.f32(); ctx.f32(); // minQFactor, maxQFactor
                ctx.u16(); // variationFlags
                result = {wbIdx, waveIdx, loopCnt, trackVol};
                break;
            }
            else if (type == FACTEVENT_PLAYWAVETRACKVARIATION ||
                     type == FACTEVENT_PLAYWAVETRACKEFFECTVARIATION)
            {
                // P11-XACT-002: retains the FULL candidate list + selection algorithm (not just
                // entry 0) so Cue::Play() can run FAudio's real per-instance selection
                // (FACT_internal.c's FACT_INTERNAL_GetNextWave) instead of always picking the
                // first authored entry.
                ctx.u8(); // flags
                uint8_t loopCnt = ctx.u8();
                ctx.u16(); ctx.u16(); // position, angle

                if (type == FACTEVENT_PLAYWAVETRACKEFFECTVARIATION)
                {
                    ctx.s16(); ctx.s16(); // minPitch, maxPitch
                    ReadVolByte(ctx); ReadVolByte(ctx);
                    ctx.f32(); ctx.f32(); ctx.f32(); ctx.f32();
                    ctx.u16(); // variationFlags
                }

                // Matches FAudio's real parse (FACT_internal.c:2303-2322): evtInfo's low 16 bits
                // are wave_count, bits 16-18 are variation_type (VARIATION_TYPE_MASK = 0x7).
                uint32_t evtInfoInner = ctx.u32();
                uint16_t waveCount = static_cast<uint16_t>(evtInfoInner & 0xFFFF);
                auto variationType = static_cast<XsbTrackVariationType>((evtInfoInner >> 16) & 0x07u);
                ctx.skip(4); // unknown

                uint16_t waveIdx = 0xFFFF;
                uint8_t  wbIdx   = 0xFF;
                std::vector<XsbTrackVariationEntry> entries;
                entries.reserve(waveCount);
                for (uint16_t j = 0; j < waveCount; ++j)
                {
                    uint16_t wi        = ctx.u16();
                    uint8_t  wb        = ctx.u8();
                    uint8_t  minWeight = ctx.u8();
                    uint8_t  maxWeight = ctx.u8();
                    // FACT_internal.c:2321: weights[j] = maxWeight - minWeight.
                    entries.push_back({wi, wb, static_cast<uint8_t>(maxWeight - minWeight)});
                    if (j == 0) { waveIdx = wi; wbIdx = wb; }
                }
                result = {wbIdx, waveIdx, loopCnt, trackVol};
                result.trackVariationEntries = std::move(entries);
                result.trackVariationType    = variationType;
                break;
            }
            else if (type == FACTEVENT_PITCH || type == FACTEVENT_VOLUME ||
                     type == FACTEVENT_PITCHREPEATING || type == FACTEVENT_VOLUMEREPEATING)
            {
                uint8_t settings = ctx.u8();
                if (settings & EVENT_SETTINGS_RAMP)
                {
                    ctx.f32(); ctx.f32(); ctx.f32(); // initialValue, initialSlope, slopeDelta
                    ctx.u16(); // duration
                }
                else
                {
                    ctx.u8(); // equation flags
                    ctx.f32(); ctx.f32(); // value1, value2
                    ctx.skip(5); // unknown

                    if (type == FACTEVENT_PITCHREPEATING || type == FACTEVENT_VOLUMEREPEATING)
                    {
                        ctx.u16(); ctx.u16(); // repeats, frequency
                    }
                }
                // Not a play event — keep scanning the track for one.
            }
            else if (type == FACTEVENT_MARKER)
            {
                ctx.u32(); // marker
            }
            else if (type == FACTEVENT_MARKERREPEATING)
            {
                ctx.u32(); // marker
                ctx.u16(); ctx.u16(); // repeats, frequency
            }
            else
            {
                // Genuinely unrecognized event type — its length can't be determined, so
                // stop scanning rather than misreading the remaining bytes as event headers.
                break;
            }
        }

        ctx.seek(saved);
        return result;
    }

    // ── XGS Parser ───────────────────────────────────────────────────────────

    XgsData ParseXgs(const std::vector<uint8_t>& data)
    {
        XgsData result;

        if (data.size() < 0x50)
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
            std::cerr << "[XGS] Warning: unexpected contentVersion " << contentVersion << "\n";

        ctx.u16(); // toolVersion
        ctx.u16(); // unknown
        ctx.skip(8); // lastModified
        ctx.u8();  // platform (3=Xbox, 7=Xbox360)

        uint16_t categoryCount      = ctx.u16();
        uint16_t variableCount      = ctx.u16();
        ctx.u16(); // blob1Count
        ctx.u16(); // blob2Count
        uint16_t rpcCount           = ctx.u16();
        ctx.u16(); // dspPresetCount
        ctx.u16(); // dspParameterCount

        uint32_t categoryOffset         = ctx.u32();
        uint32_t variableOffset         = ctx.u32();
        ctx.u32(); // blob1Offset
        ctx.u32(); // categoryNameIndexOffset (sorted index, not used directly)
        ctx.u32(); // blob2Offset
        ctx.u32(); // variableNameIndexOffset
        uint32_t categoryNameOffset     = ctx.u32();
        uint32_t variableNameOffset     = ctx.u32();
        uint32_t rpcOffset              = ctx.u32();
        // dspPresetOffset/dspParameterOffset not needed -- CNA has no DSP preset system
        // (P9-XACT-005, CHECKLIST.md)

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
        void ReadXwbSegmentTable(Ctx& ctx, uint32_t (&segOffset)[5], uint32_t (&segLength)[5])
        {
            uint32_t magic = ctx.u32();
            if (magic != 0x444E4257u)
                throw std::runtime_error("XWB: invalid magic (expected WBND)");

            uint32_t version = ctx.u32();
            if (version > 46)
                std::cerr << "[XWB] Warning: version " << version << " may not be fully supported\n";

            if (version > 43)
                ctx.u32(); // headerVersion

            // 5 segments: each {dwOffset: u32, dwLength: u32}
            for (int i = 0; i < 5; ++i)
            {
                segOffset[i] = ctx.u32();
                segLength[i] = ctx.u32();
            }
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
        ReadXwbSegmentTable(ctx, segOffset, segLength);

        // BANKDATA segment
        // Layout: flags(u32), entryCount(u32), bankName[64], entryMetaDataSize(u32),
        //         entryNameElementSize(u32), alignment(u32), compactFormat(u32), buildTime(u64)
        ctx.seek(segOffset[0]);
        uint32_t wbFlags             = ctx.u32();
        uint32_t entryCount          = ctx.u32();
        {
            // Bank name (64 bytes, null-terminated)
            const char* p = reinterpret_cast<const char*>(ctx.cur);
            result.bankName = std::string(p, strnlen(p, 64));
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
            // Compact format: 32-bit per entry: dwOffset (21 bits, units of `alignment`),
            // dwLengthDeviation (11 bits). The deviation is how many bytes shorter than the
            // aligned span the real audio is, not the length itself, so the length must come
            // from the gap to the next entry's offset (or to the end of the wave-data segment
            // for the last entry) minus that deviation.
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
                uint32_t offset = rawOffsetUnits[i] * alignment;

                result.entries[i].format          = static_cast<XwbFormat>(compactFmtTag);
                result.entries[i].channels        = compactChannels;
                result.entries[i].sampleRate      = (compactFormat >> 5) & 0x3FFFFu;
                result.entries[i].bitsPerSample   = ((compactFormat >> 31) & 1u) ? 16 : 8;
                result.entries[i].blockAlign      = compactBlockAlign;
                result.entries[i].samplesPerBlock = compactSamplesPerBlock;

                result.entries[i].dataOffset    = segOffset[4] + offset;

                // dwLengthDeviation is the aligned span's slack in bytes, not the length
                // itself, so the real length must not exceed the gap to the next entry's
                // offset (or to the end of the wave-data segment for the last entry). Check
                // in 64-bit before narrowing so a corrupt/adversarial file can't underflow
                // this into a huge uint32_t value (D7: throw rather than silently clamp).
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
                e.format            = static_cast<XwbFormat>(fmtTag);
                e.channels          = channels;
                e.sampleRate        = sampleRate;
                e.bitsPerSample     = bps ? 16 : 8;
                e.dataOffset        = segOffset[4] + playOffset;
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

        // ENTRYNAMES segment (optional)
        if (hasNames && entryNameElemSize > 0 && segLength[3] > 0)
        {
            result.entryNames.resize(entryCount);
            ctx.seek(segOffset[3]);
            for (uint32_t i = 0; i < entryCount; ++i)
            {
                const char* p = reinterpret_cast<const char*>(ctx.cur);
                result.entryNames[i] = std::string(p, strnlen(p, entryNameElemSize));
                ctx.skip(entryNameElemSize);
            }
        }

        return result;
    }

    XwbData ParseXwbStreamingHeader(const std::string& path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open())
            throw std::runtime_error("XWB: cannot open file for streaming");

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
        // of a real .xwb and is read lazily, per entry, from `path` in WaveBank::GetSoundEffect.
        uint64_t prefixLen = 0;
        for (int i = 0; i < 4; ++i)
            prefixLen = std::max<uint64_t>(prefixLen, static_cast<uint64_t>(segOffset[i]) + segLength[i]);

        std::vector<uint8_t> prefix(static_cast<std::size_t>(prefixLen));
        f.clear();
        f.seekg(0);
        f.read(reinterpret_cast<char*>(prefix.data()), static_cast<std::streamsize>(prefixLen));
        if (static_cast<uint64_t>(f.gcount()) != prefixLen)
            throw std::runtime_error("XWB: truncated header/metadata segments");

        XwbData result = ParseXwb(std::move(prefix));
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
            std::cerr << "[XSB] Warning: contentVersion " << contentVersion << "\n";

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
                const char* p = reinterpret_cast<const char*>(wc.cur);
                result.wavebankNames[i] = std::string(p, strnlen(p, 64));
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
                sound.volume         = ReadVolByteAsAmplitude(sc);
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
                }

                // RPC code references. Layout (FACT_internal.c, FACTSoundBank_Prepare's
                // SOUND_FLAG_RPC_MASK branch, parse_rpc_codes): rpcDataLength:u16 (unused --
                // parsed structurally instead of skipped by length), then if HAS_RPC a
                // sound-level list (count:u8 + count*code:u32), then if HAS_TRACK_RPC one such
                // list per track. Only the sound-level list is retained (P9-XACT-006): CNA
                // applies RPC curves at the whole-sound level, not per-track, matching how
                // per-track state elsewhere in this parser is already simplified down to "first
                // PlayWave event each" -- the per-track lists are still walked byte-for-byte so
                // the cursor stays in sync with the rest of the sound entries.
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
                            sc.skip(static_cast<std::size_t>(trackRpcCodeCount) * 4);
                        }
                    }
                }

                // Skip DSP data. The leading 2-byte field is NOT a self-inclusive length to
                // skip by (unlike the RPC block above) -- FAudio reads and discards it, then
                // reads a 1-byte code count followed by that many 4-byte codes (FACT_internal.c,
                // FACTSoundBank_Prepare's SOUND_FLAG_HAS_DSP branch). DSP presets aren't applied
                // here (SDL_mixer has no equivalent), but the codes must still be consumed
                // correctly to keep the cursor in sync with the rest of the sound entries.
                if (flags & SOUND_FLAG_HAS_DSP)
                {
                    sc.u16(); // DSP presets length -- unused
                    const uint8_t dspCodeCount = sc.u8();
                    sc.skip(static_cast<std::size_t>(dspCodeCount) * 4);
                }

                if (flags & SOUND_FLAG_COMPLEX)
                {
                    struct TrackMeta
                    {
                        float    vol;
                        uint32_t code;
                        uint8_t  filterType;
                        uint16_t frequency;
                        uint8_t  qfactor;
                    };
                    std::vector<TrackMeta> tracks(trackCount);

                    for (uint8_t t = 0; t < trackCount; ++t)
                    {
                        tracks[t].vol  = ReadVolByteAsAmplitude(sc);
                        tracks[t].code = sc.u32();

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
                    for (uint8_t t = 0; t < trackCount; ++t)
                    {
                        XsbWaveRef wr = ParseFirstPlayWave(sc, tracks[t].code, tracks[t].vol);
                        // Combine track vol with sound vol
                        wr.volume *= sound.volume;
                        wr.filterType        = tracks[t].filterType;
                        wr.filterFrequencyHz = tracks[t].frequency;
                        wr.filterQFactorRaw  = tracks[t].qfactor;
                        sound.waves.push_back(wr);
                    }
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
