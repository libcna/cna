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
         * FNA's setter for this is `internal`; restored here for NetworkSession's
         * RemoveGamer() (a sibling class, not a subclass) to update it.
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
         * Always 0, matching FNA's stub.
         *
         * @return The gamer identifier.
         */
        [[nodiscard]] SharpRuntime::bytecs getIdProperty() const;

        /**
         * @brief Gets whether this gamer is a guest.
         *
         * @return true if the gamer is a guest.
         */
        [[nodiscard]] bool getIsGuestProperty() const;

        /**
         * @brief Gets whether this gamer is the session host.
         *
         * Always true, matching FNA's stub.
         *
         * @return true if the gamer is the session host.
         */
        [[nodiscard]] bool getIsHostProperty() const;

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
        bool isGuest_{false};
        bool isMutedByLocalUser_{false};
        bool isPrivateSlot_{false};
        bool isReady_{false};
        bool isTalking_{false};
        NetworkMachine machine_;
        System::TimeSpan roundtripTime_;
        NetworkSession* session_;
    };
}
