// SPDX-License-Identifier: MS-PL
//
// plans/plan_audio.md AUD-11-028: standalone tool that lists a .xwb wave bank's parsed metadata as
// stable JSON and, optionally, exports every entry's raw payload as a real, playable .wav file
// (via the same shared CNA::Internal::Audio::WavWrapper technique WaveBank.cpp itself uses to
// reach SDL3's native PCM8/MS-ADPCM decoder) for offline diagnosis -- listening to or hex-dumping
// a specific entry's actual decoded audio without spinning up a full AudioEngine/WaveBank/Cue
// pipeline. Never plays anything; this is a pure inspection/extraction tool.
//
// Usage: cna_xwb_inspect <path-to.xwb> [output-directory-for-.wav-export]

#include "CNA/Internal/Audio/WavWrapper.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    std::string JsonEscape(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            if (c == '"' || c == '\\') out.push_back('\\');
            out.push_back(c);
        }
        return out;
    }

    const char* FormatName(CNA::Internal::Audio::XwbFormat f)
    {
        using CNA::Internal::Audio::XwbFormat;
        switch (f)
        {
            case XwbFormat::PCM:   return "PCM";
            case XwbFormat::XMA:   return "XMA";
            case XwbFormat::ADPCM: return "MS-ADPCM";
            case XwbFormat::WMA:   return "WMA";
        }
        return "unknown";
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s <path-to.xwb> [output-directory-for-.wav-export]\n", argv[0]);
        return 2;
    }

    const std::string path = argv[1];
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open())
    {
        std::printf("{\"status\":\"error\",\"message\":\"cannot open file\"}\n");
        return 1;
    }
    const auto sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> raw(static_cast<std::size_t>(sz));
    f.read(reinterpret_cast<char*>(raw.data()), sz);

    CNA::Internal::Audio::XwbData xwb;
    try
    {
        xwb = CNA::Internal::Audio::ParseXwb(std::move(raw));
    }
    catch (const std::exception& ex)
    {
        std::printf("{\"status\":\"error\",\"message\":\"%s\"}\n", JsonEscape(ex.what()).c_str());
        return 1;
    }

    const bool exportWaves = argc >= 3;
    std::filesystem::path outDir;
    if (exportWaves)
    {
        outDir = argv[2];
        std::filesystem::create_directories(outDir);
    }

    std::printf("{\"status\":\"ok\",\"bankName\":\"%s\",\"streaming\":%s,\"entryCount\":%zu,\"entries\":[\n",
                JsonEscape(xwb.bankName).c_str(), xwb.streaming ? "true" : "false", xwb.entries.size());

    for (std::size_t i = 0; i < xwb.entries.size(); ++i)
    {
        const auto& e = xwb.entries[i];
        const std::string name = (i < xwb.entryNames.size()) ? xwb.entryNames[i] : "";

        std::printf(
            "  {\"index\":%zu,\"name\":\"%s\",\"format\":\"%s\",\"channels\":%u,"
            "\"sampleRate\":%u,\"bitsPerSample\":%u,\"blockAlign\":%u,\"samplesPerBlock\":%u,"
            "\"dataOffset\":%u,\"dataLength\":%u,\"loopStartSample\":%u,\"loopTotalSamples\":%u}%s\n",
            i, JsonEscape(name).c_str(), FormatName(e.format), e.channels, e.sampleRate,
            e.bitsPerSample, e.blockAlign, e.samplesPerBlock, e.dataOffset, e.dataLength,
            e.loopStartSample, e.loopTotalSamples, (i + 1 < xwb.entries.size()) ? "," : "");

        if (exportWaves && !xwb.streaming)
        {
            // Non-streaming banks hold the whole file in fileData -- slice the entry's real
            // audio bytes straight out of it, same as WaveBank::GetSoundEffect()'s non-streaming
            // path does, without needing a real AudioEngine/WaveBank instance at all.
            if (static_cast<uint64_t>(e.dataOffset) + e.dataLength <= xwb.fileData.size())
            {
                const uint8_t* audioData = xwb.fileData.data() + e.dataOffset;
                std::vector<uint8_t> wav;
                using CNA::Internal::Audio::XwbFormat;
                if (e.format == XwbFormat::PCM)
                {
                    const uint32_t avgBytesPerSec = e.sampleRate * e.blockAlign;
                    wav = CNA::Internal::Audio::BuildWavFromWaveFormatEx(
                        audioData, e.dataLength, /*wFormatTag=*/1, e.channels, e.sampleRate,
                        avgBytesPerSec, e.blockAlign, e.bitsPerSample, /*extensionData=*/{},
                        /*factSampleFrames=*/0);
                }
                else if (e.format == XwbFormat::ADPCM)
                {
                    const uint32_t avgBytesPerSec = (e.samplesPerBlock > 0)
                        ? (e.sampleRate * e.blockAlign / e.samplesPerBlock) : e.sampleRate;
                    const uint32_t totalSamples = (e.blockAlign > 0)
                        ? (e.dataLength / e.blockAlign * e.samplesPerBlock) : 0;
                    wav = CNA::Internal::Audio::BuildWavFromWaveFormatEx(
                        audioData, e.dataLength, /*wFormatTag=*/2, e.channels, e.sampleRate,
                        avgBytesPerSec, e.blockAlign, /*bitsPerSample=*/4,
                        CNA::Internal::Audio::BuildStandardMsAdpcmExtension(e.samplesPerBlock),
                        totalSamples);
                }
                // XMA/WMA: no decode path anywhere in this stack (CHECKLIST.md accepted
                // deviation) -- nothing to export for those formats.

                if (!wav.empty())
                {
                    const std::string fileName = name.empty()
                        ? ("entry" + std::to_string(i) + ".wav")
                        : (name + ".wav");
                    std::ofstream out((outDir / fileName).string(), std::ios::binary);
                    out.write(reinterpret_cast<const char*>(wav.data()),
                              static_cast<std::streamsize>(wav.size()));
                }
            }
        }
    }

    std::printf("]}\n");
    return 0;
}
