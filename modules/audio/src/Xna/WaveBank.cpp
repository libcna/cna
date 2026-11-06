// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "Internal/AudioLog.hpp"
#include "Internal/XactStreamingHeader.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "CNA/Internal/Audio/WavWrapper.hpp"
#include "CNA/Internal/Audio/EncodedWaveBankDecoder.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"
#include "CNA/Internal/CaseInsensitivePath.hpp"
#include "TitleContentBytes.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/IO/FileStream.hpp"
#include "System/IO/Path.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <vector>

namespace Microsoft::Xna::Framework::Audio
{
    // ── Internal impl ─────────────────────────────────────────────────────────

    struct WaveBank::XactWaveBankImpl
    {
        CNA::Internal::Audio::XwbData data;
        // Keep the exact file used for metadata, even if its name or the cwd changes.
        // Access and destruction use xactImplMutex_ along with the cache below.
        std::unique_ptr<std::ifstream> streamingSource;

        // Per-entry SoundEffect cache (optional: created on first request). Concurrency (both
        // against other GetSoundEffect() calls and against Dispose()) is guarded by the owning
        // WaveBank's own xactImplMutex_ (AUD-11-024/025), not a lock in here -- Dispose() needs
        // to serialize against xactImpl_'s own destruction too, which a lock scoped to this
        // pimpl's own lifetime could never do (see WaveBank.hpp's xactImplMutex_ comment).
        std::vector<std::optional<SoundEffect>> cache;

        explicit XactWaveBankImpl(CNA::Internal::Audio::XwbData d,
                                 std::unique_ptr<std::ifstream> source = {})
            : data(std::move(d))
            , streamingSource(std::move(source))
            , cache(data.entries.size())
        {}
    };

    // ── WAV-file builders (for PCM8/ADPCM wrapping) ───────────────────────────
    //
    // AUDIO-ADPCM-001 (2026-07-17 deep audit follow-up): the previous local BuildAdpcmWav() wrote
    // a "fmt " chunk with cbSize=2 (only wSamplesPerBlock, no coefficient table at all). The
    // real MS-ADPCM decoder requires at least 7 coefficient pairs:
    // "Missing required coefficients in MS ADPCM format header") and validates them against the
    // standard preset table -- confirmed empirically that the old wrapper's output was REJECTED
    // outright by the WAV decoder ("Could not read MS ADPCM format header"), meaning every
    // MS-ADPCM-compressed XACT WaveBank entry silently failed to load (WaveBank::GetSoundEffect
    // caught the resulting exception and returned nullptr). Now shares the same corrected,
    // tested WAV-assembly logic as the XNB SoundEffectReader (CNA::Internal::Audio::
    // BuildWavFromWaveFormatEx/BuildStandardMsAdpcmExtension).

    namespace
    {
        std::string CheckedWaveBankPath(const std::string& filename)
        {
            const auto path = CNA::Internal::ResolveExistingXnaPath(System::IO::Path::GetFullPath(
                CNA::Internal::NormalizeXnaPathSeparators(filename)));
            System::IO::FileStream file(path, System::IO::FileMode::Open,
                                       System::IO::FileAccess::Read, System::IO::FileShare::Read);
            // XNA requires length > 4 and WBND magic before native resource creation.
            // Reading five bytes establishes that boundary without narrowing a large
            // file's length to Sharp Stream's current int32 Length property.
            std::array<SharpRuntime::bytecs, 5> header{};
            SharpRuntime::intcs read = 0;
            while (read < static_cast<SharpRuntime::intcs>(header.size()))
            {
                const auto count = file.Read(header.data(), read,
                    static_cast<SharpRuntime::intcs>(header.size()) - read);
                if (count == 0) break;
                read += count;
            }
            if (read != 5 || header[0] != 'W' || header[1] != 'B' ||
                header[2] != 'N' || header[3] != 'D')
                throw System::ArgumentException("XACT could not load the data provided. Make sure you are using the correct version of the XACT tool.");
            return path;
        }

        std::vector<uint8_t> BuildPcmWav(
            const uint8_t* audioData, uint32_t audioLen,
            uint16_t channels, uint32_t sampleRate, uint8_t bitsPerSample)
        {
            const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8));
            const uint32_t avgBytesPerSec = sampleRate * blockAlign;
            return CNA::Internal::Audio::BuildWavFromWaveFormatEx(
                audioData, audioLen, /*wFormatTag=*/1, channels, sampleRate, avgBytesPerSec,
                blockAlign, bitsPerSample, /*extensionData=*/{}, /*factSampleFrames=*/0);
        }

        std::vector<uint8_t> BuildAdpcmWav(
            const uint8_t* audioData, uint32_t audioLen,
            uint16_t channels, uint32_t sampleRate,
            uint16_t blockAlign, uint16_t samplesPerBlock)
        {
            const uint32_t avgBytesPerSec = (samplesPerBlock > 0)
                ? (sampleRate * blockAlign / samplesPerBlock) : sampleRate;
            const uint32_t totalSamples = (blockAlign > 0)
                ? (audioLen / blockAlign * samplesPerBlock) : 0;
            return CNA::Internal::Audio::BuildWavFromWaveFormatEx(
                audioData, audioLen, /*wFormatTag=*/2, channels, sampleRate, avgBytesPerSec,
                blockAlign, /*bitsPerSample=*/4,
                CNA::Internal::Audio::BuildStandardMsAdpcmExtension(samplesPerBlock),
                totalSamples);
        }
    }

    // TQ-084: XNA Refresh .ctors use NullNotAllowed/RequireNonNullAudioEngine,
    // InvalidContentVersion and CouldNotCreateResource from FrameworkResources.
    // ── Constructors ──────────────────────────────────────────────────────────

    WaveBank::WaveBank(AudioEngine* audioEngine,
                       const std::string& nonStreamingWaveBankFilename)
        : engine_(audioEngine)
    {
        if (!audioEngine)
            throw System::ArgumentNullException("audioEngine", "You must pass in a valid audio engine.");
        if (nonStreamingWaveBankFilename.empty())
            throw System::ArgumentNullException("nonStreamingWaveBankFilename",
                "This method does not accept null for this parameter.");

        Init(CheckedWaveBankPath(nonStreamingWaveBankFilename));
    }

    WaveBank::WaveBank(AudioEngine* audioEngine,
                       const std::string& streamingWaveBankFilename,
                       SharpRuntime::intcs offset,
                       SharpRuntime::shortcs /*packetSize*/)
        : engine_(audioEngine)
    {
        // XNA .ctor IL_0068-IL_006b forwards the byte offset to CreateStreamingHandle;
        // FNA drops it. packetSize remains unused by CNA's per-entry streaming reader.
        if (!audioEngine)
            throw System::ArgumentNullException("audioEngine", "You must pass in a valid audio engine.");
        if (streamingWaveBankFilename.empty())
            throw System::ArgumentNullException("streamingWaveBankFilename",
                "This method does not accept null for this parameter.");

        InitStreaming(CheckedWaveBankPath(streamingWaveBankFilename),
                      static_cast<SharpRuntime::uintcs>(offset));
    }

    void WaveBank::Init(const std::string& filename)
    {
        // XNA native CreateHandle IL_001c-IL_0038 refuses a released engine, after
        // the managed constructor's CheckWaveBankHeader has already succeeded.
        if (engine_->getIsDisposedProperty())
            throw System::InvalidOperationException("This resource could not be created.");
        // Both constructors supply a validated absolute cwd-based path. Unlike FNA's
        // unchecked native return, XNA refuses a resource whose metadata cannot be loaded.
        std::vector<uint8_t> raw =
            CNA::Internal::Audio::ReadTitleContentBytes(filename);

        try
        {
            auto xwb = CNA::Internal::Audio::ParseXwb(std::move(raw));
            CNA::Internal::Audio::LogAudio(CNA::LogLevel::DEBUG,
                "[WaveBank] Loaded XWB: ", filename, " bank=\"", xwb.bankName, "\" entries=",
                xwb.entries.size());
            xactImpl_ = std::make_unique<XactWaveBankImpl>(std::move(xwb));
            engine_->RegisterWaveBank(this);
        }
        catch (const std::runtime_error&)
        {
            throw System::InvalidOperationException("This resource could not be created.",
                                                    std::current_exception());
        }
    }

    void WaveBank::InitStreaming(const std::string& filename, SharpRuntime::uintcs offset)
    {
        // XNA native CreateStreamingHandle IL_001c-IL_0038 has the same engine gate.
        if (engine_->getIsDisposedProperty())
            throw System::InvalidOperationException("This resource could not be created.");
        try
        {
            const std::string resolvedFilename = CNA::Internal::ResolveExistingXnaPath(filename);
            auto source = std::make_unique<std::ifstream>(
                CNA::Internal::PathFromUtf8(resolvedFilename), std::ios::binary);
            if (!source->is_open())
                throw std::runtime_error("XWB: cannot open file for streaming");
            auto xwb = CNA::Internal::Audio::ParseXwbStreamingHeaderAtOffset(
                *source, resolvedFilename, offset);
            CNA::Internal::Audio::LogAudio(CNA::LogLevel::DEBUG,
                "[WaveBank] Loaded XWB (streaming): ", filename, " bank=\"", xwb.bankName,
                "\" entries=", xwb.entries.size());
            xactImpl_ = std::make_unique<XactWaveBankImpl>(std::move(xwb), std::move(source));
            engine_->RegisterWaveBank(this);
        }
        catch (const std::runtime_error&)
        {
            throw System::InvalidOperationException("This resource could not be created.",
                                                    std::current_exception());
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
        // AUD-11-024/025: locked before the very first `xactImpl_` access (not just around the
        // cache lookup-then-populate sequence) so this can never race Dispose()'s
        // `xactImpl_.reset()` on another thread -- both the "is xactImpl_ still alive" check and
        // everything that follows must happen under the same lock Dispose() takes, or a
        // just-checked-non-null `xactImpl_` could be destroyed out from under this function
        // between the check and its first real use. Also still serializes two threads racing to
        // first-use the same entry against each other, exactly as before.
        std::lock_guard<std::mutex> lock(xactImplMutex_);

        if (!xactImpl_ || waveIndex >= xactImpl_->data.entries.size())
            return nullptr;

        auto& cached = xactImpl_->cache[waveIndex];
        if (cached.has_value())
            return &cached.value();

        const auto& entry = xactImpl_->data.entries[waveIndex];

        std::vector<uint8_t> streamedBytes;
        const uint8_t* audioData;
        const uint32_t audioLen = entry.dataLength;
        using CNA::Internal::Audio::XwbFormat;
        const char* formatName = entry.format == XwbFormat::WMA
            ? (entry.miniFormat & 0x80000000u ? "WMA Pro" : "WMA/WMAv2")
            : entry.format == XwbFormat::XMA ? "XMA/XMA2"
            : entry.format == XwbFormat::ADPCM ? "ADPCM" : "PCM";
        if ((entry.format == XwbFormat::WMA || entry.format == XwbFormat::XMA) &&
            audioLen > CNA::Internal::Audio::EncodedWaveBankByteLimit)
        {
            CNA::Internal::Audio::LogAudio(CNA::LogLevel::ERROR,
                "[WaveBank] Wave ", waveIndex, " in bank \"", getBankName(), "\" uses ",
                formatName, ": encoded entry exceeds the 256 MiB budget");
            return nullptr;
        }

        if (xactImpl_->data.streaming)
        {
            // Lazy per-entry disk read: xactImpl_->data.fileData only holds the header/metadata
            // segments (see ParseXwbStreamingHeader), not wave audio.
            auto& sf = *xactImpl_->streamingSource;
            sf.clear();

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
                CNA::Internal::Audio::LogAudio(CNA::LogLevel::ERROR,
                    "[WaveBank] Wave ", waveIndex, " in bank \"", getBankName(), "\" uses ",
                    formatName, ": streaming data out of range");
                return nullptr;
            }
            sf.seekg(static_cast<std::streamoff>(entry.dataOffset));

            try
            {
                streamedBytes.resize(audioLen);
            }
            catch (const std::exception& ex)
            {
                CNA::Internal::Audio::LogAudio(CNA::LogLevel::ERROR,
                    "[WaveBank] Wave ", waveIndex, " in bank \"", getBankName(), "\" uses ",
                    formatName, ": streaming allocation failed: ", ex.what());
                return nullptr;
            }
            sf.read(reinterpret_cast<char*>(streamedBytes.data()), static_cast<std::streamsize>(audioLen));
            if (static_cast<uint32_t>(sf.gcount()) != audioLen)
            {
                CNA::Internal::Audio::LogAudio(CNA::LogLevel::ERROR,
                    "[WaveBank] Wave ", waveIndex, " in bank \"", getBankName(), "\" uses ",
                    formatName, ": streaming read truncated");
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
                CNA::Internal::Audio::LogAudio(CNA::LogLevel::ERROR,
                    "[WaveBank] Wave ", waveIndex, " in bank \"", getBankName(), "\" uses ",
                    formatName, ": data out of range");
                return nullptr;
            }
            audioData = fd.data() + entry.dataOffset;
        }

        try
        {
            // AUD-11-014: entry.loopStartSample/loopTotalSamples are parsed from the .xwb
            // (FACTWaveBankEntry's LoopRegion, dwStartSample/dwTotalSamples -- expressed in
            // decoded PCM sample frames, matching real FAudio's own FACT.c, which applies these
            // exact fields to the playback buffer's LoopBegin/LoopLength whenever the cue's
            // PlayWaveEvent requested looping) -- previously parsed but never wired into the
            // constructed SoundEffect at all, so every WaveBank-sourced looping cue looped the
            // *entire* track regardless of an authored intro-then-loop region. Baking the region
            // into the cached SoundEffect here is safe regardless of whether any particular play
            // actually loops: SoundEffectInstance::Play() only ever applies loopStart_/loopLength_
            // while IsLooped_ is true (see CP-17/P10-LOOP-003/004), which Cue.cpp already sets
            // per-instance from the PlayWaveEvent's own loopCount -- so this doesn't change
            // playback for any non-looping cue referencing the same cached entry.
            const int32_t loopStart  = static_cast<int32_t>(entry.loopStartSample);
            const int32_t loopLength = static_cast<int32_t>(entry.loopTotalSamples);

            if (entry.format == XwbFormat::PCM)
            {
                // 8-bit PCM is unsigned; the direct raw-audio path is used for signed 16-bit.
                if (entry.bitsPerSample == 16)
                {
                    std::vector<uint8_t> samples(audioData, audioData + audioLen);
                    AudioChannels ch = (entry.channels == 1)
                        ? AudioChannels::Mono : AudioChannels::Stereo;
                    // The WaveBank route, not the public FromBuffer one: an XACT entry's loop
                    // region is kept as authored, not rewritten the way FromBuffer rewrites a
                    // zero length.
                    cached.emplace(SoundEffect(
                        samples, 0, static_cast<SharpRuntime::intcs>(samples.size()),
                        static_cast<SharpRuntime::intcs>(entry.sampleRate), ch, loopStart,
                        loopLength, SoundEffect::RateRange::WaveBank));
                }
                else
                {
                    // Wrap 8-bit PCM in WAV because the raw-audio constructor expects S16.
                    auto wav = BuildPcmWav(audioData, audioLen,
                                           entry.channels, entry.sampleRate, 8);
                    CNA::Internal::Audio::AppendSmplChunkIfLooped(wav, loopStart, loopLength);
                    std::string s(reinterpret_cast<const char*>(wav.data()), wav.size());
                    std::istringstream ss(s);
                    // FromStream returns a heap SoundEffect* the caller owns; wrap it so the
                    // allocation is freed once its value has been moved into the cache.
                    std::unique_ptr<SoundEffect> loaded(SoundEffect::FromStream(ss));
                    // The XWB loop fields are authoritative, including a nonzero
                    // start with zero length (loop through EOF). WAV's optional smpl
                    // wrapper omits zero-length loops, so it cannot carry that case.
                    loaded->loopStart_ = entry.loopStartSample;
                    loaded->loopLength_ = entry.loopTotalSamples;
                    cached.emplace(std::move(*loaded));
                }
            }
            else if (entry.format == XwbFormat::ADPCM)
            {
                auto wav = BuildAdpcmWav(audioData, audioLen,
                                          entry.channels, entry.sampleRate,
                                          entry.blockAlign, entry.samplesPerBlock);
                CNA::Internal::Audio::AppendSmplChunkIfLooped(wav, loopStart, loopLength);
                std::string s(reinterpret_cast<const char*>(wav.data()), wav.size());
                std::istringstream ss(s);
                std::unique_ptr<SoundEffect> loaded(SoundEffect::FromStream(ss));
                // Preserve XWB metadata independently of the temporary WAV wrapper,
                // including LoopLength=0 with a nonzero authored LoopBegin.
                loaded->loopStart_ = entry.loopStartSample;
                loaded->loopLength_ = entry.loopTotalSamples;
                cached.emplace(std::move(*loaded));
            }
            else if (entry.format == XwbFormat::WMA || entry.format == XwbFormat::XMA)
            {
                // Decoding is common to both mixers, outside every audio callback.
                // Streaming banks retain their lazy full-entry/cache contract.
                auto decoder = CNA::Internal::Audio::OpenEncodedWaveBankDecoder(
                    xactImpl_->data, waveIndex, std::span<const uint8_t>(audioData, audioLen));
                const auto channels = decoder->Channels();
                const auto frames = decoder->FrameCount();
                std::vector<int16_t> native(static_cast<std::size_t>(frames) * channels);
                if (decoder->ReadFrames(native) != frames)
                    throw std::runtime_error("encoded entry ended before its decoded frame count");
                std::vector<uint8_t> pcm;
                if (channels == 6)
                    pcm = CNA::Internal::Audio::DownmixEncodedWaveBank51(native);
                else
                {
                    pcm.reserve(native.size() * 2);
                    for (const auto sample : native)
                    {
                        const auto word = static_cast<uint16_t>(sample);
                        pcm.push_back(static_cast<uint8_t>(word));
                        pcm.push_back(static_cast<uint8_t>(word >> 8));
                    }
                }
                cached.emplace(SoundEffect(pcm, 0, static_cast<SharpRuntime::intcs>(pcm.size()),
                    static_cast<SharpRuntime::intcs>(decoder->SampleRate()),
                    channels == 1 ? AudioChannels::Mono : AudioChannels::Stereo,
                    loopStart, loopLength, SoundEffect::RateRange::WaveBank));
            }
            else
            {
                CNA::Internal::Audio::LogAudio(CNA::LogLevel::WARN,
                    "[WaveBank] Wave ", waveIndex, " in bank \"", getBankName(), "\" uses ",
                    formatName, ": unsupported encoded format");
                return nullptr;
            }
        }
        catch (const std::exception& ex)
        {
            CNA::Internal::Audio::LogAudio(CNA::LogLevel::ERROR,
                "[WaveBank] Wave ", waveIndex, " in bank \"", getBankName(), "\" uses ",
                formatName, ": failed to create SoundEffect: ", ex.what());
            return nullptr;
        }

        return &cached.value();
    }

    // ── Dispose ───────────────────────────────────────────────────────────────

    void WaveBank::Dispose()
    {
        Dispose(true);
    }

    void WaveBank::Dispose(bool disposing)
    {
        if (!isDisposed_)
        {
            // XNA Dispose(bool) IL_0018-IL_003f precedes Disposing at IL_005b.
            isDisposed_ = true;
            if (engine_) engine_->UnregisterWaveBank(this);

            // P12-BANK-001: force-stop every cue still using this wave bank instead of merely
            // forgetting about it -- matches FACTWaveBank_Destroy (FACT.c:1457-1483). Snapshot
            // first: Cue::Dispose() below calls back into UnregisterCue() (for every wave bank
            // the cue uses, not just this one), which would otherwise invalidate a live
            // range-for over activeCues_ mid-iteration.
            std::vector<Cue*> cues = activeCues_;
            for (auto* cue : cues)
                if (cue) cue->Dispose(); // idempotent; safe even if already disposed elsewhere
            activeCues_.clear();

            // AUD-11-025: takes the same lock GetSoundEffect() takes before its own first
            // xactImpl_ access, so a concurrent GetSoundEffect() call on another thread either
            // completes entirely before this reset() runs, or blocks here until reset() (and the
            // isDisposed_ update) has finished -- never observes a half-destroyed xactImpl_.
            {
                std::lock_guard<std::mutex> lock(xactImplMutex_);
                xactImpl_.reset();
            }
            if (disposing)
                Disposing.Raise(this, System::EventArgs::Empty);
        }
    }

    GetTypeNameCPP(WaveBank, "Microsoft.Xna.Framework.Audio.WaveBank")
}
