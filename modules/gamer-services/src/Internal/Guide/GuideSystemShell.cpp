// SPDX-License-Identifier: MS-PL
// The system Guide as a whole: what installSystemGuide() adds to the Guide's frame -- the rail's
// root panels, the rail identity read from the service or local profile, the avatar views and the
// avatar catalogs row of the game content panel.
//
// Link boundary: this is the one place the frame's hooks name the social panels and the avatar
// system. GamerServicesDispatcher::Initialize installs it (an initialized game is one whose
// players can sign in and open any panel), as does every factory of one of those panels. Nothing
// a game reaches through Guide.IsTrialMode, Guide.ShowMarketplace or a message box may call it.
#include "GuideScreen.hpp"
#include "../GuideOverlay.hpp"
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/LocalProfiles.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace CNA::Internal::GamerServices::GuideUi {
namespace {
namespace GS=Microsoft::Xna::Framework::GamerServices;

GS::SignedInGamer* signedIn(Xna::PlayerIndex player)
{
    for(auto* gamer:*GS::Gamer::getSignedInGamersProperty())
        if(gamer->getPlayerIndexProperty()==player)return gamer;
    return nullptr;
}

std::shared_ptr<Screen> rootFor(Category category,Xna::PlayerIndex player)
{
    switch(category) {
    case Category::Home: return homeScreen(player);
    case Category::Friends: return friendsScreen(player);
    case Category::Party: return partyScreen(player);
    case Category::Messages: return messagesScreen(player);
    case Category::Achievements: return achievementsScreen(player);
    case Category::Leaderboards: return leaderboardsScreen(player);
    case Category::Players: return playersScreen(player);
    case Category::Content: return contentScreen(player);
    case Category::Settings: return settingsScreen(player);
    case Category::None: break;
    }
    return nullptr;
}

void refreshServiceIdentity(Xna::PlayerIndex player)
{
    auto& who=identity(player);
    auto* gamer=signedIn(player);
    who=Identity{};
    if(!gamer)return;
    who.gamertag=gamer->getGamertagProperty();
    who.guest=gamer->getIsGuestProperty();
    who.online=gamer->getIsSignedInToLiveProperty()&&!who.guest&&backend()->serviceEnabled();
    who.userId=GamerAccess::userId(*gamer);
    if(!who.online) {
        if(!who.guest)who.avatar=localProfileAvatar(who.gamertag);
        return;
    }
    struct Result {std::vector<unsigned char> avatar;int score=0;int unread=0;};
    auto service=backend();
    auto result=std::make_shared<std::optional<Result>>();
    const std::weak_ptr<IGamerServicesBackend> origin=service;
    service->submit([result,origin,user=who.userId,tag=who.gamertag] {
        try {
            auto executor=origin.lock();
            if(!executor)return;
            Result value;
            try{value.avatar=executor->avatars({user},Avatars::clientCatalogs()).at(0).description;}catch(...){}
            try{value.score=executor->profile(tag).gamerScore;}catch(...){}
            try{value.unread=executor->messages(user,0,1).unread;}catch(...){}
            *result=std::move(value);
        } catch(...) {}
    },[result,player,tag=who.gamertag] {
        auto& current=identity(player);
        if(!*result||current.gamertag!=tag)return;
        current.avatar=std::move((*result)->avatar);
        current.gamerScore=(*result)->score;
        current.unread=(*result)->unread;
    });
}

// The avatar catalogs, one product: built in, then installed updates.
std::optional<std::pair<std::string,std::string>> avatarCatalogs()
{
    std::vector<std::uint16_t> builtIn,installed;
    for(auto version:Avatars::availableCatalogVersions())(Avatars::embeddedManifest(version)?builtIn:installed).push_back(version);
    auto listed=[](const std::vector<std::uint16_t>& versions) {
        std::string text;
        for(auto version:versions)text+=(text.empty()?"":", ")+std::to_string(version);
        return text;
    };
    std::string detail=builtIn.empty()?"":(builtIn.size()==1?"Catalog ":"Catalogs ")+listed(builtIn)+" with this CNA release";
    if(!installed.empty())detail+=(detail.empty()?"":" \xe2\x80\xa2 ")+std::string(installed.size()==1?"catalog ":"catalogs ")+listed(installed)+" installed as updates";
    std::string updates="Catalog updates are off: avatars from newer catalogs are shown as the service adapts them";
    try {
        const auto policy=backend()->avatarCatalogPolicy();
        if(policy.updates)updates="Newer catalogs install when an avatar needs one, up to "+std::to_string(policy.maximumBytes>>20)+" MB each";
    } catch(...) {}
    return std::pair{std::move(detail),std::move(updates)};
}
}
}

namespace CNA::Internal::GamerServices {
void installSystemGuide()
{
    static const bool installed=[] {
        GuideUi::setShellHooks({&GuideUi::rootFor,&GuideUi::refreshServiceIdentity,&GuideUi::avatarCatalogs});
        GuideUi::setPortraitSourceFactory(&GuideUi::makeAvatarPortraits);
        return true;
    }();
    (void)installed;
}
}
