// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/ServiceUpdateSubscription.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "CNA/Internal/GamerServices/LocalProfiles.hpp"
#include "CNA/Internal/GamerServices/VoiceMutes.hpp"
#include "System/InvalidOperationException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "../Internal/GuideOverlay.hpp"
#include <array>
#include <memory>
#include <vector>
#include <deque>

namespace Microsoft::Xna::Framework::GamerServices {
namespace {
std::array<SignedInGamer*,4> slots{};
// Retired identities remain alive while game/event/collection consumers may still reference them.
std::vector<std::unique_ptr<SignedInGamer>> ownedGamers;
bool updating=false;
std::deque<CNA::Internal::GamerServices::BackendEvent> deferred;
void publish() {
    auto* gamers = Gamer::getSignedInGamersProperty();
    gamers->Clear(); for (auto* gamer : slots) if (gamer) gamers->Add(gamer);
}
}
bool GamerServicesDispatcher::isInitialized_=false;
SharpRuntime::IntPtr GamerServicesDispatcher::windowHandle_=0;
std::size_t GamerServicesDispatcher::freedGamerCount_=0;
System::EventHandler<System::EventArgs> GamerServicesDispatcher::InstallingTitleUpdate;
bool GamerServicesDispatcher::getIsInitializedProperty(){return isInitialized_;}
SharpRuntime::IntPtr GamerServicesDispatcher::getWindowHandleProperty(){return windowHandle_;}
void GamerServicesDispatcher::setWindowHandleProperty(SharpRuntime::IntPtr value){windowHandle_=value;}
void GamerServicesDispatcher::Initialize(System::IServiceProvider& serviceProvider) {
    // Xbox-target documentation rejects duplicate initialization; no synthetic FNA profiles.
    if(isInitialized_)throw System::InvalidOperationException("Gamer services are already initialized.");
    auto service=CNA::Internal::GamerServices::backend();
    // Without a service, profiles configured to sign in automatically are already signed in when
    // the game starts; like XNA, they appear (and raise SignedIn) at the first Update.
    std::vector<CNA::Internal::GamerServices::LocalProfile> automatic;
    if(!service->serviceEnabled())automatic=CNA::Internal::GamerServices::autoSignInLocalProfiles();
    CNA::Internal::GamerServices::installGuideOverlay(serviceProvider);publish();
    for(std::size_t slot=0;slot<automatic.size();++slot)service->signInLocal(static_cast<int>(slot),automatic[slot].gamertag);
    isInitialized_=true;
}
void GamerServicesDispatcher::ApplyLocalGameDefaults(GameDefaults& target, const CNA::Internal::GamerServices::LocalGameDefaults& source) {
    target.gameDifficulty_=static_cast<GameDifficulty>(source.gameDifficulty);
    target.controllerSensitivity_=static_cast<ControllerSensitivity>(source.controllerSensitivity);
    const auto color=[](const auto& rgb)->std::optional<Color> {
        if(!rgb)return std::nullopt;
        return Color(static_cast<int>((*rgb)[0]),static_cast<int>((*rgb)[1]),static_cast<int>((*rgb)[2]));
    };
    target.primaryColor_=color(source.primaryColor);target.secondaryColor_=color(source.secondaryColor);
    target.autoAim_=source.autoAim;target.autoCenter_=source.autoCenter;target.moveWithRightThumbStick_=source.moveWithRightThumbStick;
    target.invertYAxis_=source.invertYAxis;target.manualTransmission_=source.manualTransmission;
    target.racingCameraAngle_=static_cast<RacingCameraAngle>(source.racingCameraAngle);
    target.accelerateWithButtons_=source.accelerateWithButtons;target.brakeWithButtons_=source.brakeWithButtons;
}
void GamerServicesDispatcher::Update() {
    if(!isInitialized_)return;
    // End inside a service callback must progress work without publishing nested identity events.
    if(updating) {
        auto events=CNA::Internal::GamerServices::backend()->pump();
        std::exception_ptr firstError;
        for(auto& event:events) {
            if(event.type==CNA::Internal::GamerServices::BackendEvent::Type::Completion) {
                try {if(event.completion)event.completion();}
                catch(...) {if(!firstError)firstError=std::current_exception();}
            } else deferred.push_back(std::move(event));
        }
        try {CNA::Internal::GamerServices::dispatchServiceUpdates();}
        catch(...) {if(!firstError)firstError=std::current_exception();}
        if(firstError)std::rethrow_exception(firstError);
        return;
    }
    struct Guard {Guard(){updating=true;}~Guard(){updating=false;}} guard;
    // XNA IL: every Update reads the Guide state, trial mode included, before sign-in changes.
    Guide::isTrialMode_=Guide::simulateTrialMode_;
    auto service=CNA::Internal::GamerServices::backend();
    for(auto* gamer:slots)if(gamer&&!gamer->serviceUserId_.empty()&&gamer->presence_.changed_&&!gamer->presence_.pending_) {
        auto& presence=gamer->presence_;const auto revision=presence.revision_;auto text=presence.presence_;
        if(const auto parameter=text.find("{0}");parameter!=std::string::npos)text.replace(parameter,3,std::to_string(presence.presenceValue_));
        const auto user=gamer->serviceUserId_;const auto mode=static_cast<int>(presence.presenceMode_);
        auto succeeded=std::make_shared<bool>(false);
        auto* executor=service.get();
        try {
            service->submit([executor,user,mode,text,succeeded]{try{executor->setPresence(user,mode,text);*succeeded=true;}catch(...){}},
                [gamer,revision,succeeded]{auto& state=gamer->presence_;state.pending_=false;if(*succeeded&&revision==state.revision_)state.changed_=false;});
            presence.pending_=true;
        }catch(const GamerServicesNotAvailableException&) {
            // Automatic presence remains dirty for retry; a full queue must still be drained.
        }
    }
    auto incoming=service->pump();
    std::vector<CNA::Internal::GamerServices::BackendEvent> events;
    while(!deferred.empty()){events.push_back(std::move(deferred.front()));deferred.pop_front();}
    for(auto& event:incoming)events.push_back(std::move(event));
    std::exception_ptr firstError;
    for(auto& event:events) {
        try {
            using Type=CNA::Internal::GamerServices::BackendEvent::Type;
            if(event.type==Type::Completion){if(event.completion)event.completion();continue;}
            if(event.slot<0||event.slot>3)continue;
            if(event.type==Type::Failed) { Guide::OnSignInResult(event.slot, false, event.error); continue; }
            if(event.type!=Type::SignedIn&&event.type!=Type::SignedOut)continue;
            if(auto* previous=slots[event.slot]) {
                // Reference HandlePlayerSignInChanged: the old gamer is disposed, then SignedOut is raised.
                slots[event.slot]=nullptr;previous->isDisposed_=true;publish();SignedInGamer::OnSignOut(previous);
                // Guests leave with the account they are guests of.
                if(!previous->isGuest_)for(int guest=0;guest<4;++guest)
                    if(auto* other=slots[guest];other&&other->isGuest_&&other->guestHost_==event.slot) {
                        slots[guest]=nullptr;other->isDisposed_=true;publish();SignedInGamer::OnSignOut(other);
                    }
            }
            if(event.type==Type::SignedIn) {
                const bool guest=event.guestOf>=0;
                if(guest&&(!slots[event.guestOf]||slots[event.guestOf]->isGuest_)){Guide::OnSignInResult(event.slot,false);continue;}
                auto gamer=std::unique_ptr<SignedInGamer>(new SignedInGamer(event.identity.gamertag,event.signedInToLive,guest,static_cast<PlayerIndex>(event.slot)));
                gamer->serviceUserId_=event.identity.userId;gamer->displayName_=event.identity.displayName;gamer->guestHost_=event.guestOf;
                // A local profile has no service: no online sessions and no purchases. A guest has no
                // account the service could admit to an online session.
                gamer->privileges_.allowOnlineSessions_=event.signedInToLive&&!guest&&event.identity.allowOnlineSessions;
                if(!event.signedInToLive)gamer->privileges_.allowPurchaseContent_=false;
                // An account's other privileges are the policy its service states (the operator's
                // stand-in for parental controls); its block list mutes voice with those players.
                if(event.signedInToLive&&!guest) {
                    auto setting=[](const std::string& value) {
                        return value=="blocked"?GamerPrivilegeSetting::Blocked:value=="friends"?GamerPrivilegeSetting::FriendsOnly:GamerPrivilegeSetting::Everyone;
                    };
                    auto& privileges=gamer->privileges_;
                    privileges.allowCommunication_=setting(event.identity.communication);
                    privileges.allowProfileViewing_=setting(event.identity.profileViewing);
                    privileges.allowUserCreatedContent_=setting(event.identity.userContent);
                    privileges.allowTradeContent_=event.identity.tradeContent;
                    privileges.allowPurchaseContent_=event.identity.purchaseContent;
                    privileges.allowPremiumContent_=event.identity.premiumContent;
                    CNA::Internal::GamerServices::setBlockedPlayers(event.identity.gamertag,event.identity.blocked);
                }
                // A local profile carries its own preferred game settings; an account's come from the service.
                if(!event.signedInToLive) {
                    if(const auto profile=CNA::Internal::GamerServices::findLocalProfile(event.identity.gamertag))
                        ApplyLocalGameDefaults(gamer->gameDefaults_,profile->gameDefaults);
                } else if(!event.identity.gameDefaults.empty())
                    ApplyLocalGameDefaults(gamer->gameDefaults_,CNA::Internal::GamerServices::parseGameDefaultsJson(event.identity.gameDefaults));
                auto* pointer=gamer.get();ownedGamers.push_back(std::move(gamer));slots[event.slot]=pointer;publish();SignedInGamer::OnSignIn(pointer);Guide::OnSignInResult(event.slot, true);
            }
        }catch(...){if(!firstError)firstError=std::current_exception();}
    }
    try {CNA::Internal::GamerServices::dispatchServiceUpdates();}
    catch(...) {if(!firstError)firstError=std::current_exception();}
    // Invitation prompts and InviteAccepted belong to the outer update, never a nested End pump.
    try {CNA::Internal::GamerServices::pumpInvitations();}
    catch(...) {if(!firstError)firstError=std::current_exception();}
    try {CNA::Internal::GamerServices::pumpParties();}
    catch(...) {if(!firstError)firstError=std::current_exception();}
    try {CNA::Internal::GamerServices::pumpSocial();}
    catch(...) {if(!firstError)firstError=std::current_exception();}
    // A Guide closed from code, not by a player, hands input back here.
    CNA::Internal::GamerServices::syncSystemInputOwnership();
    // The Guide button belongs to games that draw the Guide.
    if(CNA::Internal::GamerServices::guideOverlayAttached()) {
        try {CNA::Internal::GamerServices::pollSystemGuideButton();}
        catch(...) {if(!firstError)firstError=std::current_exception();}
    }
    if(firstError)std::rethrow_exception(firstError);
}
bool GamerServicesDispatcher::UpdateAsync(){if(isInitialized_)Update();return isInitialized_;}
std::size_t GamerServicesDispatcher::GetFreedGamerCountForTesting(){return freedGamerCount_;}
}
