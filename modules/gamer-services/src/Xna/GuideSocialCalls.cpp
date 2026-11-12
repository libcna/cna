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

// XNA Refresh ErrorHandler.GetExceptionFromResult IL_0105..010f supplies the visible-Guide diagnostic.
namespace Microsoft::Xna::Framework::GamerServices
{
    void Guide::DelayNotifications(System::TimeSpan delay)
    {
        // XNA Refresh DelayNotifications IL_0009: the kernel packet needs initialized gamer services.
        CNA::Internal::GamerServices::throwIfGamerServicesNotInitialized();
        // Defers the Guide's own notifications (game invitations); at most 120 s, an active delay stays.
        CNA::Internal::GamerServices::delayNotifications(static_cast<long long>(delay.getTotalMillisecondsProperty()));
    }

    namespace {
        namespace Service = CNA::Internal::GamerServices;
        // XNA Refresh ExecuteKernelCall IL_0000-000d checks the player (CNA keeps its four-player
        // range), then IL_000e/IL_0034 reach GamerServicesDispatcher.get_PacketBuffer, which refuses
        // uninitialized gamer services (owner decision 2026-11-12).
        void RequirePlayer(PlayerIndex player) {
            const auto index=static_cast<int>(player);
            if(index<0||index>3)throw System::ArgumentOutOfRangeException("player");
        }
        void RequireInitialized() { CNA::Internal::GamerServices::throwIfGamerServicesNotInitialized(); }
        // .NET String.Length of UTF-8 text: one unit per character, two for a supplementary one.
        std::size_t Utf16Length(const std::string& text) {
            std::size_t units=0;
            for(const unsigned char byte:text) {
                if((byte&0xC0u)==0x80u)continue;
                units+=(byte&0xF8u)==0xF0u?2:1;
            }
            return units;
        }
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
            // XNA Refresh ExecuteKernelCall(player,gamer) IL_0024..0033: GamerInvalid resource.
            if(gamer->getIsDisposedProperty())throw System::ObjectDisposedException("gamer", "This Gamer is no longer valid. The profile may have been signed out.");
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
        // XNA Refresh ShowComposeMessage: the player (IL_0000-000d), text shorter than 256 UTF-16
        // units (IL_0018-0023), the recipient list, then the kernel packet (IL_0066).
        RequirePlayer(player);
        if(Utf16Length(text)>=256)throw System::ArgumentException("Text is too long.","text");
        ValidateRecipients(recipients);
        RequireInitialized(); // XNA ShowComposeMessage IL_0060-0065
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException("The function cannot be completed at this time: the Guide UI is already active. Wait until Guide.IsVisible is false before issuing this call.");
        auto* actor=SocialActor(player);RequireCommunication(actor);std::vector<std::string> tags;
        for(auto* gamer:recipients)tags.push_back(gamer->getGamertagProperty());
        Compose(player,Service::GamerAccess::userId(*actor),tags,text);
    }
    void Guide::ShowFriendRequest(PlayerIndex player,Gamer* gamer) {
        RequirePlayer(player);ValidateGamer(gamer);RequireInitialized();
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException("The function cannot be completed at this time: the Guide UI is already active. Wait until Guide.IsVisible is false before issuing this call.");
        (void)SocialActor(player);
        // The gamer card leads with the friend request.
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::gamerCardScreen(player,gamer->getGamertagProperty()),player);
    }
    void Guide::ShowFriends(PlayerIndex player) {
        RequirePlayer(player);RequireInitialized();
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException("The function cannot be completed at this time: the Guide UI is already active. Wait until Guide.IsVisible is false before issuing this call.");
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
        RequireInitialized(); // XNA ShowGameInvite IL_0009-000e
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException("The function cannot be completed at this time: the Guide UI is already active. Wait until Guide.IsVisible is false before issuing this call.");
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
        // XNA Refresh ExecuteKernelCall(player,gamer) IL_0000..001b checks player before gamer.
        // Retain the established four-player range; undefined players must still win precedence.
        if (static_cast<int>(player) < 0 || static_cast<int>(player) > 3)
            throw System::ArgumentOutOfRangeException("player");
        ValidateGamer(gamer);
        RequireInitialized();
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException("The function cannot be completed at this time: the Guide UI is already active. Wait until Guide.IsVisible is false before issuing this call.");
        auto* actor=SocialActor(player);
        if(gamer->getGamertagProperty()!=actor->getGamertagProperty()&&
           actor->getPrivilegesProperty().getAllowProfileViewingProperty()==GamerPrivilegeSetting::Blocked)
            throw GamerPrivilegeException("The profile may not view other gamers' profiles.");
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::gamerCardScreen(player,gamer->getGamertagProperty()),player);
    }

    void Guide::ShowMessages(PlayerIndex player)
    {
        RequirePlayer(player);RequireInitialized();
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException("The function cannot be completed at this time: the Guide UI is already active. Wait until Guide.IsVisible is false before issuing this call.");
        (void)SocialActor(player);
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::messagesScreen(player),player);
    }

    void Guide::ShowParty(PlayerIndex player)
    {
        RequirePlayer(player);RequireInitialized();
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException("The function cannot be completed at this time: the Guide UI is already active. Wait until Guide.IsVisible is false before issuing this call.");
        (void)SocialActor(player);
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::partyScreen(player),player);
    }

    void Guide::ShowPartySessions(PlayerIndex player)
    {
        RequirePlayer(player);RequireInitialized();
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException("The function cannot be completed at this time: the Guide UI is already active. Wait until Guide.IsVisible is false before issuing this call.");
        (void)SocialActor(player);
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::partySessionsScreen(player),player);
    }

    void Guide::ShowPlayerReview(PlayerIndex player, Gamer* gamer)
    {
        RequirePlayer(player);ValidateGamer(gamer);RequireInitialized();
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException("The function cannot be completed at this time: the Guide UI is already active. Wait until Guide.IsVisible is false before issuing this call.");
        (void)SocialActor(player);
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::reviewScreen(player,gamer->getGamertagProperty()),player);
    }

    void Guide::ShowPlayers(PlayerIndex player)
    {
        RequirePlayer(player);RequireInitialized();
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException("The function cannot be completed at this time: the Guide UI is already active. Wait until Guide.IsVisible is false before issuing this call.");
        (void)SocialActor(player);
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::playersScreen(player),player);
    }

    void Guide::ShowAchievementsEXT(PlayerIndex player)
    {
        // CNA's own pane follows the same rule as the XNA screens above.
        RequirePlayer(player);RequireInitialized();
        if(CNA::Internal::GamerServices::guideIsVisible())throw GuideAlreadyVisibleException("The function cannot be completed at this time: the Guide UI is already active. Wait until Guide.IsVisible is false before issuing this call.");
        (void)SocialActor(player);
        CNA::Internal::GamerServices::GuideUi::open(CNA::Internal::GamerServices::GuideUi::achievementsScreen(player),player);
    }
}
