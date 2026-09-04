#pragma once

#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// Minimal generator for XGS / XWB / XSB binary files.
// The formats produced match exactly what XactParser.cpp expects.
namespace XactFileGen
{
    // ── Binary write helpers ──────────────────────────────────────────────────

    inline void w8(std::vector<uint8_t>& v, uint8_t x) { v.push_back(x); }
    inline void w16(std::vector<uint8_t>& v, uint16_t x)
    {
        v.push_back(static_cast<uint8_t>(x));
        v.push_back(static_cast<uint8_t>(x >> 8));
    }
    inline void w32(std::vector<uint8_t>& v, uint32_t x)
    {
        v.push_back(static_cast<uint8_t>(x));
        v.push_back(static_cast<uint8_t>(x >>  8));
        v.push_back(static_cast<uint8_t>(x >> 16));
        v.push_back(static_cast<uint8_t>(x >> 24));
    }
    inline void ws32(std::vector<uint8_t>& v, int32_t x)  { w32(v, static_cast<uint32_t>(x)); }
    inline void wf32(std::vector<uint8_t>& v, float x)
    {
        uint32_t tmp;
        std::memcpy(&tmp, &x, 4);
        w32(v, tmp);
    }
    inline void w64z(std::vector<uint8_t>& v)             { w32(v, 0); w32(v, 0); }
    inline void wpad(std::vector<uint8_t>& v, std::size_t n) { v.insert(v.end(), n, 0u); }
    inline void wpad_to(std::vector<uint8_t>& v, std::size_t target)
    {
        if (v.size() < target) v.insert(v.end(), target - v.size(), 0u);
    }
    inline void wstr64(std::vector<uint8_t>& v, const std::string& s)
    {
        std::size_t len = std::min(s.size(), std::size_t(63));
        for (std::size_t i = 0; i < len; ++i) v.push_back(static_cast<uint8_t>(s[i]));
        for (std::size_t i = len; i < 64; ++i) v.push_back(0u);
    }
    inline void wcstr(std::vector<uint8_t>& v, const std::string& s)
    {
        for (char c : s) v.push_back(static_cast<uint8_t>(c));
        v.push_back(0u);
    }

    // ── Sine wave PCM generator ───────────────────────────────────────────────

    // Returns 16-bit signed little-endian mono PCM at 44100 Hz.
    inline std::vector<uint8_t> MakeSineWave(float freqHz,
                                              float durationSec,
                                              uint32_t sampleRate = 44100)
    {
        const std::size_t n = static_cast<std::size_t>(sampleRate * durationSec);
        std::vector<uint8_t> out(n * 2);
        const float pi2 = 2.0f * 3.14159265358979f;
        const float fadeLen = 0.02f * static_cast<float>(sampleRate);
        for (std::size_t i = 0; i < n; ++i)
        {
            float t   = static_cast<float>(i) / static_cast<float>(sampleRate);
            float env = 1.0f;
            if (i < static_cast<std::size_t>(fadeLen))
                env = static_cast<float>(i) / fadeLen;
            else if (i > n - static_cast<std::size_t>(fadeLen))
                env = static_cast<float>(n - i) / fadeLen;
            int16_t s = static_cast<int16_t>(std::sin(pi2 * freqHz * t) * env * 28000.0f);
            out[i * 2]     = static_cast<uint8_t>(s & 0xFF);
            out[i * 2 + 1] = static_cast<uint8_t>((s >> 8) & 0xFF);
        }
        return out;
    }

    // ── XGS generator ────────────────────────────────────────────────────────
    //
    // Produces a minimal AudioEngine settings file.
    // Categories: index 0 = "Music", index 1 = "SFX"
    // Variables:  index 0 = "SpeedOfSound" (init=343, range 0..1000)

    inline std::vector<uint8_t> MakeXgs()
    {
        // Layout (parser reads header then seeks to absolute offsets):
        //  [0..64]  header (65 bytes of reads), padded to 80
        //  [80]     categories: 2 × 10 bytes = 20 bytes
        //  [100]    variables:  1 × 13 bytes = 13 bytes
        //  [113]    category names: "Music\0" (6) + "SFX\0" (4)
        //  [123]    variable names: "SpeedOfSound\0" (13)
        //  [136]    end

        constexpr uint32_t CAT_OFF     = 80;
        constexpr uint32_t VAR_OFF     = CAT_OFF + 2 * 10; // 100
        constexpr uint32_t CATNAME_OFF = VAR_OFF + 1 * 13; // 113
        constexpr uint32_t VARNAME_OFF = CATNAME_OFF + 6 + 4; // 123

        std::vector<uint8_t> out;

        // ── Header ──
        w32(out, 0x46534758u); // magic "XGSF" (LE bytes: X G S F → 0x46534758)
        w16(out, 46);  // contentVersion
        w16(out, 0);   // toolVersion
        w16(out, 0);   // unknown
        wpad(out, 8);  // lastModified
        w8(out, 0);    // platform

        w16(out, 2);   // categoryCount
        w16(out, 1);   // variableCount
        w16(out, 0);   // blob1Count
        w16(out, 0);   // blob2Count
        w16(out, 0);   // rpcCount
        w16(out, 0);   // dspPresetCount
        w16(out, 0);   // dspParameterCount

        w32(out, CAT_OFF);      // categoryOffset
        w32(out, VAR_OFF);      // variableOffset
        w32(out, 0);            // blob1Offset
        w32(out, 0);            // categoryNameIndexOffset (unused by parser)
        w32(out, 0);            // blob2Offset
        w32(out, 0);            // variableNameIndexOffset (unused by parser)
        w32(out, CATNAME_OFF);  // categoryNameOffset
        w32(out, VARNAME_OFF);  // variableNameOffset
        // out.size() == 65 here; pad to minimum 80 bytes
        wpad_to(out, 80);

        // ── Category 0: "Music" (10 bytes) ──
        w8(out, 255);     // instanceLimit
        w16(out, 0);      // fadeInMS
        w16(out, 0);      // fadeOutMS
        w8(out, 0);       // maxInstanceBehavior
        w16(out, 0xFFFF); // parentIndex (no parent)
        w8(out, 180);     // volume byte (≈ amplitude 1.0)
        w8(out, 1);       // visibility

        // ── Category 1: "SFX" (10 bytes) ──
        w8(out, 255);
        w16(out, 0);
        w16(out, 0);
        w8(out, 0);
        w16(out, 0xFFFF);
        w8(out, 180);
        w8(out, 1);

        // ── Variable 0: "SpeedOfSound" (13 bytes) ──
        // PUBLIC (0x1) alone. 0x03 is PUBLIC|READONLY -- AudioEngine.cpp spells the bits out:
        // PUBLIC=0x1, READONLY=0x2, CUE=0x4 -- so the byte said read-only while this comment said
        // settable, and XactDemo's own Z and X keys called SetGlobalVariable on it to no effect.
        // A binding author copying this example writes a settings file whose variable ignores
        // every write, and what they see is SetGlobalVariable(500) followed by
        // GetGlobalVariable() == 343, which reads exactly like a marshalling bug of their own.
        w8(out,  0x01);    // accessibility: PUBLIC
        wf32(out, 343.0f); // initialValue
        wf32(out,   0.0f); // minValue
        wf32(out, 1000.f); // maxValue

        // ── Category names ──
        wcstr(out, "Music"); // 6 bytes
        wcstr(out, "SFX");   // 4 bytes

        // ── Variable names ──
        wcstr(out, "SpeedOfSound"); // 13 bytes

        assert(out.size() == 136);
        return out;
    }

    // ── XWB generator ────────────────────────────────────────────────────────
    //
    // Produces a non-compact WaveBank with N mono 16-bit PCM waves at 44100 Hz.

    inline std::vector<uint8_t> MakeXwb(const std::string& bankName,
                                         const std::vector<std::vector<uint8_t>>& waves)
    {
        const uint32_t sampleRate  = 44100;
        const uint32_t entryCount  = static_cast<uint32_t>(waves.size());

        // Pre-compute audio layout
        std::vector<uint32_t> playOffsets(entryCount);
        uint32_t totalAudio = 0;
        for (uint32_t i = 0; i < entryCount; ++i)
        {
            playOffsets[i] = totalAudio;
            totalAudio += static_cast<uint32_t>(waves[i].size());
        }

        // Fixed sizes / offsets
        // Header: magic(4) + version(4) + headerVersion(4) + 5×segment(40) = 52 bytes
        constexpr uint32_t BANKDATA_SIZE = 4+4+64+4+4+4+4+8; // 96 bytes
        const uint32_t seg0_off  = 52;
        const uint32_t seg0_len  = BANKDATA_SIZE;
        const uint32_t seg1_off  = seg0_off + seg0_len;         // 148
        const uint32_t seg1_len  = 24 * entryCount;
        // Align wave data to 4 bytes
        const uint32_t seg4_off  = (seg1_off + seg1_len + 3u) & ~3u;
        const uint32_t seg4_len  = totalAudio;

        std::vector<uint8_t> out;

        // ── XWB header ──
        w32(out, 0x444E4257u); // magic "WBND"
        w32(out, 46);          // version
        w32(out, 44);          // headerVersion (> 43 triggers extra u32 read in parser)

        // 5 segments
        w32(out, seg0_off); w32(out, seg0_len);
        w32(out, seg1_off); w32(out, seg1_len);
        w32(out, 0);        w32(out, 0);          // seg2 SEEKTABLE (empty)
        w32(out, 0);        w32(out, 0);          // seg3 ENTRYNAMES (empty)
        w32(out, seg4_off); w32(out, seg4_len);

        // ── BANKDATA (96 bytes) ──
        w32(out, 0);           // wbFlags (normal format, no names)
        w32(out, entryCount);
        wstr64(out, bankName);
        w32(out, 24);          // entryMetaDataSize
        w32(out, 0);           // entryNameElemSize (no entry names)
        w32(out, 4);           // alignment
        w32(out, 0);           // compactFormat (unused)
        w64z(out);             // buildTime

        // ── ENTRYMETADATA (24 bytes each) ──
        // fmt bitfield: fmtTag=0 (PCM), channels-1=0 (mono),
        //               sampleRate, wBlockAlign=0, bps=1 (16-bit)
        const uint32_t fmt = (sampleRate << 5) | (1u << 31);
        for (uint32_t i = 0; i < entryCount; ++i)
        {
            w32(out, 0);                                          // flagsAndDuration
            w32(out, fmt);
            w32(out, playOffsets[i]);                             // playOffset (from seg4 start)
            w32(out, static_cast<uint32_t>(waves[i].size()));    // playLength
            w32(out, 0);                                          // loopStart
            w32(out, 0);                                          // loopTotal
        }

        // ── Pad to seg4_off then append WAVEDATA ──
        wpad_to(out, seg4_off);
        for (auto& w : waves)
            out.insert(out.end(), w.begin(), w.end());

        return out;
    }

    // ── XSB generator ────────────────────────────────────────────────────────
    //
    // Produces a SoundBank with N simple cues (one cue → one simple sound →
    // one wave in the wavebank).

    struct CueDef
    {
        std::string name;
        uint16_t    waveIndex;
        uint8_t     categoryIndex; // index into XGS categories
    };

    inline std::vector<uint8_t> MakeXsb(const std::string& bankName,
                                         const std::string& waveBankName,
                                         const std::vector<CueDef>& cues)
    {
        const uint16_t cueCount   = static_cast<uint16_t>(cues.size());
        const uint16_t soundCount = cueCount; // one sound per cue

        // ── Compute all section offsets ──
        //
        // Header layout (matches exactly what XsbData ParseXsb() reads):
        //   4  magic
        //   2  contentVersion
        //   2  toolVersion
        //   2  CRC
        //   8  lastModified
        //   1  platform
        //   2  cueSimpleCount
        //   2  cueComplexCount
        //   2  unknown
        //   2  cueTotalAlign
        //   1  wavebankCount
        //   2  soundCount
        //   2  cueNameLength
        //   2  unknown
        //  40  10 × s32 offsets
        //  64  bankName[64]
        //  ──
        //  138 total header

        constexpr uint32_t HDR = 138;

        const uint32_t soundOffset       = HDR;
        const uint32_t cueSimpleOffset   = soundOffset       + 12u * soundCount;
        const uint32_t wavebankNameOffset = cueSimpleOffset  +  5u * cueCount;
        const uint32_t cueNameIndexOffset = wavebankNameOffset + 64u;

        // Cue name string offsets (absolute)
        const uint32_t cueStrBase = cueNameIndexOffset + 6u * cueCount;
        std::vector<uint32_t> nameAbsOffset(cueCount);
        uint32_t ptr = cueStrBase;
        for (uint16_t i = 0; i < cueCount; ++i)
        {
            nameAbsOffset[i] = ptr;
            ptr += static_cast<uint32_t>(cues[i].name.size()) + 1;
        }

        // Sound byte-offsets (for sbCode in cue entries)
        std::vector<uint32_t> soundCode(soundCount);
        for (uint16_t i = 0; i < soundCount; ++i)
            soundCode[i] = soundOffset + 12u * i;

        std::vector<uint8_t> out;

        // ── XSB header ──
        w32(out, 0x4B424453u); // magic "SDBK"
        w16(out, 46);          // contentVersion
        w16(out, 0);           // toolVersion
        w16(out, 0);           // CRC
        wpad(out, 8);          // lastModified
        w8(out, 0);            // platform

        w16(out, cueCount);    // cueSimpleCount
        w16(out, 0);           // cueComplexCount
        w16(out, 0);           // unknown
        w16(out, 0);           // cueTotalAlign
        w8(out, 1);            // wavebankCount
        w16(out, soundCount);  // soundCount
        w16(out, 0);           // cueNameLength
        w16(out, 0);           // unknown

        // 10 offsets (order matches parser reads at offset 34..73)
        ws32(out, static_cast<int32_t>(cueSimpleOffset));
        ws32(out, -1);  // cueComplexOffset (none)
        ws32(out, -1);  // cueNameOffset (unused)
        ws32(out, -1);  // unknown
        ws32(out, -1);  // variationOffset (none) — also re-read at 0x32
        ws32(out, -1);  // transitionOffset
        ws32(out, static_cast<int32_t>(wavebankNameOffset));
        ws32(out, -1);  // cueHashOffset
        ws32(out, static_cast<int32_t>(cueNameIndexOffset));
        ws32(out, static_cast<int32_t>(soundOffset));

        // bankName[64]
        wstr64(out, bankName);
        assert(out.size() == HDR);

        // ── Sounds (12 bytes each) ──
        // Simple sound: flags(1)+catIdx(2)+volByte(1)+pitch(2)+priority(1)+
        //               soundLen(2)+waveIdx(2)+wbIdx(1) = 12
        for (uint16_t i = 0; i < soundCount; ++i)
        {
            w8(out, 0);                      // flags = 0 (simple, no RPC/DSP)
            w16(out, cues[i].categoryIndex); // categoryIndex
            w8(out, 180);                    // volume byte (≈ amplitude 1.0)
            w16(out, 0);                     // pitchCents (int16 = 0)
            w8(out, 255);                    // priority
            w16(out, 0);                     // soundLength (unused)
            w16(out, cues[i].waveIndex);     // waveIndex into WaveBank
            w8(out, 0);                      // wavebankIndex = 0 (first wavebank)
        }

        // ── Simple cues (5 bytes each: flags + sbCode) ──
        for (uint16_t i = 0; i < cueCount; ++i)
        {
            w8(out, 0);              // flags
            w32(out, soundCode[i]);  // sbCode = absolute file offset of the sound
        }

        // ── Wavebank name (64 bytes) ──
        wstr64(out, waveBankName);

        // ── Cue name index (6 bytes each: u32 nameOffset + u16 sortIdx) ──
        for (uint16_t i = 0; i < cueCount; ++i)
        {
            w32(out, nameAbsOffset[i]);
            w16(out, i); // sort index (identity)
        }

        // ── Cue name strings ──
        for (auto& c : cues)
            wcstr(out, c.name);

        return out;
    }

    // ── File I/O ─────────────────────────────────────────────────────────────

    inline void SaveFile(const std::string& path, const std::vector<uint8_t>& data)
    {
        std::ofstream f(path, std::ios::binary);
        if (!f)
            throw std::runtime_error("XactFileGen: cannot write " + path);
        f.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
    }
}
