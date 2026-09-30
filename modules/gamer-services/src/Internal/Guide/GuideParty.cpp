// SPDX-License-Identifier: MS-PL
// The party pages: who is in the player's party and what they are doing, party invitations to
// answer, inviting friends and leaving (Guide.ShowParty); and the games party members can be
// joined in (Guide.ShowPartySessions, which XNA shows as the Friends page without a party).
#include "GuideScreen.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include <algorithm>
#include <map>

namespace CNA::Internal::GamerServices::GuideUi {
namespace {
Xna::Color presence(const ServicePartyMember& member)
{
    return !member.online?Palette::offline():member.busy?Palette::busy():member.away?Palette::away():Palette::online();
}

std::string presenceLine(const ServicePartyMember& member)
{
    if(!member.online)return "Offline";
    std::string line=member.busy?"Busy":member.away?"Away":"Online";
    if(!member.presence.empty())line+=" \xe2\x80\xa2 "+member.presence;
    return line;
}

// Common to both pages: the party, read and changed through the service, with members' avatars.
class PartyBase : public Screen {
public:
    Category category() const override {return Category::Party;}
    bool busy() const override {return access(player)==Access::Account&&!loaded_&&!failed_;}
    void start(const std::shared_ptr<PartyBase>& self) {
        self_=self;
        if(access(player)!=Access::Account)return;
        if(auto known=knownParty(userOf(player))){take(std::move(*known));}
        refresh();
    }

protected:
    void refresh() {
        load<ServiceParty>(self_.lock(),[user=userOf(player)](IGamerServicesBackend& s){return s.party(user);},
            [](PartyBase& screen,ServiceParty party){applyParty(userOf(screen.player),party);screen.take(std::move(party));},
            [](PartyBase& screen){if(!screen.loaded_)screen.failed_=true;});
    }
    void change(const std::string& action,const std::string& argument,std::string done) {
        const auto who=player;
        load<ServiceParty>(self_.lock(),[user=userOf(player),action,argument](IGamerServicesBackend& s){return s.changeParty(user,action,argument);},
            [done=std::move(done)](PartyBase& screen,ServiceParty party) {
                applyParty(userOf(screen.player),party);
                screen.take(std::move(party));
                if(!done.empty())notify({Notification::Kind::Party,done,""});
            },
            [who,action](PartyBase&) {
                inform(who,"Party",action=="invite"?"The invitation could not be sent. Only friends can be invited, and a party holds eight.":
                    "The party could not be changed. Check the connection to CNA Gamer Services.",Icon::Error);
            });
    }
    void take(ServiceParty party) {
        party_=std::move(party);
        loaded_=true;
        failed_=false;
        std::vector<std::string> ids;
        for(const auto& member:party_.members)if(!avatars_.contains(member.userId))ids.push_back(member.userId);
        for(const auto& invitation:party_.invitations)if(!avatars_.contains(invitation.senderId))ids.push_back(invitation.senderId);
        if(ids.empty())return;
        load<std::vector<ServiceAvatarRecord>>(self_.lock(),[ids](IGamerServicesBackend& s){return s.avatars(ids);},
            [ids](PartyBase& screen,std::vector<ServiceAvatarRecord> records) {
                for(std::size_t i=0;i<ids.size()&&i<records.size();++i)screen.avatars_[ids[i]]=std::move(records[i].description);
            });
    }
    std::vector<unsigned char> avatar(const std::string& id) const {
        const auto found=avatars_.find(id);
        return found==avatars_.end()?std::vector<unsigned char>{}:found->second;
    }
    void memberRow(Ui& ui,const Box& row,bool focused,const ServicePartyMember& member) {
        rowBackground(ui,row,focused);
        const Box face{row.x+ui.px(10),row.y+ui.px(7),row.h-ui.px(14),row.h-ui.px(14)};
        portrait(ui,avatar(member.userId),face);
        statusDot(ui,Xna::Vector2(face.right()-ui.px(4),face.bottom()-ui.px(4)),presence(member));
        const float tx=face.right()+ui.px(16);
        ui.style.text(ui.batch,Font::BodyBold,member.gamertag,Xna::Vector2(tx,row.y+ui.px(10)),Palette::text());
        if(member.userId==party_.leaderId) {
            const float w=ui.style.measure(Font::BodyBold,member.gamertag).X;
            ui.style.icon(ui.batch,Icon::Crown,Box{tx+w+ui.px(8),row.y+ui.px(12),ui.px(20),ui.px(20)},Palette::gold());
        }
        ui.style.text(ui.batch,Font::Caption,ui.style.fit(Font::Caption,presenceLine(member),row.right()-tx-ui.px(150)),
            Xna::Vector2(tx,row.y+ui.px(40)),member.online?Palette::muted():Palette::faint());
        if(member.joinable) {
            const std::string label="Joinable";
            const float w=ui.style.measure(Font::Caption,label).X+ui.px(38);
            const Box badge{row.right()-w-ui.px(16),row.y+(row.h-ui.px(28))/2,w,ui.px(28)};
            ui.style.rounded(ui.batch,badge,ui.px(14),Xna::Color(14,14,14,14));
            ui.style.icon(ui.batch,Icon::Controller,Box{badge.x+ui.px(8),row.y+(row.h-ui.px(18))/2,ui.px(18),ui.px(18)},Palette::online());
            ui.style.text(ui.batch,Font::Caption,label,Xna::Vector2(badge.x+ui.px(30),badge.y+ui.px(3)),Palette::online());
        }
    }
    void join(const ServicePartyMember& member) {
        const auto who=player;
        closeAll();
        joinFriendGame(userOf(who),member.gamertag,[who](std::string reason){inform(who,"Join game",reason,Icon::Info);});
    }

    std::weak_ptr<PartyBase> self_;
    ServiceParty party_;
    std::map<std::string,std::vector<unsigned char>> avatars_;
    bool loaded_=false,failed_=false;
    List list_;
};

// Guide.ShowParty.
class PartyScreen final : public PartyBase {
public:
    std::string name() const override {return "party";}
    std::string title() const override {return "Party";}
    std::string subtitle() const override {
        if(!loaded_||party_.id.empty())return "";
        return std::to_string(party_.members.size())+(party_.members.size()==1?" person":" people")+" \xe2\x80\xa2 up to eight";
    }
    void input(InputContext& ui) override {
        if(access(player)!=Access::Account)return;
        const auto list=entries();
        const int chosen=list_.input(ui,static_cast<int>(list.size()));
        if(chosen>=0)activate(chosen);
        const int focus=std::clamp(list_.focus,0,std::max(0,static_cast<int>(list.size())-1));
        if(list.empty())return;
        const auto& entry=list[static_cast<std::size_t>(focus)];
        if(ui.input(Command::X)&&entry.kind==Kind::Invitation)change("decline",party_.invitations[entry.index].party,"");
        if(ui.input(Command::Y)&&entry.kind==Kind::Member&&party_.members[entry.index].joinable&&party_.members[entry.index].userId!=userOf(player))
            join(party_.members[entry.index]);
    }
    void activate(int index) override {
        const auto list=entries();
        if(index<0||index>=static_cast<int>(list.size()))return;
        const auto& entry=list[static_cast<std::size_t>(index)];
        switch(entry.kind) {
        case Kind::Invitation:
            change("accept",party_.invitations[entry.index].party,"You joined "+party_.invitations[entry.index].senderGamertag+"'s party");
            break;
        case Kind::Member:
            if(party_.members[entry.index].userId!=userOf(player))push(gamerCardScreen(player,party_.members[entry.index].gamertag));
            break;
        case Kind::Invite: {
            auto picker=std::make_shared<PartyInviteScreen>(std::static_pointer_cast<PartyScreen>(shared_from_this()));
            picker->player=player;
            picker->start(picker);
            push(std::move(picker));
            break;
        }
        case Kind::Leave:
            change("leave","","You left the party");
            break;
        }
    }
    void draw(Ui& ui,const Box& area) override {
        if(explainAccess(ui,area,access(player)))return;
        if(failed_){emptyState(ui,area,Icon::Warning,"The party could not be read","Check the connection to CNA Gamer Services.");return;}
        if(!loaded_){loading(ui,area);return;}
        const auto list=entries();
        Box listArea=area;
        if(party_.id.empty()&&party_.invitations.empty()) {
            // Nobody yet: what a party is, and the one thing to do.
            const Box note{area.x,area.y,area.w,ui.px(250)};
            emptyState(ui,note,Icon::Party,"You're not in a party","A party keeps friends together from game to game. Invite friends to start one.");
            listArea=Box{area.x,note.bottom()+ui.px(20),area.w,area.bottom()-note.bottom()-ui.px(20)};
        }
        list_.draw(ui,listArea,static_cast<int>(list.size()),72,[&](int index,const Box& row,bool focused) {
            const auto& entry=list[static_cast<std::size_t>(index)];
            switch(entry.kind) {
            case Kind::Invitation: {
                const auto& invitation=party_.invitations[entry.index];
                rowBackground(ui,row,focused);
                const Box face{row.x+ui.px(10),row.y+ui.px(7),row.h-ui.px(14),row.h-ui.px(14)};
                portrait(ui,avatar(invitation.senderId),face);
                const float tx=face.right()+ui.px(16);
                ui.style.text(ui.batch,Font::BodyBold,invitation.senderGamertag+" invited you to a party",Xna::Vector2(tx,row.y+ui.px(10)),Palette::text());
                ui.style.text(ui.batch,Font::Caption,std::to_string(invitation.members)+(invitation.members==1?" person":" people")+" \xe2\x80\xa2 joining leaves any other party",
                    Xna::Vector2(tx,row.y+ui.px(40)),Palette::accent());
                break;
            }
            case Kind::Member: memberRow(ui,row,focused,party_.members[entry.index]); break;
            case Kind::Invite:
            case Kind::Leave: {
                rowBackground(ui,row,focused);
                const float s=ui.px(26);
                ui.style.icon(ui.batch,entry.kind==Kind::Invite?Icon::PersonAdd:Icon::SignOut,Box{row.x+ui.px(22),row.y+(row.h-s)/2,s,s},
                    focused?Palette::accent():Palette::muted());
                const float h=ui.style.measure(Font::BodyBold,"Ag").Y;
                ui.style.text(ui.batch,Font::BodyBold,entry.kind==Kind::Invite?"Invite friends":"Leave party",
                    Xna::Vector2(row.x+ui.px(66),row.y+(row.h-h)/2),Palette::text());
                break;
            }
            }
        });
    }
    std::vector<Hint> hints() const override {
        std::vector<Hint> out{{Command::Accept,"Select"}};
        const auto list=entries();
        if(!list.empty()) {
            const auto& entry=list[static_cast<std::size_t>(std::clamp(list_.focus,0,static_cast<int>(list.size())-1))];
            if(entry.kind==Kind::Invitation)out={{Command::Accept,"Join party"},{Command::X,"Decline"}};
            if(entry.kind==Kind::Member&&party_.members[entry.index].joinable&&party_.members[entry.index].userId!=userOf(player))
                out.push_back({Command::Y,"Join game"});
        }
        out.push_back({Command::Back,"Back"});
        return out;
    }
    std::vector<std::string> labels() const override {
        std::vector<std::string> out;
        for(const auto& entry:entries()) {
            switch(entry.kind) {
            case Kind::Invitation: out.push_back("Invitation from "+party_.invitations[entry.index].senderGamertag); break;
            case Kind::Member: {
                const auto& member=party_.members[entry.index];
                out.push_back(member.gamertag+(member.userId==party_.leaderId?" (leader)":"")+(member.joinable?" - joinable":""));
                break;
            }
            case Kind::Invite: out.push_back("Invite friends"); break;
            case Kind::Leave: out.push_back("Leave party"); break;
            }
        }
        return out;
    }
    int focus() const override {return list_.focus;}
    const ServiceParty& party() const {return party_;}
    void inviteFriend(const std::string& gamertag) {change("invite",gamertag,"Party invitation sent to "+gamertag);}

private:
    enum class Kind : std::uint8_t { Invitation, Member, Invite, Leave };
    struct Entry {Kind kind;std::size_t index=0;};
    std::vector<Entry> entries() const {
        std::vector<Entry> out;
        if(!loaded_)return out;
        for(std::size_t i=0;i<party_.invitations.size();++i)out.push_back({Kind::Invitation,i});
        for(std::size_t i=0;i<party_.members.size();++i)out.push_back({Kind::Member,i});
        out.push_back({Kind::Invite});
        if(!party_.id.empty())out.push_back({Kind::Leave});
        return out;
    }

    // Friends to invite: accepted friends not yet in the party, online first.
    class PartyInviteScreen final : public Screen {
    public:
        explicit PartyInviteScreen(std::weak_ptr<PartyScreen> party):party_(std::move(party)){}
        std::string name() const override {return "partyInvite";}
        std::string title() const override {return "Invite to party";}
        std::string subtitle() const override {return "Friends join from their Guide";}
        float dialogWidth() const override {return 640;}
        float dialogHeight() const override {return 520;}
        void input(InputContext& ui) override {
            const int chosen=list_.input(ui,static_cast<int>(friends_.size()));
            if(chosen>=0)activate(chosen);
        }
        void activate(int index) override {
            if(index<0||index>=static_cast<int>(friends_.size()))return;
            const auto tag=friends_[static_cast<std::size_t>(index)].gamertag;
            if(auto party=party_.lock())party->inviteFriend(tag);
            invited_.push_back(tag);
        }
        void draw(Ui& ui,const Box& area) override {
            if(!loaded_){loading(ui,area);return;}
            if(friends_.empty()){emptyState(ui,area,Icon::People,"Nobody to invite","Every friend is in your party already, or you have no friends yet.");return;}
            list_.draw(ui,area,static_cast<int>(friends_.size()),64,[&](int index,const Box& row,bool focused) {
                const auto& entry=friends_[static_cast<std::size_t>(index)];
                rowBackground(ui,row,focused);
                statusDot(ui,Xna::Vector2(row.x+ui.px(22),row.y+row.h/2),entry.online?Palette::online():Palette::offline());
                const float h=ui.style.measure(Font::BodyBold,"Ag").Y;
                ui.style.text(ui.batch,Font::BodyBold,entry.gamertag,Xna::Vector2(row.x+ui.px(44),row.y+(row.h-h)/2),Palette::text());
                if(std::find(invited_.begin(),invited_.end(),entry.gamertag)!=invited_.end())
                    ui.style.icon(ui.batch,Icon::Check,Box{row.right()-ui.px(40),row.y+(row.h-ui.px(22))/2,ui.px(22),ui.px(22)},Palette::accent());
            });
        }
        std::vector<Hint> hints() const override {return {{Command::Accept,"Invite"},{Command::Back,"Done"}};}
        std::vector<std::string> labels() const override {std::vector<std::string> out;for(const auto& f:friends_)out.push_back(f.gamertag);return out;}
        int focus() const override {return list_.focus;}
        void start(const std::shared_ptr<PartyInviteScreen>& self) {
            std::vector<std::string> members;
            if(auto party=party_.lock())for(const auto& member:party->party().members)members.push_back(member.userId);
            load<std::vector<ServiceFriend>>(self,[user=userOf(player)](IGamerServicesBackend& s){return s.friends(user);},
                [members](PartyInviteScreen& screen,std::vector<ServiceFriend> list) {
                    std::erase_if(list,[&](const ServiceFriend& f){return !f.accepted||std::find(members.begin(),members.end(),f.userId)!=members.end();});
                    std::ranges::stable_sort(list,[](const ServiceFriend& a,const ServiceFriend& b){return a.online&&!b.online;});
                    screen.friends_=std::move(list);
                    screen.loaded_=true;
                },[](PartyInviteScreen& screen){screen.loaded_=true;});
        }
    private:
        std::weak_ptr<PartyScreen> party_;
        std::vector<ServiceFriend> friends_;
        std::vector<std::string> invited_;
        bool loaded_=false;
        List list_;
    };
};

// Guide.ShowPartySessions: the games party members are in that can be joined now.
class PartySessionsScreen final : public PartyBase {
public:
    std::string name() const override {return "partySessions";}
    std::string title() const override {return "Party games";}
    std::string subtitle() const override {return loaded_?"Games your party members can be joined in":"";}
    void input(InputContext& ui) override {
        if(access(player)!=Access::Account)return;
        const int chosen=list_.input(ui,static_cast<int>(joinable().size()));
        if(chosen>=0)activate(chosen);
    }
    void activate(int index) override {
        const auto list=joinable();
        if(index>=0&&index<static_cast<int>(list.size()))join(list[static_cast<std::size_t>(index)]);
    }
    void draw(Ui& ui,const Box& area) override {
        if(explainAccess(ui,area,access(player)))return;
        if(failed_){emptyState(ui,area,Icon::Warning,"The party could not be read","Check the connection to CNA Gamer Services.");return;}
        if(!loaded_){loading(ui,area);return;}
        const auto list=joinable();
        if(list.empty()){emptyState(ui,area,Icon::Controller,"No games to join","Nobody in your party is in a game of this title that has room right now.");return;}
        list_.draw(ui,area,static_cast<int>(list.size()),76,[&](int index,const Box& row,bool focused){memberRow(ui,row,focused,list[static_cast<std::size_t>(index)]);});
    }
    std::vector<Hint> hints() const override {return {{Command::Accept,"Join game"},{Command::Back,"Back"}};}
    std::vector<std::string> labels() const override {std::vector<std::string> out;for(const auto& m:joinable())out.push_back(m.gamertag);return out;}
    int focus() const override {return list_.focus;}
private:
    std::vector<ServicePartyMember> joinable() const {
        std::vector<ServicePartyMember> out;
        for(const auto& member:party_.members)if(member.joinable&&member.userId!=userOf(player))out.push_back(member);
        return out;
    }
};
}

std::shared_ptr<Screen> partyScreen(Xna::PlayerIndex player)
{
    auto screen=std::make_shared<PartyScreen>();
    screen->player=player;
    screen->start(screen);
    return screen;
}

std::shared_ptr<Screen> partySessionsScreen(Xna::PlayerIndex player)
{
    // XNA: without a party, ShowPartySessions shows the Friends page instead.
    if(access(player)==Access::Account) {
        const auto known=knownParty(userOf(player));
        if(known&&known->id.empty())return friendsScreen(player);
    }
    auto screen=std::make_shared<PartySessionsScreen>();
    screen->player=player;
    screen->start(screen);
    return screen;
}
}
