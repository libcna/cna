// SPDX-License-Identifier: MS-PL
// This title's leaderboards as a Guide page. XNA has no system leaderboard UI -- a game reads its
// boards through LeaderboardReader -- so this page is a CNA system feature: the boards the title
// provisioned on the service, each shown as the top players, the players around you, or your
// friends, with their avatars.
#include "GuideScreen.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include <algorithm>
#include <cctype>
#include <map>

namespace CNA::Internal::GamerServices::GuideUi {
namespace {
// "BestScoreLifeTime" -> "Best Score Life Time"; a mode other than 0 is named.
std::string boardName(const ServiceLeaderboardInfo& board)
{
    std::string text;
    for(std::size_t i=0;i<board.key.size();++i) {
        const auto c=static_cast<unsigned char>(board.key[i]);
        if(c=='_'){text+=' ';continue;}
        if(i>0&&std::isupper(c)&&!std::isupper(static_cast<unsigned char>(board.key[i-1])))text+=' ';
        text+=static_cast<char>(c);
    }
    if(board.mode!=0)text+=" \xe2\x80\xa2 Mode "+std::to_string(board.mode);
    return text;
}

// 1234567 -> "1,234,567".
std::string grouped(long long value)
{
    auto digits=std::to_string(value<0?-value:value);
    for(int at=static_cast<int>(digits.size())-3;at>0;at-=3)digits.insert(static_cast<std::size_t>(at),",");
    return (value<0?"-":"")+digits;
}

class BoardScreen final : public Screen {
public:
    explicit BoardScreen(ServiceLeaderboardInfo board):board_(std::move(board)){}
    std::string name() const override {return "leaderboard";}
    std::string title() const override {return boardName(board_);}
    std::string subtitle() const override {
        static const char* const views[]{"Top players","Around you","Friends"};
        return std::string(views[view_])+(page_?" \xe2\x80\xa2 "+grouped(page_->total)+(page_->total==1?" player":" players"):"");
    }
    Category category() const override {return Category::Leaderboards;}
    void input(InputContext& ui) override {
        if(ui.input(Command::X)){view_=(view_+1)%3;reload();return;}
        const int count=page_?static_cast<int>(page_->entries.size()):0;
        const int chosen=list_.input(ui,count);
        if(chosen>=0)activate(chosen);
    }
    void activate(int index) override {
        if(page_&&index>=0&&index<static_cast<int>(page_->entries.size()))
            push(gamerCardScreen(player,page_->entries[static_cast<std::size_t>(index)].gamertag));
    }
    void draw(Ui& ui,const Box& area) override {
        if(failed_){emptyState(ui,area,Icon::Warning,"The leaderboard could not be read","Check the connection to CNA Gamer Services.");return;}
        if(!page_){loading(ui,area);return;}
        if(page_->entries.empty()) {
            emptyState(ui,area,Icon::Leaderboard,view_==1?"You are not on this board yet":view_==2?"No friends on this board yet":"Nobody on this board yet",
                view_==2?"Friends who play this game appear here once they have a score.":"Play the game to get a score.");
            return;
        }
        const auto me=identity(player).gamertag;
        list_.draw(ui,area,static_cast<int>(page_->entries.size()),68,[&](int index,const Box& row,bool focused) {
            const auto& entry=page_->entries[static_cast<std::size_t>(index)];
            const bool mine=entry.gamertag==me;
            rowBackground(ui,row,focused);
            if(mine&&!focused)ui.style.rounded(ui.batch,Box{row.x,row.y+ui.px(10),ui.px(4),row.h-ui.px(20)},ui.px(2),Palette::accent());
            // Rank, the first three in medal colours.
            const Xna::Color medal=entry.rank==1?Palette::gold():entry.rank==2?Xna::Color(176,186,200):entry.rank==3?Xna::Color(196,128,82):Palette::surface();
            const Box rank{row.x+ui.px(14),row.y+(row.h-ui.px(40))/2,ui.px(54),ui.px(40)};
            ui.style.rounded(ui.batch,rank,ui.px(12),entry.rank<=3?medal:Palette::rail());
            const auto label=grouped(entry.rank);
            const float th=ui.style.measure(Font::BodyBold,"Ag").Y;
            ui.style.text(ui.batch,Font::BodyBold,label,Xna::Vector2(rank.x+rank.w/2,rank.y+(rank.h-th)/2),
                entry.rank<=3?Palette::panel():Palette::text(),Align::Center);
            const Box face{rank.right()+ui.px(12),row.y+ui.px(6),row.h-ui.px(12),row.h-ui.px(12)};
            const auto avatar=avatars_.find(entry.userId);
            portrait(ui,avatar==avatars_.end()?std::vector<unsigned char>{}:avatar->second,face);
            const float tx=face.right()+ui.px(14);
            ui.style.text(ui.batch,Font::BodyBold,entry.gamertag,Xna::Vector2(tx,row.y+(row.h-th)/2),mine?Palette::accent():Palette::text());
            ui.style.text(ui.batch,Font::Heading,grouped(entry.rating),Xna::Vector2(row.right()-ui.px(18),row.y+(row.h-ui.style.measure(Font::Heading,"0").Y)/2),
                Palette::text(),Align::Right);
        });
    }
    std::vector<Hint> hints() const override {
        static const char* const next[]{"Around you","Friends","Top players"};
        return {{Command::Accept,"Gamer card"},{Command::X,next[view_]},{Command::Back,"Back"}};
    }
    std::vector<std::string> labels() const override {
        std::vector<std::string> out;
        if(page_)for(const auto& entry:page_->entries)out.push_back("#"+std::to_string(entry.rank)+" "+entry.gamertag+" "+std::to_string(entry.rating));
        return out;
    }
    int focus() const override {return list_.focus;}
    bool busy() const override {return !page_&&!failed_;}
    void start(const std::shared_ptr<BoardScreen>& self){self_=self;reload();}

private:
    void reload() {
        page_.reset();
        failed_=false;
        list_=List{};
        const int generation=++generation_;
        const auto me=identity(player).gamertag;
        load<ServiceLeaderboardPage>(self_.lock(),[board=board_,view=view_,user=userOf(player),me](IGamerServicesBackend& s) {
            if(view==0)return s.readLeaderboard(board.key,board.mode,0,50,"",std::nullopt);
            if(view==1)return s.readLeaderboard(board.key,board.mode,0,21,me,std::nullopt);
            std::vector<std::string> people{me};
            for(const auto& entry:s.friends(user))if(entry.accepted)people.push_back(entry.gamertag);
            if(people.size()>100)people.resize(100);
            return s.readLeaderboard(board.key,board.mode,0,100,"",people);
        },[generation](BoardScreen& screen,ServiceLeaderboardPage page) {
            if(generation!=screen.generation_)return;
            // Around you: the focus starts on your own row.
            if(screen.view_==1)
                for(std::size_t i=0;i<page.entries.size();++i)
                    if(page.entries[i].gamertag==identity(screen.player).gamertag)screen.list_.focus=static_cast<int>(i);
            std::vector<std::string> ids;
            for(const auto& entry:page.entries)if(!screen.avatars_.contains(entry.userId))ids.push_back(entry.userId);
            screen.page_=std::move(page);
            if(ids.empty())return;
            load<std::vector<ServiceAvatarRecord>>(screen.self_.lock(),[ids](IGamerServicesBackend& s){return s.avatars(ids);},
                [ids](BoardScreen& owner,std::vector<ServiceAvatarRecord> records) {
                    for(std::size_t i=0;i<ids.size()&&i<records.size();++i)owner.avatars_[ids[i]]=std::move(records[i].description);
                });
        },[generation](BoardScreen& screen){if(generation==screen.generation_)screen.failed_=true;});
    }

    ServiceLeaderboardInfo board_;
    std::weak_ptr<BoardScreen> self_;
    int view_=0,generation_=0;
    std::optional<ServiceLeaderboardPage> page_;
    std::map<std::string,std::vector<unsigned char>> avatars_;
    bool failed_=false;
    List list_;
};

class LeaderboardsScreen final : public Screen {
public:
    std::string name() const override {return "leaderboards";}
    std::string title() const override {return "Leaderboards";}
    std::string subtitle() const override {return loaded_&&!boards_.empty()?"This game's leaderboards on CNA Gamer Services":"";}
    Category category() const override {return Category::Leaderboards;}
    void input(InputContext& ui) override {
        const int chosen=list_.input(ui,static_cast<int>(boards_.size()));
        if(chosen>=0)activate(chosen);
    }
    void activate(int index) override {
        if(index<0||index>=static_cast<int>(boards_.size()))return;
        auto board=std::make_shared<BoardScreen>(boards_[static_cast<std::size_t>(index)]);
        board->player=player;
        board->start(board);
        push(std::move(board));
    }
    void draw(Ui& ui,const Box& area) override {
        const auto a=access(player);
        if(a==Access::LocalProfile) {
            emptyState(ui,area,Icon::Leaderboard,"Offline profile","Leaderboards are kept by CNA Gamer Services. Sign in with a CNA account to see them.");
            return;
        }
        if(explainAccess(ui,area,a))return;
        if(failed_){emptyState(ui,area,Icon::Warning,"Leaderboards could not be read","Check the connection to CNA Gamer Services.");return;}
        if(!loaded_){loading(ui,area);return;}
        if(boards_.empty()){emptyState(ui,area,Icon::Leaderboard,"No leaderboards","This game keeps no leaderboards on CNA Gamer Services.");return;}
        list_.draw(ui,area,static_cast<int>(boards_.size()),76,[&](int index,const Box& row,bool focused) {
            const auto& board=boards_[static_cast<std::size_t>(index)];
            rowBackground(ui,row,focused);
            const Box tile{row.x+ui.px(12),row.y+ui.px(10),row.h-ui.px(20),row.h-ui.px(20)};
            ui.style.rounded(ui.batch,tile,ui.px(12),Palette::rail());
            ui.style.icon(ui.batch,Icon::Leaderboard,tile.inset(ui.px(12)),Palette::accent());
            const float tx=tile.right()+ui.px(16);
            ui.style.text(ui.batch,Font::BodyBold,ui.style.fit(Font::BodyBold,boardName(board),row.right()-tx-ui.px(150)),Xna::Vector2(tx,row.y+ui.px(12)),Palette::text());
            std::string detail=board.ascending?"Lowest first":"Highest first";
            if(board.arbitrated)detail+=" \xe2\x80\xa2 Ranked matches";
            ui.style.text(ui.batch,Font::Caption,detail,Xna::Vector2(tx,row.y+ui.px(42)),Palette::muted());
            ui.style.text(ui.batch,Font::Body,grouped(board.entries)+(board.entries==1?" player":" players"),
                Xna::Vector2(row.right()-ui.px(18),row.y+(row.h-ui.style.measure(Font::Body,"Ag").Y)/2),Palette::muted(),Align::Right);
        });
    }
    std::vector<Hint> hints() const override {return {{Command::Accept,"Open"},{Command::Back,"Back"}};}
    std::vector<std::string> labels() const override {
        std::vector<std::string> out;
        for(const auto& board:boards_)out.push_back(boardName(board)+" - "+std::to_string(board.entries));
        return out;
    }
    int focus() const override {return list_.focus;}
    bool busy() const override {return access(player)==Access::Account&&!loaded_&&!failed_;}
    void start(const std::shared_ptr<LeaderboardsScreen>& self) {
        if(access(player)!=Access::Account)return;
        load<std::vector<ServiceLeaderboardInfo>>(self,[](IGamerServicesBackend& s){return s.leaderboards();},
            [](LeaderboardsScreen& screen,std::vector<ServiceLeaderboardInfo> boards){screen.boards_=std::move(boards);screen.loaded_=true;},
            [](LeaderboardsScreen& screen){screen.failed_=true;});
    }

private:
    std::vector<ServiceLeaderboardInfo> boards_;
    bool loaded_=false,failed_=false;
    List list_;
};
}

std::shared_ptr<Screen> leaderboardsScreen(Xna::PlayerIndex player)
{
    auto screen=std::make_shared<LeaderboardsScreen>();
    screen->player=player;
    screen->start(screen);
    return screen;
}
}
