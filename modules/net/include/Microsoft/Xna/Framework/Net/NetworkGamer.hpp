// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkMachine.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/TimeSpan.hpp"
#include <string>

namespace Microsoft::Xna::Framework::Net
{
    class NetworkSession;

    /**
     * @brief Represents a gamer participating in a network session.
     */
    class NetworkGamer : public GamerServices::Gamer
    {
    public:
        /**
         * @brief Gets whether this gamer has left the session.
         *
         * @return true if the gamer has left the session.
         */
        [[nodiscard]] bool getHasLeftSessionProperty() const;

        /**
         * @brief Marks whether this gamer has left the session.
         *
         * FNA's setter for this is `private` (`{ get; private set; }`), not `internal` -
         * FNA's own NetworkSession never actually calls it after construction, so real XNA's
         * HasLeftSession is permanently false in practice (an unimplemented FNA stub, like several
         * other NetworkSession-adjacent members). Restored here, as a NOXNA extension, so this
         * port's NetworkSession::RemoveGamer() (a sibling class, not a subclass, so it couldn't
         * reach a real `private` setter either way) can make this property actually functional.
         *
         * @param value The new value.
         */
        NOXNA void SetHasLeftSession(bool value);

        /**
         * @brief Gets whether this gamer has a voice/headset available.
         *
         * @return true if voice is available.
         */
        [[nodiscard]] bool getHasVoiceProperty() const;

        /**
         * @brief Gets the gamer's session-local identifier.
         *
         * @return The gamer identifier.
         */
        [[nodiscard]] SharpRuntime::bytecs getIdProperty() const;

        /**
         * @brief Sets the gamer's session-local identifier.
         *
         * FNA hardcodes this property's getter to 0 for every gamer (a real, confirmed upstream
         * limitation - see DEFERRED.md item #20 in the sibling cna-samples repo). Restored here,
         * NOXNA, so NetworkSession and the ENet backend can assign a real, cross-machine-consistent
         * id: NetworkSession assigns a local placeholder at construction, and ENetBackend overwrites
         * it with the wire-negotiated id once a SystemLink session actually joins/hosts.
         *
         * @param value The new identifier.
         */
        NOXNA void SetId(SharpRuntime::bytecs value);

        /**
         * @brief Gets whether this gamer is a guest.
         *
         * @return true if the gamer is a guest.
         */
        [[nodiscard]] bool getIsGuestProperty() const;

        /**
         * @brief Gets whether this gamer is the session host.
         *
         * @return true if the gamer is the session host.
         */
        [[nodiscard]] bool getIsHostProperty() const;

        /**
         * @brief Sets whether this gamer is the session host.
         *
         * FNA hardcodes this property's getter to true for every gamer (see DEFERRED.md item #20
         * in the sibling cna-samples repo - this made NetworkSession.IsHost always true on every
         * machine). Restored here, NOXNA, so NetworkSession can set a local gamer's real host
         * status at construction (true after Create(), false after Join()/JoinInvited()).
         *
         * Task 4.6: a *remote* gamer representing the actual host machine also reports this
         * correctly now - RosterEntry carries a real host flag (set from the host's own accurate
         * view of each gamer in ENetBackend.cpp's SnapshotRoster), propagated by
         * HandleServerWelcome/HandleGamerJoinBroadcast when a client learns about that gamer.
         *
         * @param value The new host state.
         */
        NOXNA void SetIsHost(bool value);

        /**
         * @brief Gets whether this gamer is a local gamer.
         *
         * FNA implements this as a `this is LocalNetworkGamer` runtime type check.
         * Ported as a virtual method overridden by LocalNetworkGamer instead, since C++
         * dynamic_cast to a not-yet-defined derived type isn't usable from the base
         * class's own header; the externally observable behavior is identical.
         *
         * @return true if this gamer is a LocalNetworkGamer.
         */
        [[nodiscard]] virtual bool getIsLocalProperty() const;

        /**
         * @brief Gets whether this gamer is muted by the local user.
         *
         * @return true if muted by the local user.
         */
        [[nodiscard]] bool getIsMutedByLocalUserProperty() const;

        /**
         * @brief Gets whether this gamer occupies a private slot.
         *
         * @return true if the gamer occupies a private slot.
         */
        [[nodiscard]] bool getIsPrivateSlotProperty() const;

        /**
         * @brief Gets whether this gamer is ready.
         *
         * @return true if the gamer is ready.
         */
        [[nodiscard]] bool getIsReadyProperty() const;

        /**
         * @brief Sets whether this gamer is ready.
         *
         * @param value The new ready state.
         */
        void setIsReadyProperty(bool value);

        /**
         * @brief Gets whether this gamer is currently talking.
         *
         * @return true if the gamer is talking.
         */
        [[nodiscard]] bool getIsTalkingProperty() const;

        /**
         * @brief Gets the network machine hosting this gamer.
         *
         * @return Const reference to the NetworkMachine.
         */
        [[nodiscard]] const NetworkMachine& getMachineProperty() const;

        /**
         * @brief Sets the network machine hosting this gamer.
         *
         * @param value The new NetworkMachine.
         */
        void setMachineProperty(NetworkMachine value);

        /**
         * @brief Gets the measured round-trip time to this gamer.
         *
         * @return The round-trip time.
         */
        [[nodiscard]] System::TimeSpan getRoundtripTimeProperty() const;

        /**
         * @brief Task 4.1: sets the measured round-trip time, wired up from the underlying
         * `ENetPeer`'s own native RTT tracking for gamers with a direct connection. Not part of
         * real XNA's public API (`RoundtripTime` has no setter there); mirrors `SetId`/`SetIsHost`'s
         * existing internal-wiring pattern.
         *
         * @param value The newly-measured round-trip time.
         */
        NOXNA void SetRoundtripTime(System::TimeSpan value);

        /**
         * @brief Gets the network session this gamer belongs to.
         *
         * @return Pointer to the NetworkSession.
         */
        [[nodiscard]] NetworkSession* getSessionProperty() const;

        /**
         * @brief Creates a NetworkGamer for CNA internal use.
         *
         * @param session The owning NetworkSession.
         * @param gamertag The gamer's gamertag. Defaults to "Stub Gamer", matching FNA's stub
         *                 behavior for gamers with no known real identity; ENetBackend passes a
         *                 real gamertag received over the wire when constructing remote gamers.
         */
        NOXNA static NetworkGamer CreateInternal(NetworkSession* session, const std::string& gamertag = "Stub Gamer");

    protected:
        /**
         * @brief Constructs a NetworkGamer bound to the given session.
         *
         * @param session The owning NetworkSession.
         * @param gamertag The gamer's gamertag. Defaults to "Stub Gamer", matching FNA's stub.
         */
        explicit NetworkGamer(NetworkSession* session, const std::string& gamertag = "Stub Gamer");

    private:
        bool hasLeftSession_{false};
        bool hasVoice_{false};
        SharpRuntime::bytecs id_{0};
        bool isGuest_{false};
        bool isHost_{false};
        bool isMutedByLocalUser_{false};
        bool isPrivateSlot_{false};
        bool isReady_{false};
        bool isTalking_{false};
        NetworkMachine machine_;
        System::TimeSpan roundtripTime_;
        NetworkSession* session_;
    };
}
