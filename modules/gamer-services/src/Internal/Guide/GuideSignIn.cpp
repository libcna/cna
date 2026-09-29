// SPDX-License-Identifier: MS-PL
// Sign-in as a console does it: the four player slots, the profiles this computer knows, a new
// profile, a CNA account, or a guest of the signed-in account. Typing starts the name at once,
// so a keyboard player never has to pick a row first.
#include "GuideScreen.hpp"
#include "../GuideOverlay.hpp"
#include "CNA/Internal/GamerServices/LocalProfiles.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include <algorithm>
#include <map>

namespace CNA::Internal::GamerServices::GuideUi {
namespace {
namespace GS=Microsoft::Xna::Framework::GamerServices;
namespace In=Microsoft::Xna::Framework::Input;

class SignInScreen final : public Screen {
public:
    SignInScreen(SignInRequest request,SignInHandlers handlers):request_(std::move(request)),handlers_(std::move(handlers))
    {
        for(const auto& name:request_.profiles)choices_.push_back({Kind::Profile,name});
        if(request_.local)choices_.push_back({Kind::NewProfile,"New profile"});
        else choices_.push_back({Kind::Account,"Sign in with a CNA account"});
        if(!request_.guestHost.empty())choices_.push_back({Kind::Guest,"Play as a guest of "+request_.guestHost});
        for(const auto& name:request_.profiles)avatars_[name]=localProfileAvatar(name);
        // Typing starts a name straight away.
        token_=In::TextInputEXT::TextInput.Add([this](In::charcs c){typed(c);});
    }
    ~SignInScreen() override {In::TextInputEXT::TextInput.Remove(token_);}
    std::string name() const override {return "signIn";}
    std::string title() const override {return "Sign in";}
    std::string subtitle() const override {return "Choose a profile for Player "+std::to_string(request_.slot+1);}
    float dialogWidth() const override {return 940;}
    float dialogHeight() const override {return std::min(600.0f,380.0f+62.0f*static_cast<float>(choices_.size()));}
    void input(InputContext& ui) override {
        if(waiting_)return;
        if(ui.input(Command::Back)){cancel();return;}
        // Letters are names here, not shortcuts.
        if(ui.input.keyboard[static_cast<std::size_t>(Command::Accept)])return;
        const int chosen=list_.input(ui,static_cast<int>(choices_.size()));
        if(chosen>=0)activate(chosen);
    }
    void activate(int index) override {
        if(waiting_||index<0||index>=static_cast<int>(choices_.size()))return;
        const auto choice=choices_[static_cast<std::size_t>(index)];
        switch(choice.kind) {
        case Kind::Profile: chooseProfile(choice.label); break;
        case Kind::NewProfile: askName({}); break;
        case Kind::Account: askAccount({}); break;
        case Kind::Guest: waiting_=true; handlers_.guest(); break;
        }
    }
    void draw(Ui& ui,const Box& area) override {
        // The four player slots.
        const float gap=ui.px(14),cardW=(area.w-3*gap)/4,cardH=ui.px(116);
        for(int slot=0;slot<4;++slot) {
            const Box card{area.x+static_cast<float>(slot)*(cardW+gap),area.y,cardW,cardH};
            const bool current=slot==request_.slot,offered=slot<request_.panes;
            ui.style.rounded(ui.batch,card,ui.px(14),current?Palette::accent():Palette::surface());
            const Box inner=card.inset(current?ui.px(3):0.0f);
            if(current)ui.style.rounded(ui.batch,inner,ui.px(12),Palette::surface());
            ui.style.text(ui.batch,Font::Caption,"Player "+std::to_string(slot+1),Xna::Vector2(inner.x+ui.px(14),inner.y+ui.px(10)),
                current?Palette::accent():Palette::muted());
            const Box face{inner.x+ui.px(12),inner.y+ui.px(40),ui.px(58),ui.px(58)};
            std::string label,detail;
            GS::SignedInGamer* gamer=nullptr;
            for(auto* candidate:*GS::Gamer::getSignedInGamersProperty())
                if(static_cast<int>(candidate->getPlayerIndexProperty())==slot)gamer=candidate;
            if(gamer) {
                portrait(ui,identityAvatar(*gamer),face);
                label=gamer->getGamertagProperty();
                detail=gamer->getIsGuestProperty()?"Guest":gamer->getIsSignedInToLiveProperty()?"Online":"Offline profile";
            } else if(current&&waiting_) {
                loading(ui,Box{face.x,face.y,face.w,face.h},"");
                label="Signing in";
            } else if(current) {
                ui.style.rounded(ui.batch,face,ui.px(12),Palette::rail());
                ui.style.icon(ui.batch,Icon::Person,face.inset(ui.px(14)),Palette::accent());
                label="Choosing";
                detail="Pick below";
            } else {
                ui.style.rounded(ui.batch,face,ui.px(12),Palette::rail());
                ui.style.icon(ui.batch,Icon::Controller,face.inset(ui.px(16)),offered?Palette::faint():Xna::Color(40,46,58));
                label=offered?"Next":"";
            }
            const float tx=face.right()+ui.px(12);
            ui.style.text(ui.batch,Font::Body,ui.style.fit(Font::Body,label,inner.right()-tx-ui.px(8)),Xna::Vector2(tx,face.y+ui.px(8)),Palette::text());
            if(!detail.empty())ui.style.text(ui.batch,Font::Caption,ui.style.fit(Font::Caption,detail,inner.right()-tx-ui.px(8)),
                Xna::Vector2(tx,face.y+ui.px(36)),Palette::muted());
        }
        // What this player can sign in as.
        const Box list{area.x,area.y+cardH+ui.px(22),area.w,area.h-cardH-ui.px(22)};
        list_.draw(ui,list,static_cast<int>(choices_.size()),62,[&](int index,const Box& row,bool focused) {
            const auto& choice=choices_[static_cast<std::size_t>(index)];
            rowBackground(ui,row,focused);
            const Box face{row.x+ui.px(10),row.y+ui.px(6),row.h-ui.px(12),row.h-ui.px(12)};
            if(choice.kind==Kind::Profile) {
                portrait(ui,avatars_[choice.label],face);
            } else {
                ui.style.rounded(ui.batch,face,ui.px(10),Palette::rail());
                const Icon icon=choice.kind==Kind::NewProfile?Icon::PersonAdd:choice.kind==Kind::Account?Icon::Globe:Icon::Person;
                ui.style.icon(ui.batch,icon,face.inset(ui.px(10)),Palette::accent());
            }
            const float tx=face.right()+ui.px(16);
            ui.style.text(ui.batch,Font::BodyBold,choice.label,Xna::Vector2(tx,row.y+ui.px(8)),Palette::text());
            const char* detail=choice.kind==Kind::Profile?"Offline profile on this computer":choice.kind==Kind::NewProfile?"A profile kept on this computer":
                               choice.kind==Kind::Account?"Friends, achievements and online play":"Plays on its account's sign-in";
            ui.style.text(ui.batch,Font::Caption,detail,Xna::Vector2(tx,row.y+ui.px(34)),Palette::muted());
        });
        ui.style.text(ui.batch,Font::Caption,request_.local?"Or type a profile name.":"Or type your account name.",
            Xna::Vector2(area.x,area.bottom()+ui.px(22)),Palette::faint());
    }
    std::vector<Hint> hints() const override {return {{Command::Accept,"Select"},{Command::Back,"Cancel"}};}
    std::vector<std::string> labels() const override {
        std::vector<std::string> out;
        for(const auto& choice:choices_)out.push_back(choice.label);
        return out;
    }
    int focus() const override {return list_.focus;}

private:
    enum class Kind : std::uint8_t { Profile, NewProfile, Account, Guest };
    struct Choice {Kind kind;std::string label;};

    static std::vector<unsigned char> identityAvatar(const GS::SignedInGamer& gamer)
    {
        return identity(gamer.getPlayerIndexProperty()).gamertag==gamer.getGamertagProperty()?identity(gamer.getPlayerIndexProperty()).avatar:
               gamer.getIsSignedInToLiveProperty()?std::vector<unsigned char>{}:localProfileAvatar(gamer.getGamertagProperty());
    }
    bool onTop() const {
        return currentScreenForTesting()=="signIn"&&!pendingKeyboard()&&!pendingMessageBox();
    }
    void typed(In::charcs c) {
        if(waiting_||!onTop())return;
        if(c==u'\r'||c==u'\n'){activate(list_.focus);return;}
        if(c<0x20||c==0x7F)return;
        const std::string first(1,static_cast<char>(c<0x80?c:'?'));
        if(request_.local)askName(first);else askAccount(first);
    }
    void chooseProfile(const std::string& name) {
        waiting_=true;
        handlers_.local(name);
    }
    void askName(const std::string& start) {
        (void)showGuideKeyboardInput(player,"New profile","A name of 1 to 15 letters, digits and spaces",start,[this](System::IAsyncResult& input) {
            std::unique_ptr<System::IAsyncResult> owned(&input);
            if(GS::Guide::WasKeyboardInputCanceledEXT(&input))return;
            waiting_=true;
            handlers_.local(GS::Guide::EndShowKeyboardInput(&input));
        },{});
    }
    void askAccount(const std::string& start) {
        const auto prompt=request_.guestHost.empty()?std::string("Account name"):"Account name, or Guest to play as a guest of "+request_.guestHost;
        (void)showGuideKeyboardInput(player,"CNA account",prompt,start,[this](System::IAsyncResult& input) {
            std::unique_ptr<System::IAsyncResult> owned(&input);
            if(GS::Guide::WasKeyboardInputCanceledEXT(&input))return;
            const auto username=GS::Guide::EndShowKeyboardInput(&input);
            std::string lowered=username;
            for(auto& c:lowered)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if(username.empty()||username.size()>64)return;
            if(!request_.guestHost.empty()&&lowered=="guest"){waiting_=true;handlers_.guest();return;}
            (void)showGuideKeyboardInput(player,"CNA account","Password for "+username,"",[this,username](System::IAsyncResult& secret) {
                std::unique_ptr<System::IAsyncResult> ownedSecret(&secret);
                if(GS::Guide::WasKeyboardInputCanceledEXT(&secret))return;
                auto password=GS::Guide::EndShowKeyboardInput(&secret);
                if(password.size()>256){std::fill(password.begin(),password.end(),'\0');return;}
                waiting_=true;
                handlers_.account(username,std::move(password));
            },{},true);
        },{});
    }
    void cancel() {
        auto done=handlers_.cancel;
        pop(this);
        if(done)done();
    }

    SignInRequest request_;
    SignInHandlers handlers_;
    std::vector<Choice> choices_;
    std::map<std::string,std::vector<unsigned char>> avatars_;
    List list_;
    bool waiting_=false;
    System::MulticastAction<In::charcs>::Token token_=System::MulticastAction<In::charcs>::InvalidToken;
};
}

std::shared_ptr<Screen> signInScreen(Xna::PlayerIndex player,SignInRequest request,SignInHandlers handlers)
{
    auto screen=std::make_shared<SignInScreen>(std::move(request),std::move(handlers));
    screen->player=player;
    return screen;
}
}
