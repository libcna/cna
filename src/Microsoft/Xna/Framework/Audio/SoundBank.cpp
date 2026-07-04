// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <vector>

namespace Microsoft::Xna::Framework::Audio
{
    namespace
    {
        // Force-sweep a still-playing fire-and-forget cue after this long, purely as a safety
        // net against unbounded growth (see FireAndForget's comment in SoundBank.hpp) -- far
        // longer than any normal one-shot SFX, so it never affects ordinary playback.
        constexpr std::chrono::minutes kFireAndForgetSafetyNet{5};
    }

    // ── Internal impl ─────────────────────────────────────────────────────────

    struct SoundBank::XactSoundBankImpl
    {
        CNA::Internal::Audio::XsbData data;

        explicit XactSoundBankImpl(CNA::Internal::Audio::XsbData d)
            : data(std::move(d))
        {}
    };

    // ── Constructors ──────────────────────────────────────────────────────────

    SoundBank::SoundBank(AudioEngine* audioEngine, const std::string& filename)
        : engine_(audioEngine)
    {
        if (!audioEngine)
            throw System::ArgumentNullException("audioEngine");
        if (filename.empty())
            throw System::ArgumentNullException("filename");

        std::ifstream f(filename, std::ios::binary | std::ios::ate);
        if (!f.is_open())
        {
            std::cerr << "[SoundBank] Cannot open XSB: " << filename << "\n";
            return;
        }
        auto sz = f.tellg(); f.seekg(0);
        std::vector<uint8_t> raw(static_cast<std::size_t>(sz));
        f.read(reinterpret_cast<char*>(raw.data()), sz);

        try
        {
            auto xsb = CNA::Internal::Audio::ParseXsb(raw);
            std::cerr << "[SoundBank] Loaded XSB: " << filename
                      << " cues=" << xsb.cues.size()
                      << " sounds=" << xsb.sounds.size() << "\n";
            xactImpl_ = std::make_unique<XactSoundBankImpl>(std::move(xsb));
        }
        catch (const std::exception& ex)
        {
            std::cerr << "[SoundBank] XSB parse error (" << filename << "): " << ex.what() << "\n";
        }
    }

    SoundBank::~SoundBank()
    {
        Dispose();
    }

    // ── Properties ────────────────────────────────────────────────────────────

    bool SoundBank::getIsDisposedProperty() const { return isDisposed_; }

    bool SoundBank::getIsInUseProperty() const
    {
        for (const auto& faf : fireAndForget_)
            if (faf.cue && faf.cue->getIsPlayingProperty())
                return true;
        return false;
    }

    const CNA::Internal::Audio::XsbData* SoundBank::GetXsbData() const
    {
        return xactImpl_ ? &xactImpl_->data : nullptr;
    }

    // ── GetCue ────────────────────────────────────────────────────────────────

    Cue* SoundBank::GetCue(const std::string& name)
    {
        if (name.empty())
            throw System::ArgumentNullException("name");
        if (isDisposed_)
            throw System::ObjectDisposedException("SoundBank");

        if (xactImpl_)
        {
            auto it = xactImpl_->data.cueNameMap.find(name);
            if (it != xactImpl_->data.cueNameMap.end())
                return new Cue(name, this, it->second);
        }

        throw System::InvalidOperationException("Invalid cue name!");
    }

    // ── PlayCue ───────────────────────────────────────────────────────────────

    void SoundBank::PlayCue(const std::string& name)
    {
        PlayCueInternal(name, nullptr, nullptr);
    }

    void SoundBank::PlayCue(const std::string& name,
                             const AudioListener& listener,
                             const AudioEmitter& emitter)
    {
        PlayCueInternal(name, &listener, &emitter);
    }

    void SoundBank::PlayCueInternal(const std::string& name,
                                     const AudioListener* listener,
                                     const AudioEmitter* emitter)
    {
        if (name.empty())
            throw System::ArgumentNullException("name");
        if (isDisposed_)
            throw System::ObjectDisposedException("SoundBank");

        // Sweep fire-and-forget cues that have finished playing (destroying a Cue whose sound
        // has already stopped is effectively a no-op) plus any still-playing entry past the
        // safety-net timeout -- NOT simply "older than N seconds", which would cut off any
        // one-shot or music cue longer than that regardless of whether it was still playing.
        auto now = std::chrono::steady_clock::now();
        fireAndForget_.erase(
            std::remove_if(
                fireAndForget_.begin(), fireAndForget_.end(),
                [&now](const FireAndForget& faf)
                {
                    if (faf.cue && faf.cue->getIsPlayingProperty())
                    {
                        return now - faf.created >= kFireAndForgetSafetyNet;
                    }
                    return true;
                }),
            fireAndForget_.end());

        std::unique_ptr<Cue> cue(GetCue(name));
        cue->Play();
        // FNA computes the 3D dsp settings before FACTSoundBank_Play3D, so the cue is already
        // positioned from its very first output frame -- Apply3D() here runs synchronously,
        // before this thread's next real audio callback, so there is no observable difference.
        if (listener && emitter)
            cue->Apply3D(*listener, *emitter);
        // Keep the Cue alive so its SoundEffectInstances (and their SDL3_mixer
        // tracks) are not destroyed before the sound has had a chance to play.
        fireAndForget_.push_back({std::move(cue), now});
    }

    // ── Dispose ───────────────────────────────────────────────────────────────

    void SoundBank::Dispose()
    {
        if (!isDisposed_)
        {
            Disposing.Raise(this, System::EventArgs::Empty);
            fireAndForget_.clear(); // stops any still-playing fire-and-forget cues
            xactImpl_.reset();
            isDisposed_ = true;
        }
    }

    GetTypeNameCPP(SoundBank, "Microsoft.Xna.Framework.Audio.SoundBank")
}
