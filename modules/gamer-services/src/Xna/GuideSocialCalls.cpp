// SPDX-License-Identifier: MS-PL
// The Guide calls that open the service-backed system Guide: friends, messages, gamer cards,
// party, invitations, reviews, recent players and achievements. Each opens the full system Guide,
// which a player can navigate from there, so these are what link it (with its avatar views) into
// a game that calls them.
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GuideAlreadyVisibleException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "../Internal/GuideOverlay.hpp"
#include "../Internal/Guide/GuideScreen.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::GamerServices
{
    void Guide::DelayNotifications(System::TimeSpan delay)
    {
        // Defers the Guide's own notifications (game invitations); at most 120 s, an active delay stays.
        CNA::Internal::GamerServices::delayNotifications(static_cast<long long>(delay.getTotalMillisecondsProperty()));
    }

    namespace {
        namespace Service = CNA::Internal::GamerServices;
        SignedInGamer* SocialActor(PlayerIndex player) {
            const auto index=static_cast<int>(player);
            if(index<0||index>3)throw System::ArgumentOutOfRangeException("player");
            if(!Service::backend()->serviceEnabled())throw GamerServicesNotAvailableException("No CNA account service is configured.");
            for(auto* gamer:*Gamer::getSignedInGamersProperty())
                if(gamer->getPlayerIndexProperty()==player&&gamer->getIsSignedInToLiveProperty())return gamer;
            throw GamerServicesNotAvailableException("The requested player is not signed in.");
        }
        void SocialMessage(PlayerIndex player,const std::string& title,const std::string& text) {
            (void)CNA::Internal::GamerServices::showGuideMessageBox(player,title,text,{"OK"},0,MessageBoxIcon::None,[](System::IAsyncResult& result){
                std::unique_ptr<System::IAsyncResult> owned(&result);(void)Guide::EndShowMessageBox(&result);
            },{});
        }
        void ValidateRecipients(const std::vector<Gamer*>& recipients) {
            // Reference Gamer.ValidateGamerList: at most 100 gamers, none null or disposed.
            if(recipients.size()>100)throw System::ArgumentException("Too many gamers.","recipients");
            for(auto* gamer:recipients) {
                if(!gamer)throw System::ArgumentException("Gamer is null.","recipients");
                if(gamer->getIsDisposedProperty())throw System::ObjectDisposedException("recipients");
            }
        }
        void ValidateGamer(Gamer* gamer) {
            if(!gamer)throw System::ArgumentNullException("gamer");
            if(gamer->getIsDisposedProperty())throw System::ObjectDisposedException("gamer");
        }
        // Runs one service call on the backend executor; the outcome is reported at Update.
        void SocialCall(PlayerIndex player,std::function<void(Service::IGamerServicesBackend&)> work,std::string failure) {
            auto service=Service::backend();auto* executor=service.get();auto failed=std::make_shared<bool>(false);
            service->submit([executor,work=std::move(work),failed]{try{work(*executor);}catch(...){*failed=true;}},
                [player,failed,failure=std::move(failure)]{if(*failed)SocialMessage(player,"CNA Gamer Services",failure);});
        }
        // XNA refuses these Guide calls with GamerPrivilegeException when the profile's privilege
        // is Blocked (the native result ProfileNotPrivileged, mapped by ErrorHandler); Guide.xml
        // names AllowCommunication for messages and invitations and AllowProfileViewing for gamer
        // cards. FriendsOnly is enforced by the service per recipient.
        void RequireCommunication(SignedInGamer* actor) {
            if(actor->getPrivilegesProperty().getAllowCommunicationProperty()==GamerPrivilegeSetting::Blocked)
                throw GamerPrivilegeException("The profile may not communicate with other gamers.");
        }
        void SendMessage(PlayerIndex player,const std::string& user,const std::vector<std::string>& tags,const std::string& text) {
            SocialCall(player,[user,tags,text](auto& executor){executor.sendMessage(user,tags,text);},"The message could not be sent.");
        }
        void Compose(PlayerIndex player,const std::string& user,const std::vector<std::string>& tags,const std::string& text) {
            std::string names;for(const auto& tag:tags)names+=(names.empty()?"":", ")+tag;
            (void)CNA::Internal::GamerServices::showGuideKeyboardInput(player,"Compose message",names.empty()?"Message":"To: "+names,text,
                [player,user,tags](System::IAsyncResult& input) {
                    std::unique_ptr<System::IAsyncResult> owned(&input);
                    if(Guide::WasKeyboardInputCanceledEXT(&input))return;
                    const auto body=Guide::EndShowKeyboardInput(&input);
                    if(!tags.empty()){SendMessage(player,user,tags,body);return;}
                    (void)CNA::Internal::GamerServices::showGuideKeyboardInput(player,"Compose message","Recipient gamertag","",[player,user,body](System::IAsyncResult& recipient) {
                        std::unique_ptr<System::IAsyncResult> ownedRecipient(&recipient);
                        if(Guide::WasKeyboardInputCanceledEXT(&recipient))return;
                        const auto tag=Guide::EndShowKeyboardInput(&recipient);
                        if(!tag.empty())SendMessage(player,user,{tag},body);
                    },{});
                },{});
        }
    }
    void Guide::ShowComposeMessage(PlayerIndex player,const std::string& text,const std::vector<Gamer*>& recipients) {
        // Reference: text shorter than 256 characters, then the recipient list, then the Guide.
        if(text.size()>=256)throw System::ArgumentException("Text is too long.","text");
        ValidateRecipients(recipients);
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        auto* actor=SocialActor(player);RequireCommunication(actor);std::vector<std::string> tags;
        for(auto* gamer:recipients)tags.push_back(gamer->getGamertagProperty());
        Compose(player,Service::GamerAccess::userId(*actor),tags,text);
    }
    void Guide::ShowFriendRequest(PlayerIndex player,Gamer* gamer) {
        ValidateGamer(gamer);
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        (void)SocialActor(player);
        // The gamer card leads with the friend request.
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::gamerCardScreen(player,gamer->getGamertagProperty()),player);
    }
    void Guide::ShowFriends(PlayerIndex player) {
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        (void)SocialActor(player);
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::friendsScreen(player),player);
    }

    void Guide::ShowGameInvite(PlayerIndex player,const std::vector<Gamer*>& recipients) {
        // Reference ShowGameInvite validates the recipient list (at most 100, no null/disposed
        // gamers) before showing the Guide; an empty list prompts for the recipient.
        if(recipients.size()>100)throw System::ArgumentException("Too many gamers.","recipients");
        for(auto* gamer:recipients) {
            if(!gamer)throw System::ArgumentException("Gamer is null.","recipients");
            if(gamer->getIsDisposedProperty())throw System::ObjectDisposedException("recipients");
        }
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        auto* actor=SocialActor(player);RequireCommunication(actor);
        if(!Service::activeOnlineSession())
            throw System::InvalidOperationException("There is no online network session to invite gamers to.");
        (void)actor;
        std::vector<std::string> tags;for(auto* gamer:recipients)tags.push_back(gamer->getGamertagProperty());
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::inviteScreen(player,std::move(tags)),player);
    }

    void Guide::ShowGameInvite(const std::string& /*sessionId*/)
    {
        // The reference assembly supports this overload only for Windows Phone LIVE titles.
        throw System::NotSupportedException("ShowGameInvite(sessionId) is supported only on Windows Phone.");
    }

    void Guide::ShowGamerCard(PlayerIndex player,Gamer* gamer) {
        ValidateGamer(gamer);
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        auto* actor=SocialActor(player);
        if(gamer->getGamertagProperty()!=actor->getGamertagProperty()&&
           actor->getPrivilegesProperty().getAllowProfileViewingProperty()==GamerPrivilegeSetting::Blocked)
            throw GamerPrivilegeException("The profile may not view other gamers' profiles.");
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::gamerCardScreen(player,gamer->getGamertagProperty()),player);
    }

    void Guide::ShowMessages(PlayerIndex player)
    {
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        (void)SocialActor(player);
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::messagesScreen(player),player);
    }

    void Guide::ShowParty(PlayerIndex player)
    {
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        (void)SocialActor(player);
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::partyScreen(player),player);
    }

    void Guide::ShowPartySessions(PlayerIndex player)
    {
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        (void)SocialActor(player);
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::partySessionsScreen(player),player);
    }

    void Guide::ShowPlayerReview(PlayerIndex player, Gamer* gamer)
    {
        ValidateGamer(gamer);
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        (void)SocialActor(player);
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::reviewScreen(player,gamer->getGamertagProperty()),player);
    }

    void Guide::ShowPlayers(PlayerIndex player)
    {
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        (void)SocialActor(player);
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::playersScreen(player),player);
    }

    void Guide::ShowAchievementsEXT(PlayerIndex player)
    {
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException();
        (void)SocialActor(player);
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::achievementsScreen(player),player);
    }
}
