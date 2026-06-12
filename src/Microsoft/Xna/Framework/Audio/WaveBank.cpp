// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"

#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
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
                       const std::string& filename)
        : engine_(audioEngine)
    {
        if (!audioEngine)
            throw std::invalid_argument("audioEngine must not be null");
        if (filename.empty())
            throw std::invalid_argument("filename must not be empty");

        // Load entire file into memory
        std::ifstream f(filename, std::ios::binary | std::ios::ate);
        if (!f.is_open())
        {
            std::cerr << "[WaveBank] Cannot open XWB: " << filename << "\n";
            return;
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
            audioEngine->RegisterWaveBank(this);
        }
        catch (const std::exception& ex)
        {
            std::cerr << "[WaveBank] XWB parse error (" << filename << "): " << ex.what() << "\n";
        }
    }

    WaveBank::WaveBank(AudioEngine* audioEngine,
                       const std::string& filename,
                       SharpRuntime::intcs /*offset*/,
                       SharpRuntime::shortcs /*packetSize*/)
        : WaveBank(audioEngine, filename)
    {
    }

    WaveBank::~WaveBank()
    {
        Dispose();
    }

    // ── Properties ────────────────────────────────────────────────────────────

    bool WaveBank::getIsDisposedProperty() const { return isDisposed_; }
    bool WaveBank::getIsPreparedProperty() const { return !isDisposed_ && xactImpl_ != nullptr; }
    bool WaveBank::getIsInUseProperty()    const { return false; }

    // ── Private accessors (used by AudioEngine / Cue) ─────────────────────────

    const std::string& WaveBank::getBankName() const
    {
        static const std::string empty;
        return xactImpl_ ? xactImpl_->data.bankName : empty;
    }

    const SoundEffect* WaveBank::GetSoundEffect(unsigned short waveIndex)
    {
        if (!xactImpl_ || waveIndex >= xactImpl_->data.entries.size())
            return nullptr;

        auto& cached = xactImpl_->cache[waveIndex];
        if (cached.has_value())
            return &cached.value();

        const auto& entry = xactImpl_->data.entries[waveIndex];

        // Check that data range is valid
        const auto& fd = xactImpl_->data.fileData;
        if (entry.dataOffset + entry.dataLength > fd.size())
        {
            std::cerr << "[WaveBank] Wave " << waveIndex << " data out of range\n";
            return nullptr;
        }

        const uint8_t* audioData = fd.data() + entry.dataOffset;
        uint32_t       audioLen  = entry.dataLength;

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
                    cached.emplace(*SoundEffect::FromStream(ss));
                }
            }
            else if (entry.format == XwbFormat::ADPCM)
            {
                auto wav = BuildAdpcmWav(audioData, audioLen,
                                          entry.channels, entry.sampleRate,
                                          entry.blockAlign, entry.samplesPerBlock);
                std::string s(reinterpret_cast<const char*>(wav.data()), wav.size());
                std::istringstream ss(s);
                cached.emplace(*SoundEffect::FromStream(ss));
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
            xactImpl_.reset();
            isDisposed_ = true;
        }
    }

    GetTypeNameCPP(WaveBank, "Microsoft::Xna::Framework::Audio::WaveBank")
}
