// SPDX-License-Identifier: MS-PL
// The Guide's screens: home, friends, gamer card, messages, achievements, recent players, player
// review, invitations (sent and received), settings, party and game content.
#include "GuideScreen.hpp"
#include "../GuideOverlay.hpp"
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/LocalProfiles.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Achievement.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <set>

namespace CNA::Internal::GamerServices::GuideUi {
namespace {
namespace GS=Microsoft::Xna::Framework::GamerServices;

GS::SignedInGamer* signedIn(Xna::PlayerIndex player)
{
    for(auto* gamer:*GS::Gamer::getSignedInGamersProperty())
        if(gamer->getPlayerIndexProperty()==player)return gamer;
    return nullptr;
}

// Service screens need a signed-in account; a local profile or nobody gets an honest explanation.
enum class Access : std::uint8_t { Account, LocalProfile, Guest, Nobody, NoService };
Access access(Xna::PlayerIndex player)
{
    auto* gamer=signedIn(player);
    if(!gamer)return Access::Nobody;
    if(gamer->getIsGuestProperty())return Access::Guest;
    if(!gamer->getIsSignedInToLiveProperty())return Access::LocalProfile;
    if(!backend()->serviceEnabled())return Access::NoService;
    return Access::Account;
}

bool explainAccess(Ui& ui,const Box& area,Access value)
{
    switch(value) {
    case Access::Account: return false;
    case Access::Nobody: emptyState(ui,area,Icon::Person,"Nobody is signed in","Sign in from Home to use CNA Gamer Services."); return true;
    case Access::Guest: emptyState(ui,area,Icon::Person,"Playing as a guest","A guest plays on its account's sign-in and has no friends list, messages or achievements of its own."); return true;
    case Access::LocalProfile: emptyState(ui,area,Icon::Globe,"Offline profile","This profile lives on this computer. Friends, messages, parties and invitations need a CNA Gamer Services account."); return true;
    case Access::NoService: emptyState(ui,area,Icon::Globe,"Not connected","No CNA Gamer Services endpoint is configured for this game."); return true;
    }
    return false;
}

std::string userOf(Xna::PlayerIndex player)
{
    auto* gamer=signedIn(player);
    return gamer?GamerAccess::userId(*gamer):std::string();
}

std::string lower(std::string value)
{
    for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

Xna::Color presenceColor(const ServiceFriend& entry)
{
    if(!entry.accepted||!entry.online)return Palette::offline();
    return entry.away?Palette::away():entry.busy?Palette::busy():Palette::online();
}

std::string presenceLine(const ServiceFriend& entry)
{
    if(entry.requestReceived)return "Wants to be your friend";
    if(entry.requestSent)return "Friend request sent";
    if(!entry.online)return "Offline";
    std::string status=entry.away?"Away":entry.busy?"Busy":"Online";
    if(!entry.presence.empty())status+=" \xe2\x80\xa2 "+entry.presence;
    return status;
}

// Avatars of several accounts in one service read (16 per request).
std::map<std::string,std::vector<unsigned char>> readAvatars(IGamerServicesBackend& service,const std::vector<std::string>& ids)
{
    std::map<std::string,std::vector<unsigned char>> out;
    for(std::size_t start=0;start<ids.size();start+=16) {
        std::vector<std::string> batch(ids.begin()+static_cast<std::ptrdiff_t>(start),ids.begin()+static_cast<std::ptrdiff_t>(std::min(ids.size(),start+16)));
        try {
            const auto records=service.avatars(batch);
            for(std::size_t i=0;i<batch.size()&&i<records.size();++i)out[batch[i]]=records[i].description;
        } catch(...) {}
    }
    return out;
}

void keyboardPrompt(Xna::PlayerIndex player,std::string title,std::string prompt,std::function<void(const std::string&)> done)
{
    (void)showGuideKeyboardInput(player,std::move(title),std::move(prompt),"",[done=std::move(done)](System::IAsyncResult& input) {
        std::unique_ptr<System::IAsyncResult> owned(&input);
        if(GS::Guide::WasKeyboardInputCanceledEXT(&input))return;
        const auto text=GS::Guide::EndShowKeyboardInput(&input);
        if(!text.empty())done(text);
    },{});
}

void sendMessageTo(Xna::PlayerIndex player,std::vector<std::string> tags)
{
    const auto user=userOf(player);
    std::string names;
    for(const auto& tag:tags)names+=(names.empty()?"":", ")+tag;
    keyboardPrompt(player,"Message to "+names,"Up to 256 characters",[player,user,tags](const std::string& text) {
        auto service=backend();
        auto* executor=service.get();
        auto failed=std::make_shared<bool>(false);
        service->submit([executor,user,tags,text,failed]{try{executor->sendMessage(user,tags,text);}catch(...){*failed=true;}},
            [player,failed]{if(*failed)inform(player,"Messages","The message could not be sent.",Icon::Error);
                            else notify({Notification::Kind::Message,"Message sent",""});});
    });
}

// ---- Home ---------------------------------------------------------------------------------------
class HomeScreen final : public Screen {
public:
    std::string name() const override {return "home";}
    std::string title() const override {return "Home";}
    std::string subtitle() const override {
        const auto& who=identity(player);
        return who.gamertag.empty()?"No profile signed in":who.online?"Signed in to CNA Gamer Services":who.guest?"Playing as a guest":"Offline profile";
    }
    Category category() const override {return Category::Home;}
    std::vector<std::pair<Icon,std::string>> items() const {
        const auto a=access(player);
        if(a==Access::Nobody)return {{Icon::Person,"Sign in"}};
        std::vector<std::pair<Icon,std::string>> list;
        if(a==Access::Guest)return {{Icon::SignOut,"Sign out"}};
        if(a==Access::Account) {
            list.push_back({Icon::People,friendsOnline_>=0?"Friends \xe2\x80\xa2 "+std::to_string(friendsOnline_)+" online":"Friends"});
            list.push_back({Icon::Message,"Messages"});
        }
        list.push_back({Icon::Trophy,"Achievements"});
        list.push_back({Icon::Pencil,"Edit avatar"});
        if(a==Access::Account)list.push_back({Icon::Settings,"Online status and gamer zone"});
        list.push_back({Icon::SignOut,"Sign out"});
        return list;
    }
    void input(InputContext& ui) override {
        const auto chosen=list_.input(ui,static_cast<int>(items().size()));
        if(chosen>=0)activate(chosen);
    }
    void activate(int index) override {
        const auto list=items();
        if(index<0||index>=static_cast<int>(list.size()))return;
        const auto& label=list[static_cast<std::size_t>(index)].second;
        if(label=="Sign in") {
            const int panes=player==Xna::PlayerIndex::One?1:player==Xna::PlayerIndex::Two?2:4;
            closeAll();
            try{GS::Guide::ShowSignIn(panes,false);}catch(const std::exception& error){inform(player,"Sign in",error.what(),Icon::Error);}
        } else if(label.starts_with("Friends")) replace(friendsScreen(player));
        else if(label=="Messages") replace(messagesScreen(player));
        else if(label=="Achievements") replace(achievementsScreen(player));
        else if(label=="Edit avatar") push(avatarEditorScreen(player));
        else if(label.starts_with("Online status")) replace(settingsScreen(player));
        else if(label=="Sign out") {
            auto* gamer=signedIn(player);
            const int slot=static_cast<int>(player);
            closeAll();
            if(gamer&&gamer->getIsGuestProperty())backend()->signOutGuest(slot);
            else if(gamer)backend()->signOut(slot);
        }
    }
    void draw(Ui& ui,const Box& area) override {
        const auto& who=identity(player);
        const Box stage{area.x,area.y,ui.px(250),area.h};
        portrait(ui,who.avatar,stage,Framing::Body,true);
        const Box right{stage.right()+ui.px(24),area.y,area.right()-stage.right()-ui.px(24),area.h};
        drawActions(ui,list_,right,items());
    }
    std::vector<Hint> hints() const override {return {{Command::Accept,"Select"},{Command::Back,"Close"}};}
    std::vector<std::string> labels() const override {std::vector<std::string> out;for(const auto& [icon,label]:items())out.push_back(label);return out;}
    int focus() const override {return list_.focus;}
    void start(const std::shared_ptr<HomeScreen>& self) {
        if(access(player)!=Access::Account)return;
        load<int>(self,[user=userOf(player)](IGamerServicesBackend& s) {
            int online=0;
            for(const auto& entry:s.friends(user))online+=entry.accepted&&entry.online;
            return online;
        },[](HomeScreen& screen,int online){screen.friendsOnline_=online;});
    }
private:
    List list_;
    int friendsOnline_=-1;
};

// ---- Friends ------------------------------------------------------------------------------------
class FriendsScreen final : public Screen {
public:
    std::string name() const override {return "friends";}
    std::string title() const override {return "Friends";}
    std::string subtitle() const override {
        if(!loaded_)return "";
        int online=0,friends=0;
        for(const auto& entry:friends_){friends+=entry.accepted;online+=entry.accepted&&entry.online;}
        return std::to_string(online)+" of "+std::to_string(friends)+" online";
    }
    Category category() const override {return Category::Friends;}
    void input(InputContext& ui) override {
        if(access(player)!=Access::Account)return;
        const int chosen=list_.input(ui,static_cast<int>(friends_.size()));
        if(chosen>=0)activate(chosen);
        if(ui.input(Command::Y)&&!ui.input.keyboard[static_cast<std::size_t>(Command::Y)]) findGamer();
        else if(ui.input(Command::Y)) findGamer();
        if(ui.input(Command::X)&&canInvite())invite();
    }
    void activate(int index) override {
        if(index>=0&&index<static_cast<int>(friends_.size()))push(gamerCardScreen(player,friends_[static_cast<std::size_t>(index)].gamertag));
    }
    void draw(Ui& ui,const Box& area) override {
        if(explainAccess(ui,area,access(player)))return;
        if(failed_){emptyState(ui,area,Icon::Warning,"Friends could not be loaded","Check the connection to CNA Gamer Services.");return;}
        if(!loaded_){loading(ui,area);return;}
        if(friends_.empty()){emptyState(ui,area,Icon::PersonAdd,"No friends yet","Find other players by their gamertag and send them a friend request.");return;}
        list_.draw(ui,area,static_cast<int>(friends_.size()),76,[&](int index,const Box& row,bool focused) {
            const auto& entry=friends_[static_cast<std::size_t>(index)];
            rowBackground(ui,row,focused);
            const Box face{row.x+ui.px(10),row.y+ui.px(7),row.h-ui.px(14),row.h-ui.px(14)};
            const auto avatar=avatars_.find(entry.userId);
            portrait(ui,avatar==avatars_.end()?std::vector<unsigned char>{}:avatar->second,face);
            statusDot(ui,Xna::Vector2(face.right()-ui.px(4),face.bottom()-ui.px(4)),presenceColor(entry));
            const float tx=face.right()+ui.px(16);
            ui.style.text(ui.batch,Font::BodyBold,entry.gamertag,Xna::Vector2(tx,row.y+ui.px(10)),Palette::text());
            ui.style.text(ui.batch,Font::Caption,ui.style.fit(Font::Caption,presenceLine(entry),row.right()-tx-ui.px(150)),
                Xna::Vector2(tx,row.y+ui.px(40)),entry.online?Palette::muted():Palette::faint());
            float bx=row.right()-ui.px(16);
            auto badge=[&](Icon icon,const std::string& label,Xna::Color color) {
                const float w=ui.style.measure(Font::Caption,label).X+ui.px(38);
                bx-=w;
                ui.style.rounded(ui.batch,Box{bx,row.y+(row.h-ui.px(28))/2,w,ui.px(28)},ui.px(14),Xna::Color(14,14,14,14));
                ui.style.icon(ui.batch,icon,Box{bx+ui.px(8),row.y+(row.h-ui.px(18))/2,ui.px(18),ui.px(18)},color);
                ui.style.text(ui.batch,Font::Caption,label,Xna::Vector2(bx+ui.px(30),row.y+(row.h-ui.px(28))/2+ui.px(3)),color);
                bx-=ui.px(8);
            };
            if(entry.inviteReceivedFrom)badge(Icon::Invite,"Invited you",Palette::accent());
            if(entry.joinable)badge(Icon::Controller,"Joinable",Palette::online());
            if(entry.requestReceived)badge(Icon::PersonAdd,"Request",Palette::accent());
        });
    }
    std::vector<Hint> hints() const override {
        std::vector<Hint> out{{Command::Accept,"Gamer card"}};
        if(canInvite())out.push_back({Command::X,"Invite"});
        out.push_back({Command::Y,"Find gamer"});
        out.push_back({Command::Back,"Back"});
        return out;
    }
    std::vector<std::string> labels() const override {
        std::vector<std::string> out;
        for(const auto& entry:friends_)out.push_back(entry.gamertag+" - "+presenceLine(entry));
        return out;
    }
    int focus() const override {return list_.focus;}
    void start(const std::shared_ptr<FriendsScreen>& self) {
        if(access(player)!=Access::Account)return;
        struct Result {std::vector<ServiceFriend> friends;std::map<std::string,std::vector<unsigned char>> avatars;};
        load<Result>(self,[user=userOf(player)](IGamerServicesBackend& s) {
            Result result{s.friends(user),{}};
            std::vector<std::string> ids;
            for(const auto& entry:result.friends)if(!entry.userId.empty())ids.push_back(entry.userId);
            result.avatars=readAvatars(s,ids);
            return result;
        },[](FriendsScreen& screen,Result result) {
            // Requests first, then who is online (joinable first), then everyone else, by name.
            std::ranges::stable_sort(result.friends,[](const ServiceFriend& a,const ServiceFriend& b) {
                auto rank=[](const ServiceFriend& f){return f.requestReceived?0:f.online&&f.joinable?1:f.online?2:f.requestSent?4:3;};
                return rank(a)!=rank(b)?rank(a)<rank(b):lower(a.gamertag)<lower(b.gamertag);
            });
            screen.friends_=std::move(result.friends);
            screen.avatars_=std::move(result.avatars);
            screen.loaded_=true;
        },[](FriendsScreen& screen){screen.failed_=true;});
    }
private:
    bool canInvite() const {
        const auto focused=static_cast<std::size_t>(list_.focus);
        return activeOnlineSession().has_value()&&focused<friends_.size()&&friends_[focused].accepted;
    }
    void invite() {
        const auto tag=friends_[static_cast<std::size_t>(list_.focus)].gamertag;
        const auto who=player;
        sendInvitations(userOf(player),{tag},[who,tag](int failures) {
            if(failures)inform(who,"Game invitation","The invitation to "+tag+" could not be sent.",Icon::Error);
            else notify({Notification::Kind::Invitation,"Invitation sent",tag});
        });
    }
    void findGamer() {
        const auto who=player;
        keyboardPrompt(player,"Find gamer","Gamertag",[who](const std::string& tag){push(gamerCardScreen(who,tag));});
    }
    List list_;
    std::vector<ServiceFriend> friends_;
    std::map<std::string,std::vector<unsigned char>> avatars_;
    bool loaded_=false,failed_=false;
};

// ---- Gamer card ---------------------------------------------------------------------------------
class GamerCardScreen final : public Screen {
public:
    explicit GamerCardScreen(std::string tag):tag_(std::move(tag)){}
    std::string name() const override {return "gamerCard";}
    std::string title() const override {return loaded_?person_.gamertag:tag_;}
    std::string subtitle() const override {return loaded_?relationship():"";}
    Category category() const override {return Category::Friends;}
    std::vector<std::pair<Icon,std::string>> items() const {
        std::vector<std::pair<Icon,std::string>> out;
        if(!loaded_||self_)return out;
        if(friendship_&&friendship_->accepted)out.push_back({Icon::Cross,"Remove friend"});
        else if(friendship_&&friendship_->requestReceived)out.push_back({Icon::PersonAdd,"Accept friend request"});
        else if(friendship_&&friendship_->requestSent)out.push_back({Icon::Cross,"Cancel friend request"});
        else out.push_back({Icon::PersonAdd,"Send friend request"});
        if(activeOnlineSession())out.push_back({Icon::Invite,"Invite to game"});
        out.push_back({Icon::Message,"Send message"});
        out.push_back({Icon::StarOutline,"Review player"});
        return out;
    }
    void input(InputContext& ui) override {
        if(access(player)!=Access::Account)return;
        const int chosen=list_.input(ui,static_cast<int>(items().size()));
        if(chosen>=0)activate(chosen);
    }
    void activate(int index) override {
        const auto list=items();
        if(index<0||index>=static_cast<int>(list.size()))return;
        const auto& label=list[static_cast<std::size_t>(index)].second;
        const auto who=player;
        const auto tag=person_.gamertag;
        if(label=="Invite to game") {
            sendInvitations(userOf(player),{tag},[who,tag](int failures) {
                if(failures)inform(who,"Game invitation","The invitation could not be sent.",Icon::Error);
                else notify({Notification::Kind::Invitation,"Invitation sent",tag});
            });
        } else if(label=="Send message") sendMessageTo(player,{tag});
        else if(label=="Review player") push(reviewScreen(player,tag));
        else {
            const std::string action=label=="Remove friend"||label=="Cancel friend request"?"remove":label=="Accept friend request"?"accept":"add";
            busy_=true;
            load<bool>(std::static_pointer_cast<GamerCardScreen>(shared_from_this()),[user=userOf(player),tag,action](IGamerServicesBackend& s) {
                s.changeFriend(user,tag,action);return true;
            },[](GamerCardScreen& screen,bool){screen.busy_=false;screen.start(std::static_pointer_cast<GamerCardScreen>(screen.shared_from_this()));
                notify({Notification::Kind::FriendRequest,"Friends updated",screen.person_.gamertag});},
              [who](GamerCardScreen& screen){screen.busy_=false;inform(who,"Friends","The change could not be made.",Icon::Error);});
        }
    }
    void draw(Ui& ui,const Box& area) override {
        if(explainAccess(ui,area,access(player)))return;
        if(failed_){emptyState(ui,area,Icon::Search,"Gamer not found","Nobody on CNA Gamer Services has the gamertag \""+tag_+"\".");return;}
        if(!loaded_){loading(ui,area);return;}
        // The avatar stands on the card; the member's facts and what can be done sit beside it.
        const Box stage{area.x,area.y,ui.px(250),area.h};
        portrait(ui,avatar_,stage,Framing::Body,true);
        float x=stage.right()+ui.px(26),y=area.y;
        const float right=area.right();
        if(!person_.motto.empty()) {
            ui.style.text(ui.batch,Font::Body,"\xe2\x80\x9c"+ui.style.fit(Font::Body,person_.motto,right-x-ui.px(20))+"\xe2\x80\x9d",Xna::Vector2(x,y),Palette::muted());
            y+=ui.px(34);
        }
        if(friendship_&&friendship_->accepted) {
            statusDot(ui,Xna::Vector2(x+ui.px(7),y+ui.px(12)),presenceColor(*friendship_));
            ui.style.text(ui.batch,Font::Body,ui.style.fit(Font::Body,presenceLine(*friendship_),right-x-ui.px(24)),Xna::Vector2(x+ui.px(22),y),Palette::text());
            y+=ui.px(36);
        }
        // Facts as tiles.
        struct Fact {Icon icon;std::string value;std::string label;};
        static const char* const zones[]{"Unknown","Recreation","Pro","Family","Underground"};
        std::vector<Fact> facts{{Icon::Trophy,std::to_string(person_.gamerScore),"Gamerscore"},
            {Icon::Check,std::to_string(person_.totalAchievements),"Achievements"},
            {Icon::Controller,std::to_string(person_.titlesPlayed),"Games played"},
            {Icon::Globe,zones[std::clamp(person_.gamerZone,0,4)],"Gamer zone"}};
        const float tileW=(right-x-ui.px(12))/2.0f,tileH=ui.px(58);
        for(std::size_t i=0;i<facts.size();++i) {
            const Box tile{x+static_cast<float>(i%2)*(tileW+ui.px(12)),y+static_cast<float>(i/2)*(tileH+ui.px(10)),tileW,tileH};
            ui.style.rounded(ui.batch,tile,ui.px(12),Palette::surface());
            ui.style.icon(ui.batch,facts[i].icon,Box{tile.x+ui.px(12),tile.y+ui.px(16),ui.px(26),ui.px(26)},i==0?Palette::gold():Palette::accent());
            ui.style.text(ui.batch,Font::BodyBold,facts[i].value,Xna::Vector2(tile.x+ui.px(50),tile.y+ui.px(6)),Palette::text());
            ui.style.text(ui.batch,Font::Caption,facts[i].label,Xna::Vector2(tile.x+ui.px(50),tile.y+ui.px(32)),Palette::muted());
        }
        y+=2*(tileH+ui.px(10));
        // Reputation, in stars from what other players said, when anyone has.
        ui.style.text(ui.batch,Font::Caption,"Reputation",Xna::Vector2(x,y+ui.px(3)),Palette::muted());
        const float stars=person_.reputation.value_or(0.0f);
        const float starsX=x+ui.style.measure(Font::Caption,"Reputation").X+ui.px(14);
        for(int star=0;star<5;++star) {
            const Box s{starsX+static_cast<float>(star)*ui.px(26),y,ui.px(22),ui.px(22)};
            const float fill=std::clamp(stars-static_cast<float>(star),0.0f,1.0f);
            ui.style.icon(ui.batch,fill>=0.5f?Icon::Star:Icon::StarOutline,s,fill>=0.5f?Palette::gold():Palette::faint());
        }
        if(!person_.reputation)ui.style.text(ui.batch,Font::Caption,"Not yet reviewed",Xna::Vector2(starsX+ui.px(140),y+ui.px(3)),Palette::faint());
        y+=ui.px(40);
        const auto list=items();
        if(!list.empty())drawActions(ui,list_,Box{x,y,right-x,area.bottom()-y},list);
        else if(self_)ui.style.text(ui.batch,Font::Body,"This is you.",Xna::Vector2(x,y),Palette::muted());
    }
    std::vector<Hint> hints() const override {
        if(items().empty())return {{Command::Back,"Back"}};
        return {{Command::Accept,"Select"},{Command::Back,"Back"}};
    }
    std::vector<std::string> labels() const override {
        std::vector<std::string> out;
        if(loaded_)out.push_back(person_.gamertag);
        for(const auto& [icon,label]:items())out.push_back(label);
        return out;
    }
    int focus() const override {return list_.focus;}
    void start(const std::shared_ptr<GamerCardScreen>& self) {
        if(access(player)!=Access::Account)return;
        struct Result {ServiceIdentity person;std::vector<unsigned char> avatar;std::optional<ServiceFriend> friendship;};
        load<Result>(self,[user=userOf(player),tag=tag_](IGamerServicesBackend& s) {
            Result result{s.profile(tag),{},{}};
            try{result.avatar=s.avatars({result.person.userId}).at(0).description;}catch(...){}
            for(auto& entry:s.friends(user))if(lower(entry.gamertag)==lower(result.person.gamertag))result.friendship=entry;
            return result;
        },[user=userOf(player)](GamerCardScreen& screen,Result result) {
            screen.self_=result.person.userId==user;
            screen.person_=std::move(result.person);
            screen.avatar_=std::move(result.avatar);
            screen.friendship_=std::move(result.friendship);
            screen.loaded_=true;
            if(!screen.self_)rememberRecentPlayer(screen.person_.gamertag);
        },[](GamerCardScreen& screen){screen.failed_=true;});
    }
private:
    std::string relationship() const {
        if(self_)return "Your gamer card";
        if(!friendship_)return "Not on your friends list";
        if(friendship_->accepted)return "On your friends list";
        if(friendship_->requestReceived)return "Wants to be your friend";
        return "Friend request sent";
    }
    std::string tag_;
    ServiceIdentity person_;
    std::vector<unsigned char> avatar_;
    std::optional<ServiceFriend> friendship_;
    List list_;
    bool loaded_=false,failed_=false,self_=false,busy_=false;
};

// ---- Messages -----------------------------------------------------------------------------------
class MessageScreen final : public Screen {
public:
    explicit MessageScreen(ServiceMessage message):message_(std::move(message)){}
    std::string name() const override {return "message";}
    std::string title() const override {return message_.sender;}
    std::string subtitle() const override {return "Message";}
    Category category() const override {return Category::Messages;}
    void input(InputContext& ui) override {
        const int chosen=list_.input(ui,3);
        if(chosen>=0)activate(chosen);
    }
    void activate(int index) override {
        const auto user=userOf(player);
        if(index==0){sendMessageTo(player,{message_.sender});}
        else if(index==1) {
            auto service=backend();auto* executor=service.get();
            service->submit([executor,user,id=message_.id]{try{executor->updateMessage(user,id,true);}catch(...){}},[]{});
            pop(this);
        } else if(index==2) push(gamerCardScreen(player,message_.sender));
    }
    void draw(Ui& ui,const Box& area) override {
        const Box bubble{area.x,area.y,area.w,std::min(area.h*0.55f,ui.px(220))};
        ui.style.rounded(ui.batch,bubble,ui.px(16),Palette::surface());
        float y=bubble.y+ui.px(18);
        for(const auto& line:ui.style.wrap(Font::Body,message_.text,bubble.w-ui.px(40))) {
            if(y>bubble.bottom()-ui.px(30))break;
            ui.style.text(ui.batch,Font::Body,line,Xna::Vector2(bubble.x+ui.px(20),y),Palette::text());
            y+=ui.style.measure(Font::Body,"Ag").Y;
        }
        drawActions(ui,list_,Box{area.x,bubble.bottom()+ui.px(18),area.w*0.6f,area.bottom()-bubble.bottom()-ui.px(18)},
            {{Icon::Message,"Reply"},{Icon::Cross,"Delete"},{Icon::Person,"View gamer card"}});
    }
    std::vector<std::string> labels() const override {return {message_.sender,message_.text,"Reply","Delete","View gamer card"};}
    int focus() const override {return list_.focus;}
private:
    ServiceMessage message_;
    List list_;
};

class MessagesScreen final : public Screen {
public:
    std::string name() const override {return "messages";}
    std::string title() const override {return "Messages";}
    std::string subtitle() const override {
        if(!loaded_)return "";
        int unread=0;for(const auto& m:messages_)unread+=!m.read;
        return unread?std::to_string(unread)+" unread":std::to_string(messages_.size())+(messages_.size()==1?" message":" messages");
    }
    Category category() const override {return Category::Messages;}
    void input(InputContext& ui) override {
        if(access(player)!=Access::Account)return;
        const int chosen=list_.input(ui,static_cast<int>(messages_.size()));
        if(chosen>=0)activate(chosen);
        if(ui.input(Command::Y)) {
            const auto who=player;
            keyboardPrompt(player,"New message","Recipient gamertag",[who](const std::string& tag){sendMessageTo(who,{tag});});
        }
    }
    void activate(int index) override {
        if(index<0||index>=static_cast<int>(messages_.size()))return;
        auto& message=messages_[static_cast<std::size_t>(index)];
        if(!message.read) {
            message.read=true;
            auto service=backend();auto* executor=service.get();
            service->submit([executor,user=userOf(player),id=message.id]{try{executor->updateMessage(user,id,false);}catch(...){}},[]{});
            auto& who=identity(player);
            who.unread=std::max(0,who.unread-1);
        }
        push(std::make_shared<MessageScreen>(message));
    }
    void draw(Ui& ui,const Box& area) override {
        if(explainAccess(ui,area,access(player)))return;
        if(failed_){emptyState(ui,area,Icon::Warning,"Messages could not be read","Check the connection to CNA Gamer Services.");return;}
        if(!loaded_){loading(ui,area);return;}
        if(messages_.empty()){emptyState(ui,area,Icon::Message,"No messages","Messages from other players appear here.");return;}
        list_.draw(ui,area,static_cast<int>(messages_.size()),76,[&](int index,const Box& row,bool focused) {
            const auto& message=messages_[static_cast<std::size_t>(index)];
            rowBackground(ui,row,focused);
            const Box tile{row.x+ui.px(12),row.y+ui.px(12),row.h-ui.px(24),row.h-ui.px(24)};
            ui.style.rounded(ui.batch,tile,ui.px(10),Palette::rail());
            ui.style.icon(ui.batch,Icon::Message,tile.inset(ui.px(10)),message.read?Palette::faint():Palette::accent());
            if(!message.read)ui.style.disc(ui.batch,Xna::Vector2(tile.right()-ui.px(2),tile.y+ui.px(2)),ui.px(6),Palette::accent());
            const float tx=tile.right()+ui.px(16);
            ui.style.text(ui.batch,message.read?Font::Body:Font::BodyBold,message.sender,Xna::Vector2(tx,row.y+ui.px(10)),Palette::text());
            ui.style.text(ui.batch,Font::Caption,ui.style.fit(Font::Caption,message.text,row.right()-tx-ui.px(20)),Xna::Vector2(tx,row.y+ui.px(40)),Palette::muted());
        });
    }
    std::vector<Hint> hints() const override {return {{Command::Accept,"Read"},{Command::Y,"New message"},{Command::Back,"Back"}};}
    std::vector<std::string> labels() const override {std::vector<std::string> out;for(const auto& m:messages_)out.push_back(m.sender+": "+m.text);return out;}
    int focus() const override {return list_.focus;}
    void start(const std::shared_ptr<MessagesScreen>& self) {
        if(access(player)!=Access::Account)return;
        load<ServiceMessagePage>(self,[user=userOf(player)](IGamerServicesBackend& s){return s.messages(user,0,50);},
            [](MessagesScreen& screen,ServiceMessagePage page){screen.messages_=std::move(page.messages);screen.loaded_=true;},
            [](MessagesScreen& screen){screen.failed_=true;});
    }
private:
    List list_;
    std::vector<ServiceMessage> messages_;
    bool loaded_=false,failed_=false;
};

// ---- Achievements -------------------------------------------------------------------------------
struct AchievementRow {std::string name,description,howToEarn;int score=0;bool earned=false;long long earnedTicks=0;bool secret=false;};

class AchievementsScreen final : public Screen {
public:
    std::string name() const override {return "achievements";}
    std::string title() const override {return "Achievements";}
    std::string subtitle() const override {
        if(!loaded_)return "";
        int earned=0,score=0,total=0;
        for(const auto& a:rows_){earned+=a.earned;total+=a.score;score+=a.earned?a.score:0;}
        return std::to_string(earned)+" of "+std::to_string(rows_.size())+" unlocked \xe2\x80\xa2 "+std::to_string(score)+" of "+std::to_string(total)+" points";
    }
    Category category() const override {return Category::Achievements;}
    void input(InputContext& ui) override {(void)list_.input(ui,static_cast<int>(rows_.size()),2);}
    void draw(Ui& ui,const Box& area) override {
        const auto a=access(player);
        if(a==Access::Nobody||a==Access::Guest){explainAccess(ui,area,a);return;}
        if(failed_){emptyState(ui,area,Icon::Warning,"Achievements could not be read","Check the connection to CNA Gamer Services.");return;}
        if(!loaded_){loading(ui,area);return;}
        if(rows_.empty()){emptyState(ui,area,Icon::Trophy,"No achievements","This game has no achievements.");return;}
        list_.draw(ui,area,static_cast<int>(rows_.size()),104,[&](int index,const Box& tile,bool focused) {
            const auto& row=rows_[static_cast<std::size_t>(index)];
            rowBackground(ui,tile,focused);
            const Box badge{tile.x+ui.px(12),tile.y+ui.px(12),tile.h-ui.px(24),tile.h-ui.px(24)};
            ui.style.rounded(ui.batch,badge,ui.px(14),row.earned?Xna::Color(96,72,22):Palette::rail());
            ui.style.icon(ui.batch,row.earned?Icon::Trophy:Icon::Lock,badge.inset(ui.px(14)),row.earned?Palette::gold():Palette::faint());
            const float tx=badge.right()+ui.px(14),w=tile.right()-tx-ui.px(14);
            const bool hidden=row.secret&&!row.earned;
            ui.style.text(ui.batch,Font::BodyBold,ui.style.fit(Font::BodyBold,hidden?"Secret achievement":row.name,w-ui.px(56)),Xna::Vector2(tx,tile.y+ui.px(10)),
                row.earned?Palette::text():Palette::muted());
            const auto score=std::to_string(row.score);
            ui.style.text(ui.batch,Font::BodyBold,score,Xna::Vector2(tile.right()-ui.px(16),tile.y+ui.px(10)),row.earned?Palette::gold():Palette::faint(),Align::Right);
            const auto text=hidden?"Keep playing to discover it.":row.earned?row.description:(row.howToEarn.empty()?row.description:row.howToEarn);
            float y=tile.y+ui.px(40);
            for(const auto& line:ui.style.wrap(Font::Caption,text,w)) {
                if(y>tile.bottom()-ui.px(22))break;
                ui.style.text(ui.batch,Font::Caption,line,Xna::Vector2(tx,y),Palette::muted());
                y+=ui.style.measure(Font::Caption,"Ag").Y;
            }
        },2);
    }
    std::vector<std::string> labels() const override {
        std::vector<std::string> out;
        for(const auto& row:rows_)out.push_back((row.earned?"[x] ":"[ ] ")+row.name);
        return out;
    }
    int focus() const override {return list_.focus;}
    void start(const std::shared_ptr<AchievementsScreen>& self) {
        const auto a=access(player);
        if(a==Access::Account) {
            load<std::vector<ServiceAchievement>>(self,[user=userOf(player)](IGamerServicesBackend& s){return s.achievements(user);},
                [](AchievementsScreen& screen,std::vector<ServiceAchievement> list) {
                    for(auto& item:list)
                        screen.rows_.push_back({item.name,item.description,item.howToEarn,item.score,item.earnedTicks!=0,item.earnedTicks,!item.displayBeforeEarned});
                    screen.loaded_=true;
                },[](AchievementsScreen& screen){screen.failed_=true;});
        } else if(a==Access::LocalProfile) {
            // An offline profile keeps its achievements on this computer.
            try {
                auto* gamer=signedIn(player);
                auto achievements=gamer->GetAchievements();
                for(const auto& item:achievements)
                    rows_.push_back({item.getNameProperty(),item.getDescriptionProperty(),item.getHowToEarnProperty(),item.getGamerScoreProperty(),
                        item.getIsEarnedProperty(),0,!item.getDisplayBeforeEarnedProperty()});
                loaded_=true;
            } catch(...) {failed_=true;}
        }
    }
private:
    List list_;
    std::vector<AchievementRow> rows_;
    bool loaded_=false,failed_=false;
};

// ---- Recent players -----------------------------------------------------------------------------
class PlayersScreen final : public Screen {
public:
    std::string name() const override {return "players";}
    std::string title() const override {return "Recent players";}
    std::string subtitle() const override {return players_.empty()?"":"People you played with lately";}
    Category category() const override {return Category::Players;}
    void input(InputContext& ui) override {
        const int chosen=list_.input(ui,static_cast<int>(players_.size()));
        if(chosen>=0)activate(chosen);
    }
    void activate(int index) override {
        if(index>=0&&index<static_cast<int>(players_.size()))push(gamerCardScreen(player,players_[static_cast<std::size_t>(index)]));
    }
    void draw(Ui& ui,const Box& area) override {
        if(explainAccess(ui,area,access(player)))return;
        if(players_.empty()){emptyState(ui,area,Icon::Clock,"Nobody yet","Players you meet in online games appear here.");return;}
        list_.draw(ui,area,static_cast<int>(players_.size()),76,[&](int index,const Box& row,bool focused) {
            const auto& tag=players_[static_cast<std::size_t>(index)];
            rowBackground(ui,row,focused);
            const Box face{row.x+ui.px(10),row.y+ui.px(7),row.h-ui.px(14),row.h-ui.px(14)};
            const auto avatar=avatars_.find(tag);
            portrait(ui,avatar==avatars_.end()?std::vector<unsigned char>{}:avatar->second,face);
            const float h=ui.style.measure(Font::BodyBold,"Ag").Y;
            ui.style.text(ui.batch,Font::BodyBold,tag,Xna::Vector2(face.right()+ui.px(16),row.y+(row.h-h)/2),Palette::text());
        });
    }
    std::vector<Hint> hints() const override {return {{Command::Accept,"Gamer card"},{Command::Back,"Back"}};}
    std::vector<std::string> labels() const override {return players_;}
    int focus() const override {return list_.focus;}
    void start(const std::shared_ptr<PlayersScreen>& self) {
        players_=recentPlayers();
        if(access(player)!=Access::Account||players_.empty())return;
        load<std::map<std::string,std::vector<unsigned char>>>(self,[tags=players_](IGamerServicesBackend& s) {
            std::map<std::string,std::string> ids;
            for(const auto& tag:tags)try{ids[s.profile(tag).userId]=tag;}catch(...){}
            std::vector<std::string> list;for(const auto& [id,tag]:ids)list.push_back(id);
            std::map<std::string,std::vector<unsigned char>> byTag;
            for(auto& [id,avatar]:readAvatars(s,list))byTag[ids[id]]=std::move(avatar);
            return byTag;
        },[](PlayersScreen& screen,std::map<std::string,std::vector<unsigned char>> avatars){screen.avatars_=std::move(avatars);});
    }
private:
    List list_;
    std::vector<std::string> players_;
    std::map<std::string,std::vector<unsigned char>> avatars_;
};

// ---- Player review (dialog) -----------------------------------------------------------------------
class ReviewScreen final : public Screen {
public:
    explicit ReviewScreen(std::string tag):tag_(std::move(tag)){}
    std::string name() const override {return "review";}
    std::string title() const override {return "Review "+tag_;}
    float dialogWidth() const override {return 560;}
    float dialogHeight() const override {return 350;}
    void input(InputContext& ui) override {
        if(ui.input(Command::Back)){pop(this);return;}
        const int chosen=list_.input(ui,3);
        if(chosen>=0)activate(chosen);
    }
    void activate(int index) override {
        static const char* const ratings[]{"prefer","avoid","clear"};
        if(index<0||index>2)return;
        const auto who=player;
        const std::string rating=ratings[index];
        auto service=backend();auto* executor=service.get();auto failed=std::make_shared<bool>(false);
        service->submit([executor,user=userOf(player),tag=tag_,rating,failed]{try{executor->reviewPlayer(user,tag,rating);}catch(...){*failed=true;}},
            [who,failed]{if(*failed)inform(who,"Player review","The review could not be recorded.",Icon::Error);
                         else notify({Notification::Kind::Info,"Review recorded",""});});
        pop(this);
    }
    void draw(Ui& ui,const Box& area) override {
        ui.style.text(ui.batch,Font::Body,"How was playing with "+tag_+"?",Xna::Vector2(area.x,area.y),Palette::muted());
        drawActions(ui,list_,Box{area.x,area.y+ui.px(40),area.w,area.h-ui.px(40)},
            {{Icon::Star,"Prefer: I would play with them again"},{Icon::Cross,"Avoid: do not match us again"},{Icon::StarOutline,"Clear my review"}});
    }
    std::vector<Hint> hints() const override {return {{Command::Accept,"Select"},{Command::Back,"Cancel"}};}
    std::vector<std::string> labels() const override {return {"Prefer","Avoid","Clear"};}
    int focus() const override {return list_.focus;}
private:
    std::string tag_;
    List list_;
};

// ---- Invite friends -----------------------------------------------------------------------------
class InviteScreen final : public Screen {
public:
    explicit InviteScreen(std::vector<std::string> chosen):chosen_(chosen.begin(),chosen.end()){}
    std::string name() const override {return "invite";}
    std::string title() const override {return "Invite to game";}
    std::string subtitle() const override {return chosen_.empty()?"Choose friends to invite":std::to_string(chosen_.size())+" chosen";}
    Category category() const override {return Category::Friends;}
    void input(InputContext& ui) override {
        const int chosen=list_.input(ui,static_cast<int>(friends_.size()));
        if(chosen>=0)activate(chosen);
        if(ui.input(Command::X)) {
            // Anyone can be invited by gamertag, not only friends.
            auto self=std::static_pointer_cast<InviteScreen>(shared_from_this());
            keyboardPrompt(player,"Invite a gamer","Gamertag",[weak=std::weak_ptr<InviteScreen>(self)](const std::string& tag) {
                if(auto screen=weak.lock())screen->chosen_.insert(tag);
            });
        }
        if(ui.input(Command::Y)&&!chosen_.empty())send();
    }
    void activate(int index) override {
        if(index<0||index>=static_cast<int>(friends_.size()))return;
        const auto& tag=friends_[static_cast<std::size_t>(index)].gamertag;
        if(!chosen_.erase(tag))chosen_.insert(tag);
    }
    void draw(Ui& ui,const Box& area) override {
        if(explainAccess(ui,area,access(player)))return;
        if(!activeOnlineSession()){emptyState(ui,area,Icon::Controller,"No online game","Invitations need an online game in progress.");return;}
        if(!loaded_){loading(ui,area);return;}
        // Chosen gamers who are not friends (added by gamertag) are listed above the friends.
        std::string extra;
        for(const auto& tag:chosen_)
            if(std::ranges::none_of(friends_,[&](const auto& entry){return entry.gamertag==tag;}))extra+=(extra.empty()?"":", ")+tag;
        Box list=area;
        if(!extra.empty()) {
            ui.style.text(ui.batch,Font::Body,ui.style.fit(Font::Body,"Also inviting: "+extra,area.w),Xna::Vector2(area.x,area.y),Palette::accent());
            list.y+=ui.px(38);list.h-=ui.px(38);
        }
        if(friends_.empty()){emptyState(ui,list,Icon::People,"No friends to invite","Invite anyone by gamertag with X.");return;}
        list_.draw(ui,list,static_cast<int>(friends_.size()),64,[&](int index,const Box& row,bool focused) {
            const auto& entry=friends_[static_cast<std::size_t>(index)];
            rowBackground(ui,row,focused);
            const bool on=chosen_.contains(entry.gamertag);
            const Box check{row.x+ui.px(14),row.y+(row.h-ui.px(28))/2,ui.px(28),ui.px(28)};
            ui.style.rounded(ui.batch,check,ui.px(8),on?Palette::accent():Palette::rail());
            if(on)ui.style.icon(ui.batch,Icon::Check,check.inset(ui.px(4)),Palette::panel());
            statusDot(ui,Xna::Vector2(check.right()+ui.px(22),row.y+row.h/2),presenceColor(entry));
            const float h=ui.style.measure(Font::BodyBold,"Ag").Y;
            ui.style.text(ui.batch,Font::BodyBold,entry.gamertag,Xna::Vector2(check.right()+ui.px(38),row.y+(row.h-h)/2),Palette::text());
            ui.style.text(ui.batch,Font::Caption,presenceLine(entry),Xna::Vector2(row.right()-ui.px(16),row.y+(row.h-h)/2+ui.px(3)),Palette::muted(),Align::Right);
        });
    }
    std::vector<Hint> hints() const override {return {{Command::Accept,"Choose"},{Command::X,"Add gamertag"},{Command::Y,"Send"},{Command::Back,"Cancel"}};}
    std::vector<std::string> labels() const override {
        std::vector<std::string> out;
        for(const auto& entry:friends_)out.push_back((chosen_.contains(entry.gamertag)?"[x] ":"[ ] ")+entry.gamertag);
        for(const auto& tag:chosen_)
            if(std::ranges::none_of(friends_,[&](const auto& entry){return entry.gamertag==tag;}))out.push_back("[x] "+tag);
        return out;
    }
    int focus() const override {return list_.focus;}
    void start(const std::shared_ptr<InviteScreen>& self) {
        if(access(player)!=Access::Account)return;
        load<std::vector<ServiceFriend>>(self,[user=userOf(player)](IGamerServicesBackend& s){return s.friends(user);},
            [](InviteScreen& screen,std::vector<ServiceFriend> list) {
                for(auto& entry:list)if(entry.accepted)screen.friends_.push_back(std::move(entry));
                std::ranges::stable_sort(screen.friends_,[](const auto& a,const auto& b){return a.online>b.online;});
                screen.loaded_=true;
            });
    }
    void send() {
        const auto who=player;
        std::vector<std::string> tags(chosen_.begin(),chosen_.end());
        const auto count=tags.size();
        sendInvitations(userOf(player),tags,[who,count](int failures) {
            if(failures)inform(who,"Game invitation",failures==static_cast<int>(count)?"The invitations could not be sent.":"Some invitations could not be sent.",Icon::Error);
            else notify({Notification::Kind::Invitation,count==1?"Invitation sent":std::to_string(count)+" invitations sent",""});
        });
        pop(this);
    }
private:
    std::set<std::string> chosen_;
    std::vector<ServiceFriend> friends_;
    List list_;
    bool loaded_=false;
};

// ---- Settings -----------------------------------------------------------------------------------
class SettingsScreen final : public Screen {
public:
    std::string name() const override {return "settings";}
    std::string title() const override {return "Settings";}
    Category category() const override {return Category::Settings;}
    void input(InputContext& ui) override {
        const int count=access(player)==Access::Account?3:1;
        const int chosen=list_.input(ui,count);
        const int row=list_.focus;
        if(count==3&&row<2&&(ui.input(Command::Left)||ui.input(Command::Right)))change(row,ui.input(Command::Right)?1:-1);
        if(chosen>=0)activate(chosen);
    }
    void activate(int index) override {
        if(access(player)!=Access::Account)index=2;
        if(index<2){change(index,1);return;}
        auto* gamer=signedIn(player);
        const int slot=static_cast<int>(player);
        closeAll();
        if(gamer&&gamer->getIsGuestProperty())backend()->signOutGuest(slot);
        else if(gamer)backend()->signOut(slot);
    }
    void draw(Ui& ui,const Box& area) override {
        static const char* const statuses[]{"Online","Away","Busy"};
        static const char* const zones[]{"Recreation","Pro","Family","Underground"};
        std::vector<std::pair<std::string,std::string>> rows;
        if(access(player)==Access::Account) {
            rows.push_back({"Online status",statuses[status_]});
            rows.push_back({"Gamer zone",zone_<0?"Not chosen":zones[zone_]});
        }
        rows.push_back({"Sign out",""});
        list_.draw(ui,area,static_cast<int>(rows.size()),64,[&](int index,const Box& row,bool focused) {
            rowBackground(ui,row,focused);
            const float h=ui.style.measure(Font::BodyBold,"Ag").Y;
            ui.style.text(ui.batch,Font::BodyBold,rows[static_cast<std::size_t>(index)].first,Xna::Vector2(row.x+ui.px(22),row.y+(row.h-h)/2),Palette::text());
            const auto& value=rows[static_cast<std::size_t>(index)].second;
            if(!value.empty()) {
                const float right=row.right()-ui.px(22);
                ui.style.icon(ui.batch,Icon::ChevronRight,Box{right-ui.px(18),row.y+(row.h-ui.px(18))/2,ui.px(18),ui.px(18)},Palette::accent());
                ui.style.text(ui.batch,Font::Body,value,Xna::Vector2(right-ui.px(28),row.y+(row.h-h)/2),Palette::text(),Align::Right);
                ui.style.icon(ui.batch,Icon::ChevronLeft,Box{right-ui.px(46)-ui.style.measure(Font::Body,value).X,row.y+(row.h-ui.px(18))/2,ui.px(18),ui.px(18)},Palette::accent());
            }
        });
        ui.style.text(ui.batch,Font::Caption,"Friends see your online status. Your gamer zone is the style of play you prefer.",
            Xna::Vector2(area.x,area.bottom()-ui.px(24)),Palette::faint());
    }
    std::vector<Hint> hints() const override {return {{Command::Accept,"Change"},{Command::Back,"Back"}};}
    std::vector<std::string> labels() const override {return {"Online status","Gamer zone","Sign out"};}
    int focus() const override {return list_.focus;}
    void start() {
        const auto& who=identity(player);
        status_=who.status=="away"?1:who.status=="busy"?2:0;
    }
private:
    void change(int row,int step) {
        static const char* const statuses[]{"online","away","busy"};
        static const char* const zones[]{"recreation","pro","family","underground"};
        const auto user=userOf(player);
        const auto who=player;
        auto service=backend();auto* executor=service.get();auto failed=std::make_shared<bool>(false);
        if(row==0) {
            status_=(status_+step+3)%3;
            const std::string value=statuses[status_];
            identity(player).status=value;
            service->submit([executor,user,value,failed]{try{executor->setPresenceStatus(user,value);}catch(...){*failed=true;}},
                [who,failed]{if(*failed)inform(who,"Online status","The online status could not be changed.",Icon::Error);});
        } else {
            zone_=((zone_<0?-1:zone_)+step+4)%4;
            const std::string value=zones[zone_];
            service->submit([executor,user,value,failed]{try{executor->setGamerZone(user,value);}catch(...){*failed=true;}},
                [who,failed]{if(*failed)inform(who,"Gamer zone","The gamer zone could not be changed.",Icon::Error);});
        }
    }
    List list_;
    int status_=0,zone_=-1;
};

// ---- Party and game content (the services behind them are added separately) --------------------
class PartyScreen final : public Screen {
public:
    std::string name() const override {return "party";}
    std::string title() const override {return "Party";}
    Category category() const override {return Category::Party;}
    void draw(Ui& ui,const Box& area) override {
        if(explainAccess(ui,area,access(player)))return;
        emptyState(ui,area,Icon::Party,"Parties are not available","This CNA Gamer Services server offers no parties. Invite friends to your game from their gamer cards.");
    }
    std::vector<std::string> labels() const override {return {"Parties are not available"};}
};

class ContentScreen final : public Screen {
public:
    std::string name() const override {return "content";}
    std::string title() const override {return "Game content";}
    std::string subtitle() const override {return "What this game has installed";}
    Category category() const override {return Category::Content;}
    void input(InputContext& ui) override {(void)list_.input(ui,static_cast<int>(rows_.size()));}
    void draw(Ui& ui,const Box& area) override {
        list_.draw(ui,area,static_cast<int>(rows_.size()),72,[&](int index,const Box& row,bool focused) {
            const auto& [title,detail]=rows_[static_cast<std::size_t>(index)];
            rowBackground(ui,row,focused);
            ui.style.icon(ui.batch,detail.starts_with("Part of")?Icon::Store:Icon::Download,Box{row.x+ui.px(18),row.y+(row.h-ui.px(28))/2,ui.px(28),ui.px(28)},Palette::accent());
            ui.style.text(ui.batch,Font::BodyBold,title,Xna::Vector2(row.x+ui.px(62),row.y+ui.px(10)),Palette::text());
            ui.style.text(ui.batch,Font::Caption,detail,Xna::Vector2(row.x+ui.px(62),row.y+ui.px(40)),Palette::muted());
        });
    }
    std::vector<std::string> labels() const override {std::vector<std::string> out;for(const auto& [title,detail]:rows_)out.push_back(title);return out;}
    int focus() const override {return list_.focus;}
    void start() {
        for(auto version:Avatars::availableCatalogVersions()) {
            const bool builtIn=Avatars::embeddedManifest(version)!=nullptr;
            rows_.push_back({"Avatar catalog "+std::to_string(version),builtIn?"Part of this CNA release":"Installed catalog update"});
        }
    }
private:
    List list_;
    std::vector<std::pair<std::string,std::string>> rows_;
};

// ---- A received invitation (dialog) -------------------------------------------------------------
class InvitationScreen final : public Screen {
public:
    InvitationScreen(std::string sender,std::string senderId,std::string detail,std::function<void(std::optional<bool>)> answer)
        : sender_(std::move(sender)),senderId_(std::move(senderId)),detail_(std::move(detail)),answer_(std::move(answer)){}
    ~InvitationScreen() override {
        // However the card went away (Later, the Guide closed), the invitation is answered once.
        if(auto answer=std::exchange(answer_,nullptr))try{answer(std::nullopt);}catch(...){}
    }
    std::string name() const override {return "invitation";}
    std::string title() const override {return "Game invitation";}
    float dialogWidth() const override {return 640;}
    float dialogHeight() const override {return 440;}
    void input(InputContext& ui) override {
        if(ui.input(Command::Back)){pop(this);return;}  // closed unanswered: it stays in the inbox
        const int chosen=list_.input(ui,3);
        if(chosen>=0)activate(chosen);
    }
    void activate(int index) override {
        if(index==2){push(gamerCardScreen(player,sender_));return;}
        if(index<0||index>1)return;
        auto answer=std::exchange(answer_,nullptr);
        pop(this);
        if(answer)answer(index==0);
    }
    void draw(Ui& ui,const Box& area) override {
        const Box face{area.x,area.y,ui.px(96),ui.px(96)};
        portrait(ui,avatar_,face);
        const float tx=face.right()+ui.px(20);
        ui.style.text(ui.batch,Font::Heading,sender_,Xna::Vector2(tx,area.y),Palette::text());
        ui.style.text(ui.batch,Font::Body,"invites you to play",Xna::Vector2(tx,area.y+ui.px(36)),Palette::muted());
        ui.style.text(ui.batch,Font::Caption,ui.style.fit(Font::Caption,detail_,area.right()-tx),Xna::Vector2(tx,area.y+ui.px(66)),Palette::faint());
        drawActions(ui,list_,Box{area.x,face.bottom()+ui.px(18),area.w,area.bottom()-face.bottom()-ui.px(18)},
            {{Icon::Check,"Accept"},{Icon::Cross,"Decline"},{Icon::Person,"View gamer card"}});
    }
    std::vector<Hint> hints() const override {return {{Command::Accept,"Select"},{Command::Back,"Later"}};}
    std::vector<std::string> labels() const override {return {sender_,"Accept","Decline","View gamer card"};}
    int focus() const override {return list_.focus;}
    void start(const std::shared_ptr<InvitationScreen>& self) {
        if(senderId_.empty())return;
        load<std::vector<unsigned char>>(self,[id=senderId_](IGamerServicesBackend& s){return s.avatars({id}).at(0).description;},
            [](InvitationScreen& screen,std::vector<unsigned char> avatar){screen.avatar_=std::move(avatar);});
    }
private:
    std::string sender_,senderId_,detail_;
    std::function<void(std::optional<bool>)> answer_;
    std::vector<unsigned char> avatar_;
    List list_;
};

template<typename S,typename... A>
std::shared_ptr<S> make(Xna::PlayerIndex player,A&&... arguments)
{
    auto screen=std::make_shared<S>(std::forward<A>(arguments)...);
    screen->player=player;
    return screen;
}
}

std::shared_ptr<Screen> homeScreen(Xna::PlayerIndex player){auto s=make<HomeScreen>(player);s->start(s);return s;}
std::shared_ptr<Screen> friendsScreen(Xna::PlayerIndex player){auto s=make<FriendsScreen>(player);s->start(s);return s;}
std::shared_ptr<Screen> gamerCardScreen(Xna::PlayerIndex player,std::string tag){auto s=make<GamerCardScreen>(player,std::move(tag));s->start(s);return s;}
std::shared_ptr<Screen> messagesScreen(Xna::PlayerIndex player){auto s=make<MessagesScreen>(player);s->start(s);return s;}
std::shared_ptr<Screen> achievementsScreen(Xna::PlayerIndex player){auto s=make<AchievementsScreen>(player);s->start(s);return s;}
std::shared_ptr<Screen> playersScreen(Xna::PlayerIndex player){auto s=make<PlayersScreen>(player);s->start(s);return s;}
std::shared_ptr<Screen> reviewScreen(Xna::PlayerIndex player,std::string tag){return make<ReviewScreen>(player,std::move(tag));}
std::shared_ptr<Screen> inviteScreen(Xna::PlayerIndex player,std::vector<std::string> chosen){auto s=make<InviteScreen>(player,std::move(chosen));s->start(s);return s;}
std::shared_ptr<Screen> settingsScreen(Xna::PlayerIndex player){auto s=make<SettingsScreen>(player);s->start();return s;}
std::shared_ptr<Screen> partyScreen(Xna::PlayerIndex player){return make<PartyScreen>(player);}
std::shared_ptr<Screen> contentScreen(Xna::PlayerIndex player){auto s=make<ContentScreen>(player);s->start();return s;}
std::shared_ptr<Screen> invitationScreen(Xna::PlayerIndex player,std::string sender,std::string senderId,std::string detail,std::function<void(std::optional<bool>)> answer)
{
    auto s=make<InvitationScreen>(player,std::move(sender),std::move(senderId),std::move(detail),std::move(answer));
    s->start(s);
    return s;
}

void refreshIdentity(Xna::PlayerIndex player)
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
            try{value.avatar=executor->avatars({user}).at(0).description;}catch(...){}
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
}
