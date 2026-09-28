// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "GuideOverlay.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include <algorithm>
#include <chrono>
#include <deque>
#include <set>

namespace CNA::Internal::GamerServices {
namespace {
using Microsoft::Xna::Framework::GamerServices::Gamer;
using Microsoft::Xna::Framework::GamerServices::Guide;
using Microsoft::Xna::Framework::GamerServices::MessageBoxIcon;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;
using Clock=std::chrono::steady_clock;
constexpr auto PollInterval=std::chrono::seconds(5);
constexpr auto FailureInterval=std::chrono::seconds(30);
constexpr std::size_t MaxQueued=16,MaxSeen=1024;
struct Pending {std::string user;ServiceInvitation invitation;};
struct State {
    // Identity only: queued work and this process-wide state never own a backend.
    const IGamerServicesBackend* origin=nullptr;
    Clock::time_point nextPoll{};
    bool polling=false,prompting=false,accepting=false;
    std::deque<std::string> seenOrder;
    std::set<std::string> seen;
    std::deque<Pending> queue;
    std::optional<ActiveOnlineSession> active;
    std::optional<AcceptedInvitation> accepted;
    std::function<void(const AcceptedInvitation&)> sink;
    Clock::time_point quietUntil{};
    std::deque<std::string> recent;
};
State& state(){static State value;return value;}
SignedInGamer* published(const std::string& user) {
    for(auto* gamer:*Gamer::getSignedInGamersProperty())
        if(gamer&&!gamer->getIsGuestProperty()&&gamer->getIsSignedInToLiveProperty()&&GamerAccess::userId(*gamer)==user)return gamer;
    return nullptr;
}
void remember(const std::string& invite) {
    auto& current=state();
    if(!current.seen.insert(invite).second)return;
    current.seenOrder.push_back(invite);
    while(current.seenOrder.size()>MaxSeen){current.seen.erase(current.seenOrder.front());current.seenOrder.pop_front();}
}
std::string category(ServiceSessionKind kind){return kind==ServiceSessionKind::Ranked?"Ranked":"Player Match";}
void accept(Pending pending) {
    auto& current=state();auto service=backend();auto* executor=service.get();
    auto result=std::make_shared<std::optional<ServiceInvitation>>();
    current.accepting=true;
    const std::weak_ptr<IGamerServicesBackend> weak=service;
    service->submit([executor,pending,result] {
        try{*result=executor->sessionDirectory().acceptInvite(pending.user,pending.invitation.invite);}catch(...){}
    },[pending,result,weak,origin=executor] {
        auto& now=state();if(now.origin!=origin)return;now.accepting=false;
        auto* gamer=published(pending.user);
        if(!*result||!gamer) {
            if(gamer)(void)showGuideMessageBox(gamer->getPlayerIndexProperty(),"Game invitation",
                "The invitation is no longer available.",{"OK"},0,MessageBoxIcon::None,[](System::IAsyncResult& value) {
                    std::unique_ptr<System::IAsyncResult> owned(&value);(void)Guide::EndShowMessageBox(&value);
                },{});
            return;
        }
        now.accepted=AcceptedInvitation{**result,pending.user,gamer,weak};
        if(now.sink)now.sink(*now.accepted);
    });
}
void prompt() {
    auto& current=state();
    while(!current.queue.empty()&&!published(current.queue.front().user))current.queue.pop_front();
    if(current.queue.empty())return;
    auto pending=std::move(current.queue.front());current.queue.pop_front();
    auto* gamer=published(pending.user);
    const auto text=pending.invitation.senderGamertag+" invited "+gamer->getGamertagProperty()+" to join a "
        +category(pending.invitation.kind)+" game.\nAccepting leaves any game you are playing.";
    current.prompting=true;
    try {
        (void)showGuideMessageBox(gamer->getPlayerIndexProperty(),"Game invitation",text,{"Accept","Decline"},0,
            MessageBoxIcon::None,[pending](System::IAsyncResult& value) {
                std::unique_ptr<System::IAsyncResult> owned(&value);const auto answer=Guide::EndShowMessageBox(&value);
                auto& now=state();now.prompting=false;
                auto service=backend();
                if(now.origin!=service.get())return;
                if(answer&&*answer==0){accept(pending);return;}
                if(answer&&*answer==1) {
                    auto* executor=service.get();
                    service->submit([executor,pending]{try{(void)executor->sessionDirectory().dismissInvite(pending.user,pending.invitation.invite);}catch(...){}},[]{});
                }
                // Closing the prompt leaves the invitation pending in the service inbox.
            },{});
    }catch(...) {current.prompting=false;current.queue.push_front(std::move(pending));}
}
void poll() {
    auto& current=state();
    std::vector<std::string> users;
    for(auto* gamer:*Gamer::getSignedInGamersProperty())
        if(gamer&&!gamer->getIsGuestProperty()&&gamer->getIsSignedInToLiveProperty()&&!GamerAccess::userId(*gamer).empty())
            users.push_back(GamerAccess::userId(*gamer));
    current.nextPoll=Clock::now()+PollInterval;
    if(users.empty())return;
    auto results=std::make_shared<std::vector<Pending>>();auto failed=std::make_shared<bool>(false);
    auto service=backend();auto* executor=service.get();current.polling=true;
    try {
        service->submit([executor,users,results,failed] {
            for(const auto& user:users) {
                try {
                    for(auto& invitation:executor->sessionDirectory().listInvites(user,0,32).invites)
                        results->push_back(Pending{user,std::move(invitation)});
                }catch(...){*failed=true;}
            }
        },[results,failed,origin=executor] {
            auto& now=state();if(now.origin!=origin)return;now.polling=false;
            if(*failed)now.nextPoll=Clock::now()+FailureInterval;
            for(auto& pending:*results) {
                // Receipt is not acceptance: only unanswered pending invitations prompt, once each.
                if(pending.invitation.state!=ServiceInvitationState::Pending||now.seen.contains(pending.invitation.invite))continue;
                remember(pending.invitation.invite);
                if(now.queue.size()<MaxQueued)now.queue.push_back(std::move(pending));
            }
        });
    }catch(...){current.polling=false;current.nextPoll=Clock::now()+FailureInterval;}
}
}
void setActiveOnlineSession(std::optional<ActiveOnlineSession> value){state().active=std::move(value);}
const std::optional<ActiveOnlineSession>& activeOnlineSession(){return state().active;}
std::optional<AcceptedInvitation>& acceptedInvitation(){return state().accepted;}
void setInviteAcceptedSink(std::function<void(const AcceptedInvitation&)> sink){state().sink=std::move(sink);}
void pumpInvitations() {
    auto& current=state();auto service=backend();
    if(current.origin!=service.get()) {
        // A replaced backend has different accounts; its invitations and prompts no longer apply.
        current.origin=service.get();current.nextPoll={};current.polling=current.prompting=current.accepting=false;
        current.seen.clear();current.seenOrder.clear();current.queue.clear();current.accepted.reset();
    }
    if(!service||!service->serviceEnabled())return;
    if(!current.polling&&Clock::now()>=current.nextPoll)poll();
    if(!current.prompting&&!current.accepting&&!guideIsVisible()&&Clock::now()>=current.quietUntil)prompt();
}
void delayNotifications(long long milliseconds) {
    auto& current=state();const auto now=Clock::now();
    if(now<current.quietUntil||milliseconds<=0)return;
    current.quietUntil=now+std::chrono::milliseconds(std::min<long long>(milliseconds,120000));
}
void rememberRecentPlayer(const std::string& gamertag) {
    auto& current=state();
    std::erase(current.recent,gamertag);current.recent.push_front(gamertag);
    while(current.recent.size()>30)current.recent.pop_back();
}
std::vector<std::string> recentPlayers(){const auto& recent=state().recent;return {recent.begin(),recent.end()};}
void sendInvitations(const std::string& user,const std::vector<std::string>& gamertags,std::function<void(int)> done) {
    auto& current=state();
    if(!current.active)throw ServiceOperationError("INVALID_STATE");
    auto origin=current.active->origin.lock();
    if(!origin)throw ServiceOperationError("INVALID_STATE");
    auto* executor=origin.get();const auto session=current.active->session;
    auto failures=std::make_shared<int>(0);
    origin->submit([executor,user,gamertags,session,failures] {
        for(const auto& tag:gamertags){try{(void)executor->sessionDirectory().sendInvite(user,session,tag);}catch(...){++*failures;}}
    },[done=std::move(done),failures]{if(done)done(*failures);});
}
void pollInvitationsNowForTesting(){state().nextPoll={};}
void resetInvitationsForTesting() {
    auto& current=state();current.origin=nullptr;current.nextPoll={};current.polling=current.prompting=current.accepting=false;
    current.seen.clear();current.seenOrder.clear();current.queue.clear();current.accepted.reset();
    current.quietUntil={};current.recent.clear();
}
}
