// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/XactTypes.hpp"

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
                // Complex track variation — pick first entry
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

                uint32_t evtInfoInner = ctx.u32();
                uint16_t waveCount = static_cast<uint16_t>(evtInfoInner & 0xFFFF);
                ctx.skip(4); // unknown

                uint16_t waveIdx = 0xFFFF;
                uint8_t  wbIdx   = 0xFF;
                for (uint16_t j = 0; j < waveCount; ++j)
                {
                    uint16_t wi = ctx.u16();
                    uint8_t  wb = ctx.u8();
                    ctx.u8(); ctx.u8(); // min/max weight
                    if (j == 0) { waveIdx = wi; wbIdx = wb; }
                }
                result = {wbIdx, waveIdx, loopCnt, trackVol};
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
        // Accept LE "XGSF" or BE "FSGX"
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
        ctx.u16(); // rpcCount
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
        // rest of offsets not needed

        // Category names (sequential null-terminated at categoryNameOffset)
        std::vector<std::string> catNames;
        catNames.reserve(categoryCount);
        {
            Ctx nc = ctx;
            nc.seek(categoryNameOffset);
            for (uint16_t i = 0; i < categoryCount; ++i)
                catNames.push_back(nc.cstr());
        }

        // Category data (10 bytes each at categoryOffset)
        result.categories.resize(categoryCount);
        {
            Ctx cc = ctx;
            cc.seek(categoryOffset);
            for (uint16_t i = 0; i < categoryCount; ++i)
            {
                auto& cat = result.categories[i];
                cat.name          = (i < catNames.size()) ? catNames[i] : ("cat" + std::to_string(i));
                cat.instanceLimit = cc.u8();
                cat.fadeInMS      = cc.u16();
                cat.fadeOutMS     = cc.u16();
                cat.instanceLimit = cc.u8() >> 3; // maxInstanceBehavior packed here — overwrite
                // Actually re-read: FAudio reads instanceLimit first (1 byte), then fadeInMS, fadeOutMS, maxInstanceBehavior
                // Let me reparse correctly:
                // We already advanced 5 bytes (1+2+2). Now read parentCategory, volume, visibility.
                cat.parentIndex   = cc.u16();
                cat.volume        = ReadVolByteAsAmplitude(cc);
                cc.u8(); // visibility
            }
        }

        // Fix: the above category parsing has a bug (reread instanceLimit). Reparse properly.
        result.categories.clear();
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
                cc.u8();                               // maxInstanceBehavior >> 3, skip
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

        // Build lookup maps
        for (uint16_t i = 0; i < categoryCount; ++i)
            result.categoryNameMap[result.categories[i].name] = i;
        for (uint16_t i = 0; i < variableCount; ++i)
            result.variableNameMap[result.variables[i].name] = i;

        return result;
    }

    // ── XWB Parser ───────────────────────────────────────────────────────────

    XwbData ParseXwb(std::vector<uint8_t> fileData)
    {
        XwbData result;
        result.fileData = std::move(fileData);
        const auto& fd = result.fileData;

        if (fd.size() < 52)
            throw std::runtime_error("XWB: file too small");

        Ctx ctx{fd.data(), fd.data() + fd.size(), fd.data()};

        uint32_t magic = ctx.u32();
        if (magic != 0x444E4257u)
            throw std::runtime_error("XWB: invalid magic (expected WBND)");

        uint32_t version = ctx.u32();
        if (version > 46)
            std::cerr << "[XWB] Warning: version " << version << " may not be fully supported\n";

        if (version > 43)
            ctx.u32(); // headerVersion

        // 5 segments: each {dwOffset: u32, dwLength: u32}
        uint32_t segOffset[5], segLength[5];
        for (int i = 0; i < 5; ++i)
        {
            segOffset[i] = ctx.u32();
            segLength[i] = ctx.u32();
        }

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

            uint8_t wba = static_cast<uint8_t>((compactFormat >> 23) & 0xFFu);
            for (uint32_t i = 0; i < entryCount; ++i)
            {
                uint32_t offset = rawOffsetUnits[i] * alignment;

                result.entries[i].format        = static_cast<XwbFormat>(compactFormat & 0x3u);
                result.entries[i].channels      = static_cast<uint8_t>(((compactFormat >> 2) & 0x7u) + 1);
                result.entries[i].sampleRate    = (compactFormat >> 5) & 0x3FFFFu;
                result.entries[i].bitsPerSample = ((compactFormat >> 31) & 1u) ? 16 : 8;
                result.entries[i].blockAlign    = wba;

                result.entries[i].dataOffset    = segOffset[4] + offset;
                result.entries[i].dataLength    = (i + 1 < entryCount)
                    ? (rawOffsetUnits[i + 1] * alignment - offset - deviations[i])
                    : (segLength[4] - offset);
            }
        }
        else
        {
            // Normal format: FACTWaveBankEntry (up to 24 bytes)
            for (uint32_t i = 0; i < entryCount; ++i)
            {
                const uint8_t* entryPtr = ctx.cur;

                uint32_t flagsAndDuration = ctx.u32();
                uint32_t fmt              = ctx.u32();
                uint32_t playOffset       = ctx.u32();
                uint32_t playLength       = ctx.u32();
                uint32_t loopStart        = ctx.u32();
                uint32_t loopTotal        = ctx.u32();

                // entryMetaDataSize may be < 24 in old XWB versions
                if (entryMetaDataSize < 24)
                {
                    // Rewind to entry start + entryMetaDataSize
                    ctx.cur = entryPtr + entryMetaDataSize;
                    playLength = segLength[4]; // use entire wave data
                }

                // Decode FACTWaveBankMiniWaveFormat bitfield
                uint8_t  fmtTag    = static_cast<uint8_t>(fmt & 0x3u);
                uint8_t  channels  = static_cast<uint8_t>(((fmt >> 2) & 0x7u) + 1);
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

    // ── XSB Parser ───────────────────────────────────────────────────────────

    XsbData ParseXsb(const std::vector<uint8_t>& data)
    {
        XsbData result;

        if (data.size() < 0x50)
            throw std::runtime_error("XSB: file too small");

        Ctx ctx{data.data(), data.data() + data.size(), data.data()};

        uint32_t magic = ctx.u32();
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
        ctx.s32(); // variationOffset (handle below after sounds)
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

                if (flags & SOUND_FLAG_COMPLEX)
                {
                    uint8_t trackCount = sc.u8();
                    struct TrackMeta { float vol; uint32_t code; };
                    std::vector<TrackMeta> tracks(trackCount);

                    for (uint8_t t = 0; t < trackCount; ++t)
                    {
                        tracks[t].vol  = ReadVolByteAsAmplitude(sc);
                        tracks[t].code = sc.u32();
                        // filter data (2 bytes) + frequency (2 bytes)
                        sc.u16(); // filterData
                        sc.u16(); // frequency
                    }

                    // Parse track events (they're at absolute offsets)
                    sound.waves.reserve(trackCount);
                    for (uint8_t t = 0; t < trackCount; ++t)
                    {
                        XsbWaveRef wr = ParseFirstPlayWave(sc, tracks[t].code, tracks[t].vol);
                        // Combine track vol with sound vol
                        wr.volume *= sound.volume;
                        sound.waves.push_back(wr);
                    }
                }
                else
                {
                    // Simple sound: one inline wave reference
                    uint16_t waveIdx = sc.u16();
                    uint8_t  wbIdx   = sc.u8();
                    sound.waves.push_back({wbIdx, waveIdx, 0, sound.volume});
                }

                // Skip RPC data
                if (flags & SOUND_FLAG_RPC_MASK)
                {
                    uint16_t rpcDataLength = sc.u16();
                    // rpcDataLength includes the 2-byte length field itself
                    if (rpcDataLength >= 2)
                        sc.skip(rpcDataLength - 2);
                }

                // Skip DSP data
                if (flags & SOUND_FLAG_HAS_DSP)
                {
                    uint16_t dspLen = sc.u16();
                    if (dspLen >= 2)
                        sc.skip(dspLen - 2);
                }
            }
        }

        // soundCode → index map
        std::unordered_map<uint32_t, uint32_t> soundCodeMap;
        for (uint32_t i = 0; i < soundCount; ++i)
            soundCodeMap[soundCodes[i]] = i;

        // ── Re-read variationOffset from header ──────────────────────────────
        // (we already advanced past it; re-read from original header position)
        int32_t variationOffset = -1;
        {
            Ctx hc = ctx;
            // variationOffset is at position 0x32 in the XSB header
            hc.seek(0x32);
            variationOffset = hc.s32();
        }

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
                result.cues[cueIdx].soundIndex = (it != soundCodeMap.end()) ? it->second : 0;
                result.cues[cueIdx].varIndex   = 0;
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
                cc.u8();  // instanceLimit
                cc.u16(); // fadeInMS
                cc.u16(); // fadeOutMS
                cc.u8();  // maxInstanceBehavior

                bool isSingle = (flags & CUE_FLAG_SINGLE_SOUND) != 0;
                result.cues[cueIdx].isSingleSound = isSingle;

                if (isSingle)
                {
                    auto it = soundCodeMap.find(sbCode);
                    result.cues[cueIdx].soundIndex = (it != soundCodeMap.end()) ? it->second : 0;
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
                                entry.soundIndex = (sit != soundCodeMap.end()) ? sit->second : 0;
                            }
                            else // INTERACTIVE (type==2): skip
                            {
                                entry.isSoundEntry  = true;
                                uint32_t code       = vc.u32();
                                vc.f32(); vc.f32(); // var_min, var_max
                                vc.u32(); // linger
                                auto sit = soundCodeMap.find(code);
                                entry.soundIndex = (sit != soundCodeMap.end()) ? sit->second : 0;
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
                        result.cues[cueIdx].soundIndex = (it != soundCodeMap.end()) ? it->second : 0;
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
