// SPDX-License-Identifier: MS-PL
// The game content panel (Guide.ShowMarketplace) and XNA's test purchase prompt.
//
// Link boundary: kept apart from the social panels (GuideSocial.cpp) so that a game which only
// offers its full version links this panel and the Guide's frame -- not the rest of the system
// Guide and its avatar views, which reach it through the installed hooks (installSystemGuide).
#include "GuideScreen.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include <string>
#include <utility>
#include <vector>

namespace CNA::Internal::GamerServices::GuideUi {
namespace {
namespace GS=Microsoft::Xna::Framework::GamerServices;

// XNA's own emulation of a purchase for a game simulating trial mode (its resources' "Test
// Purchase" prompt): Yes turns SimulateTrialMode off, and IsTrialMode follows at the next update.
class TestPurchaseScreen final : public Screen {
public:
    std::string name() const override {return "testPurchase";}
    std::string title() const override {return "Test Purchase";}
    float dialogWidth() const override {return 640;}
    float dialogHeight() const override {return 400;}
    void input(InputContext& ui) override {
        const int chosen=list_.input(ui,2);
        if(chosen>=0)activate(chosen);
    }
    void activate(int index) override {
        if(index<0||index>1)return;
        if(index==0)GS::Guide::setSimulateTrialModeProperty(false);
        pop(this);
    }
    void draw(Ui& ui,const Box& area) override {
        float y=area.y;
        for(const auto& line:ui.style.wrap(Font::Body,text(),area.w)) {
            ui.style.text(ui.batch,Font::Body,line,Xna::Vector2(area.x,y),Palette::text());
            y+=ui.style.measure(Font::Body,"Ag").Y;
        }
        drawActions(ui,list_,Box{area.x,y+ui.px(18),area.w,area.bottom()-y-ui.px(18)},{{Icon::Check,"Yes"},{Icon::Cross,"No"}});
    }
    std::vector<Hint> hints() const override {return {{Command::Accept,"Select"},{Command::Back,"No"}};}
    std::vector<std::string> labels() const override {return {"Yes","No"};}
    int focus() const override {return list_.focus;}
private:
    static std::string text() {
        return "Would you like to simulate purchasing the full version of this game? This screen is an emulation provided "
               "for testing; CNA Gamer Services sells nothing itself.";
    }
    List list_;
};

// What this game has: its license, and the avatar catalogs as one product -- the ones this CNA
// release carries and the updates installed since, with the update setting.
class ContentScreen final : public Screen {
public:
    std::string name() const override {return "content";}
    std::string title() const override {return "Game content";}
    std::string subtitle() const override {return "This game and what it has installed";}
    Category category() const override {return Category::Content;}
    void input(InputContext& ui) override {
        const int chosen=list_.input(ui,static_cast<int>(rows().size()));
        if(chosen>=0)activate(chosen);
    }
    void activate(int index) override {
        if(index==0&&GS::Guide::getSimulateTrialModeProperty())push(testPurchaseScreen());
    }
    void draw(Ui& ui,const Box& area) override {
        const auto list=rows();
        list_.draw(ui,area,static_cast<int>(list.size()),84,[&](int index,const Box& row,bool focused) {
            const auto& entry=list[static_cast<std::size_t>(index)];
            rowBackground(ui,row,focused);
            const Box tile{row.x+ui.px(12),row.y+ui.px(12),row.h-ui.px(24),row.h-ui.px(24)};
            ui.style.rounded(ui.batch,tile,ui.px(12),Palette::rail());
            ui.style.icon(ui.batch,entry.icon,tile.inset(ui.px(12)),entry.attention?Palette::away():Palette::accent());
            const float tx=tile.right()+ui.px(16);
            ui.style.text(ui.batch,Font::BodyBold,entry.title,Xna::Vector2(tx,row.y+ui.px(10)),Palette::text());
            ui.style.text(ui.batch,Font::Caption,ui.style.fit(Font::Caption,entry.detail,row.right()-tx-ui.px(16)),Xna::Vector2(tx,row.y+ui.px(38)),Palette::muted());
            if(!entry.more.empty())
                ui.style.text(ui.batch,Font::Caption,ui.style.fit(Font::Caption,entry.more,row.right()-tx-ui.px(16)),Xna::Vector2(tx,row.y+ui.px(58)),Palette::faint());
        });
    }
    std::vector<Hint> hints() const override {
        if(GS::Guide::getSimulateTrialModeProperty()&&list_.focus==0)return {{Command::Accept,"Buy (test)"},{Command::Back,"Back"}};
        return {{Command::Back,"Back"}};
    }
    std::vector<std::string> labels() const override {std::vector<std::string> out;for(const auto& row:rows())out.push_back(row.title);return out;}
    int focus() const override {return list_.focus;}

private:
    struct Row {Icon icon;std::string title,detail,more;bool attention=false;};
    std::vector<Row> rows() const {
        std::vector<Row> out;
        if(GS::Guide::getIsTrialModeProperty()||GS::Guide::getSimulateTrialModeProperty())
            out.push_back({Icon::Lock,"Trial version",GS::Guide::getSimulateTrialModeProperty()?"Trial mode is simulated for testing (Guide.SimulateTrialMode)":
                "Waiting for gamer services to confirm the license","Online sessions are unavailable in a trial.",true});
        else
            out.push_back({Icon::Check,"Full version","This game is fully licensed",""});
        // The avatar catalogs, one product, when the system Guide (and with it the avatars) is here.
        if(shellHooks().avatarCatalogs)
            if(auto catalogs=shellHooks().avatarCatalogs())
                out.push_back({Icon::Person,"CNA avatars",std::move(catalogs->first),std::move(catalogs->second)});
        return out;
    }
    static std::shared_ptr<Screen> testPurchaseScreen() {return std::make_shared<TestPurchaseScreen>();}
    List list_;
};

}

std::shared_ptr<Screen> contentScreen(Xna::PlayerIndex player)
{
    auto screen=std::make_shared<ContentScreen>();
    screen->player=player;
    return screen;
}

std::shared_ptr<Screen> testPurchaseScreen(Xna::PlayerIndex player)
{
    auto screen=std::make_shared<TestPurchaseScreen>();
    screen->player=player;
    return screen;
}
}
