// SPDX-License-Identifier: MS-PL
// Network voice (XNA's automatic voice routing): the microphone of one local gamer per machine,
// speech detection, Opus at 16 kHz in 20 ms frames, one frame per remote machine that may hear it,
// and a stream per talker played through the SoundEffect path.
#include "CNA/Internal/Net/VoiceChat.hpp"
#include "CNA/Internal/Audio/RecordingProvider.hpp"
#include "CNA/Internal/GamerServices/VoiceMutes.hpp"
#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkMachine.hpp"

#ifdef CNA_VOICE_OPUS
#include <opus.h>
#endif

#include <algorithm>
#include <mutex>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace CNA::Internal::Net
{
    namespace
    {
        namespace Xna = Microsoft::Xna::Framework;
        using Clock = std::chrono::steady_clock;
        constexpr auto KeepaliveInterval = std::chrono::seconds(1);
        constexpr auto VoiceTimeout = std::chrono::seconds(3);
        constexpr auto TalkingTimeout = std::chrono::milliseconds(250);
        constexpr auto PermissionInterval = std::chrono::seconds(5);
        // Speech is kept 300 ms after the level drops, so words are not clipped between syllables.
        constexpr int HangoverFrames = 15;
        constexpr int MaxConcealedFrames = 2;
        // A capture burst longer than this (a stalled game) is dropped rather than sent late.
        constexpr std::size_t MaxPendingSamples = VoiceSampleRate / 2;

        std::function<std::unique_ptr<IVoiceCapture>()>& captureFactory()
        {
            static std::function<std::unique_ptr<IVoiceCapture>()> value;
            return value;
        }
        std::function<std::unique_ptr<IVoicePlayback>()>& playbackFactory()
        {
            static std::function<std::unique_ptr<IVoicePlayback>()> value;
            return value;
        }

        std::int16_t clamp16(float value)
        {
            return static_cast<std::int16_t>(std::clamp(value, -32768.0f, 32767.0f));
        }

        // Any capture rate to 16 kHz: a one-pole low-pass when decimating, then linear interpolation.
        class Resampler
        {
        public:
            void setRate(std::uint32_t rate)
            {
                rate_ = rate;
                next_ = 1.0;
                previous_ = filtered_ = 0.0f;
                alpha_ = rate > static_cast<std::uint32_t>(VoiceSampleRate)
                    ? static_cast<float>(1.0 - std::exp(-2.0 * 3.141592653589793 * 7000.0 / rate)) : 1.0f;
            }
            void push(float sample, std::vector<std::int16_t>& out)
            {
                filtered_ += alpha_ * (sample - filtered_);
                const double step = static_cast<double>(rate_) / VoiceSampleRate;
                // next_ is where the next output falls, with the previous input at 0 and this one at 1.
                while (next_ <= 1.0)
                {
                    out.push_back(clamp16(previous_ + static_cast<float>(next_) * (filtered_ - previous_)));
                    next_ += step;
                }
                next_ -= 1.0;
                previous_ = filtered_;
            }

        private:
            std::uint32_t rate_ = VoiceSampleRate;
            double next_ = 1.0;
            float previous_ = 0.0f, filtered_ = 0.0f, alpha_ = 1.0f;
        };

        // The machine's default recording device, opened as a session of its own so a game using
        // XNA's Microphone reads everything it would have read without voice.
        class MicrophoneCapture final : public IVoiceCapture
        {
        public:
            ~MicrophoneCapture() override { setOpen(false); }
            bool present() override
            {
                const auto now = Clock::now();
                if (now >= nextCheck_)
                {
                    nextCheck_ = now + std::chrono::seconds(2);
                    auto* provider = CNA::Internal::Audio::RecordingProvider();
                    try
                    {
                        present_ = provider != nullptr && !provider->GetDevices().empty();
                    }
                    catch (...)
                    {
                        present_ = false;
                    }
                }
                return present_;
            }
            void setOpen(bool open) override
            {
                if (!open)
                {
                    if (device_) device_->Close();
                    device_.reset();
                    return;
                }
                if (device_ || Clock::now() < retry_) return;
                retry_ = Clock::now() + std::chrono::seconds(5);
                auto* provider = CNA::Internal::Audio::RecordingProvider();
                if (provider == nullptr) return;
                try
                {
                    const auto devices = provider->GetDevices();
                    if (devices.empty()) return;
                    device_ = provider->CreateDevice(devices.front().id);
                    if (!device_) return;
                    format_ = device_->Open({static_cast<std::uint32_t>(VoiceSampleRate), 1,
                                             CNA::Audio::Platform::AudioSampleFormat::Signed16});
                    resampler_.setRate(format_.sampleRate);
                }
                catch (...)
                {
                    device_.reset();
                }
            }
            void read(std::vector<std::int16_t>& out) override
            {
                using CNA::Audio::Platform::AudioRecordingIoStatus;
                using CNA::Audio::Platform::AudioSampleFormat;
                if (!device_) return;
                const std::size_t sampleBytes = format_.sampleFormat == AudioSampleFormat::Float32 ? 4 : 2;
                const std::size_t frameBytes = sampleBytes * std::max<std::size_t>(1, format_.channels);
                for (int round = 0; round < 8; ++round)
                {
                    buffer_.resize(4096 - 4096 % frameBytes);
                    const auto result = device_->Read(std::span<std::byte>(buffer_.data(), buffer_.size()));
                    if (result.status == AudioRecordingIoStatus::DeviceLost || result.status == AudioRecordingIoStatus::Error)
                    {
                        device_.reset();
                        return;
                    }
                    if (result.status != AudioRecordingIoStatus::Success) return;
                    for (std::size_t offset = 0; offset + frameBytes <= result.byteCount; offset += frameBytes)
                    {
                        float sum = 0.0f;
                        for (std::size_t channel = 0; channel < format_.channels; ++channel)
                        {
                            const auto* at = buffer_.data() + offset + channel * sampleBytes;
                            if (sampleBytes == 4)
                            {
                                float value;
                                std::memcpy(&value, at, 4);
                                sum += value * 32767.0f;
                            }
                            else
                            {
                                std::int16_t value;
                                std::memcpy(&value, at, 2);
                                sum += value;
                            }
                        }
                        resampler_.push(sum / std::max<std::size_t>(1, format_.channels), out);
                    }
                }
            }

        private:
            std::unique_ptr<CNA::Audio::Platform::IAudioRecordingDevice> device_;
            CNA::Audio::Platform::AudioFormat format_;
            Resampler resampler_;
            std::vector<std::byte> buffer_;
            Clock::time_point nextCheck_{}, retry_{};
            bool present_ = false;
        };

        // A DynamicSoundEffectInstance per talker; without audio output voice is silently absent.
        class SoundPlayback final : public IVoicePlayback
        {
        public:
            void play(std::uint8_t talker, std::span<const std::int16_t> pcm) override
            {
                if (failed_) return;
                try
                {
                    auto& stream = streams_[talker];
                    if (!stream)
                        stream = std::make_unique<Xna::Audio::DynamicSoundEffectInstance>(VoiceSampleRate, Xna::Audio::AudioChannels::Mono);
                    // More than 120 ms queued: the network delivered a burst; skip rather than lag.
                    if (stream->getPendingBufferCountProperty() > 6) return;
                    std::vector<SharpRuntime::bytecs> bytes(pcm.size() * 2);
                    std::memcpy(bytes.data(), pcm.data(), bytes.size());
                    stream->SubmitBuffer(bytes);
                    if (stream->getStateProperty() != Xna::Audio::SoundState::Playing) stream->Play();
                }
                catch (...)
                {
                    failed_ = true;
                    streams_.clear();
                }
            }
            void release(std::uint8_t talker) override { streams_.erase(talker); }

        private:
            std::map<std::uint8_t, std::unique_ptr<Xna::Audio::DynamicSoundEffectInstance>> streams_;
            bool failed_ = false;
        };

        // Speech or not, from the frame's level against a floor that follows the room's noise.
        class SpeechDetector
        {
        public:
            bool frame(const std::int16_t* pcm)
            {
                double sum = 0.0;
                for (int index = 0; index < VoiceFrameSamples; ++index) sum += static_cast<double>(pcm[index]) * pcm[index];
                const float level = static_cast<float>(std::sqrt(sum / VoiceFrameSamples));
                noise_ = level < noise_ ? noise_ * 0.9f + level * 0.1f : noise_ * 0.999f + level * 0.001f;
                noise_ = std::max(noise_, 30.0f);
                if (level > std::max(noise_ * 3.0f, 400.0f)) hangover_ = HangoverFrames;
                else if (hangover_ > 0) --hangover_;
                return hangover_ > 0;
            }
            void reset()
            {
                noise_ = 300.0f;
                hangover_ = 0;
            }

        private:
            float noise_ = 300.0f;
            int hangover_ = 0;
        };

#ifdef CNA_VOICE_OPUS
        class Encoder
        {
        public:
            ~Encoder()
            {
                if (encoder_) opus_encoder_destroy(encoder_);
            }
            std::vector<SharpRuntime::bytecs> encode(const std::int16_t* pcm)
            {
                if (!encoder_)
                {
                    int error = OPUS_OK;
                    encoder_ = opus_encoder_create(VoiceSampleRate, 1, OPUS_APPLICATION_VOIP, &error);
                    if (error != OPUS_OK) encoder_ = nullptr;
                    if (!encoder_) return {};
                    opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(16000));
                    opus_encoder_ctl(encoder_, OPUS_SET_COMPLEXITY(5));
                    opus_encoder_ctl(encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
                }
                std::vector<SharpRuntime::bytecs> out(MaxVoicePayloadBytes);
                const int size = opus_encode(encoder_, pcm, VoiceFrameSamples, out.data(), static_cast<opus_int32>(out.size()));
                if (size <= 0) return {};
                out.resize(static_cast<std::size_t>(size));
                return out;
            }

        private:
            OpusEncoder* encoder_ = nullptr;
        };

        class Decoder
        {
        public:
            Decoder()
            {
                int error = OPUS_OK;
                decoder_ = opus_decoder_create(VoiceSampleRate, 1, &error);
                if (error != OPUS_OK) decoder_ = nullptr;
            }
            ~Decoder()
            {
                if (decoder_) opus_decoder_destroy(decoder_);
            }
            Decoder(const Decoder&) = delete;
            Decoder& operator=(const Decoder&) = delete;
            // A frame; null data conceals one that was lost.
            bool decode(const std::vector<SharpRuntime::bytecs>* data, std::int16_t* pcm)
            {
                if (!decoder_) return false;
                const int samples = data
                    ? opus_decode(decoder_, data->data(), static_cast<opus_int32>(data->size()), pcm, VoiceFrameSamples, 0)
                    : opus_decode(decoder_, nullptr, 0, pcm, VoiceFrameSamples, 0);
                return samples == VoiceFrameSamples;
            }

        private:
            OpusDecoder* decoder_ = nullptr;
        };
#else
        class Encoder
        {
        public:
            std::vector<SharpRuntime::bytecs> encode(const std::int16_t*) { return {}; }
        };
        class Decoder
        {
        public:
            bool decode(const std::vector<SharpRuntime::bytecs>*, std::int16_t*) { return false; }
        };
#endif

        Xna::GamerServices::SignedInGamer* signedIn(VoiceChat::Gamer* gamer)
        {
            auto* local = dynamic_cast<Xna::Net::LocalNetworkGamer*>(gamer);
            return local != nullptr ? local->getSignedInGamerProperty() : nullptr;
        }
    }

    void setVoiceDevicesForTesting(std::function<std::unique_ptr<IVoiceCapture>()> capture,
                                   std::function<std::unique_ptr<IVoicePlayback>()> playback)
    {
        captureFactory() = std::move(capture);
        playbackFactory() = std::move(playback);
    }

    namespace
    {
        // What friends see as FriendGamer.HasVoice for this machine: voice built and on, and a
        // recording device. The device list is read at most every two seconds.
        bool captureDevicePresent()
        {
            static std::mutex mutex;
            static Clock::time_point next{};
            static bool present = false;
            std::lock_guard lock(mutex);
            if (Clock::now() >= next)
            {
                next = Clock::now() + std::chrono::seconds(2);
                auto* provider = CNA::Internal::Audio::RecordingProvider();
                try { present = provider != nullptr && !provider->GetDevices().empty(); }
                catch (...) { present = false; }
            }
            return present;
        }
        const bool capabilityInstalled = [] {
            (void)GamerServices::setLocalVoiceCapabilityProvider([] { return voiceAvailable() && captureDevicePresent(); });
            return true;
        }();
    }

    bool voiceAvailable()
    {
#ifdef CNA_VOICE_OPUS
        static const bool wanted = [] {
            const char* value = std::getenv("CNA_VOICE");
            return !(value != nullptr && std::string(value) == "0");
        }();
        return wanted;
#else
        return false;
#endif
    }

    struct VoiceChat::Impl
    {
        struct Remote
        {
            bool known = false;
            Clock::time_point heard{}, spoke{};
            int lastSequence = -1;
            std::unique_ptr<Decoder> decoder;
        };

        Hooks hooks;
        std::unique_ptr<IVoiceCapture> capture;
        std::unique_ptr<IVoicePlayback> playback;
        Encoder encoder;
        SpeechDetector detector;
        std::vector<std::int16_t> pending;
        std::uint16_t sequence = 0;
        Clock::time_point nextKeepalive{}, nextPermissions{};
        std::map<Gamer*, Remote> remotes;
        std::set<std::pair<Gamer*, Gamer*>> disabled;
        std::map<Gamer*, bool> communicates;
        Gamer* owner = nullptr;
        bool present = false, open = false, talking = false;

        // The gamer who owns this machine's microphone: Player One's, else the first local gamer.
        static Gamer* ownerOf(const std::vector<Gamer*>& locals)
        {
            for (auto* gamer : locals)
                if (auto* profile = signedIn(gamer); profile && profile->getPlayerIndexProperty() == Xna::PlayerIndex::One)
                    return gamer;
            return locals.empty() ? nullptr : locals.front();
        }
        static bool muted(const std::vector<Gamer*>& locals, Gamer* remote)
        {
            for (auto* local : locals)
                if (auto* profile = signedIn(local);
                    profile && GamerServices::voiceMuted(profile->getGamertagProperty(), remote->getGamertagProperty()))
                    return true;
            return false;
        }
        // GamerPrivileges.AllowCommunication of the microphone's owner.
        bool allowed(Gamer* remote) const
        {
            auto* profile = signedIn(owner);
            if (profile == nullptr) return false;
            switch (profile->getPrivilegesProperty().getAllowCommunicationProperty())
            {
            case Xna::GamerServices::GamerPrivilegeSetting::Everyone: return true;
            case Xna::GamerServices::GamerPrivilegeSetting::Blocked: return false;
            default:
                try
                {
                    return profile->IsFriend(remote);
                }
                catch (...)
                {
                    return false;
                }
            }
        }
        bool hears(const std::vector<Gamer*>& locals, Gamer* remote) const
        {
            const auto found = communicates.find(remote);
            return found != communicates.end() && found->second && !muted(locals, remote);
        }
        // One target per remote machine: its first gamer the filter accepts.
        template <typename Filter>
        std::vector<Gamer*> targets(const std::vector<Gamer*>& others, Filter accept) const
        {
            std::vector<Gamer*> chosen;
            std::set<const Xna::Net::NetworkMachine*> machines;
            for (auto* remote : others)
                if (accept(remote) && machines.insert(&remote->getMachineProperty()).second) chosen.push_back(remote);
            return chosen;
        }
    };

    VoiceChat::VoiceChat(Hooks hooks) : impl_(std::make_unique<Impl>())
    {
        impl_->hooks = std::move(hooks);
        impl_->capture = captureFactory() ? captureFactory()() : std::make_unique<MicrophoneCapture>();
        impl_->playback = playbackFactory() ? playbackFactory()() : std::make_unique<SoundPlayback>();
    }

    VoiceChat::~VoiceChat()
    {
        if (impl_->capture && impl_->open) impl_->capture->setOpen(false);
    }

    void VoiceChat::update(Clock::time_point now)
    {
        auto& self = *impl_;
        std::vector<Gamer*> locals, others;
        for (auto* gamer : self.hooks.gamers())
        {
            if (gamer == nullptr || gamer->getHasLeftSessionProperty()) continue;
            (gamer->getIsLocalProperty() ? locals : others).push_back(gamer);
        }
        self.owner = Impl::ownerOf(locals);
        self.present = self.owner != nullptr && self.capture && self.capture->present();

        if (now >= self.nextPermissions || std::any_of(others.begin(), others.end(), [&](Gamer* remote) { return !self.communicates.contains(remote); }))
        {
            self.nextPermissions = now + PermissionInterval;
            self.communicates.clear();
            for (auto* remote : others) self.communicates[remote] = self.allowed(remote);
        }
        auto sendsTo = [&](Gamer* remote) { return self.hears(locals, remote) && !self.disabled.contains({self.owner, remote}); };

        // Capture only while someone could hear it.
        const bool audience = self.present && std::any_of(others.begin(), others.end(), sendsTo);
        if (audience != self.open)
        {
            self.capture->setOpen(audience);
            self.open = audience;
            self.pending.clear();
            self.detector.reset();
        }
        // Talking follows the detector across updates: at a high frame rate most updates complete
        // no 20 ms frame, and must not read as silence.
        if (!self.open) self.talking = false;
        if (self.open)
        {
            self.capture->read(self.pending);
            if (self.pending.size() > MaxPendingSamples)
                self.pending.erase(self.pending.begin(), self.pending.end() - static_cast<std::ptrdiff_t>(MaxPendingSamples));
            std::size_t offset = 0;
            for (; offset + VoiceFrameSamples <= self.pending.size(); offset += VoiceFrameSamples)
            {
                const auto* frame = self.pending.data() + offset;
                self.talking = self.detector.frame(frame);
                if (!self.talking) continue;
                VoiceDataMessage message;
                message.Flags = VoiceFlagTalking;
                message.Payload = self.encoder.encode(frame);
                if (message.Payload.empty()) continue;
                message.Sequence = self.sequence++;
                for (auto* target : self.targets(others, sendsTo)) self.hooks.send(self.owner, target, message);
            }
            self.pending.erase(self.pending.begin(), self.pending.begin() + static_cast<std::ptrdiff_t>(offset));
        }
        // Once a second each remote machine hears that this one has voice, talking or not.
        if (self.present && now >= self.nextKeepalive)
        {
            self.nextKeepalive = now + KeepaliveInterval;
            VoiceDataMessage keepalive;
            keepalive.Sequence = self.sequence;
            for (auto* target : self.targets(others, [](Gamer*) { return true; })) self.hooks.send(self.owner, target, keepalive);
        }

        for (auto* gamer : locals)
            self.hooks.apply(*gamer, gamer == self.owner && self.present, gamer == self.owner && self.talking, false);
        for (auto* gamer : others)
        {
            const auto found = self.remotes.find(gamer);
            const bool hasVoice = found != self.remotes.end() && found->second.known && now - found->second.heard < VoiceTimeout;
            const bool talking = hasVoice && now - found->second.spoke < TalkingTimeout;
            self.hooks.apply(*gamer, hasVoice, talking, Impl::muted(locals, gamer));
        }
    }

    void VoiceChat::receive(Gamer* sender, const VoiceDataMessage& frame, Clock::time_point now)
    {
        auto& self = *impl_;
        if (sender == nullptr || sender->getIsLocalProperty()) return;
        auto& remote = self.remotes[sender];
        remote.known = true;
        remote.heard = now;
        if ((frame.Flags & VoiceFlagTalking) == 0) return;
        remote.spoke = now;
        std::vector<Gamer*> locals;
        for (auto* gamer : self.hooks.gamers())
            if (gamer != nullptr && gamer->getIsLocalProperty()) locals.push_back(gamer);
        if (!self.hears(locals, sender) || !self.playback) return;
        // Frames arrive unreliably and out of order: late ones are dropped, a short loss concealed.
        const int gap = remote.lastSequence < 0 ? 1 : static_cast<std::int16_t>(frame.Sequence - static_cast<std::uint16_t>(remote.lastSequence));
        if (gap <= 0) return;
        if (!remote.decoder) remote.decoder = std::make_unique<Decoder>();
        std::int16_t pcm[VoiceFrameSamples];
        const auto talker = static_cast<std::uint8_t>(sender->getIdProperty());
        for (int lost = 1; lost < gap && lost <= MaxConcealedFrames; ++lost)
            if (remote.decoder->decode(nullptr, pcm)) self.playback->play(talker, pcm);
        remote.lastSequence = frame.Sequence;
        if (remote.decoder->decode(&frame.Payload, pcm)) self.playback->play(talker, pcm);
    }

    void VoiceChat::enableSend(Gamer* local, Gamer* remote, bool enable)
    {
        if (enable) impl_->disabled.erase({local, remote});
        else impl_->disabled.insert({local, remote});
    }

    void VoiceChat::forget(Gamer* gamer)
    {
        auto& self = *impl_;
        if (self.remotes.erase(gamer) && self.playback) self.playback->release(static_cast<std::uint8_t>(gamer->getIdProperty()));
        self.communicates.erase(gamer);
        std::erase_if(self.disabled, [&](const auto& pair) { return pair.first == gamer || pair.second == gamer; });
        if (self.owner == gamer) self.owner = nullptr;
    }
}
