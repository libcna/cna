// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
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
    (void)CNA::Internal::GamerServices::backend();
    CNA::Internal::GamerServices::installGuideOverlay(serviceProvider);publish();isInitialized_=true;
}
void GamerServicesDispatcher::Update() {
    if(!isInitialized_)return;
    // End inside a service callback must progress work without publishing nested identity events.
    if(updating) {
        auto events=CNA::Internal::GamerServices::backend()->pump();
        for(auto& event:events) {
            if(event.type==CNA::Internal::GamerServices::BackendEvent::Type::Completion) {
                if(event.completion)event.completion();
            } else deferred.push_back(std::move(event));
        }
        return;
    }
    struct Guard {Guard(){updating=true;}~Guard(){updating=false;}} guard;
    auto incoming=CNA::Internal::GamerServices::backend()->pump();
    std::vector<CNA::Internal::GamerServices::BackendEvent> events;
    while(!deferred.empty()){events.push_back(std::move(deferred.front()));deferred.pop_front();}
    for(auto& event:incoming)events.push_back(std::move(event));
    std::exception_ptr firstError;
    for(auto& event:events) {
        try {
            using Type=CNA::Internal::GamerServices::BackendEvent::Type;
            if(event.type==Type::Completion){if(event.completion)event.completion();continue;}
            if(event.slot<0||event.slot>3)continue;
            if(event.type==Type::Failed) { Guide::OnSignInResult(event.slot, false); continue; }
            if(event.type!=Type::SignedIn&&event.type!=Type::SignedOut)continue;
            if(auto* previous=slots[event.slot]) {
                slots[event.slot]=nullptr;previous->isSignedInToLive_=false;publish();SignedInGamer::OnSignOut(previous);
            }
            if(event.type==Type::SignedIn) {
                auto gamer=std::unique_ptr<SignedInGamer>(new SignedInGamer(event.identity.gamertag,true,false,static_cast<PlayerIndex>(event.slot)));
                gamer->serviceUserId_=event.identity.userId;gamer->displayName_=event.identity.displayName;
                gamer->privileges_.allowOnlineSessions_=event.identity.allowOnlineSessions;
                auto* pointer=gamer.get();ownedGamers.push_back(std::move(gamer));slots[event.slot]=pointer;publish();SignedInGamer::OnSignIn(pointer);Guide::OnSignInResult(event.slot, true);
            }
        }catch(...){if(!firstError)firstError=std::current_exception();}
    }
    if(firstError)std::rethrow_exception(firstError);
}
bool GamerServicesDispatcher::UpdateAsync(){if(isInitialized_)Update();return isInitialized_;}
std::size_t GamerServicesDispatcher::GetFreedGamerCountForTesting(){return freedGamerCount_;}
}
