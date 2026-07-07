// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include <utility>

namespace Microsoft::Xna::Framework::Net
{
    NetworkGamer::NetworkGamer(NetworkSession* session, const std::string& gamertag)
        : Gamer(gamertag, gamertag)
        , machine_(NetworkMachine::CreateInternal())
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
    void NetworkGamer::setIsReadyProperty(bool value)         { isReady_ = value; }
    bool NetworkGamer::getIsTalkingProperty() const           { return isTalking_; }

    const NetworkMachine& NetworkGamer::getMachineProperty() const { return machine_; }
    void NetworkGamer::setMachineProperty(NetworkMachine value)    { machine_ = std::move(value); }

    System::TimeSpan NetworkGamer::getRoundtripTimeProperty() const { return roundtripTime_; }
    void NetworkGamer::SetRoundtripTime(System::TimeSpan value) { roundtripTime_ = value; }
    NetworkSession* NetworkGamer::getSessionProperty() const        { return session_; }
}
