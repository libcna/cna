#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace Microsoft::Xna::Framework::Audio
{
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
            throw std::invalid_argument("audioEngine must not be null");
        if (filename.empty())
            throw std::invalid_argument("filename must not be empty");

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
    bool SoundBank::getIsInUseProperty()    const { return false; }

    const CNA::Internal::Audio::XsbData* SoundBank::GetXsbData() const
    {
        return xactImpl_ ? &xactImpl_->data : nullptr;
    }

    // ── GetCue ────────────────────────────────────────────────────────────────

    Cue* SoundBank::GetCue(const std::string& name)
    {
        if (name.empty())
            throw std::invalid_argument("name must not be empty");
        if (isDisposed_)
            throw std::runtime_error("SoundBank is disposed");

        uint16_t cueIndex = 0xFFFF;

        if (xactImpl_)
        {
            auto it = xactImpl_->data.cueNameMap.find(name);
            if (it != xactImpl_->data.cueNameMap.end())
                cueIndex = it->second;
            else
                std::cerr << "[SoundBank] Cue not found: " << name << "\n";
        }

        return new Cue(name, this, cueIndex);
    }

    // ── PlayCue ───────────────────────────────────────────────────────────────

    void SoundBank::PlayCue(const std::string& name)
    {
        if (name.empty())
            throw std::invalid_argument("name must not be empty");
        if (isDisposed_)
            throw std::runtime_error("SoundBank is disposed");

        // Fire-and-forget: create a Cue, play it, immediately discard the object.
        // The underlying SoundEffect handles lifetime via SDL3_mixer callbacks.
        std::unique_ptr<Cue> cue(GetCue(name));
        cue->Play();
        // Cue destructor stops immediately — for fire-and-forget, Play() has
        // already submitted the audio buffers; the track lives on in SDL3_mixer.
    }

    void SoundBank::PlayCue(const std::string& name,
                             const AudioListener& /*listener*/,
                             const AudioEmitter& /*emitter*/)
    {
        PlayCue(name);
    }

    // ── Dispose ───────────────────────────────────────────────────────────────

    void SoundBank::Dispose()
    {
        if (!isDisposed_)
        {
            Disposing.Raise(this, System::EventArgs::Empty);
            xactImpl_.reset();
            isDisposed_ = true;
        }
    }

    GetTypeNameCPP(SoundBank, "Microsoft::Xna::Framework::Audio::SoundBank")
}
