// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/IO/FileNotFoundException.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <vector>

namespace Microsoft::Xna::Framework::Audio
{
    // ── Internal impl ─────────────────────────────────────────────────────────

    struct WaveBank::XactWaveBankImpl
    {
        CNA::Internal::Audio::XwbData data;

        // Per-entry SoundEffect cache (optional: created on first request)
        std::vector<std::optional<SoundEffect>> cache;

        explicit XactWaveBankImpl(CNA::Internal::Audio::XwbData d)
            : data(std::move(d))
            , cache(data.entries.size())
        {}
    };

    // ── WAV-file builders (for ADPCM wrapping) ────────────────────────────────

    namespace
    {
        static void w16(std::vector<uint8_t>& v, uint16_t x)
        {
            v.push_back(static_cast<uint8_t>(x));
            v.push_back(static_cast<uint8_t>(x >> 8));
        }
        static void w32(std::vector<uint8_t>& v, uint32_t x)
        {
            v.push_back(static_cast<uint8_t>(x));
            v.push_back(static_cast<uint8_t>(x >>  8));
            v.push_back(static_cast<uint8_t>(x >> 16));
            v.push_back(static_cast<uint8_t>(x >> 24));
        }
        static void tag(std::vector<uint8_t>& v, const char* t)
        {
            v.push_back(static_cast<uint8_t>(t[0]));
            v.push_back(static_cast<uint8_t>(t[1]));
            v.push_back(static_cast<uint8_t>(t[2]));
            v.push_back(static_cast<uint8_t>(t[3]));
        }

        std::vector<uint8_t> BuildPcmWav(
            const uint8_t* audioData, uint32_t audioLen,
            uint16_t channels, uint32_t sampleRate, uint8_t bitsPerSample)
        {
            uint16_t blockAlign      = static_cast<uint16_t>(channels * (bitsPerSample / 8));
            uint32_t avgBytesPerSec  = sampleRate * blockAlign;
            uint32_t riffPayload     = 4 + (8 + 16) + (8 + audioLen);

            std::vector<uint8_t> wav;
            wav.reserve(12 + 24 + 8 + audioLen);

            tag(wav, "RIFF"); w32(wav, riffPayload);
            tag(wav, "WAVE");
            tag(wav, "fmt "); w32(wav, 16);
            w16(wav, 1); w16(wav, channels);
            w32(wav, sampleRate); w32(wav, avgBytesPerSec);
            w16(wav, blockAlign); w16(wav, bitsPerSample);
            tag(wav, "data"); w32(wav, audioLen);
            wav.insert(wav.end(), audioData, audioData + audioLen);
            return wav;
        }

        std::vector<uint8_t> BuildAdpcmWav(
            const uint8_t* audioData, uint32_t audioLen,
            uint16_t channels, uint32_t sampleRate,
            uint16_t blockAlign, uint16_t samplesPerBlock)
        {
            // nAvgBytesPerSec
            uint32_t avgBytesPerSec = (samplesPerBlock > 0)
                ? (sampleRate * blockAlign / samplesPerBlock) : sampleRate;
            uint32_t totalSamples   = (blockAlign > 0)
                ? (audioLen / blockAlign * samplesPerBlock) : 0;

            // fmt chunk: 18 bytes (16 standard + 2 cbSize + 2 wSamplesPerBlock = 20)
            uint32_t fmtSize = 20;
            uint32_t riffPayload = 4 + (8 + fmtSize) + (8 + 4) + (8 + audioLen);

            std::vector<uint8_t> wav;
            wav.reserve(12 + 8 + fmtSize + 12 + 8 + audioLen);

            tag(wav, "RIFF"); w32(wav, riffPayload);
            tag(wav, "WAVE");
            tag(wav, "fmt "); w32(wav, fmtSize);
            w16(wav, 2);       // MS-ADPCM
            w16(wav, channels);
            w32(wav, sampleRate); w32(wav, avgBytesPerSec);
            w16(wav, blockAlign); w16(wav, 4); // bitsPerSample=4
            w16(wav, 2);       // cbSize = 2
            w16(wav, samplesPerBlock);
            tag(wav, "fact"); w32(wav, 4);
            w32(wav, totalSamples);
            tag(wav, "data"); w32(wav, audioLen);
            wav.insert(wav.end(), audioData, audioData + audioLen);
            return wav;
        }
    }

    // ── Constructors ──────────────────────────────────────────────────────────

    WaveBank::WaveBank(AudioEngine* audioEngine,
                       const std::string& nonStreamingWaveBankFilename)
        : engine_(audioEngine)
    {
        if (!audioEngine)
            throw System::ArgumentNullException("audioEngine");
        if (nonStreamingWaveBankFilename.empty())
            throw System::ArgumentNullException("nonStreamingWaveBankFilename");

        Init(nonStreamingWaveBankFilename);
    }

    WaveBank::WaveBank(AudioEngine* audioEngine,
                       const std::string& streamingWaveBankFilename,
                       SharpRuntime::intcs /*offset*/,
                       SharpRuntime::shortcs /*packetSize*/)
        : engine_(audioEngine)
    {
        // offset/packetSize are unused: FNA's own streaming ctor (WaveBank.cs) never forwards
        // them to FACTStreamingParameters either (only .file is set) -- FAudio only consults
        // packetSize when a custom I/O layer is installed, which FNA never does, so matching
        // FNA exactly means these two ctor parameters are dead on both sides (T-3F).
        if (!audioEngine)
            throw System::ArgumentNullException("audioEngine");
        if (streamingWaveBankFilename.empty())
            throw System::ArgumentNullException("streamingWaveBankFilename");

        InitStreaming(streamingWaveBankFilename);
    }

    void WaveBank::Init(const std::string& filename)
    {
        // FNA's non-streaming WaveBank ctor reads filename via TitleContainer.ReadToPointer,
        // which throws FileNotFoundException on a missing file before ever reaching FACT
        // (WaveBank.cs) -- match that here (P9-HARDWARE-003). Corrupt-but-existing content stays
        // a silent stub below: FNA never checks FACTAudioEngine_CreateInMemoryWaveBank's return
        // code either. The streaming ctor (InitStreaming) is unaffected: FNA's streaming path
        // never goes through TitleContainer at all, it opens the file with the native
        // FAudio_fopen instead, so a missing streaming file doesn't throw in FNA either.
        std::ifstream f(filename, std::ios::binary | std::ios::ate);
        if (!f.is_open())
        {
            throw System::IO::FileNotFoundException(
                "Could not find file '" + filename + "'.", filename);
        }
        auto sz = f.tellg(); f.seekg(0);
        std::vector<uint8_t> raw(static_cast<std::size_t>(sz));
        f.read(reinterpret_cast<char*>(raw.data()), sz);

        try
        {
            auto xwb = CNA::Internal::Audio::ParseXwb(std::move(raw));
            std::cerr << "[WaveBank] Loaded XWB: " << filename
                      << " bank=\"" << xwb.bankName << "\""
                      << " entries=" << xwb.entries.size() << "\n";
            xactImpl_ = std::make_unique<XactWaveBankImpl>(std::move(xwb));
            engine_->RegisterWaveBank(this);
        }
        catch (const std::exception& ex)
        {
            std::cerr << "[WaveBank] XWB parse error (" << filename << "): " << ex.what() << "\n";
        }
    }

    void WaveBank::InitStreaming(const std::string& filename)
    {
        try
        {
            auto xwb = CNA::Internal::Audio::ParseXwbStreamingHeader(filename);
            std::cerr << "[WaveBank] Loaded XWB (streaming): " << filename
                      << " bank=\"" << xwb.bankName << "\""
                      << " entries=" << xwb.entries.size() << "\n";
            xactImpl_ = std::make_unique<XactWaveBankImpl>(std::move(xwb));
            engine_->RegisterWaveBank(this);
        }
        catch (const std::exception& ex)
        {
            std::cerr << "[WaveBank] XWB streaming parse error (" << filename << "): "
                      << ex.what() << "\n";
        }
    }

    WaveBank::~WaveBank()
    {
        Dispose();
    }

    // ── Properties ────────────────────────────────────────────────────────────

    bool WaveBank::getIsDisposedProperty() const { return isDisposed_; }
    bool WaveBank::getIsPreparedProperty() const { return !isDisposed_ && xactImpl_ != nullptr; }

    bool WaveBank::getIsInUseProperty() const
    {
        // XA-7: a paused cue is still in use -- see SoundBank::getIsInUseProperty's identical
        // fix for the rationale (FACT_STATE_INUSE stays set while paused).
        for (const auto* cue : activeCues_)
            if (cue && (cue->getIsPlayingProperty() || cue->getIsPausedProperty()))
                return true;
        return false;
    }

    void WaveBank::RegisterCue(Cue* cue)
    {
        if (!cue) return;
        activeCues_.push_back(cue);
    }

    void WaveBank::UnregisterCue(Cue* cue)
    {
        activeCues_.erase(std::remove(activeCues_.begin(), activeCues_.end(), cue), activeCues_.end());
    }

    // ── Private accessors (used by AudioEngine / Cue) ─────────────────────────

    const std::string& WaveBank::getBankName() const
    {
        static const std::string empty;
        return xactImpl_ ? xactImpl_->data.bankName : empty;
    }

    bool WaveBank::StreamingInternal() const
    {
        return xactImpl_ && xactImpl_->data.streaming;
    }

    std::size_t WaveBank::ResidentFileBytesInternal() const
    {
        return xactImpl_ ? xactImpl_->data.fileData.size() : 0;
    }

    const SoundEffect* WaveBank::GetSoundEffect(unsigned short waveIndex)
    {
        if (!xactImpl_ || waveIndex >= xactImpl_->data.entries.size())
            return nullptr;

        auto& cached = xactImpl_->cache[waveIndex];
        if (cached.has_value())
            return &cached.value();

        const auto& entry = xactImpl_->data.entries[waveIndex];

        std::vector<uint8_t> streamedBytes;
        const uint8_t* audioData;
        const uint32_t audioLen = entry.dataLength;

        if (xactImpl_->data.streaming)
        {
            // Lazy per-entry disk read: xactImpl_->data.fileData only holds the header/metadata
            // segments (see ParseXwbStreamingHeader), not wave audio.
            std::ifstream sf(xactImpl_->data.sourcePath, std::ios::binary);
            if (!sf.is_open())
            {
                std::cerr << "[WaveBank] Cannot reopen streaming source for wave " << waveIndex
                          << ": " << xactImpl_->data.sourcePath << "\n";
                return nullptr;
            }

            // IN-9: dataOffset/dataLength come straight from the parsed .xwb header and are
            // never range-checked for the streaming path (unlike the non-streaming path below,
            // which checks against the fully-resident buffer's size) -- a corrupt/adversarial
            // dataLength could otherwise drive an unbounded resize() before any try/catch runs.
            // Bound it against the real on-disk file size first.
            sf.seekg(0, std::ios::end);
            const std::streamoff fileSize = sf.tellg();
            if (fileSize < 0 ||
                static_cast<uint64_t>(entry.dataOffset) + audioLen > static_cast<uint64_t>(fileSize))
            {
                std::cerr << "[WaveBank] Wave " << waveIndex << " streaming data out of range\n";
                return nullptr;
            }
            sf.seekg(static_cast<std::streamoff>(entry.dataOffset));

            try
            {
                streamedBytes.resize(audioLen);
            }
            catch (const std::exception& ex)
            {
                std::cerr << "[WaveBank] Wave " << waveIndex << " streaming allocation failed: "
                          << ex.what() << "\n";
                return nullptr;
            }
            sf.read(reinterpret_cast<char*>(streamedBytes.data()), static_cast<std::streamsize>(audioLen));
            if (static_cast<uint32_t>(sf.gcount()) != audioLen)
            {
                std::cerr << "[WaveBank] Wave " << waveIndex << " streaming read truncated\n";
                return nullptr;
            }
            audioData = streamedBytes.data();
        }
        else
        {
            // Check that data range is valid. Widen to 64-bit before summing so a corrupt/
            // adversarial entry can't wrap this check via uint32_t overflow and pass with an
            // out-of-range offset.
            const auto& fd = xactImpl_->data.fileData;
            if (static_cast<uint64_t>(entry.dataOffset) + entry.dataLength > fd.size())
            {
                std::cerr << "[WaveBank] Wave " << waveIndex << " data out of range\n";
                return nullptr;
            }
            audioData = fd.data() + entry.dataOffset;
        }

        try
        {
            using CNA::Internal::Audio::XwbFormat;

            if (entry.format == XwbFormat::PCM)
            {
                // 8-bit PCM: SDL expects unsigned 8-bit; we use raw approach for 16-bit
                if (entry.bitsPerSample == 16)
                {
                    std::vector<uint8_t> samples(audioData, audioData + audioLen);
                    AudioChannels ch = (entry.channels == 1)
                        ? AudioChannels::Mono : AudioChannels::Stereo;
                    cached.emplace(samples,
                                   static_cast<SharpRuntime::intcs>(entry.sampleRate),
                                   ch);
                }
                else
                {
                    // 8-bit PCM: wrap in WAV since MIX_LoadRawAudio expects S16
                    auto wav = BuildPcmWav(audioData, audioLen,
                                           entry.channels, entry.sampleRate, 8);
                    std::string s(reinterpret_cast<const char*>(wav.data()), wav.size());
                    std::istringstream ss(s);
                    // FromStream returns a heap SoundEffect* the caller owns; wrap it so the
                    // allocation is freed once its value has been moved into the cache.
                    std::unique_ptr<SoundEffect> loaded(SoundEffect::FromStream(ss));
                    cached.emplace(std::move(*loaded));
                }
            }
            else if (entry.format == XwbFormat::ADPCM)
            {
                auto wav = BuildAdpcmWav(audioData, audioLen,
                                          entry.channels, entry.sampleRate,
                                          entry.blockAlign, entry.samplesPerBlock);
                std::string s(reinterpret_cast<const char*>(wav.data()), wav.size());
                std::istringstream ss(s);
                std::unique_ptr<SoundEffect> loaded(SoundEffect::FromStream(ss));
                cached.emplace(std::move(*loaded));
            }
            else
            {
                std::cerr << "[WaveBank] Unsupported format " << static_cast<int>(entry.format)
                          << " for wave " << waveIndex << "\n";
                return nullptr;
            }
        }
        catch (const std::exception& ex)
        {
            std::cerr << "[WaveBank] Failed to create SoundEffect for wave "
                      << waveIndex << ": " << ex.what() << "\n";
            return nullptr;
        }

        return &cached.value();
    }

    // ── Dispose ───────────────────────────────────────────────────────────────

    void WaveBank::Dispose()
    {
        if (!isDisposed_)
        {
            Disposing.Raise(this, System::EventArgs::Empty);
            if (engine_) engine_->UnregisterWaveBank(this);
            activeCues_.clear();
            xactImpl_.reset();
            isDisposed_ = true;
        }
    }

    GetTypeNameCPP(WaveBank, "Microsoft.Xna.Framework.Audio.WaveBank")
}
