// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/Net/NetPacketCodec.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <vector>

namespace Microsoft::Xna::Framework::Net { class NetworkGamer; }

namespace CNA::Internal::Net
{
    /** @brief Sample rate of captured, sent and played voice (mono, 16-bit). */
    inline constexpr int VoiceSampleRate = 16000;
    /** @brief Samples in one 20 ms voice frame. */
    inline constexpr int VoiceFrameSamples = 320;

    /** @brief Where voice comes from: 16 kHz mono samples, pulled once a frame. */
    class IVoiceCapture
    {
    public:
        virtual ~IVoiceCapture() = default;
        /** @brief Whether a microphone exists (a gamer's HasVoice), without opening it.
         * @return Present. */
        [[nodiscard]] virtual bool present() = 0;
        /** @brief Opens or closes capture; capture runs only while someone could hear it.
         * @param open Wanted state. */
        virtual void setOpen(bool open) = 0;
        /** @brief Appends what was captured since the last call. @param out 16 kHz mono samples. */
        virtual void read(std::vector<std::int16_t>& out) = 0;
    };

    /** @brief Where received voice is played: one stream per talker. */
    class IVoicePlayback
    {
    public:
        virtual ~IVoicePlayback() = default;
        /** @brief Queues decoded speech. @param talker Session gamer ID. @param pcm 16 kHz mono. */
        virtual void play(std::uint8_t talker, std::span<const std::int16_t> pcm) = 0;
        /** @brief Releases a talker's stream. @param talker Session gamer ID. */
        virtual void release(std::uint8_t talker) = 0;
    };

    /** @brief Replaces the devices voice uses; empty factories restore the microphone and the
     * SoundEffect path. Deterministic tests only. @param capture Capture factory.
     * @param playback Playback factory. */
    void setVoiceDevicesForTesting(std::function<std::unique_ptr<IVoiceCapture>()> capture,
                                   std::function<std::unique_ptr<IVoicePlayback>()> playback);

    /** @brief Whether this build carries network voice: an Opus codec was found at configure time
     * and CNA_VOICE is not 0. @return Available. */
    bool voiceAvailable();

    /**
     * @brief Network voice for one session: captures the microphone of the local gamer that owns
     * it, detects speech, encodes it with Opus, routes each frame to every remote machine allowed
     * to hear it, and plays what arrives; keeps each gamer's HasVoice, IsTalking and
     * IsMutedByLocalUser.
     *
     * Transport-neutral: the session supplies how to enumerate gamers, send a frame and set flags.
     */
    class VoiceChat
    {
    public:
        /** @brief A session gamer. */
        using Gamer = Microsoft::Xna::Framework::Net::NetworkGamer;
        /** @brief What the session supplies. */
        struct Hooks
        {
            /** @brief The session's gamers. */
            std::function<std::vector<Gamer*>()> gamers;
            /** @brief Sends a frame from a local gamer to a remote gamer. */
            std::function<void(Gamer* sender, Gamer* target, const VoiceDataMessage& frame)> send;
            /** @brief Sets a gamer's HasVoice, IsTalking and IsMutedByLocalUser. */
            std::function<void(Gamer& gamer, bool hasVoice, bool talking, bool muted)> apply;
        };

        /** @brief Starts voice for a session. @param hooks Session callbacks. */
        explicit VoiceChat(Hooks hooks);
        /** @brief Closes capture and releases every stream. */
        ~VoiceChat();
        VoiceChat(const VoiceChat&) = delete;
        VoiceChat& operator=(const VoiceChat&) = delete;

        /** @brief Captures and sends, expires what went quiet, and sets every gamer's flags; once a
         * frame, at the session's Update. @param now Current time. */
        void update(std::chrono::steady_clock::time_point now);
        /** @brief Takes a frame the transport received for a local gamer. @param sender Remote gamer.
         * @param frame The frame. @param now Arrival time. */
        void receive(Gamer* sender, const VoiceDataMessage& frame, std::chrono::steady_clock::time_point now);
        /** @brief LocalNetworkGamer.EnableSendVoice: whether a local gamer's voice goes to a remote
         * gamer (all are enabled at first). @param local Local gamer. @param remote Remote gamer.
         * @param enable Wanted. */
        void enableSend(Gamer* local, Gamer* remote, bool enable);
        /** @brief Forgets a gamer that left. @param gamer Gamer. */
        void forget(Gamer* gamer);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
