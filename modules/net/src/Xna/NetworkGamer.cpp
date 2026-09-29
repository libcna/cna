// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionState.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ArgumentNullException.hpp"
#include <utility>

namespace Microsoft::Xna::Framework::Net
{
    GetTypeNameCPP(NetworkGamer, "Microsoft.Xna.Framework.Net.NetworkGamer")

    NetworkGamer::NetworkGamer(NetworkSession* session, const std::string& gamertag)
        : Gamer(gamertag, gamertag)
        , machine_(std::make_shared<NetworkMachine>(NetworkMachine::CreateInternal()))
        , session_(session)
    {
    }

    NetworkGamer NetworkGamer::CreateInternal(NetworkSession* session, const std::string& gamertag)
    {
        return NetworkGamer(session, gamertag);
    }

    bool NetworkGamer::getHasLeftSessionProperty() const     { return hasLeftSession_; }
    void NetworkGamer::SetHasLeftSession(bool value)         { hasLeftSession_ = value; }
    bool NetworkGamer::getHasVoiceProperty() const            { return hasVoice_; }
    SharpRuntime::bytecs NetworkGamer::getIdProperty() const  { return id_; }
    void NetworkGamer::SetId(SharpRuntime::bytecs value)      { id_ = value; }
    bool NetworkGamer::getIsGuestProperty() const             { return isGuest_; }
    bool NetworkGamer::getIsHostProperty() const              { return isHost_; }
    void NetworkGamer::SetIsHost(bool value)                  { isHost_ = value; }
    bool NetworkGamer::getIsLocalProperty() const             { return false; }
    bool NetworkGamer::getIsMutedByLocalUserProperty() const  { return isMutedByLocalUser_; }
    bool NetworkGamer::getIsPrivateSlotProperty() const       { return isPrivateSlot_; }
    bool NetworkGamer::getIsReadyProperty() const             { return isReady_; }
    void NetworkGamer::setIsReadyProperty(bool value)
    {
        // Reference NetworkGamer.IsReady setter: a gamer that left, a remote gamer, or any state
        // but Lobby is refused; an unchanged value sends nothing.
        if (hasLeftSession_) throw System::InvalidOperationException("This NetworkGamer is no longer valid. The gamer may have left the session.");
        if (!getIsLocalProperty()) throw System::InvalidOperationException("This operation is only valid on local gamers.");
        if (session_ == nullptr || session_->getSessionStateProperty() != NetworkSessionState::Lobby)
            throw System::InvalidOperationException("This operation is only valid when the session state is Lobby.");
        if (value == isReady_) return;
        isReady_ = value;
        session_->PublishGamerReady({this});
    }
    void NetworkGamer::SetIsReadyInternal(bool value)         { isReady_ = value; }
    bool NetworkGamer::getIsTalkingProperty() const           { return isTalking_; }

    const NetworkMachine& NetworkGamer::getMachineProperty() const { return *machine_; }
    void NetworkGamer::setMachineProperty(NetworkMachine value)    { machine_ = std::make_shared<NetworkMachine>(std::move(value)); }
    void NetworkGamer::SetSharedMachine(std::shared_ptr<NetworkMachine> machine)
    {
        if (!machine) throw System::ArgumentNullException("machine");
        machine_ = std::move(machine);
    }
    std::shared_ptr<NetworkMachine> NetworkGamer::GetSharedMachine() const { return machine_; }
    void NetworkGamer::SetIsPrivateSlot(bool value) { isPrivateSlot_ = value; }

    System::TimeSpan NetworkGamer::getRoundtripTimeProperty() const { return roundtripTime_; }
    void NetworkGamer::SetRoundtripTime(System::TimeSpan value) { roundtripTime_ = value; }
    NetworkSession* NetworkGamer::getSessionProperty() const        { return session_; }
}
